/**************************************************************************/
/*  display_server_3ds.cpp                                                 */
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

#include "display_server_3ds.h"

#include "os_3ds.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/input_event.h"
#include "servers/display/native_menu.h"
#include "rendering/rasterizer_citro3d.h"

#include "ctru.h"

static Vector<String> _get_rendering_drivers() {
	Vector<String> drivers;
	drivers.push_back("citro3d");
	return drivers;
}

static DisplayServer *_create_3ds(const String &p_rendering_driver, DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, DisplayServerEnums::Context p_context, int64_t p_parent_window, Error &r_error) {
	return memnew(DisplayServer3DS(p_mode, p_vsync_mode, r_error));
}

void DisplayServer3DS::register_3ds_driver() {
	register_create_function("3ds", _create_3ds, _get_rendering_drivers);
}

DisplayServer3DS::DisplayServer3DS(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, Error &r_error) {
	native_menu = memnew(NativeMenu);
	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	gfxInitDefault();
	gfxSet3D(true);
	RasterizerCitro3D::make_current();

	WindowData main;
	main.screen = 0;
	windows[DisplayServerEnums::MAIN_WINDOW_ID] = main;
	window_id_counter = DisplayServerEnums::MAIN_WINDOW_ID + 1;
	r_error = OK;
}

DisplayServer3DS::~DisplayServer3DS() {
	gfxExit();
	if (native_menu) {
		memdelete(native_menu);
		native_menu = nullptr;
	}
}

bool DisplayServer3DS::has_feature(DisplayServerEnums::Feature p_feature) const {
	switch (p_feature) {
		case DisplayServerEnums::FEATURE_SUBWINDOWS:
		case DisplayServerEnums::FEATURE_TOUCHSCREEN:
		case DisplayServerEnums::FEATURE_VIRTUAL_KEYBOARD:
			return true;
		default:
			return false;
	}
}

String DisplayServer3DS::get_name() const {
	return "3DS";
}

int DisplayServer3DS::get_screen_count() const {
	return 2;
}

int DisplayServer3DS::get_primary_screen() const {
	return 0;
}

static int _resolve_screen(int p_screen) {
	if (p_screen == DisplayServerEnums::SCREEN_OF_MAIN_WINDOW || p_screen == DisplayServerEnums::SCREEN_PRIMARY || p_screen == DisplayServerEnums::SCREEN_WITH_KEYBOARD_FOCUS || p_screen == DisplayServerEnums::SCREEN_WITH_MOUSE_FOCUS) {
		return p_screen == DisplayServerEnums::SCREEN_WITH_MOUSE_FOCUS ? 1 : 0;
	}
	return CLAMP(p_screen, 0, 1);
}

Point2i DisplayServer3DS::screen_get_position(int p_screen) const {
	// Screens are laid out as on the console: top above bottom, centered.
	return _resolve_screen(p_screen) == 1 ? Point2i(40, SCREEN_HEIGHT) : Point2i();
}

Size2i DisplayServer3DS::screen_get_size(int p_screen) const {
	return screen_size(_resolve_screen(p_screen));
}

Rect2i DisplayServer3DS::screen_get_usable_rect(int p_screen) const {
	return Rect2i(screen_get_position(p_screen), screen_get_size(p_screen));
}

int DisplayServer3DS::screen_get_dpi(int p_screen) const {
	// Top: 400 px across ~3.88 in (New 3DS XL 4.88 in); a mid value.
	return 120;
}

Vector<DisplayServerEnums::WindowID> DisplayServer3DS::get_window_list() const {
	Vector<DisplayServerEnums::WindowID> list;
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		list.push_back(E.key);
	}
	return list;
}

DisplayServerEnums::WindowID DisplayServer3DS::_window_on_screen(int p_screen) const {
	// The most recent window on a screen owns it.
	DisplayServerEnums::WindowID found = DisplayServerEnums::INVALID_WINDOW_ID;
	for (const KeyValue<DisplayServerEnums::WindowID, WindowData> &E : windows) {
		if (E.value.screen == p_screen) {
			found = E.key;
		}
	}
	return found;
}

DisplayServerEnums::WindowID DisplayServer3DS::create_sub_window(DisplayServerEnums::WindowMode p_mode, DisplayServerEnums::VSyncMode p_vsync_mode, uint32_t p_flags, const Rect2i &p_rect, bool p_exclusive, DisplayServerEnums::WindowID p_transient_parent) {
	// Sub-windows are how a game draws to the bottom screen.
	DisplayServerEnums::WindowID id = window_id_counter++;
	WindowData wd;
	wd.screen = 1;
	windows[id] = wd;
	return id;
}

