#ifndef __SLIPPY_SUITES_H__
#define __SLIPPY_SUITES_H__

#include "IllustratorSDK.h"
#include "Suites.hpp"

#include "AIMenu.h"
#include "AIDocumentList.h"
#include "AIDocument.h"
#include "AILayer.h"
#include "AIArt.h"
#include "AIPath.h"
#include "AIPathStyle.h"
#include "AIArtboard.h"
#include "AIActionManager.h"
#include "AICommandManager.h"
#include "AIMatchingArt.h"
#include "AITransformArt.h"
#include "AITextFrame.h"
#include "AITimer.h"
#include "AIUndo.h"
#include "AIUser.h"
#include "AIUUID.h"
#include "AIRuntime.h"
#include "AIPanel.h"
#include "AIFileFormat.h"
#include "AIAnnotator.h"
#include "AIAnnotatorDrawer.h"
#include "AIDocumentView.h"
#include "AIGroup.h"
#include "AIPlaced.h"
#include "ASUserInteraction.h"
#include "AIScriptMessage.h"
#include "AISymbol.h"
#include "AIIsolationMode.h"
#include "AITool.h"
#include "AIHitTest.h"
#include "AISwatchList.h"
#include "AICustomColor.h"
#include "AIGradient.h"
#include "AIPattern.h"
#include "AIArtStyle.h"
#include "AIArtStyleParser.h"
#include "AILiveEffect.h"
#include "AIDictionary.h"
#include "AIPaintStyle.h"
#include "AIShapeConstruction.h"
#include "AIPathfinder.h"
#include "AIArtConverter.h"
#include "AIExpand.h"
#include "AIEnvelope.h"
#include "AIRepeat.h"
#include "AIPathConstruction.h"
#include "AIFont.h"
#include "AIATEPaint.h"
#include "SPInterf.h"
#include "ATETextSuitesImportHelper.h"
#include "AIAssert.hpp"

extern "C" SPBlocksSuite*			sSPBlocks;
extern "C" AIMenuSuite*				sAIMenu;
extern "C" AIUnicodeStringSuite*	sAIUnicodeString;
extern "C" AIDocumentListSuite*		sAIDocumentList;
extern "C" AIDocumentSuite*			sAIDocument;
extern "C" AILayerSuite*			sAILayer;
extern "C" AIArtSuite*				sAIArt;
extern "C" AIPathSuite*				sAIPath;
extern "C" AIPathStyleSuite*		sAIPathStyle;
extern "C" AIArtboardSuite*			sAIArtboard;
extern "C" AIMatchingArtSuite*		sAIMatchingArt;
extern "C" AIActionManagerSuite*	sAIActionManager;
extern "C" AICommandManagerSuite*	sAICommandManager;
extern "C" AITransformArtSuite*		sAITransformArt;
extern "C" AITextFrameSuite*		sAITextFrame;
extern "C" AIUndoSuite*				sAIUndo;
extern "C" AIUUIDSuite*				sAIUUID;
extern "C" AIRuntimeSuite*			sAIRuntime;
extern "C" AIPanelSuite*			sAIPanel;
extern "C" AIFileFormatSuite*		sAIFileFormat;
extern "C" AIAnnotatorSuite*		sAIAnnotator;
extern "C" AIAnnotatorDrawerSuite*	sAIAnnotatorDrawer;
extern "C" AIDocumentViewSuite*	sAIDocumentView;
extern "C" AIGroupSuite*			sAIGroup;
extern "C" AIPlacedSuite*			sAIPlaced;
extern "C" ASUserInteractionSuite*	sASUserInteraction;
extern "C" SPInterfaceSuite*		sSPInterface;
extern "C" AISymbolSuite*			sAISymbol;
extern "C" AIIsolationModeSuite*	sAIIsolationMode;
extern "C" AIToolSuite*				sAITool;
extern "C" AIHitTestSuite*			sAIHitTest;
extern "C" AISwatchListSuite*		sAISwatchList;
extern "C" AISwatchGroupSuite*		sAISwatchGroup;
extern "C" AICustomColorSuite*		sAICustomColor;
extern "C" AIGradientSuite*			sAIGradient;
extern "C" AIPatternSuite*			sAIPattern;
extern "C" AIArtStyleSuite*			sAIArtStyle;
extern "C" AIArtStyleParserSuite*	sAIArtStyleParser;
extern "C" AILiveEffectSuite*		sAILiveEffect;
extern "C" AIDictionarySuite*		sAIDictionary;
extern "C" AIDictionaryIteratorSuite*	sAIDictionaryIterator;
extern "C" AIPaintStyleSuite*		sAIPaintStyle;
extern "C" AIShapeConstructionSuite*	sAIShapeConstruction;
extern "C" AIPathfinderSuite*		sAIPathfinder;
extern "C" AIArtConverterSuite*		sAIArtConverter;
extern "C" AIExpandSuite*			sAIExpand;
extern "C" AIEnvelopeSuite*			sAIEnvelope;
extern "C" AIRepeatSuite*			sAIRepeat;
extern "C" AIPathConstructionSuite*	sAIPathConstruction;
extern "C" AIFontSuite*				sAIFont;
extern "C" AIATEPaintSuite*			sAIATEPaint;

// Timer suite, whichever version this Illustrator has (0 = none).
// Version 6 inserted functions, so older layouts need their own table.
int SlippyTimerVersion();
// undoable: run in an undo-tracked context (the calls timer); the overlay's
// animation timer changes nothing, so it doesn't need one.
AIErr SlippyAddTimer(SPPluginRef self, const char* name, ai::int32 period, AITimerHandle* timer, bool undoable = true);
AIErr SlippySetTimerActive(AITimerHandle timer, AIBoolean active);

#endif // __SLIPPY_SUITES_H__
