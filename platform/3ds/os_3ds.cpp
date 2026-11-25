/**************************************************************************/
/*  os_3ds.cpp                                                            */
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

#include "os_3ds.h"

#include "display_server_3ds.h"

#include "core/config/project_settings.h"
#include "core/os/thread.h"
#include "drivers/unix/ip_unix.h"
#include "drivers/unix/net_socket_unix.h"
#include "main/main.h"

#include "ctru.h"

#include <malloc.h>

#ifdef THREADS_ENABLED
// The 3DS kernel doesn't time-slice threads of equal priority, so a busy
// worker at the main thread's priority would freeze it. Godot's threads run
// below the main thread and use the time it spends waiting (vsync, I/O).
static void _set_thread_priority(Thread::Priority p_priority) {
	s32 main_prio = 0x30;
	int prio = p_priority == Thread::PRIORITY_HIGH ? main_prio + 1 : (p_priority == Thread::PRIORITY_LOW ? 0x3F : 0x3E);
	svcSetThreadPriority(CUR_THREAD_HANDLE, prio);
}
#endif

int OS_3DS::get_processor_count() const {
	// Worker threads share core 0 below the main thread; two is plenty.
	return 2;
}

void OS_3DS::initialize() {
	OS_Unix::initialize_core();
#ifdef THREADS_ENABLED
	Thread::PlatformFunctions pf;
	pf.set_priority = _set_thread_priority;
	Thread::_set_platform_functions(pf);
#endif
	// BSD sockets through soc:u; the service needs a 0x1000-aligned buffer.
	soc_buffer = (u32 *)memalign(0x1000, SOC_BUFFER_SIZE);
	if (soc_buffer && R_SUCCEEDED(socInit(soc_buffer, SOC_BUFFER_SIZE))) {
		IPUnix::make_default();
		NetSocketUnix::make_default();
	} else {
		if (soc_buffer) {
			free(soc_buffer);
			soc_buffer = nullptr;
		}
		ERR_PRINT("3DS: socInit failed; networking is unavailable.");
	}
	DisplayServer3DS::register_3ds_driver();
}

void OS_3DS::initialize_joypads() {
	joypad = memnew(JoypadN3DS(Input::get_singleton()));
}

void OS_3DS::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

MainLoop *OS_3DS::get_main_loop() const {
	return main_loop;
}

void OS_3DS::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;
}

void OS_3DS::finalize() {
	delete_main_loop();
	if (soc_buffer) {
		socExit();
		free(soc_buffer);
		soc_buffer = nullptr;
	}
	if (joypad) {
		memdelete(joypad);
		joypad = nullptr;
	}
}

bool OS_3DS::_check_internal_feature_support(const String &p_feature) {
	if (p_feature == "3ds" || p_feature == "mobile" || p_feature == "arm32" || p_feature == "touchscreen") {
		return true;
	}
	if (p_feature == "new_3ds") {
		bool is_new = false;
		APT_CheckNew3DS(&is_new);
		return is_new;
	}
	return false;
}

String OS_3DS::get_version() const {
	char buf[64] = { 0 };
	if (R_SUCCEEDED(osGetSystemVersionDataString(nullptr, nullptr, buf, sizeof(buf)))) {
		return String::utf8(buf);
	}
	u32 v = osGetFirmVersion();
	return vformat("%d.%d-%d", GET_VERSION_MAJOR(v), GET_VERSION_MINOR(v), GET_VERSION_REVISION(v));
}

Vector<String> OS_3DS::get_video_adapter_driver_info() const {
	Vector<String> info;
	info.push_back("PICA200");
	info.push_back("citro3d");
	return info;
}

String OS_3DS::get_user_data_dir(const String &p_user_dir) const {
	// user:// lives on the SD card next to other homebrew data
	// (p_user_dir is "godot/app_userdata/<name>" or a custom dir name).
	return String("sdmc:/3ds/").path_join(p_user_dir);
}

