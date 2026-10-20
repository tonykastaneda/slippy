#ifndef SLIPPY_SUITE_COMPAT_H
#define SLIPPY_SUITE_COMPAT_H

#include "IllustratorSDK.h"
#include "SPBasic.h"
#include <string>
#include <vector>

// Used by the generated copy of the SDK's Suites.cpp. The SDK stays untouched.
SPErr SlippyAcquireSuite(const char* name, ai::int32 version, const void** suite);
SPErr SlippyReleaseSuite(const char* name, ai::int32 version);
std::vector<std::string> SlippyCompatibilitySuites();

#endif
