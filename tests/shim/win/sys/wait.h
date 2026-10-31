/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* the host tests on Windows (clang, mingw's headers: tests/run_host_win.sh). mingw has no fork: a test that reaches
 * one ends there as SKIP (exit 77), never as a pass. Anywhere else this is the system's own <sys/wait.h> */
#ifndef _WIN32
#include_next <sys/wait.h>
#else
#pragma once
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
static inline int fork(void)
{
    printf("SKIP: this test forks, and this host cannot\n");
    fflush(stdout);
    exit(77);
}
static inline int waitpid(int p, int *s, int o) { (void)p; (void)o; if (s) *s = 0; return -1; }
#define WIFEXITED(s) 1
#define WEXITSTATUS(s) 0
#define WIFSIGNALED(s) 0
#define WTERMSIG(s) 0
static inline int pipe(int fd[2]) { return _pipe(fd, 4096, _O_BINARY); }
#endif
