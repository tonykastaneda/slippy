#ifndef __SLIPPY_KIT_H__
#define __SLIPPY_KIT_H__

// What every command file shares: errors, params, art ids, paint, and the
// command table's shape. Commands.cpp holds the core commands and defines
// these; each Cmd*.cpp adds a family of commands with its Add*Commands.
// Main thread only, like everything that touches the SDK.

#include "IllustratorSDK.h"
#include "Commands.h"
#include "SlippySuites.h"

#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace slippy {

// ---- errors: throw from a command; the dispatcher turns them into JSON-RPC errors

struct CommandError {
	int code;
	std::string message;
	AIErr aiError = kNoErr;
};

[[noreturn]] void Fail(int code, const std::string& message);
std::string ErrText(AIErr e);
void Check(AIErr e, const char* what);   // throws kErrIllustrator when e is set

template <typename T>
T* Need(T* suite, const char* name)
{
	if (!suite) Fail(kErrUnavailable, std::string(name) + " isn't available in this Illustrator");
	return suite;
}

// ---- strings and params

ai::UnicodeString U(const std::string& s);
std::string S(const ai::UnicodeString& u);
std::string Lower(std::string s);

const json::Value& Required(const json::Value& p, const char* key);
std::string ReqStr(const json::Value& p, const char* key);
double ReqNum(const json::Value& p, const char* key);
AIRealPoint Point(const json::Value& v, const char* what);
json::Value PointJson(const AIRealPoint& p);
json::Value RectJson(const AIRealRect& r);

// ---- documents, art and layers

AIDocumentHandle ActiveDocument();   // fails plainly when none is open
// Each open document once, in Illustrator's order, with its window count:
// the document list has a row per window (Window > New Window repeats one).
std::vector<std::pair<AIDocumentHandle, int>> OpenDocuments();
std::string DocName(AIDocumentHandle doc);

std::string ArtId(AIArtHandle art);
AIArtHandle ArtById(const std::string& idText);   // fails with kErrNotFound
bool IsId(const json::Value& v);
std::string IdText(const json::Value& v);
std::vector<AIArtHandle> SelectedArt();
void SelectDeep(AIArtHandle art);   // with everything inside, as a click selects a group
void SelectOnly(const std::vector<AIArtHandle>& arts);
// 'id' / 'ids' from p; or the selection when selectionIfMissing.
std::vector<AIArtHandle> ArtList(const json::Value& p, bool selectionIfMissing = false);
short ArtType(AIArtHandle art);
const char* TypeName(short type);
bool Attr(AIArtHandle art, ai::int32 which);
json::Value ArtSummary(AIArtHandle art, int depth, bool skipClipped = false);
std::string LayerTitle(AILayerHandle layer);
AILayerHandle LayerByParam(const json::Value& p);
// Where new art goes: 'parent' group, top of 'layer', or the current layer.
void Placement(const json::Value& p, ai::int16& order, AIArtHandle& prep);

// ---- text (Commands.cpp)

std::string TextOf(AIArtHandle art);
void SetText(AIArtHandle art, const std::string& text, const json::Value& p);

// ---- paint

json::Value ColorJson(const AIColor& c);
AIColor ParseColor(const json::Value& v, const char* what);
json::Value StyleJson(AIArtHandle art);
void ApplyStyle(AIArtHandle art, const json::Value& p);   // fill, stroke and stroke detail (kPaintOptions)
// Name, style and selection for new art; returns its summary.
json::Value Finish(AIArtHandle art, const json::Value& p);

// ---- paths (Commands.cpp)

// A path's points: {"closed", "segments": [{"p", "in"?, "out"?, "smooth"?, "selected"?}]}.
json::Value PathJson(AIArtHandle art);
// One point as path.create takes it: [x, y], or {"p", "in", "out", "smooth"}.
AIPathSegment SegmentFrom(const json::Value& v);

// Named paint (CmdPaint.cpp): {"swatch"}, {"spot", "tint"}, {"gradient", "angle"...},
// {"pattern", "scale"...} in and out, sized to the art it lands on.
bool NamedPaintFromJson(const json::Value& v, AIColor& c);
json::Value NamedPaintJson(const AIColor& c);
void FitPaintToArt(AIArtHandle art, AIColor& c, const json::Value& spec);
void SetStrokeAlign(AIArtHandle art, const std::string& align);
// opacity / blendMode / knockout / isolate, when present (CmdAppearance.cpp).
void SetBlend(AIArtHandle art, const json::Value& p);

// Dictionaries (effect settings, blend, metadata) as JSON, both ways (CmdAppearance.cpp).
json::Value DictJson(ConstAIDictionaryRef dict);
void JsonIntoDict(const json::Value& v, AIDictionaryRef dict);

// ---- the command table

struct Command {
	std::string description;
	json::Value params;   // name -> "type - meaning"
	std::function<json::Value(const json::Value&)> run;
	bool changesDocument;
};
using CommandTable = std::map<std::string, Command>;

json::Value Params(std::initializer_list<std::pair<const char*, const char*>> list);
json::Value RunCommand(const std::string& method, const json::Value& params);   // one command from another

inline const char* const kWhere = "'layer' (name/index) or 'parent' (group id) to place it; default: top of the current layer";
inline const char* const kPaint = "\"#RRGGBB\" | \"none\" | {\"rgb\":[0-255 x3]} | {\"cmyk\":[0-100 x4]} | {\"gray\":0-100} | "
	"{\"swatch\":name} | {\"spot\":name,\"tint\":0-100} | {\"gradient\":name,\"angle\",\"origin\",\"length\"} | {\"pattern\":name,\"scale\",\"rotate\"}";
inline const char* const kPaintOptions = "stroke detail: dash ([lengths]), dashOffset, cap (butt|round|projecting), join (miter|round|bevel), "
	"miterLimit, strokeAlign (center|inside|outside), fillOverprint, strokeOverprint, evenOdd";
inline const char* const kIds = "string[] - art ids (default: the selection)";

// Families of commands, each in its own file.
void AddCatalogCommands(CommandTable& t);   // CmdCatalog.cpp: menu.list, action.list/describe, tool.*
void AddSymbolCommands(CommandTable& t);    // CmdSymbols.cpp: symbol.*, isolation
void AddViewCommands(CommandTable& t);      // CmdView.cpp: view.*, hit.test
void AddGeometryCommands(CommandTable& t);  // CmdGeometry.cpp: art.distance, art.above
void AddPaintCommands(CommandTable& t);     // CmdPaint.cpp: swatch.*, spot.*, gradient.*, pattern.*
void AddAppearanceCommands(CommandTable& t);   // CmdAppearance.cpp: style.*, appearance.*, effect.*
void AddShapeCommands(CommandTable& t);
void AddTextCommands(CommandTable& t);
void AddDocumentCommands(CommandTable& t);  // CmdDocument.cpp: artboard.*, document settings, layer.tree, select.*, edit.*, guide.*, image.*, data      // CmdText.cpp: text.*, font.list, charStyle.*, paraStyle.*     // CmdShapes.cpp: shape.*, pathfinder.*, compound.*, path.*, envelope.*, repeat.*...

} // namespace slippy

#endif // __SLIPPY_KIT_H__
