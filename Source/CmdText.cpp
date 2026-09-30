// Text: area text and text on a path (AITextFrame), character and paragraph
// formatting on whole frames or ranges (the ATE text engine's features),
// character / paragraph styles (the document's text resources), fonts
// (AIFont), threading, outlines, and find / replace across the document.

#include "Kit.h"
#include "IText.h"

#include <algorithm>
#include <cmath>

namespace slippy {

namespace {

using ATETextDOM::Unicode;

std::basic_string<ASUnicode> Wide(const std::string& s) { return U(s).as_ASUnicode(); }
const Unicode* W(const std::basic_string<ASUnicode>& s) { return (const Unicode*) s.c_str(); }

std::vector<AIArtHandle> TextFrames(const json::Value& p)
{
	Need(sAITextFrame, "The text frame suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	for (AIArtHandle a : arts) if (ArtType(a) != kTextFrameArt) Fail(kErrInvalidParams, "art " + ArtId(a) + " isn't text");
	return arts;
}

// The frame's text, or the part 'range' [start, end) of it (characters, from 0).
ATE::ITextRange RangeOf(AIArtHandle frame, const json::Value& p)
{
	TextRangeRef ref = nullptr;
	Check(sAITextFrame->GetATETextRange(frame, &ref), "GetATETextRange");
	ATE::ITextRange range(ref);
	if (p.has("range")) {
		AIRealPoint r = Point(p.get("range"), "range");
		ATETextDOM::Int32 start = range.GetStart(), end = range.GetEnd();
		ATETextDOM::Int32 a = start + (ATETextDOM::Int32) std::max(0.0, (double) r.h), b = start + (ATETextDOM::Int32) std::max(0.0, (double) r.v);
		range.SetRange(std::min(a, end), std::min(std::max(a, b), end));
	}
	return range;
}

// ---- fonts

template <typename Get>
std::string FontString(AIFontKey key, Get get)
{
	char buf[256] = {0};
	return get(key, buf, sizeof buf) ? "" : buf;
}

// A font by PostScript name ("Helvetica-Bold"), or family [+ style] ("Helvetica", "Bold").
AIFontKey FindFontKey(const std::string& name, const std::string& style)
{
	Need(sAIFont, "The font suite");
	AIFontKey key = nullptr;
	if (style.empty() && !sAIFont->FindFont(name.c_str(), kAIAnyFontTechnology, kUnknownAIScript, false, &key) && key) return key;
	ai::int32 count = 0;
	sAIFont->CountFonts(&count);
	AIFontKey family = nullptr;
	for (ai::int32 i = 0; i < count; i++) {
		AIFontKey k = nullptr;
		if (sAIFont->IndexFontList(i, &k) || !k) continue;
		std::string ps = FontString(k, sAIFont->GetPostScriptFontName), fam = FontString(k, sAIFont->GetFontFamilyUIName),
			sty = FontString(k, sAIFont->GetFontStyleUIName), full = FontString(k, sAIFont->GetFullFontName);
		if (style.empty() && (Lower(ps) == Lower(name) || Lower(full) == Lower(name))) return k;
		if (Lower(fam) == Lower(name)) {
			if (!style.empty() && Lower(sty) == Lower(style)) return k;
			if (!family || Lower(sty) == "regular" || Lower(sty) == "roman") family = k;
		}
	}
	if (family && style.empty()) return family;
	Fail(kErrNotFound, "no font '" + name + (style.empty() ? "" : " " + style) + "' - see font.list");
}

json::Value FontJson(AIFontKey key)
{
	json::Value v;
	v["name"] = FontString(key, sAIFont->GetPostScriptFontName);
	v["family"] = FontString(key, sAIFont->GetFontFamilyUIName);
	v["style"] = FontString(key, sAIFont->GetFontStyleUIName);
	return v;
}

json::Value FontList(const json::Value& p)
{
	Need(sAIFont, "The font suite");
	std::string search = Lower(p.str("search", ""));
	int limit = (int) p.num("limit", 200);
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAIFont->CountFonts(&count);
	for (ai::int32 i = 0; i < count && (int) list.size() < limit; i++) {
		AIFontKey k = nullptr;
		if (sAIFont->IndexFontList(i, &k) || !k) continue;
		json::Value f = FontJson(k);
		if (!search.empty() && Lower(f.str("name")).find(search) == std::string::npos && Lower(f.str("family")).find(search) == std::string::npos) continue;
		list.push(f);
	}
	json::Value v;
	v["fonts"] = list;
	v["total"] = count;
	return v;
}

// ---- features

ATE::IApplicationPaint Paint(const AIColor& c)
{
	Need(sAIATEPaint, "The text paint suite");
	ATE::ApplicationPaintRef ref = nullptr;
	Check(sAIATEPaint->CreateATEApplicationPaint(&c, &ref), "CreateATEApplicationPaint");
	return ATE::IApplicationPaint(ref);
}

bool HasCharFeature(const json::Value& p)
{
	for (const char* k : {"font", "size", "leading", "tracking", "baselineShift", "horizontalScale", "verticalScale", "caps", "underline", "strikethrough", "fill", "stroke", "strokeWidth"})
		if (p.has(k)) return true;
	return false;
}

bool HasParaFeature(const json::Value& p)
{
	for (const char* k : {"align", "firstIndent", "leftIndent", "rightIndent", "spaceBefore", "spaceAfter", "hyphenate"})
		if (p.has(k)) return true;
	return false;
}

ATE::ICharFeatures CharFeatures(const json::Value& p)
{
	ATE::ICharFeatures f;
	if (p.has("font")) {
		FontRef font = nullptr;
		Check(sAIFont->FontFromFontKey(FindFontKey(ReqStr(p, "font"), p.str("fontStyle", "")), &font), "FontFromFontKey");
		f.SetFont(ATE::IFont(font));
	}
	if (p.has("size")) f.SetFontSize((ATETextDOM::Real) ReqNum(p, "size"));
	if (p.has("leading")) {
		if (p.get("leading").isString() && p.get("leading").asString() == "auto") f.SetAutoLeading(true);
		else { f.SetAutoLeading(false); f.SetLeading((ATETextDOM::Real) ReqNum(p, "leading")); }
	}
	if (p.has("tracking")) f.SetTracking((ATETextDOM::Int32) ReqNum(p, "tracking"));
	if (p.has("baselineShift")) f.SetBaselineShift((ATETextDOM::Real) ReqNum(p, "baselineShift"));
	if (p.has("horizontalScale")) f.SetHorizontalScale((ATETextDOM::Real) (ReqNum(p, "horizontalScale") / 100));
	if (p.has("verticalScale")) f.SetVerticalScale((ATETextDOM::Real) (ReqNum(p, "verticalScale") / 100));
	if (p.has("caps")) {
		std::string c = ReqStr(p, "caps");
		f.SetFontCapsOption(c == "smallCaps" ? ATE::kFontSmallCaps : c == "allCaps" ? ATE::kFontAllCaps : c == "allSmallCaps" ? ATE::kFontAllSmallCaps : ATE::kFontNormalCaps);
	}
	if (p.has("underline")) f.SetUnderlinePosition(p.boolean("underline", false) ? ATE::kUnderlineOn_RightInVertical : ATE::kUnderlineOff);
	if (p.has("strikethrough")) f.SetStrikethroughPosition(p.boolean("strikethrough", false) ? ATE::kStrikethroughOn_XHeight : ATE::kStrikethroughOff);
	if (p.has("fill")) {
		AIColor c = ParseColor(p.get("fill"), "fill");
		f.SetFill(c.kind != kNoneColor);
		if (c.kind != kNoneColor) f.SetFillColor(Paint(c));
	}
	if (p.has("stroke")) {
		AIColor c = ParseColor(p.get("stroke"), "stroke");
		f.SetStroke(c.kind != kNoneColor);
		if (c.kind != kNoneColor) f.SetStrokeColor(Paint(c));
	}
	if (p.has("strokeWidth")) f.SetLineWidth((ATETextDOM::Real) ReqNum(p, "strokeWidth"));
	return f;
}

ATE::IParaFeatures ParaFeatures(const json::Value& p)
{
	ATE::IParaFeatures f;
	if (p.has("align")) {
		std::string a = ReqStr(p, "align");
		ATE::ParagraphJustification j = a == "left" ? ATE::kLeftJustify : a == "center" ? ATE::kCenterJustify : a == "right" ? ATE::kRightJustify
			: a == "justify" ? ATE::kFullJustifyLastLineLeft : a == "justifyCenter" ? ATE::kFullJustifyLastLineCenter
			: a == "justifyRight" ? ATE::kFullJustifyLastLineRight : a == "justifyAll" ? ATE::kFullJustifyLastLineFull
			: (Fail(kErrInvalidParams, "'align' must be left, center, right, justify, justifyCenter, justifyRight or justifyAll"), ATE::kLeftJustify);
		f.SetJustification(j);
	}
	if (p.has("firstIndent")) f.SetFirstLineIndent((ATETextDOM::Real) ReqNum(p, "firstIndent"));
	if (p.has("leftIndent")) f.SetStartIndent((ATETextDOM::Real) ReqNum(p, "leftIndent"));
	if (p.has("rightIndent")) f.SetEndIndent((ATETextDOM::Real) ReqNum(p, "rightIndent"));
	if (p.has("spaceBefore")) f.SetSpaceBefore((ATETextDOM::Real) ReqNum(p, "spaceBefore"));
	if (p.has("spaceAfter")) f.SetSpaceAfter((ATETextDOM::Real) ReqNum(p, "spaceAfter"));
	if (p.has("hyphenate")) f.SetAutoHyphenate(p.boolean("hyphenate", false));
	return f;
}

json::Value PaintJson(ATE::IApplicationPaint paint)
{
	AIColor c;
	if (paint.IsNull() || !sAIATEPaint || sAIATEPaint->GetAIColor(paint.GetRef(), &c)) return json::Value();
	return ColorJson(c);
}

// What the range has in common; a feature that varies across it is "mixed".
json::Value FeaturesJson(ATE::ITextRange range)
{
	json::Value v;
	bool set = false;
	ATE::ICharFeatures c = range.GetUniqueCharFeatures();
	ATE::IFont font = c.GetFont(&set);
	if (set && !font.IsNull() && sAIFont) {
		AIFontKey key = nullptr;
		if (!sAIFont->FontKeyFromFont(font.GetRef(), &key) && key) v["font"] = FontJson(key);
	}
	else v["font"] = "mixed";
	double size = c.GetFontSize(&set);
	v["size"] = set ? json::Value(size) : json::Value("mixed");
	bool autoLeading = c.GetAutoLeading(&set);
	if (set && autoLeading) v["leading"] = "auto";
	else { double l = c.GetLeading(&set); if (set) v["leading"] = l; }
	int tracking = c.GetTracking(&set);
	if (set && tracking) v["tracking"] = tracking;
	double shift = c.GetBaselineShift(&set);
	if (set && shift != 0) v["baselineShift"] = shift;
	bool fill = c.GetFill(&set);
	if (set) v["fill"] = fill ? PaintJson(c.GetFillColor(&set)) : json::Value("none");
	bool stroke = c.GetStroke(&set);
	if (set && stroke) { v["stroke"] = PaintJson(c.GetStrokeColor(&set)); v["strokeWidth"] = (double) c.GetLineWidth(&set); }
	ATE::FontCapsOption caps = c.GetFontCapsOption(&set);
	if (set && caps != ATE::kFontNormalCaps) v["caps"] = caps == ATE::kFontSmallCaps ? "smallCaps" : caps == ATE::kFontAllCaps ? "allCaps" : "allSmallCaps";
	if (c.GetUnderlinePosition(&set) != ATE::kUnderlineOff && set) v["underline"] = true;
	if (c.GetStrikethroughPosition(&set) != ATE::kStrikethroughOff && set) v["strikethrough"] = true;
	ATE::IParaFeatures para = range.GetUniqueParaFeatures();
	ATE::ParagraphJustification j = para.GetJustification(&set);
	if (set) {
		const char* names[] = {"left", "right", "center", "justify", "justifyRight", "justifyCenter", "justifyAll"};
		v["align"] = j >= 0 && j < 7 ? names[j] : "other";
	}
	double indent = para.GetFirstLineIndent(&set);
	if (set && indent != 0) v["firstIndent"] = indent;
	double before = para.GetSpaceBefore(&set);
	if (set && before != 0) v["spaceBefore"] = before;
	double after = para.GetSpaceAfter(&set);
	if (set && after != 0) v["spaceAfter"] = after;
	return v;
}

// ---- commands

json::Value TextGet(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : TextFrames(p)) {
		json::Value v = ArtSummary(a, 0);
		AITextFrameType type = kPointTextType;
		sAITextFrame->GetType(a, &type);
		v["kind"] = type == kInPathTextType ? "area" : type == kOnPathTextType ? "path" : "point";
		v["contents"] = TextOf(a);
		AIBool8 linked = false;
		if (!sAITextFrame->PartOfLinkedText(a, &linked) && linked) v["threaded"] = true;
		v["format"] = FeaturesJson(RangeOf(a, p));
		out.push(v);
	}
	return out;
}

json::Value TextFormat(const json::Value& p)
{
	if (!HasCharFeature(p) && !HasParaFeature(p) && !p.has("case")) Fail(kErrInvalidParams, "nothing to change - pass font, size, fill, align...");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : TextFrames(p)) {
		ATE::ITextRange range = RangeOf(a, p);
		if (HasCharFeature(p)) range.ReplaceOrAddLocalCharFeatures(CharFeatures(p));
		if (HasParaFeature(p)) range.ReplaceOrAddLocalParaFeatures(ParaFeatures(p));
		if (p.has("case")) {
			std::string c = ReqStr(p, "case");
			range.ChangeCase(c == "upper" ? ATE::kUppercase : c == "lower" ? ATE::kLowercase : c == "title" ? ATE::kTitleCase
				: c == "sentence" ? ATE::kSentenceCase : (Fail(kErrInvalidParams, "'case' must be upper, lower, title or sentence"), ATE::kUppercase));
		}
		json::Value v = ArtSummary(a, 0);
		v["format"] = FeaturesJson(RangeOf(a, p));
		out.push(v);
	}
	return out;
}

AIArtHandle NewFrameFrom(const json::Value& p, AIArtHandle frame)
{
	if (p.get("contents").isString()) SetText(frame, p.get("contents").asString(), p);
	if (HasCharFeature(p) || HasParaFeature(p)) {
		ATE::ITextRange range = RangeOf(frame, json::Value::MakeObject());
		if (HasCharFeature(p)) range.ReplaceOrAddLocalCharFeatures(CharFeatures(p));
		if (HasParaFeature(p)) range.ReplaceOrAddLocalParaFeatures(ParaFeatures(p));
	}
	return frame;
}

AITextOrientation Orientation(const json::Value& p) { return p.str("orientation", "horizontal") == "vertical" ? kVerticalTextOrientation : kHorizontalTextOrientation; }

json::Value TextArea(const json::Value& p)
{
	Need(sAITextFrame, "The text frame suite");
	ai::int16 order;
	AIArtHandle prep;
	Placement(p, order, prep);
	AIArtHandle shape = nullptr;
	if (IsId(p.get("path"))) shape = ArtById(IdText(p.get("path")));
	else {
		double x = ReqNum(p, "x"), y = ReqNum(p, "y"), w = ReqNum(p, "width"), h = ReqNum(p, "height");
		Check(Need(sAIShapeConstruction, "The shape construction suite")->NewRect((AIReal) y, (AIReal) x, (AIReal) (y - h), (AIReal) (x + w), false, &shape), "NewRect");
	}
	if (ArtType(shape) != kPathArt) Fail(kErrInvalidParams, "'path' must be a path");
	AIArtHandle frame = nullptr;
	Check(sAITextFrame->NewInPathText(order, prep, Orientation(p), shape, nullptr, false, &frame), "NewInPathText");
	return Finish(NewFrameFrom(p, frame), json::Value::MakeObject());
}

json::Value TextOnPath(const json::Value& p)
{
	Need(sAITextFrame, "The text frame suite");
	AIArtHandle path = ArtById(IdText(Required(p, "path")));
	if (ArtType(path) != kPathArt) Fail(kErrInvalidParams, "'path' must be a path");
	ai::int16 count = 0;
	AIBoolean closed = false;
	sAIPath->GetPathSegmentCount(path, &count);
	sAIPath->GetPathClosed(path, &closed);
	AIReal end = (AIReal) (closed ? count : count - 1);
	AIArtHandle frame = nullptr;
	Check(sAITextFrame->NewOnPathText(kPlaceAbove, path, Orientation(p), path, (AIReal) (p.num("start", 0) * end), end, nullptr, false, &frame), "NewOnPathText");
	return Finish(NewFrameFrom(p, frame), json::Value::MakeObject());
}

json::Value TextOutline(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : TextFrames(p)) {
		AIArtHandle outline = nullptr;
		Check(sAITextFrame->CreateOutline(a, &outline), "CreateOutline");
		if (p.boolean("keepText", false) == false) sAIArt->DisposeArt(a);
		if (outline) out.push(ArtSummary(outline, 1));
	}
	return out;
}

