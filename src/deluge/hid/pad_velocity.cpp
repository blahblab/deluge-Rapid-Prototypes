/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// The codec's input ring (2048 frames, 46 ms) is written by DMA all the time; the internal mic is its left channel.
// A press notes where the DMA is. The thump often starts a little before the pad's switch closes, so the level is the
// loudest from 3 ms before the press to 2.5 ms after: measured on the hardware (2026-11-22) that tracks the tap's whole
// peak (r 0.96, about 3.5 dB), against some 10 dB between soft, medium and hard.

#include "hid/pad_velocity.h"
#include "OSLikeStuff/scheduler_api.h"
#include "definitions.h"
#include "fatfs/ff.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

extern "C" {
#include "drivers/ssi/ssi.h"
}

namespace deluge::pad_velocity {

bool enabled = false;

namespace {

constexpr int32_t kRing = SSI_RX_BUFFER_NUM_SAMPLES;
constexpr int32_t kBefore = 132; // 3 ms
constexpr int32_t kAfter = 110;  // 2.5 ms
constexpr int32_t kStale = kRing - kBefore - 50;
constexpr uint32_t kBounceSamples = 1764; // 40 ms: the same pad again this soon is its switch bouncing

// The calibration: four corners and the centre, soft / medium / hard, three taps each
constexpr int32_t kPoints = 5;
constexpr int32_t kStrengths = 3;
constexpr int32_t kTaps = 3;
constexpr int32_t kSteps = kPoints * kStrengths * kTaps;
constexpr int8_t kPointX[kPoints] = {0, 15, 0, 15, 7};
constexpr int8_t kPointY[kPoints] = {7, 7, 0, 0, 4};
constexpr char const* kPointName[kPoints] = {"Top left", "Top right", "Bottom left", "Bottom right", "Centre"};
constexpr char const* kStrengthName[kStrengths] = {"soft", "medium", "hard"};
constexpr float kMinSpreadDb = 3.f; // Soft under medium under hard by at least this, or that corner again

constexpr char const* kPath = "SETTINGS/PADVEL.BIN";
constexpr uint32_t kMagic = 0x50565631; // "PVV1"

struct Saved {
	uint32_t magic;
	uint8_t enabled;
	uint8_t calibrated;
	uint8_t spare[2];
	float db[kPoints][kStrengths]; // The thump's level, dB of full scale
	uint8_t curve[kCurvePoints];   // The velocity each curve point plays at (added after the first files)
};
constexpr UINT kFirstFileSize = offsetof(Saved, curve);

Saved saved{.curve = {1, 30, 118, 127}};
bool loaded = false;

struct Pending {
	int8_t x, y;
	uint8_t tag;
	int16_t step; // The calibration step it's for, or -1
	int32_t frame;
	uint32_t timer;
};
std::array<Pending, 16> pending;
int32_t numPending = 0;

struct Recorded {
	uint8_t tag; // 0: free
	InstrumentClip* clip;
	int32_t noteRowId;
	int32_t pos;
};
std::array<Recorded, 8> recorded{};

uint8_t currentTag = 0;
uint8_t nextTag = 1;
int8_t lastX = -1, lastY = -1;
uint32_t lastTimer = 0;

// Calibration
bool calibratingNow = false;
int32_t step = 0;
float taps[kSteps];
bool finishQueued = false;
int32_t redoPoint = -1; // A point being tapped again: done after its taps

int32_t dmaFrame() {
	const auto* now = static_cast<int32_t const*>(getRxBufferCurrentPlace());
	return static_cast<int32_t>((now - getRxBufferStart()) / NUM_MONO_INPUT_CHANNELS);
}

bool micAvailable() {
	return !AudioEngine::micPluggedIn && !AudioEngine::lineInPluggedIn;
}

void load() {
	loaded = true;
	FIL file;
	if (f_open(&file, kPath, FA_READ) != FR_OK) {
		return;
	}
	Saved s{};
	UINT got = 0;
	if (f_read(&file, &s, sizeof(s), &got) == FR_OK && got >= kFirstFileSize && s.magic == kMagic) {
		const Saved defaults = saved;
		saved = s;
		if (got < sizeof(s)) {
			std::memcpy(saved.curve, defaults.curve, sizeof(saved.curve)); // A file from before the curve
		}
		enabled = saved.enabled != 0;
	}
	f_close(&file);
}

void save() {
	saved.magic = kMagic;
	saved.enabled = enabled ? 1 : 0;
	FIL file;
	if (f_open(&file, kPath, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
		display->displayPopup("Card?");
		return;
	}
	UINT done = 0;
	f_write(&file, &saved, sizeof(saved), &done);
	f_close(&file);
}

/// The level around a press: dB of full scale, the mic's channel
float levelAt(int32_t frame) {
	int32_t const* const ring = getRxBufferStart();
	int32_t peak = 0;
	for (int32_t i = -kBefore; i < kAfter; i++) {
		const int32_t l = ring[((frame + i + kRing) % kRing) * NUM_MONO_INPUT_CHANNELS];
		peak = std::max(peak, l ^ (l >> 31));
	}
	return peak > 0 ? 20.f * std::log10(static_cast<float>(peak) * (1.f / 2147483648.f)) : -200.f;
}

/// The calibrated level of a strength at a pad: the five points, weighted by closeness (exact on a point)
float expectedDb(int32_t x, int32_t y, int32_t strength) {
	float sum = 0.f, weights = 0.f;
	for (int32_t p = 0; p < kPoints; p++) {
		const float dx = static_cast<float>(x - kPointX[p]);
		const float dy = static_cast<float>(y - kPointY[p]);
		const float d2 = dx * dx + dy * dy;
		if (d2 == 0.f) {
			return saved.db[p][strength];
		}
		sum += saved.db[p][strength] / d2;
		weights += 1.f / d2;
	}
	return sum / weights;
}

uint8_t velocityFor(int32_t x, int32_t y, float db) {
	float at[kStrengths];
	for (int32_t s = 0; s < kStrengths; s++) {
		at[s] = expectedDb(x, y, s);
	}
	// The curve's points in dB: Lightest a band under soft, Soft, Hard, Hardest a band over hard (medium falls between
	// Soft and Hard); straight between them, flat past either end
	const float points[kCurvePoints] = {at[0] - (at[1] - at[0]), at[0], at[2], at[2] + (at[2] - at[1])};
	if (db <= points[0]) {
		return saved.curve[0];
	}
	for (int32_t i = 0; i < kCurvePoints - 1; i++) {
		if (db < points[i + 1]) {
			const float span = std::max(points[i + 1] - points[i], 0.5f);
			const float v = saved.curve[i] + (db - points[i]) * (saved.curve[i + 1] - saved.curve[i]) / span;
			return static_cast<uint8_t>(std::clamp(v + 0.5f, 1.f, 127.f));
		}
	}
	return saved.curve[kCurvePoints - 1];
}

void prompt() {
	const int32_t point = step / (kStrengths * kTaps);
	const int32_t strength = (step / kTaps) % kStrengths;
	const int32_t tap = step % kTaps;
	char text[64];
	std::strcpy(text, kPointName[point]);
	std::strcat(text, " pad\nTap ");
	std::strcat(text, kStrengthName[strength]);
	char n[] = " 1 of 3";
	n[1] = static_cast<char>('1' + tap);
	std::strcat(text, n);
	display->popupText(text);
}

/// After the last tap is in: each point's soft, medium and hard as the middle of its three
void finish(void) {
	finishQueued = false;
	if (numPending > 0) { // A tap still being measured
		finishQueued = true;
		addOnceTask(finish, 100, 0.02, "pad velocity calibration", RESOURCE_NONE);
		return;
	}
	for (int32_t p = 0; p < kPoints; p++) {
		float mid[kStrengths];
		for (int32_t s = 0; s < kStrengths; s++) {
			float t[kTaps];
			for (int32_t k = 0; k < kTaps; k++) {
				t[k] = taps[(p * kStrengths + s) * kTaps + k];
			}
			std::sort(t, t + kTaps);
			mid[s] = t[kTaps / 2];
		}
		if (mid[1] - mid[0] < kMinSpreadDb || mid[2] - mid[1] < kMinSpreadDb) {
			step = p * kStrengths * kTaps; // Not far enough apart: that point again
			redoPoint = p;
			display->popupText("Softer and harder");
			addOnceTask(prompt, 100, 1.5, "pad velocity prompt", RESOURCE_NONE);
			return;
		}
		for (int32_t s = 0; s < kStrengths; s++) {
			saved.db[p][s] = mid[s];
		}
	}
	calibratingNow = false;
	redoPoint = -1;
	saved.calibrated = 1;
	enabled = true;
	save();
	display->cancelPopup();
	display->displayPopup("Calibrated");
}

/// The notes that press recorded: their velocity, if their clip is still the one on screen (else let them be: the
/// clip may be gone)
void correctRecorded(uint8_t tag, uint8_t velocity) {
	for (Recorded& r : recorded) {
		if (r.tag != tag) {
			continue;
		}
		r.tag = 0;
		if (velocity == 0 || r.clip != getCurrentClip()) {
			continue;
		}
		NoteRow* row = r.clip->getNoteRowFromId(r.noteRowId);
		if (row == nullptr) {
			continue;
		}
		const int32_t i = row->notes.search(r.pos, GREATER_OR_EQUAL);
		Note* note = row->notes.getElement(i);
		if (note != nullptr && note->pos == r.pos) {
			note->setVelocity(velocity);
			uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
		}
	}
}

void correct(uint8_t tag, uint8_t velocity) {
	for (Sound* sound : AudioEngine::sounds) {
		for (const Sound::ActiveVoice& voice : sound->voices()) {
			if (voice->padPress() == tag) {
				voice->changeVelocity(velocity);
			}
		}
	}
}

} // namespace

bool pressStart(int32_t x, int32_t y) {
	currentTag = 0;
	if (!loaded) {
		load();
	}
	const uint32_t now = AudioEngine::audioSampleTimer;
	const bool bounce = x == lastX && y == lastY && now - lastTimer < kBounceSamples;
	lastX = static_cast<int8_t>(x);
	lastY = static_cast<int8_t>(y);
	lastTimer = now;

	if (calibratingNow) {
		if (x >= kDisplayWidth) {
			calibratingNow = false; // A sidebar pad: cancel
			display->cancelPopup();
			display->displayPopup("Cancelled");
			return true;
		}
		const int32_t point = step / (kStrengths * kTaps);
		if (bounce || finishQueued || x != kPointX[point] || y != kPointY[point]
		    || numPending == static_cast<int32_t>(pending.size())) {
			return true;
		}
		pending[numPending++] = {
		    static_cast<int8_t>(x), static_cast<int8_t>(y), 0, static_cast<int16_t>(step), dmaFrame(), now};
		step++;
		if (redoPoint >= 0 && step == (redoPoint + 1) * kStrengths * kTaps) {
			step = kSteps;
		}
		if (step == kSteps) {
			finishQueued = true;
			addOnceTask(finish, 100, 0.02, "pad velocity calibration", RESOURCE_NONE);
		}
		else {
			prompt();
		}
		return true;
	}

	if (!enabled || !saved.calibrated || !micAvailable() || bounce
	    || numPending == static_cast<int32_t>(pending.size())) {
		return false;
	}
	currentTag = nextTag;
	nextTag = nextTag == 255 ? 1 : nextTag + 1;
	pending[numPending++] = {static_cast<int8_t>(x), static_cast<int8_t>(y), currentTag, -1, dmaFrame(), now};
	return false;
}

void noteRecorded(InstrumentClip* clip, int32_t noteRowId, int32_t pos) {
	if (currentTag == 0) {
		return;
	}
	for (Recorded& r : recorded) {
		if (r.tag == 0) {
			r = {currentTag, clip, noteRowId, pos};
			return;
		}
	}
}

void pressEnd() {
	currentTag = 0;
}

uint8_t pressTag() {
	return currentTag;
}

void service() {
	if (numPending == 0) {
		return;
	}
	const int32_t now = dmaFrame();
	int32_t kept = 0;
	for (int32_t i = 0; i < numPending; i++) {
		const Pending& p = pending[i];
		const int32_t since = (now - p.frame + kRing) % kRing;
		const bool stale = AudioEngine::audioSampleTimer - p.timer > static_cast<uint32_t>(kStale) || since > kStale;
		if (!stale && since < kAfter) {
			pending[kept++] = p;
			continue;
		}
		const float db = stale ? -200.f : levelAt(p.frame);
		if (p.step >= 0) {
			taps[p.step] = db;
		}
		else if (!stale) {
			const uint8_t v = velocityFor(p.x, p.y, db);
			correct(p.tag, v);
			correctRecorded(p.tag, v);
		}
		else {
			correctRecorded(p.tag, 0); // Too late to measure: frees its notes, as they are
		}
	}
	numPending = kept;
}

void startCalibration() {
	if (!loaded) {
		load();
	}
	if (!micAvailable()) {
		display->displayPopup("Unplug line in / mic");
		return;
	}
	calibratingNow = true;
	finishQueued = false;
	redoPoint = -1;
	step = 0;
	numPending = 0;
	prompt();
}

bool calibrating() {
	return calibratingNow;
}

int32_t curvePoint(int32_t point) {
	if (!loaded) {
		load();
	}
	return saved.curve[point];
}

void setCurvePoint(int32_t point, int32_t velocity) {
	saved.curve[point] = static_cast<uint8_t>(std::clamp<int32_t>(velocity, 1, 127));
	save();
}

bool isEnabled() {
	if (!loaded) {
		load();
	}
	return enabled;
}

void setEnabled(bool on) {
	enabled = on;
	save();
}

} // namespace deluge::pad_velocity
