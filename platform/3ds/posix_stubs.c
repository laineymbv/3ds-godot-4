/**************************************************************************/
/*  posix_stubs.c                                                         */
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

// POSIX process functions that the shared unix driver references but the 3DS
// has no equivalent for. They fail with ENOSYS; OS_3DS already reports
// ERR_UNAVAILABLE for execute()/create_process() before reaching them.

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>

int pipe(int fds[2]) {
	(void)fds;
	errno = ENOSYS;
	return -1;
}

int execvp(const char *file, char *const argv[]) {
	(void)file;
	(void)argv;
	errno = ENOSYS;
	return -1;
}

pid_t setsid(void) {
	errno = ENOSYS;
	return -1;
}

pid_t waitpid(pid_t pid, int *status, int options) {
	(void)pid;
	(void)status;
	(void)options;
	errno = ECHILD;
	return -1;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old) {
	(void)sig;
	(void)act;
	if (old) {
		old->sa_handler = SIG_DFL;
	}
	return 0;
}

FILE *popen(const char *command, const char *mode) {
	(void)command;
	(void)mode;
	errno = ENOSYS;
	return NULL;
}

int pclose(FILE *stream) {
	(void)stream;
	errno = ENOSYS;
	return -1;
}

int mkfifo(const char *path, mode_t mode) {
	(void)path;
	(void)mode;
	errno = ENOSYS;
	return -1;
}
