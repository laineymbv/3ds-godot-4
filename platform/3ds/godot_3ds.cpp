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
#if THREEDS_LINEAR_HEAP_MB > 0
	__ctru_linear_heap_size = THREEDS_LINEAR_HEAP_MB * 1024 * 1024;
#else
	u32 remaining = osGetMemRegionFree(MEMREGION_APPLICATION) & ~0xFFFFF;
	u32 linear = (remaining / 10) * 3; // 30 %
	const u32 mib = 1024 * 1024;
	if (linear < 8 * mib) {
		linear = 8 * mib;
	} else if (linear > 32 * mib) {
		linear = 32 * mib;
	}
	__ctru_linear_heap_size = linear & ~(mib - 1);
#endif
	__real___system_allocateHeaps();
}
}

// The game is a .pck inside the .3dsx RomFS. A path given by the launcher
// (argv[1], e.g. from 3dslink) overrides it for quick testing.
static const char *DEFAULT_PACK = "romfs:/game.pck";

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

	const char *pack = DEFAULT_PACK;
	if (argc > 1 && argv[1] && strstr(argv[1], ".pck")) {
		pack = argv[1];
	}
	const char *args[] = { "--main-pack", pack };
	Error err = Main::setup(argc > 0 ? argv[0] : "sdmc:/3ds/godot.3dsx", 2, (char **)args);
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