void DisplayServer3DS::show_window(DisplayServerEnums::WindowID p_id) {
	ERR_FAIL_COND(!windows.has(p_id));
	_send_window_event(p_id, DisplayServerEnums::WINDOW_EVENT_FOCUS_IN);
}

void DisplayServer3DS::delete_sub_window(DisplayServerEnums::WindowID p_id) {
	ERR_FAIL_COND_MSG(p_id == DisplayServerEnums::MAIN_WINDOW_ID, "The main window (top screen) can't be deleted.");
	windows.erase(p_id);
	if (touch_window == p_id) {
		touch_window = DisplayServerEnums::INVALID_WINDOW_ID;
		touching = false;
	}
}

DisplayServerEnums::WindowID DisplayServer3DS::get_window_at_screen_position(const Point2i &p_position) const {
	int screen = p_position.y >= SCREEN_HEIGHT ? 1 : 0;
	return _window_on_screen(screen);
}

void DisplayServer3DS::window_attach_instance_id(ObjectID p_instance, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(!windows.has(p_window));
	windows[p_window].instance_id = p_instance;
}

ObjectID DisplayServer3DS::window_get_attached_instance_id(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(!windows.has(p_window), ObjectID());
	return windows[p_window].instance_id;
}

void DisplayServer3DS::window_set_rect_changed_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(!windows.has(p_window));
	windows[p_window].rect_changed_callback = p_callable;
}

void DisplayServer3DS::window_set_window_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(!windows.has(p_window));
	windows[p_window].event_callback = p_callable;
}

void DisplayServer3DS::window_set_input_event_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(!windows.has(p_window));
	windows[p_window].input_event_callback = p_callable;
}

void DisplayServer3DS::window_set_input_text_callback(const Callable &p_callable, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(!windows.has(p_window));
	windows[p_window].input_text_callback = p_callable;
}

int DisplayServer3DS::window_get_current_screen(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(!windows.has(p_window), DisplayServerEnums::INVALID_SCREEN);
	return windows[p_window].screen;
}

void DisplayServer3DS::window_set_current_screen(int p_screen, DisplayServerEnums::WindowID p_window) {
	ERR_FAIL_COND(!windows.has(p_window));
	int screen = _resolve_screen(p_screen);
	if (windows[p_window].screen == screen) {
		return;
	}
	windows[p_window].screen = screen;
	if (windows[p_window].rect_changed_callback.is_valid()) {
		windows[p_window].rect_changed_callback.call(Rect2i(screen_get_position(screen), screen_size(screen)));
	}
}

Point2i DisplayServer3DS::window_get_position(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(!windows.has(p_window), Point2i());
	return screen_get_position(windows[p_window].screen);
}

Point2i DisplayServer3DS::window_get_position_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return window_get_position(p_window);
}

Size2i DisplayServer3DS::window_get_size(DisplayServerEnums::WindowID p_window) const {
	ERR_FAIL_COND_V(!windows.has(p_window), Size2i());
	return screen_size(windows[p_window].screen);
}

Size2i DisplayServer3DS::window_get_size_with_decorations(DisplayServerEnums::WindowID p_window) const {
	return window_get_size(p_window);
}

DisplayServerEnums::WindowMode DisplayServer3DS::window_get_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::WINDOW_MODE_FULLSCREEN;
}

DisplayServerEnums::VSyncMode DisplayServer3DS::window_get_vsync_mode(DisplayServerEnums::WindowID p_window) const {
	return DisplayServerEnums::VSYNC_ENABLED;
}

bool DisplayServer3DS::window_is_focused(DisplayServerEnums::WindowID p_window) const {
	return focused;
}

bool DisplayServer3DS::window_can_draw(DisplayServerEnums::WindowID p_window) const {
	return windows.has(p_window);
}

bool DisplayServer3DS::can_any_window_draw() const {
	return true;
}

Point2i DisplayServer3DS::mouse_get_position() const {
	return Point2i(last_touch);
}

