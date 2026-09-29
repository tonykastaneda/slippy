#include "IllustratorSDK.h"
#include "KAGESuites.h"

extern "C" {
	SPBlocksSuite*			sSPBlocks = nullptr;
	AIMenuSuite*			sAIMenu = nullptr;
	AIUnicodeStringSuite*	sAIUnicodeString = nullptr;
	AIDocumentListSuite*	sAIDocumentList = nullptr;
	AIDocumentSuite*		sAIDocument = nullptr;
	AILayerSuite*			sAILayer = nullptr;
	AIArtSuite*				sAIArt = nullptr;
	AIPathSuite*			sAIPath = nullptr;
	AIPathStyleSuite*		sAIPathStyle = nullptr;
	AIArtboardSuite*		sAIArtboard = nullptr;
	AIMatchingArtSuite*		sAIMatchingArt = nullptr;
	AIActionManagerSuite*	sAIActionManager = nullptr;
	AICommandManagerSuite*	sAICommandManager = nullptr;
	AITransformArtSuite*	sAITransformArt = nullptr;
	AITextFrameSuite*		sAITextFrame = nullptr;
	AIUndoSuite*			sAIUndo = nullptr;
	AIUUIDSuite*			sAIUUID = nullptr;
	AIRuntimeSuite*			sAIRuntime = nullptr;
	AIPanelSuite*			sAIPanel = nullptr;
	AIFileFormatSuite*		sAIFileFormat = nullptr;
	EXTERN_TEXT_SUITES
	AIAssertionSuite*		sAIAssertion = nullptr;	// used by the SDK's IAIArtboards.cpp asserts
};

// Only what the plug-in can't start without is required; commands check the
// optional suites before use and report what's missing instead.
ImportSuite gImportSuites[] =
{
	kSPBlocksSuite, kSPBlocksSuiteVersion, &sSPBlocks,
	kAIMenuSuite, kAIMenuSuiteVersion, &sAIMenu,
	kAIUnicodeStringSuite, kAIUnicodeStringVersion, &sAIUnicodeString,
	kAIDocumentListSuite, kAIDocumentListSuiteVersion, &sAIDocumentList,
	kAIDocumentSuite, kAIDocumentSuiteVersion, &sAIDocument,
	kAILayerSuite, kAILayerSuiteVersion, &sAILayer,
	kAIArtSuite, kAIArtSuiteVersion, &sAIArt,
	nullptr, kStartOptionalSuites, nullptr,
	kAIPathSuite, kAIPathSuiteVersion, &sAIPath,
	kAIPathStyleSuite, kAIPathStyleSuiteVersion, &sAIPathStyle,
	kAIArtboardSuite, kAIArtboardSuiteVersion, &sAIArtboard,
	kAIMatchingArtSuite, kAIMatchingArtSuiteVersion, &sAIMatchingArt,
	kAIActionManagerSuite, kAIActionManagerSuiteVersion, &sAIActionManager,
	kAICommandManagerSuite, kAICommandManagerSuiteVersion, &sAICommandManager,
	kAITransformArtSuite, kAITransformArtSuiteVersion, &sAITransformArt,
	kAITextFrameSuite, kAITextFrameSuiteVersion, &sAITextFrame,
	kAIUndoSuite, kAIUndoSuiteVersion, &sAIUndo,
	kAIUUIDSuite, kAIUUIDSuiteVersion, &sAIUUID,
	kAIRuntimeSuite, kAIRuntimeSuiteVersion, &sAIRuntime,
	kAIPanelSuite, kAIPanelSuiteVersion, &sAIPanel,			// the KAGE panel (checked before use)
	kAIFileFormatSuite, kAIFileFormatSuiteVersion, &sAIFileFormat,
	IMPORT_TEXT_SUITES
	kAIAssertionSuite, kAIAssertionSuiteVersion, &sAIAssertion,
	nullptr, 0, nullptr
};

namespace {
struct TimerSuiteV5 {
	AIAPI AIErr (*AddTimer) (SPPluginRef self, const char* name, ai::int32 period, AITimerHandle* timer);
	AIAPI AIErr (*GetTimerName) (AITimerHandle timer, char** name);
	AIAPI AIErr (*GetTimerActive) (AITimerHandle timer, AIBoolean* active);
	AIAPI AIErr (*SetTimerActive) (AITimerHandle timer, AIBoolean active);
};
}

extern "C" SPBasicSuite* sSPBasic;

static int gTimerVersion = -1;
static const void* gTimerSuite = nullptr;

int KAGETimerVersion()
{
	if (gTimerVersion < 0) {
		gTimerVersion = 0;
		for (int v = 6; v >= 2 && !gTimerSuite; v--) {
			if (!sSPBasic->AcquireSuite(kAITimerSuite, AIAPI_VERSION(v), &gTimerSuite) && gTimerSuite) gTimerVersion = v;
			else gTimerSuite = nullptr;
		}
	}
	return gTimerVersion;
}

AIErr KAGEAddTimer(SPPluginRef self, const char* name, ai::int32 period, AITimerHandle* timer)
{
	int v = KAGETimerVersion();
	if (!v) return kCantHappenErr;
	// Version 6 can run the timer in an undo-tracked context, so agent edits
	// land on Edit > Undo; older ones get a standard context set in GoTimer.
	if (v >= 6) return ((const AITimerSuite*) gTimerSuite)->AddTimerWithOptions(self, name, period, kAITimerOptionUndoableContext, timer);
	return ((const TimerSuiteV5*) gTimerSuite)->AddTimer(self, name, period, timer);
}

AIErr KAGESetTimerActive(AITimerHandle timer, AIBoolean active)
{
	int v = KAGETimerVersion();
	if (!v) return kCantHappenErr;
	return v >= 6 ? ((const AITimerSuite*) gTimerSuite)->SetTimerActive(timer, active)
		: ((const TimerSuiteV5*) gTimerSuite)->SetTimerActive(timer, active);
}
