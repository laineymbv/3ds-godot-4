/**************************************************************************/
/*  joypad_3ds.cpp                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "joypad_3ds.h"

#include "ctru.h"

struct ButtonMap {
	u32 key;
	JoyButton button;
};

static const ButtonMap BUTTONS[] = {
	{ KEY_B, JoyButton::A },
	{ KEY_A, JoyButton::B },
	{ KEY_Y, JoyButton::X },
	{ KEY_X, JoyButton::Y },
	{ KEY_SELECT, JoyButton::BACK },
	{ KEY_START, JoyButton::START },
	{ KEY_L, JoyButton::LEFT_SHOULDER },
	{ KEY_R, JoyButton::RIGHT_SHOULDER },
	{ KEY_DUP, JoyButton::DPAD_UP },
	{ KEY_DDOWN, JoyButton::DPAD_DOWN },
	{ KEY_DLEFT, JoyButton::DPAD_LEFT },
	{ KEY_DRIGHT, JoyButton::DPAD_RIGHT },
};

JoypadN3DS::JoypadN3DS(Input *p_input) {
	input = p_input;
	bool is_new = false;
	APT_CheckNew3DS(&is_new);
	Dictionary info;
	info["model"] = is_new ? "new_3ds" : "3ds";
	input->joy_connection_changed(DEVICE, true, is_new ? "New Nintendo 3DS" : "Nintendo 3DS", "__3ds_builtin__", info);
	if (is_new) {
		irrstInit(); // C-stick and ZL/ZR on New 3DS come through ir:rst.
	}
	HIDUSER_EnableAccelerometer();
	HIDUSER_EnableGyroscope();
}

JoypadN3DS::~JoypadN3DS() {
	HIDUSER_DisableAccelerometer();
	HIDUSER_DisableGyroscope();
	irrstExit();
}

void JoypadN3DS::_axis(JoyAxis p_axis, float p_value) {
	int i = (int)p_axis;
	if (i < 0 || i >= 6 || last_axes[i] == p_value) {
		return;
	}
	last_axes[i] = p_value;
	input->joy_axis(DEVICE, p_axis, p_value);
}

void JoypadN3DS::process(u32 p_down, u32 p_up, u32 p_held) {
	for (const ButtonMap &m : BUTTONS) {
		if (p_down & m.key) {
			input->joy_button(DEVICE, m.button, true);
		}
		if (p_up & m.key) {
			input->joy_button(DEVICE, m.button, false);
		}
	}

	// ZL/ZR are digital, reported as full-scale triggers.
	_axis(JoyAxis::TRIGGER_LEFT, (p_held & KEY_ZL) ? 1.0f : 0.0f);
	_axis(JoyAxis::TRIGGER_RIGHT, (p_held & KEY_ZR) ? 1.0f : 0.0f);

	circlePosition cp;
	hidCircleRead(&cp);
	// The circle pad reports roughly +-156; Godot's Y axis points down.
	_axis(JoyAxis::LEFT_X, CLAMP(cp.dx / 156.0f, -1.0f, 1.0f));
	_axis(JoyAxis::LEFT_Y, CLAMP(-cp.dy / 156.0f, -1.0f, 1.0f));

	circlePosition cs;
	hidCstickRead(&cs);
	_axis(JoyAxis::RIGHT_X, CLAMP(cs.dx / 146.0f, -1.0f, 1.0f));
	_axis(JoyAxis::RIGHT_Y, CLAMP(-cs.dy / 146.0f, -1.0f, 1.0f));

	accelVector a;
	angularRate g;
	hidAccelRead(&a);
	hidGyroRead(&g);
	// Accelerometer: about 512 units per g. Gyro: raw units, scaled to rad/s.
	Vector3 accel(a.x / 512.0f * 9.80665f, a.y / 512.0f * 9.80665f, a.z / 512.0f * 9.80665f);
	Vector3 gyro(g.x, g.y, g.z);
	gyro *= (float)(Math::PI / 180.0) / 14.375f;
	input->set_accelerometer(accel);
	input->set_gyroscope(gyro);

	last_held = p_held;
}