json::Value TextLink(const json::Value& p)
{
	Need(sAITextFrame, "The text frame suite");
	AIArtHandle from = ArtById(IdText(Required(p, "from"))), to = ArtById(IdText(Required(p, "to")));
	Check(sAITextFrame->Link(from, to), "Link (both must be area text; the second empty)");
	json::Value v;
	v["from"] = ArtId(from);
	v["to"] = ArtId(to);
	return v;
}

json::Value TextUnlink(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : TextFrames(p)) { Check(sAITextFrame->Unlink(a, true, true), "Unlink"); out.push(ArtSummary(a, 0)); }
	return out;
}

std::vector<AIArtHandle> AllTextFrames()
{
	std::vector<AIArtHandle> out;
	AIMatchingArtSpec spec(kTextFrameArt, 0, 0);
	AIArtHandle** matches = nullptr;
	ai::int32 n = 0;
	if (!sAIMatchingArt->GetMatchingArt(&spec, 1, &matches, &n) && matches) {
		for (ai::int32 i = 0; i < n; i++) out.push_back((*matches)[i]);
		sSPBlocks->FreeBlock(matches);
	}
	return out;
}

// Positions of 'search' in text (byte offsets in UTF-8 -> character offsets in UTF-16).
std::vector<std::pair<int, int>> Matches(const std::string& text, const std::string& search, bool matchCase)
{
	std::vector<std::pair<int, int>> out;
	std::basic_string<ASUnicode> t = Wide(matchCase ? text : Lower(text)), s = Wide(matchCase ? search : Lower(search));
	if (s.empty()) return out;
	for (size_t at = t.find(s); at != std::basic_string<ASUnicode>::npos; at = t.find(s, at + s.size())) out.push_back({(int) at, (int) s.size()});
	return out;
}

