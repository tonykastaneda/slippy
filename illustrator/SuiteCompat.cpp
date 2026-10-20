#include "IllustratorSDK.h"
#include "SuiteCompat.h"
#include "AINotifier.h"
#include <cstring>

extern "C" SPBasicSuite* sSPBasic;

namespace {
// Illustrator 30.1 exposes v4. v5 inserted AddNotifierEx after AddNotifier,
// so casting the entire v4 table to v5 would shift every subsequent member.
// The SDK's Plugin.cpp uses only AddNotifier. Expose that shared first slot
// through a zero-initialized v5 table; never expose the mismatched tail.
struct NotifierV4Prefix {
    decltype(AINotifierSuite::AddNotifier) AddNotifier;
};
static_assert(kAINotifierSuiteVersion == AIAPI_VERSION(5),
              "Review the Notifier adapter when upgrading the Illustrator SDK");
AINotifierSuite gNotifierAdapter{};
unsigned gNotifierReferences = 0;
constexpr ai::int32 kNotifierV4 = AIAPI_VERSION(4);

bool IsNotifierV5(const char* name, ai::int32 version)
{
    return name && !std::strcmp(name, kAINotifierSuite) && version == kAINotifierSuiteVersion;
}
}

SPErr SlippyAcquireSuite(const char* name, ai::int32 version, const void** suite)
{
    // Prefer the native table, including on newer Illustrator installations.
    SPErr error = sSPBasic->AcquireSuite(name, version, suite);
    if (!error || !suite || !IsNotifierV5(name, version)) return error;

    const void* legacy = nullptr;
    SPErr fallback = sSPBasic->AcquireSuite(name, kNotifierV4, &legacy);
    if (fallback) return error;
    if (!legacy || !static_cast<const NotifierV4Prefix*>(legacy)->AddNotifier) {
        sSPBasic->ReleaseSuite(name, kNotifierV4);
        return error;
    }
    gNotifierAdapter.AddNotifier = static_cast<const NotifierV4Prefix*>(legacy)->AddNotifier;
    ++gNotifierReferences;
    *suite = &gNotifierAdapter;
    return kSPNoError;
}

SPErr SlippyReleaseSuite(const char* name, ai::int32 version)
{
    if (IsNotifierV5(name, version) && gNotifierReferences) {
        SPErr error = sSPBasic->ReleaseSuite(name, kNotifierV4);
        if (!error && !--gNotifierReferences) gNotifierAdapter = {};
        return error;
    }
    return sSPBasic->ReleaseSuite(name, version);
}

std::vector<std::string> SlippyCompatibilitySuites()
{
    if (gNotifierReferences) return {"AI Notifier Suite v1004 (AddNotifier adapter)"};
    return {};
}
