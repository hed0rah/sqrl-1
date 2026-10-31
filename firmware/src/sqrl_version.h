/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 databurn */
/* SQRL-1's version, the one place it is set: INFO, the splash, ABOUT, the console and the manual read it, and
 * tools/build.py --release refuses a number that is not this one. A build that is not a release says so after it
 * (SQRL_DEV). FELUCCA_VERSION stays the Felucca release SQRL-1 is built on */
#ifndef SQRL_VERSION_H
#define SQRL_VERSION_H
#define SQRL_VERSION_NUM "v1.0.1"
#if SQRL_DEV
#define SQRL_VERSION SQRL_VERSION_NUM "-dev"
#else
#define SQRL_VERSION SQRL_VERSION_NUM
#endif
#ifdef SQRL_BUILD                       /* the commit it was built from (tools/build.py): ABOUT, INFO, the console */
#define SQRL_VERSION_ID SQRL_VERSION "+" SQRL_BUILD
#else
#define SQRL_VERSION_ID SQRL_VERSION
#endif
#endif