json::Value TextFind(const json::Value& p)
{
	ActiveDocument();
	std::string search = ReqStr(p, "search");
	bool matchCase = p.boolean("matchCase", false);
	std::vector<AIArtHandle> frames = p.has("id") || p.has("ids") ? TextFrames(p) : AllTextFrames();
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : frames) {
		auto hits = Matches(TextOf(a), search, matchCase);
		if (hits.empty()) continue;
		json::Value v = ArtSummary(a, 0);
		json::Value ranges = json::Value::MakeArray();
		for (auto& h : hits) ranges.push(json::Array{(double) h.first, (double) (h.first + h.second)});
		v["ranges"] = ranges;
		out.push(v);
	}
	return out;
}

json::Value TextReplace(const json::Value& p)
{
	ActiveDocument();
	std::string search = ReqStr(p, "search"), with = p.str("replace", "");
	bool matchCase = p.boolean("matchCase", false);
	std::basic_string<ASUnicode> replacement = Wide(with);
	std::vector<AIArtHandle> frames = p.has("id") || p.has("ids") ? TextFrames(p) : AllTextFrames();
	int count = 0;
	for (AIArtHandle a : frames) {
		auto hits = Matches(TextOf(a), search, matchCase);
		for (auto it = hits.rbegin(); it != hits.rend(); ++it) {   // from the end, so earlier offsets hold
			TextRangeRef ref = nullptr;
			if (sAITextFrame->GetATETextRange(a, &ref)) continue;
			ATE::ITextRange range(ref);
			ATETextDOM::Int32 start = range.GetStart() + it->first;
			range.SetRange(start, start + it->second);
			range.Remove();
			if (!replacement.empty()) range.InsertAfter(W(replacement), (ATETextDOM::Int32) replacement.size());
			count++;
		}
	}
	json::Value v;
	v["replaced"] = count;
	return v;
}

