#include "IllustratorSDK.h"
#include "SlippySuites.h"

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
	AIAnnotatorSuite*		sAIAnnotator = nullptr;
	AIAnnotatorDrawerSuite*	sAIAnnotatorDrawer = nullptr;
	AIDocumentViewSuite*	sAIDocumentView = nullptr;
	AIGroupSuite*			sAIGroup = nullptr;
	AIPlacedSuite*			sAIPlaced = nullptr;
	ASUserInteractionSuite*	sASUserInteraction = nullptr;
	SPInterfaceSuite*		sSPInterface = nullptr;
	EXTERN_TEXT_SUITES
#if AI_ASSERTS_ENABLED
	AIAssertionSuite*		sAIAssertion = nullptr;	// used by the SDK's IAIArtboards.cpp asserts
#endif
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
	kAIPanelSuite, kAIPanelSuiteVersion, &sAIPanel,			// the Slippy panel (checked before use)
	kAIFileFormatSuite, kAIFileFormatSuiteVersion, &sAIFileFormat,
	kAIAnnotatorSuite, kAIAnnotatorSuiteVersion, &sAIAnnotator,		// the canvas overlay (Overlay.h)
	kAIAnnotatorDrawerSuite, kAIAnnotatorDrawerSuiteVersion, &sAIAnnotatorDrawer,
	kAIDocumentViewSuite, kAIDocumentViewSuiteVersion, &sAIDocumentView,
	kAIGroupSuite, kAIGroupSuiteVersion, &sAIGroup,
	kAIPlacedSuite, kAIPlacedSuiteVersion, &sAIPlaced,
	kASUserInteractionSuite, kASUserInteractionSuiteVersion, &sASUserInteraction,	// alerts off while agents run
	kSPInterfaceSuite, kSPInterfaceSuiteVersion, &sSPInterface,		// plugin.message
	IMPORT_TEXT_SUITES
#if AI_ASSERTS_ENABLED
	kAIAssertionSuite, kAIAssertionSuiteVersion, &sAIAssertion,
#endif
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

int SlippyTimerVersion()
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

AIErr SlippyAddTimer(SPPluginRef self, const char* name, ai::int32 period, AITimerHandle* timer, bool undoable)
{
	int v = SlippyTimerVersion();
	if (!v) return kCantHappenErr;
	// Version 6 can run the timer in an undo-tracked context, so agent edits
	// land on Edit > Undo; older ones get a standard context set in GoTimer.
	if (v >= 6 && undoable) return ((const AITimerSuite*) gTimerSuite)->AddTimerWithOptions(self, name, period, kAITimerOptionUndoableContext, timer);
	return ((const TimerSuiteV5*) gTimerSuite)->AddTimer(self, name, period, timer);
}

AIErr SlippySetTimerActive(AITimerHandle timer, AIBoolean active)
{
	int v = SlippyTimerVersion();
	if (!v) return kCantHappenErr;
	return v >= 6 ? ((const AITimerSuite*) gTimerSuite)->SetTimerActive(timer, active)
		: ((const TimerSuiteV5*) gTimerSuite)->SetTimerActive(timer, active);
}
