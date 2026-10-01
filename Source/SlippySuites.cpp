#include "IllustratorSDK.h"
#include <cstring>
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
	AIPanelFlyoutMenuSuite*	sAIPanelFlyoutMenu = nullptr;
	AIFileFormatSuite*		sAIFileFormat = nullptr;
	AIAnnotatorSuite*		sAIAnnotator = nullptr;
	AIAnnotatorDrawerSuite*	sAIAnnotatorDrawer = nullptr;
	AIDocumentViewSuite*	sAIDocumentView = nullptr;
	AIGroupSuite*			sAIGroup = nullptr;
	AIPlacedSuite*			sAIPlaced = nullptr;
	ASUserInteractionSuite*	sASUserInteraction = nullptr;
	SPInterfaceSuite*		sSPInterface = nullptr;
	AISymbolSuite*			sAISymbol = nullptr;
	AIIsolationModeSuite*	sAIIsolationMode = nullptr;
	AIToolSuite*			sAITool = nullptr;
	AIHitTestSuite*			sAIHitTest = nullptr;
	AISwatchListSuite*		sAISwatchList = nullptr;
	AISwatchGroupSuite*		sAISwatchGroup = nullptr;
	AICustomColorSuite*		sAICustomColor = nullptr;
	AIGradientSuite*		sAIGradient = nullptr;
	AIPatternSuite*			sAIPattern = nullptr;
	AIArtStyleSuite*		sAIArtStyle = nullptr;
	AIArtStyleParserSuite*	sAIArtStyleParser = nullptr;
	AILiveEffectSuite*		sAILiveEffect = nullptr;
	AIDictionarySuite*		sAIDictionary = nullptr;
	AIDictionaryIteratorSuite*	sAIDictionaryIterator = nullptr;
	AIPaintStyleSuite*		sAIPaintStyle = nullptr;
	AIShapeConstructionSuite*	sAIShapeConstruction = nullptr;
	AIPathfinderSuite*		sAIPathfinder = nullptr;
	AIArtConverterSuite*	sAIArtConverter = nullptr;
	AIExpandSuite*			sAIExpand = nullptr;
	AIEnvelopeSuite*		sAIEnvelope = nullptr;
	AIRepeatSuite*			sAIRepeat = nullptr;
	AIPathConstructionSuite*	sAIPathConstruction = nullptr;
	AIFontSuite*			sAIFont = nullptr;
	AIATEPaintSuite*		sAIATEPaint = nullptr;
	AIArtSetSuite*			sAIArtSet = nullptr;
	AIRasterizeSuite*		sAIRasterize = nullptr;
	AIVectorizeSuite*		sAIVectorize = nullptr;
	AIPreferenceSuite*		sAIPreference = nullptr;
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
	kAIPanelFlyoutMenuSuite, kAIPanelFlyoutMenuSuiteVersion, &sAIPanelFlyoutMenu,
	kAIFileFormatSuite, kAIFileFormatSuiteVersion, &sAIFileFormat,
	kAIAnnotatorSuite, kAIAnnotatorSuiteVersion, &sAIAnnotator,		// the canvas overlay (Overlay.h)
	kAIAnnotatorDrawerSuite, kAIAnnotatorDrawerSuiteVersion, &sAIAnnotatorDrawer,
	kAIDocumentViewSuite, kAIDocumentViewSuiteVersion, &sAIDocumentView,
	kAIGroupSuite, kAIGroupSuiteVersion, &sAIGroup,
	kAIPlacedSuite, kAIPlacedSuiteVersion, &sAIPlaced,
	kASUserInteractionSuite, kASUserInteractionSuiteVersion, &sASUserInteraction,	// alerts off while agents run
	kSPInterfaceSuite, kSPInterfaceSuiteVersion, &sSPInterface,		// plugin.message
	kAISymbolSuite, kAISymbolSuiteVersion, &sAISymbol,
	kAIIsolationModeSuite, kAIIsolationModeSuiteVersion, &sAIIsolationMode,
	kAIToolSuite, kAIToolSuiteVersion, &sAITool,
	kAIHitTestSuite, kAIHitTestSuiteVersion, &sAIHitTest,
	kAISwatchListSuite, kAISwatchListSuiteVersion, &sAISwatchList,
	kAISwatchGroupSuite, kAISwatchGroupSuiteVersion, &sAISwatchGroup,
	kAICustomColorSuite, kAICustomColorSuiteVersion, &sAICustomColor,
	kAIGradientSuite, kAIGradientSuiteVersion, &sAIGradient,
	kAIPatternSuite, kAIPatternSuiteVersion, &sAIPattern,
	kAIArtStyleSuite, kAIArtStyleSuiteVersion, &sAIArtStyle,
	kAIArtStyleParserSuite, kAIArtStyleParserSuiteVersion, &sAIArtStyleParser,
	kAILiveEffectSuite, kAILiveEffectSuiteVersion, &sAILiveEffect,
	kAIDictionarySuite, kAIDictionarySuiteVersion, &sAIDictionary,
	kAIDictionaryIteratorSuite, kAIDictionaryIteratorSuiteVersion, &sAIDictionaryIterator,
	kAIPaintStyleSuite, kAIPaintStyleSuiteVersion, &sAIPaintStyle,
	kAIShapeConstructionSuite, kAIShapeConstructionSuiteVersion, &sAIShapeConstruction,
	kAIPathfinderSuite, kAIPathfinderSuiteVersion, &sAIPathfinder,
	kAIArtConverterSuite, kAIArtConverterSuiteVersion, &sAIArtConverter,
	kAIExpandSuite, kAIExpandSuiteVersion, &sAIExpand,
	kAIEnvelopeSuite, kAIEnvelopeSuiteVersion, &sAIEnvelope,
	kAIRepeatSuite, kAIRepeatSuiteVersion, &sAIRepeat,
	kAIPathConstructionSuite, kAIPathConstructionSuiteVersion, &sAIPathConstruction,
	kAIFontSuite, kAIFontSuiteVersion, &sAIFont,
	kAIATEPaintSuite, kAIATEPaintSuiteVersion, &sAIATEPaint,
	kAIArtSetSuite, kAIArtSetSuiteVersion, &sAIArtSet,
	kAIRasterizeSuite, kAIRasterizeSuiteVersion, &sAIRasterize,
	kAIVectorizeSuite, kAIVectorizeSuiteVersion, &sAIVectorize,
	kAIPreferenceSuite, kAIPreferenceSuiteVersion, &sAIPreference,
	IMPORT_TEXT_SUITES
