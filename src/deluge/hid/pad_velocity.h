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

#pragma once
#include <cstdint>

class InstrumentClip;

/// Pad velocity from the mic (2026-11-22). A pad's note starts at once as ever; 2.5 ms later the finger's thump in the
/// internal mic says how hard it was, and the voices that press started take that velocity. Only while nothing is
/// plugged into line in or the mic jack (either switches the internal mic off), and once calibrated: the four corners
/// and the centre, soft, medium and hard, three taps each, interpolated in dB across the grid
namespace deluge::pad_velocity {

extern bool enabled; ///< Settings > Pads > Velocity (saved with the calibration)

/// From the matrix driver, around the UI's handling of a pad press: the voices started in between belong to it.
/// True if the press was taken by the calibration (the UI mustn't see it)
bool pressStart(int32_t x, int32_t y);
void pressEnd();
/// The press now starting notes (0: none): Voice::noteOn keeps it
uint8_t pressTag();

/// A note recorded into a clip during a press (InstrumentClip::recordNoteOn): it takes the measured velocity too, so
/// the grid's brightness and playback match what was heard
void noteRecorded(InstrumentClip* clip, int32_t noteRowId, int32_t pos);

/// From the audio routine: measures presses 2.5 ms on and corrects their voices
void service();

/// Settings > Pads > Velocity: read (from the card the first time) and set (saved)
bool isEnabled();
void setEnabled(bool on);

/// Settings > Mic velocity > Curve: the velocity (1 to 127) four tap strengths play at: Lightest (a band under soft),
/// Soft, Hard and Hardest (a band over hard), as calibrated at each pad; straight between them in dB
constexpr int32_t kCurvePoints = 4;
int32_t curvePoint(int32_t point);
void setCurvePoint(int32_t point, int32_t velocity);

/// Settings > Mic velocity > Calibrate: prompts on the display; pads are the calibration's until it's done. A sidebar
/// pad cancels
void startCalibration();
[[nodiscard]] bool calibrating();

} // namespace deluge::pad_velocity
