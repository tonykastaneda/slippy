#ifndef __SLIPPY_OVERLAY_H__
#define __SLIPPY_OVERLAY_H__

// Shows what Slippy is working on, right on the canvas: a green box around the
// art a call touched and a Slippy cursor that glides there, labelled with the
// call's plain-English line. Drawn by an annotator (like selection
// highlights), so it's never part of the artwork or the undo history.
// Main thread only.

#include "SlippySuites.h"

#include <string>

namespace slippy {
namespace overlay {

void Init(SPPluginRef plugin);   // startup: registers the annotator
void Shutdown();

// Commands.cpp reports what each call touches. Calls between BeginBatch and
// EndBatch (a batch) are shown together, with the last call's line.
void BeginCall();
void Touch(AIArtHandle art);   // measured now, so deleted art still shows
void EndCall(const std::string& line, bool ok);
void BeginBatch();
void EndBatch();

// From the plug-in's message handler.
bool IsTimer(AITimerHandle timer);
void Tick();
AIErr Annotate(const char* selector, AIAnnotatorMessage* message);

} // namespace overlay
} // namespace slippy

#endif // __SLIPPY_OVERLAY_H__