// ---- styles

ATE::IDocumentTextResources Resources()
{
	ActiveDocument();
	DocumentTextResourcesRef ref = nullptr;
	Check(sAIDocument->GetDocumentTextResources(&ref), "GetDocumentTextResources");
	return ATE::IDocumentTextResources(ref);
}

template <typename Style>
std::string StyleName(Style s)
{
	ASUnicode buf[256] = {0};
	ATETextDOM::Int32 n = s.GetName((Unicode*) buf, 255);
	return S(ai::UnicodeString(buf, (ai::UnicodeString::size_type) std::max<ATETextDOM::Int32>(0, n)));
}

json::Value CharStyleList(const json::Value&)
{
	json::Value list = json::Value::MakeArray();
	ATE::ICharStylesIterator it(Resources().GetCharStylesInDocument());
	for (it.MoveToFirst(); it.IsNotDone(); it.Next()) list.push(StyleName(it.Item()));
	return list;
}

json::Value ParaStyleList(const json::Value&)
{
	json::Value list = json::Value::MakeArray();
	ATE::IParaStylesIterator it(Resources().GetParaStylesInDocument());
	for (it.MoveToFirst(); it.IsNotDone(); it.Next()) list.push(StyleName(it.Item()));
	return list;
}

json::Value CharStyleCreate(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	ATE::ICharStyle style = Resources().CreateCharStyleWithFeatures(W(Wide(name)), CharFeatures(p));
	if (style.IsNull()) Fail(kErrInvalidParams, "couldn't make the style (is the name taken?)");
	json::Value v;
	v["name"] = StyleName(style);
	return v;
}

