#include "IllustratorSDK.h"
#include "SuiteCompat.h"
#include "AINotifier.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" { SPBasicSuite* sSPBasic = nullptr; }
namespace {
std::vector<int> acquired, released;
bool nativeAvailable = false, legacyAvailable = true, invalidLegacy = false;
bool releaseFails = false;
constexpr SPErr kMissing = 1234;
int registrations = 0;
AIAPI AIErr Add(SPPluginRef, const char*, const char*, AINotifierHandle*)
{
    ++registrations;
    return kNoErr;
}
struct LegacyPrefix { decltype(AINotifierSuite::AddNotifier) AddNotifier; };
LegacyPrefix legacy{Add};
AINotifierSuite native{};
SPAPI SPErr Acquire(const char* name, ai::int32 version, const void** out)
{
    acquired.push_back(version);
    *out = nullptr;
    if (std::strcmp(name, kAINotifierSuite)) return kMissing;
    if (version == kAINotifierSuiteVersion && nativeAvailable) { *out = &native; return 0; }
    if (version == AIAPI_VERSION(4) && legacyAvailable) { *out = invalidLegacy ? nullptr : &legacy; return 0; }
    return kMissing;
}
SPAPI SPErr Release(const char*, ai::int32 version)
{
    released.push_back(version);
    return releaseFails ? kMissing : 0;
}
void Reset()
{
    acquired.clear(); released.clear();
    nativeAvailable = false; legacyAvailable = true; invalidLegacy = false; releaseFails = false;
}
}

int main()
{
    SPBasicSuite basic{};
    basic.AcquireSuite = Acquire; basic.ReleaseSuite = Release; sSPBasic = &basic;
    const void* out = nullptr;
    // Current hosts get their native table, not the compatibility shim.
    nativeAvailable = true;
    assert(!SlippyAcquireSuite(kAINotifierSuite, kAINotifierSuiteVersion, &out));
    assert(out == &native && acquired.size() == 1);
    assert(!SlippyReleaseSuite(kAINotifierSuite, kAINotifierSuiteVersion));
    assert(released.back() == kAINotifierSuiteVersion);

    Reset();
    assert(!SlippyAcquireSuite(kAINotifierSuite, kAINotifierSuiteVersion, &out));
    auto* adapter = static_cast<const AINotifierSuite*>(out);
    assert(!adapter->AddNotifier(nullptr, "test", "started", nullptr));
    assert(registrations == 1 && !adapter->AddNotifierEx && !adapter->GetNotifierName);
    assert(SlippyCompatibilitySuites().size() == 1);
    // Acquisitions and releases must balance using the actual host version.
    assert(!SlippyAcquireSuite(kAINotifierSuite, kAINotifierSuiteVersion, &out));
    assert(!SlippyReleaseSuite(kAINotifierSuite, kAINotifierSuiteVersion));
    assert(adapter->AddNotifier && released.back() == AIAPI_VERSION(4));
    releaseFails = true;
    assert(SlippyReleaseSuite(kAINotifierSuite, kAINotifierSuiteVersion) == kMissing);
    assert(adapter->AddNotifier && !SlippyCompatibilitySuites().empty());
    releaseFails = false;
    assert(!SlippyReleaseSuite(kAINotifierSuite, kAINotifierSuiteVersion));
    assert(!adapter->AddNotifier && SlippyCompatibilitySuites().empty());

    Reset(); legacyAvailable = false;
    assert(SlippyAcquireSuite(kAINotifierSuite, kAINotifierSuiteVersion, &out) == kMissing);
    assert(SlippyCompatibilitySuites().empty());
    Reset(); invalidLegacy = true;
    assert(SlippyAcquireSuite(kAINotifierSuite, kAINotifierSuiteVersion, &out) == kMissing);
    assert(released.size() == 1 && released[0] == AIAPI_VERSION(4));
    Reset();
    assert(SlippyAcquireSuite("Unrelated Suite", 987, &out) == kMissing);
    assert(acquired.size() == 1); // Never guess older versions of arbitrary tables.
    std::cout << "Suite compatibility checks passed\n";
}