#if AI_ASSERTS_ENABLED
	kAIAssertionSuite, kAIAssertionSuiteVersion, &sAIAssertion,
#endif
	nullptr, 0, nullptr
};

extern "C" SPBasicSuite* sSPBasic;

namespace {
struct Substitute { std::string name; int version; const void* suite; };
std::vector<Substitute> gSubstitutes;
}

// Text engine suites missing at the SDK headers' version: Illustrator 30.2
// has ParaFeatures, ParaInspector and DocumentTextResources only up to v102
// (the headers ask v103), and calling through the null pointer crashed it.
// A newer version only adds functions at the end, so it stands in; an older
// one might not match the headers' layout, so only a documented one is used.
void SlippyAcquireNewerSuites()
{
	bool optional = false;
	for (const ImportSuite* s = gImportSuites; s->name || s->version; s++) {
		if (!s->name) { optional = s->version == kStartOptionalSuites; continue; }
		if (!optional || !s->suite || *(void**) s->suite || strncmp(s->name, "ATE ", 4)) continue;
		std::vector<int> versions;
		for (int v = s->version + 1; v <= s->version + 12; v++) versions.push_back(v);
		// The one older version known to fit: DocumentTextResources v103 only
		// appended two functions (ATE-5451; the header keeps v102's layout).
		if (!strcmp(s->name, "ATE DocumentTextResources Suite")) versions.push_back(s->version - 1);
		for (int v : versions) {
			const void* suite = nullptr;
			if (!sSPBasic->AcquireSuite(s->name, v, &suite) && suite) {
				*(const void**) s->suite = suite;
				gSubstitutes.push_back({s->name, v, suite});
				break;
			}
		}
	}
}

void SlippyReleaseNewerSuites()
{
	for (const Substitute& s : gSubstitutes) sSPBasic->ReleaseSuite(s.name.c_str(), s.version);
	gSubstitutes.clear();
}

std::vector<std::string> SlippyNewerSuites()
{
	std::vector<std::string> list;
	for (const Substitute& s : gSubstitutes) list.push_back(s.name + " v" + std::to_string(s.version));
	return list;
}

// Every optional suite Illustrator didn't hand over ("name vN"); its pointer
// stays null, and calling through one crashes.
std::vector<std::string> SlippyMissingSuites()
{
	std::vector<std::string> missing;
	bool optional = false;
	for (const ImportSuite* s = gImportSuites; s->name || s->version; s++) {
		if (!s->name) { optional = s->version == kStartOptionalSuites; continue; }
		if (!optional || !s->suite || *(void**) s->suite) continue;
		// Which versions this Illustrator does have (asked for, then handed back).
		std::string has;
		for (int v = 1; v <= 200; v++) {
			const void* suite = nullptr;
			if (sSPBasic->AcquireSuite(s->name, v, &suite) || !suite) continue;
			sSPBasic->ReleaseSuite(s->name, v);
			has += (has.empty() ? " (has v" : ", v") + std::to_string(v);
		}
		missing.push_back(std::string(s->name) + " v" + std::to_string(s->version) + (has.empty() ? "" : has + ")"));
	}
	return missing;
}

namespace {
struct TimerSuiteV5 {
	AIAPI AIErr (*AddTimer) (SPPluginRef self, const char* name, ai::int32 period, AITimerHandle* timer);
	AIAPI AIErr (*GetTimerName) (AITimerHandle timer, char** name);
	AIAPI AIErr (*GetTimerActive) (AITimerHandle timer, AIBoolean* active);
	AIAPI AIErr (*SetTimerActive) (AITimerHandle timer, AIBoolean active);
};
}

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
