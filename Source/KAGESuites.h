#ifndef __KAGE_SUITES_H__
#define __KAGE_SUITES_H__

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

// Timer suite, whichever version this Illustrator has (0 = none).
// Version 6 inserted functions, so older layouts need their own table.
int KAGETimerVersion();
AIErr KAGEAddTimer(SPPluginRef self, const char* name, ai::int32 period, AITimerHandle* timer);
AIErr KAGESetTimerActive(AITimerHandle timer, AIBoolean active);

#endif // __KAGE_SUITES_H__
