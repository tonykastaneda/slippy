#ifndef __SLIPPY_VERSION_H__
#define __SLIPPY_VERSION_H__

// Slippy's version, the same for both plug-ins. It lives in one place, the
// VERSION file at the repo's root; the builds turn it into SlippyVersion.h
// (the Makefile on macOS, CMake on Windows), which the .rc files use too.

#include "SlippyVersion.h"

#define kSlippyVersion		SLIPPY_VERSION_STRING

#endif // __SLIPPY_VERSION_H__
