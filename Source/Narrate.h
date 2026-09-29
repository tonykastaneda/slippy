#ifndef __KAGE_NARRATE_H__
#define __KAGE_NARRATE_H__

// Plain-English lines for the panel's feed: what a call did ("Rotated “Kage”
// 15°"), or what it tried and why it didn't work. Pure JSON in, text out - no
// SDK - so the preview app uses it too.

#include "Json.h"

#include <string>

namespace kage {

// result: the call's result (null on failure). error: its message ("" on success).
// A failure comes back as "short line\nfull error message".
std::string Narrate(const std::string& method, const json::Value& params, const json::Value& result, const std::string& error);

} // namespace kage

#endif // __KAGE_NARRATE_H__