String OS_3DS::get_data_path() const {
	return "sdmc:/3ds/godot";
}

String OS_3DS::get_config_path() const {
	return "sdmc:/3ds/godot";
}

String OS_3DS::get_cache_path() const {
	return "sdmc:/3ds/godot/cache";
}

String OS_3DS::get_temp_path() const {
	return "sdmc:/3ds/godot/tmp";
}

String OS_3DS::get_executable_path() const {
	return OS::get_executable_path();
}

String OS_3DS::get_locale() const {
	u8 lang = CFG_LANGUAGE_EN;
	if (R_SUCCEEDED(cfguInit())) {
		CFGU_GetSystemLanguage(&lang);
		cfguExit();
	}
	switch (lang) {
		case CFG_LANGUAGE_JP:
			return "ja";
		case CFG_LANGUAGE_FR:
			return "fr";
		case CFG_LANGUAGE_DE:
			return "de";
		case CFG_LANGUAGE_IT:
			return "it";
		case CFG_LANGUAGE_ES:
			return "es";
		case CFG_LANGUAGE_ZH:
			return "zh_CN";
		case CFG_LANGUAGE_KO:
			return "ko";
		case CFG_LANGUAGE_NL:
			return "nl";
		case CFG_LANGUAGE_PT:
			return "pt";
		case CFG_LANGUAGE_RU:
			return "ru";
		case CFG_LANGUAGE_TW:
			return "zh_TW";
		default:
			return "en";
	}
}

extern "C" u32 __ctru_heap_size;
extern "C" u32 __ctru_linear_heap_size;

Dictionary OS_3DS::get_memory_info() const {
	// physical: the application memory region. free: unused Godot heap plus
	// unused linear (GPU) heap. Code and other system use are not counted.
	Dictionary meminfo;
	struct mallinfo mi = mallinfo();
	s64 heap_free = (s64)__ctru_heap_size - (s64)mi.uordblks;
	s64 linear_free = (s64)linearSpaceFree();
	meminfo["physical"] = (s64)osGetMemRegionSize(MEMREGION_APPLICATION);
	meminfo["free"] = heap_free + linear_free;
	meminfo["available"] = heap_free + linear_free;
	meminfo["heap_free"] = heap_free;
	meminfo["heap_used"] = (s64)mi.uordblks;
	meminfo["heap_size"] = (s64)__ctru_heap_size;
	meminfo["linear_size"] = (s64)__ctru_linear_heap_size;
	meminfo["linear_free"] = linear_free;
	meminfo["vram_free"] = (s64)vramSpaceFree();
	meminfo["stack"] = -1;
	return meminfo;
}

Error OS_3DS::get_entropy(uint8_t *r_buffer, int p_bytes) {
	// PS:GenerateRandomBytes is the system CSPRNG.
	if (R_SUCCEEDED(psInit())) {
		Result res = PS_GenerateRandomBytes(r_buffer, p_bytes);
		psExit();
		if (R_SUCCEEDED(res)) {
			return OK;
		}
	}
	u64 seed = svcGetSystemTick();
	for (int i = 0; i < p_bytes; i++) {
		seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
		r_buffer[i] = (uint8_t)(seed >> 56);
	}
	return OK;
}

void OS_3DS::run() {
	if (!main_loop) {
		return;
	}
	main_loop->initialize();
	while (aptMainLoop()) {
		DisplayServer::get_singleton()->process_events();
#ifndef THREADS_ENABLED
		audio_driver.update(); // Threaded builds mix on the audio thread.
#endif
		if (Main::iteration()) {
			break;
		}
	}
	main_loop->finalize();
}

OS_3DS::OS_3DS() {
	osSetSpeedupEnable(true); // 804 MHz + L2 cache on New 3DS; no-op on old models.
	AudioDriverManager::add_driver(&audio_driver);
}

OS_3DS::~OS_3DS() {
}