void DisplayServer3DS::virtual_keyboard_show(const String &p_existing_text, const Rect2 &p_screen_rect, DisplayServerEnums::VirtualKeyboardType p_type, int p_max_length, int p_cursor_start, int p_cursor_end) {
	// The system keyboard is modal: it runs here and returns the final text,
	// which is replayed into the focused control as key events.
	SwkbdState swkbd;
	int max_len = p_max_length > 0 ? MIN(p_max_length, 255) : 255;
	bool multiline = p_type == DisplayServerEnums::KEYBOARD_TYPE_MULTILINE;
	swkbdInit(&swkbd, (p_type == DisplayServerEnums::KEYBOARD_TYPE_NUMBER || p_type == DisplayServerEnums::KEYBOARD_TYPE_NUMBER_DECIMAL) ? SWKBD_TYPE_NUMPAD : SWKBD_TYPE_NORMAL, 2, max_len);
	CharString existing = p_existing_text.utf8();
	swkbdSetInitialText(&swkbd, existing.get_data());
	if (p_type == DisplayServerEnums::KEYBOARD_TYPE_PASSWORD) {
		swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
	}
	if (multiline) {
		swkbdSetFeatures(&swkbd, SWKBD_MULTILINE);
	}
	char buf[1024] = { 0 };
	SwkbdButton button = swkbdInputText(&swkbd, buf, sizeof(buf));
	if (button != SWKBD_BUTTON_CONFIRM) {
		return;
	}
	String text = String::utf8(buf);
	Input *input = Input::get_singleton();
	auto key = [&](Key p_key, char32_t p_unicode) {
		for (int pressed = 1; pressed >= 0; pressed--) {
			Ref<InputEventKey> ev;
			ev.instantiate();
			ev->set_pressed(pressed);
			ev->set_keycode(p_key);
			ev->set_physical_keycode(p_key);
			ev->set_unicode(pressed ? p_unicode : 0);
			input->parse_input_event(ev);
		}
	};
	// Clear what the control had, then type the result.
	for (int i = 0; i < p_existing_text.length(); i++) {
		key(Key::BACKSPACE, 0);
	}
	for (int i = 0; i < text.length(); i++) {
		char32_t c = text[i];
		key(c == '\n' ? Key::ENTER : Key::NONE, c == '\n' ? 0 : c);
	}
}

void DisplayServer3DS::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	static_cast<DisplayServer3DS *>(get_singleton())->_dispatch_input_event(p_event);
}

void DisplayServer3DS::_dispatch_input_event(const Ref<InputEvent> &p_event) {
	DisplayServerEnums::WindowID target = DisplayServerEnums::MAIN_WINDOW_ID;
	Ref<InputEventFromWindow> from_window = p_event;
	if (from_window.is_valid()) {
		target = from_window->get_window_id();
	}
	if (!windows.has(target)) {
		target = DisplayServerEnums::MAIN_WINDOW_ID;
	}
	Callable cb = windows[target].input_event_callback;
	if (cb.is_valid()) {
		cb.call(p_event);
	}
}

void DisplayServer3DS::_send_window_event(DisplayServerEnums::WindowID p_window, DisplayServerEnums::WindowEvent p_event) {
	if (windows.has(p_window) && windows[p_window].event_callback.is_valid()) {
		windows[p_window].event_callback.call(int(p_event));
	}
}

void DisplayServer3DS::_process_touch(u32 p_down, u32 p_up, u32 p_held) {
	// Touches go to the window on the bottom screen. With none, they go to
	// the top-screen window, scaled to its size.
	DisplayServerEnums::WindowID win = _window_on_screen(1);
	Vector2 scale(1, 1);
	if (win == DisplayServerEnums::INVALID_WINDOW_ID) {
		win = DisplayServerEnums::MAIN_WINDOW_ID;
		scale = Vector2((float)TOP_WIDTH / BOTTOM_WIDTH, 1);
	}

	if (p_held & KEY_TOUCH) {
		touchPosition tp;
		hidTouchRead(&tp);
		Vector2 pos = Vector2(tp.px, tp.py) * scale;
		if (!touching) {
			touching = true;
			touch_window = win;
			last_touch = pos;
			Ref<InputEventScreenTouch> ev;
			ev.instantiate();
			ev->set_window_id(touch_window);
			ev->set_index(0);
			ev->set_position(pos);
			ev->set_pressed(true);
			Input::get_singleton()->parse_input_event(ev);
		} else if (pos != last_touch) {
			Ref<InputEventScreenDrag> ev;
			ev.instantiate();
			ev->set_window_id(touch_window);
			ev->set_index(0);
			ev->set_position(pos);
			ev->set_relative(pos - last_touch);
			ev->set_relative_screen_position(pos - last_touch);
			last_touch = pos;
			Input::get_singleton()->parse_input_event(ev);
		}
	} else if (touching) {
		touching = false;
		Ref<InputEventScreenTouch> ev;
		ev.instantiate();
		ev->set_window_id(touch_window);
		ev->set_index(0);
		ev->set_position(last_touch);
		ev->set_pressed(false);
		Input::get_singleton()->parse_input_event(ev);
	}
}

void DisplayServer3DS::process_events() {
	hidScanInput();
	u32 down = hidKeysDown();
	u32 up = hidKeysUp();
	u32 held = hidKeysHeld();

	OS_3DS *os = static_cast<OS_3DS *>(OS::get_singleton());
	if (os->get_joypad()) {
		os->get_joypad()->process(down, up, held);
	}
	_process_touch(down, up, held);

	Input::get_singleton()->flush_buffered_events();
}
