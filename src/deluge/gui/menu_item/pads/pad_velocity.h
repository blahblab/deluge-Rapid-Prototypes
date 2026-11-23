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

#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "hid/pad_velocity.h"

namespace deluge::gui::menu_item::pads {

/// Settings > Pads > Velocity: pad velocity from the mic on or off (hid/pad_velocity.h)
class MicVelocity final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(pad_velocity::isEnabled()); }
	void writeCurrentValue() override { pad_velocity::setEnabled(this->getValue()); }
};

/// Settings > Mic velocity > Curve: one of its four points (Lightest, Soft, Hard, Hardest), the velocity that tap
/// strength plays at
class MicVelocityPoint final : public Integer {
public:
	MicVelocityPoint(l10n::String name, int32_t point, char const* label)
	    : Integer(name, name), point_(point), label_(label) {}
	void readCurrentValue() override { this->setValue(pad_velocity::curvePoint(point_)); }
	void writeCurrentValue() override { pad_velocity::setCurvePoint(point_, this->getValue()); }
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 127; }
	void getColumnLabel(StringBuf& label) override { label.append(label_); }

private:
	int32_t point_;
	char const* label_;
};

/// Settings > Mic velocity > Calibrate: leaves the menu and starts the taps
class MicVelocityCalibrate final : public MenuItem {
public:
	using MenuItem::MenuItem;
	MenuItem* selectButtonPress() override {
		soundEditor.exitCompletely();
		pad_velocity::startCalibration();
		return NO_NAVIGATION;
	}
	bool shouldEnterSubmenu() override { return false; }
};

} // namespace deluge::gui::menu_item::pads
