/**************************************************************************/
/*  os_3ds.h                                                              */
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

#pragma once

#include "audio_driver_3ds.h"
#include "joypad_3ds.h"

#include "core/input/input.h"
#include "core/os/main_loop.h"
#include "drivers/unix/os_unix.h"
#include "servers/audio/audio_server.h"

class OS_3DS : public OS_Unix {
	MainLoop *main_loop = nullptr;
	JoypadN3DS *joypad = nullptr;
	AudioDriver3DS audio_driver;
	static constexpr uint32_t SOC_BUFFER_SIZE = 0x100000;
	uint32_t *soc_buffer = nullptr;

public:
	void initialize() override;
	void initialize_joypads() override;
	void set_main_loop(MainLoop *p_main_loop) override;
	MainLoop *get_main_loop() const override;
	void delete_main_loop() override;
	void finalize() override;

	bool _check_internal_feature_support(const String &p_feature) override;

	String get_name() const override { return "3DS"; }
	String get_distribution_name() const override { return "Horizon"; }
	String get_version() const override;
	Vector<String> get_video_adapter_driver_info() const override;

	String get_user_data_dir(const String &p_user_dir) const override;
	String get_data_path() const override;
	String get_config_path() const override;
	String get_cache_path() const override;
	String get_temp_path() const override;
	String get_executable_path() const override;
	String get_locale() const override;
	int get_processor_count() const override;
	Dictionary get_memory_info() const override;

	Error get_entropy(uint8_t *r_buffer, int p_bytes) override;

	Error execute(const String &p_path, const List<String> &p_arguments, String *r_pipe = nullptr, int *r_exitcode = nullptr, bool read_stderr = false, Mutex *p_pipe_mutex = nullptr, bool p_open_console = false) override { return ERR_UNAVAILABLE; }
	Error create_process(const String &p_path, const List<String> &p_arguments, ProcessID *r_child_id = nullptr, bool p_open_console = false) override { return ERR_UNAVAILABLE; }
	Error kill(const ProcessID &p_pid) override { return ERR_UNAVAILABLE; }
	bool is_process_running(const ProcessID &p_pid) const override { return false; }
	int get_process_exit_code(const ProcessID &p_pid) const override { return -1; }
	int get_process_id() const override { return 1; }
	Error shell_open(const String &p_uri) override { return ERR_UNAVAILABLE; }

	JoypadN3DS *get_joypad() const { return joypad; }
	AudioDriver3DS *get_audio_driver() { return &audio_driver; }

	void run();

	OS_3DS();
	~OS_3DS();
};
