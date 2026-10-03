#ifndef __SLIPPY_CRASH_LOG_H__
#define __SLIPPY_CRASH_LOG_H__

// If the app (Illustrator, Photoshop) crashes, crash.log (next to calls.log) gets what Slippy was
// running and the stack, then the crash carries on to whoever handled it
// before (Adobe's crash reporter). macOS; a no-op on Windows for now.

#include <string>

namespace slippy {
namespace crashlog {

void Install(const std::string& path, const char* app = "Illustrator");   // main thread, after startup
void Uninstall();

// The call being run, for the report (main thread; cleared when it returns).
void SetCurrentCall(const std::string& method, const std::string& params);
void ClearCurrentCall();

} // namespace crashlog
} // namespace slippy

#endif // __SLIPPY_CRASH_LOG_H__
