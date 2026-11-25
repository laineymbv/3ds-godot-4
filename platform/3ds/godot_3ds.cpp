/**************************************************************************/
/*  godot_3ds.cpp                                                         */
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

#include "main/main.h"

#include "ctru.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/stat.h>

// libctru splits application memory into the normal heap (Godot) and the
// GPU-visible linear heap (textures, meshes, audio, 2D streams) before main().
// How much a .3dsx gets varies (about 64 MB from the Homebrew Launcher on
// most consoles, 124 MB for a New 3DS CIA), so the split is chosen here from
// what is actually free, then libctru's own allocator runs (-Wl,--wrap).
extern "C" {
// libctru's default main-thread stack (32 KiB) is far too small for Godot.
u32 __stacksize__ = 1024 * 1024;

extern u32 __ctru_linear_heap_size;
void __real___system_allocateHeaps(void);

void __wrap___system_allocateHeaps(void) {
	const u32 mib = 1024 * 1024;
	u32 remaining = osGetMemRegionFree(MEMREGION_APPLICATION) & ~(mib - 1);
#if THREEDS_MEMORY_LIMIT_MB > 0
	if (remaining > THREEDS_MEMORY_LIMIT_MB * mib) {
		remaining = THREEDS_MEMORY_LIMIT_MB * mib;
	}
#endif
#if THREEDS_LINEAR_HEAP_MB > 0
	u32 linear = THREEDS_LINEAR_HEAP_MB * mib;
#else
	u32 linear = (remaining / 10) * 3; // 30 %
	if (linear < 8 * mib) {
		linear = 8 * mib;
	} else if (linear > 32 * mib) {
		linear = 32 * mib;
	}
#endif
	__ctru_linear_heap_size = linear & ~(mib - 1);
#if THREEDS_MEMORY_LIMIT_MB > 0
	// Without a cap libctru gives the normal heap everything that is left.
	if (remaining > __ctru_linear_heap_size) {
		extern u32 __ctru_heap_size;
		__ctru_heap_size = remaining - __ctru_linear_heap_size;
	}
#endif
	__real___system_allocateHeaps();
}

// std::thread (used by Godot's Thread) can't pass a stack size, and libctru
// gives such threads 32 KiB: far too little for resource loading or
// GDScript. Give them 512 KiB unless a size was asked for.
int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);

int __wrap_pthread_create(pthread_t *r_thread, const pthread_attr_t *p_attr, void *(*p_func)(void *), void *p_arg) {
	size_t size = 0;
	if (p_attr) {
		pthread_attr_getstacksize(p_attr, &size);
	}
	if (size >= 64 * 1024) {
		return __real_pthread_create(r_thread, p_attr, p_func, p_arg);
	}
	pthread_attr_t attr;
	if (p_attr) {
		attr = *p_attr;
	} else {
		pthread_attr_init(&attr);
	}
	pthread_attr_setstacksize(&attr, 512 * 1024);
	return __real_pthread_create(r_thread, &attr, p_func, p_arg);
}
}

// The game is a .pck inside the .3dsx RomFS. A path given by the launcher
// (argv[1], e.g. from 3dslink) overrides it for quick testing.
static const char *DEFAULT_PACK = "romfs:/game.pck";

#include "core/string/ustring.h"
#include "core/templates/vector.h"

int main(int argc, char *argv[]) {
	romfsInit();

	// Godot's stdout/stderr go to a file on the SD card: the 3DS has no
	// console window, and this is how logs are read back from emulators.
	mkdir("sdmc:/3ds", 0777);
	mkdir("sdmc:/3ds/godot", 0777);
	freopen("sdmc:/3ds/godot/stdout.txt", "w", stdout);
	freopen("sdmc:/3ds/godot/stdout.txt", "a", stderr);
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IOLBF, 0);

	OS_3DS os;
	setlocale(LC_CTYPE, "");

	// Arguments: the .pck from RomFS, then whatever the launcher passed
	// (3dslink: `3dslink game.3dsx -- --remote-debug tcp://pc:6007`), then
	// one argument per line from sdmc:/3ds/godot/args.txt if it exists.
	Vector<CharString> args;
	String pack = DEFAULT_PACK;
	for (int i = 1; i < argc; i++) {
		String a = String::utf8(argv[i]);
		if (i == 1 && a.ends_with(".pck")) {
			pack = a;
		} else {
			args.push_back(a.utf8());
		}
	}
	FILE *af = fopen("sdmc:/3ds/godot/args.txt", "r");
	if (af) {
		char line[512];
		while (fgets(line, sizeof(line), af)) {
			String a = String::utf8(line).strip_edges();
			if (!a.is_empty() && !a.begins_with("#")) {
				args.push_back(a.utf8());
			}
		}
		fclose(af);
	}
	args.insert(0, pack.utf8());
	args.insert(0, CharString("--main-pack"));
	Vector<char *> arg_ptrs;
	for (CharString &c : args) {
		arg_ptrs.push_back(c.ptrw());
		printf("arg: %s\n", c.get_data());
	}
	Error err = Main::setup(argc > 0 ? argv[0] : "sdmc:/3ds/godot.3dsx", arg_ptrs.size(), arg_ptrs.ptrw());
	if (err != OK) {
		printf("Main::setup failed: %d\n", (int)err);
		fflush(stdout);
		romfsExit();
		return err == ERR_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();
	fflush(stdout);
	romfsExit();
	return os.get_exit_code();
}