json::Value ParaStyleCreate(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	if (HasCharFeature(p)) Fail(kErrInvalidParams, "paragraph styles take paragraph settings here; make a character style for the rest");
	ATE::IParaStyle style = Resources().CreateParaStyleWithFeatures(W(Wide(name)), ParaFeatures(p));
	if (style.IsNull()) Fail(kErrInvalidParams, "couldn't make the style (is the name taken?)");
	json::Value v;
	v["name"] = StyleName(style);
	return v;
}

json::Value StyleApplyText(const json::Value& p, bool paragraph)
{
	std::string name = ReqStr(p, "name");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : TextFrames(p)) {
		ATE::ITextRange range = RangeOf(a, p);
		bool ok = paragraph ? range.SetNamedParaStyle(W(Wide(name))) : range.SetNamedCharStyle(W(Wide(name)));
		if (!ok) Fail(kErrNotFound, "no " + std::string(paragraph ? "paragraph" : "character") + " style named '" + name + "'");
		if (p.boolean("clearOverrides", false)) { if (paragraph) range.ClearLocalParaFeatures(); else range.ClearLocalCharFeatures(); }
		out.push(ArtSummary(a, 0));
	}
	return out;
}

json::Value StyleDeleteText(const json::Value& p, bool paragraph)
{
	std::string name = ReqStr(p, "name");
	bool ok = paragraph ? Resources().RemoveParaStyle(W(Wide(name))) : Resources().RemoveCharStyle(W(Wide(name)));
	if (!ok) Fail(kErrNotFound, "no style named '" + name + "' (or it can't be deleted)");
	json::Value v;
	v["deleted"] = name;
	return v;
}

} // namespace

void AddTextCommands(CommandTable& t)
{
	const char* ids = "string[] - text frame ids (default: the selection)";
	const char* range = "[start, end] - characters, from 0 (default: all the frame's text)";
	auto chars = [](json::Value v) {
		for (auto& kv : std::initializer_list<std::pair<const char*, const char*>>{
			{"font", "string - PostScript name (\"Helvetica-Bold\") or family (\"Helvetica\")"}, {"fontStyle", "string - with a family: \"Bold\""},
			{"size", "number"}, {"leading", "number | \"auto\""}, {"tracking", "number - 1/1000 em"}, {"baselineShift", "number"},
			{"horizontalScale", "percent"}, {"verticalScale", "percent"}, {"caps", "normal | smallCaps | allCaps | allSmallCaps"},
			{"underline", "boolean"}, {"strikethrough", "boolean"}, {"fill", kPaint}, {"stroke", kPaint}, {"strokeWidth", "number"},
			{"align", "left | center | right | justify | justifyCenter | justifyRight | justifyAll"}, {"firstIndent", "number"},
			{"leftIndent", "number"}, {"rightIndent", "number"}, {"spaceBefore", "number"}, {"spaceAfter", "number"}, {"hyphenate", "boolean"}})
			v[kv.first] = kv.second;
		return v;
	};
	t["text.get"] = {"Text frames (default: the selection): kind (point / area / path), contents, and their formatting (font, size, leading, color, alignment...; \"mixed\" where it varies).",
		Params({{"ids", ids}, {"id", "string"}, {"range", range}}), TextGet, false};
	t["text.format"] = {"Format text (default: the selection's frames), all of it or a 'range': character and paragraph settings, and case changes.",
		chars(Params({{"ids", ids}, {"id", "string"}, {"range", range}, {"case", "upper | lower | title | sentence"}})), TextFormat, true};
	t["text.area"] = {"Area text: text that wraps inside a rectangle (x, y top-left, width, height) or inside an existing path.",
		chars(Params({{"x", "number"}, {"y", "number"}, {"width", "number"}, {"height", "number"}, {"path", "string - a path id to fill instead"},
			{"contents", "string"}, {"orientation", "horizontal | vertical"}, {"layer", kWhere}, {"parent", kWhere}})), TextArea, true};
	t["text.onPath"] = {"Type on a path: text that runs along a path.",
		chars(Params({{"path", "string - path id"}, {"contents", "string"}, {"start", "number 0-1 - where the text starts along the path"}, {"orientation", "horizontal | vertical"}})), TextOnPath, true};
	t["text.outline"] = {"Type > Create Outlines: text becomes shapes (the text goes, unless keepText).",
		Params({{"ids", ids}, {"id", "string"}, {"keepText", "boolean"}}), TextOutline, true};
	t["text.link"] = {"Thread text: overflow from area text 'from' continues in area text 'to'.", Params({{"from", "string"}, {"to", "string"}}), TextLink, true};
	t["text.unlink"] = {"Unthread text frames.", Params({{"ids", ids}, {"id", "string"}}), TextUnlink, true};
	t["text.find"] = {"Find text in every frame (or the given ones): which frames, and the character ranges.",
		Params({{"search", "string"}, {"matchCase", "boolean"}, {"ids", "string[]"}}), TextFind, false};
	t["text.replace"] = {"Find and replace text in every frame (or the given ones).",
		Params({{"search", "string"}, {"replace", "string"}, {"matchCase", "boolean"}, {"ids", "string[]"}}), TextReplace, true};
	t["font.list"] = {"Installed fonts (PostScript name, family, style); 'search' filters.", Params({{"search", "string"}, {"limit", "number (default 200)"}}), FontList, false};
	t["charStyle.list"] = {"Character styles in the document.", Params({}), CharStyleList, false};
	t["charStyle.create"] = {"New character style from character settings.", chars(Params({{"name", "string"}})), CharStyleCreate, true};
	t["charStyle.apply"] = {"Apply a character style to text (default: the selection), all of it or a 'range'.",
		Params({{"name", "string"}, {"ids", ids}, {"id", "string"}, {"range", range}, {"clearOverrides", "boolean"}}), [](const json::Value& p) { return StyleApplyText(p, false); }, true};
	t["charStyle.delete"] = {"Delete a character style.", Params({{"name", "string"}}), [](const json::Value& p) { return StyleDeleteText(p, false); }, true};
	t["paraStyle.list"] = {"Paragraph styles in the document.", Params({}), ParaStyleList, false};
	t["paraStyle.create"] = {"New paragraph style from paragraph settings (align, indents, spacing, hyphenate).",
		Params({{"name", "string"}, {"align", "string"}, {"firstIndent", "number"}, {"leftIndent", "number"}, {"rightIndent", "number"},
			{"spaceBefore", "number"}, {"spaceAfter", "number"}, {"hyphenate", "boolean"}}), ParaStyleCreate, true};
	t["paraStyle.apply"] = {"Apply a paragraph style to text (default: the selection).",
		Params({{"name", "string"}, {"ids", ids}, {"id", "string"}, {"range", range}, {"clearOverrides", "boolean"}}), [](const json::Value& p) { return StyleApplyText(p, true); }, true};
	t["paraStyle.delete"] = {"Delete a paragraph style.", Params({{"name", "string"}}), [](const json::Value& p) { return StyleDeleteText(p, true); }, true};
}

} // namespace slippy
