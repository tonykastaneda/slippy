// Appearance: what the Appearance, Transparency and Graphic Styles panels do.
// An object's appearance is an art style (AIArtStyleSuite) - its fills and
// strokes, live effects and transparency - read and rebuilt with the style
// parser (AIArtStyleParserSuite). Effect settings and transparency are
// dictionaries, which come out as JSON and go back in the same shape.

#include "Kit.h"

#include <cmath>

namespace slippy {

// ---- dictionaries <-> JSON (Kit.h)

json::Value DictJson(ConstAIDictionaryRef dict)
{
	json::Value v = json::Value::MakeObject();
	if (!dict || !sAIDictionary || !sAIDictionaryIterator) return v;
	AIDictionaryIterator it = nullptr;
	if (sAIDictionary->Begin(dict, &it) || !it) return v;
	for (; !sAIDictionaryIterator->AtEnd(it); sAIDictionaryIterator->Next(it)) {
		AIDictKey key = sAIDictionaryIterator->GetKey(it);
		const char* name = sAIDictionary->GetKeyString(key);
		if (!name) continue;
		AIEntryType type = UnknownType;
		sAIDictionary->GetEntryType(dict, key, &type);
		switch (type) {
		case IntegerType: { ai::int32 i = 0; if (!sAIDictionary->GetIntegerEntry(dict, key, &i)) v[name] = i; break; }
		case BooleanType: { AIBoolean b = false; if (!sAIDictionary->GetBooleanEntry(dict, key, &b)) v[name] = (bool) b; break; }
		case RealType: { AIReal r = 0; if (!sAIDictionary->GetRealEntry(dict, key, &r)) v[name] = (double) r; break; }
		case StringType: { const char* s = nullptr; if (!sAIDictionary->GetStringEntry(dict, key, &s) && s) v[name] = s; break; }
		case UnicodeStringType: { ai::UnicodeString s; if (!sAIDictionary->GetUnicodeStringEntry(dict, key, s)) v[name] = S(s); break; }
		case DictType: {
			AIDictionaryRef sub = nullptr;
			if (!sAIDictionary->GetDictEntry(dict, key, &sub) && sub) { v[name] = DictJson(sub); sAIDictionary->Release(sub); }
			break;
		}
		default: {
			static const char* names[] = {"unknown", "integer", "boolean", "real", "string", "dict", "array", "binary", "point", "matrix",
				"patternRef", "brushPatternRef", "customColorRef", "gradientRef", "pluginObjectRef", "fillStyle", "strokeStyle", "uid", "uidRef",
				"xmlNode", "svgFilter", "artStyle", "symbolPatternRef", "graphDesignRef", "blendStyle", "graphicObject", "unicodeString", "pointer"};
			json::Value t;
			t["type"] = type >= 0 && type < (AIEntryType) (sizeof names / sizeof names[0]) ? names[type] : "other";
			t["note"] = "not representable as JSON; left as it is";
			v[name] = t;
		}
		}
	}
	sAIDictionaryIterator->Release(it);
	return v;
}

// Values keep the type an existing entry has; {"type": "real"|"integer"|..., "value": x} forces one.
void JsonIntoDict(const json::Value& v, AIDictionaryRef dict)
{
	Need(sAIDictionary, "The dictionary suite");
	if (!v.isObject()) Fail(kErrInvalidParams, "settings must be a JSON object");
	for (const auto& kv : v.asObject()) {
		AIDictKey key = sAIDictionary->Key(kv.first.c_str());
		const json::Value* value = &kv.second;
		std::string type;
		if (value->isObject() && value->get("type").isString() && value->has("value")) { type = value->get("type").asString(); value = &value->get("value"); }
		AIEntryType existing = UnknownType;
		if (sAIDictionary->IsKnown(dict, key)) sAIDictionary->GetEntryType(dict, key, &existing);
		if (type.empty()) {
			if (value->isBool()) type = "boolean";
			else if (value->isString()) type = existing == StringType ? "string" : "unicode";
			else if (value->isNumber()) type = existing == RealType ? "real" : existing == IntegerType ? "integer"
				: value->asNumber() == std::floor(value->asNumber()) ? "integer" : "real";
			else if (value->isObject()) type = "dict";
			else Fail(kErrInvalidParams, "can't store '" + kv.first + "' - use numbers, booleans, strings or objects");
		}
		if (type == "boolean") Check(sAIDictionary->SetBooleanEntry(dict, key, value->asBool()), kv.first.c_str());
		else if (type == "integer") Check(sAIDictionary->SetIntegerEntry(dict, key, (ai::int32) value->asNumber()), kv.first.c_str());
		else if (type == "real") Check(sAIDictionary->SetRealEntry(dict, key, (AIReal) value->asNumber()), kv.first.c_str());
		else if (type == "string") Check(sAIDictionary->SetStringEntry(dict, key, value->asString().c_str()), kv.first.c_str());
		else if (type == "unicode") Check(sAIDictionary->SetUnicodeStringEntry(dict, key, U(value->asString())), kv.first.c_str());
		else if (type == "dict") {
			AIDictionaryRef sub = nullptr;
			Check(sAIDictionary->CreateDictionary(&sub), "CreateDictionary");
			if (existing == DictType) { AIDictionaryRef old = nullptr; if (!sAIDictionary->GetDictEntry(dict, key, &old) && old) { sAIDictionary->Copy(sub, old); sAIDictionary->Release(old); } }
			try { JsonIntoDict(*value, sub); }
			catch (...) { sAIDictionary->Release(sub); throw; }
			AIErr e = sAIDictionary->SetDictEntry(dict, key, sub);
			sAIDictionary->Release(sub);
			Check(e, kv.first.c_str());
		}
		else Fail(kErrInvalidParams, "unknown type '" + type + "' for '" + kv.first + "'");
	}
}

namespace {

// ---- the style parser, released however a command ends

struct Parser {
	AIStyleParser p = nullptr;
	Parser() { Check(Need(sAIArtStyleParser, "The art style parser suite")->NewParser(&p), "NewParser"); }
	~Parser() { if (p) sAIArtStyleParser->DisposeParser(p); }
	Parser(const Parser&) = delete;
	Parser& operator=(const Parser&) = delete;
};

struct Dict {
	AIDictionaryRef d = nullptr;
	Dict() { Check(Need(sAIDictionary, "The dictionary suite")->CreateDictionary(&d), "CreateDictionary"); }
	~Dict() { if (d) sAIDictionary->Release(d); }
	Dict(const Dict&) = delete;
	Dict& operator=(const Dict&) = delete;
};

AIArtStyleHandle StyleOf(AIArtHandle art)
{
	Need(sAIArtStyle, "The art style suite");
	AIArtStyleHandle style = nullptr;
	Check(sAIArtStyle->GetArtStyle(art, &style), "GetArtStyle");
	return style;
}

// Parse the art's style, let 'edit' change the parser, then give the art the result.
template <typename Edit>
void Restyle(AIArtHandle art, Edit edit)
{
	Parser parser;
	Check(sAIArtStyleParser->ParseStyle(parser.p, StyleOf(art)), "ParseStyle");
	edit(parser.p);
	AIArtStyleHandle style = nullptr;
	Check(sAIArtStyleParser->CreateNewStyle(parser.p, &style), "CreateNewStyle");
	Check(sAIArtStyle->SetArtStyle(art, style), "SetArtStyle");
}

// Blend modes as the blend dictionary numbers them.
const char* const kBlendModes[] = {"normal", "multiply", "screen", "overlay", "softLight", "hardLight", "colorDodge", "colorBurn",
	"darken", "lighten", "difference", "exclusion", "hue", "saturation", "color", "luminosity"};
const int kBlendModeCount = sizeof kBlendModes / sizeof kBlendModes[0];

json::Value BlendJson(AIStyleParser parser)
{
	AIParserBlendField field = nullptr;
	if (sAIArtStyleParser->GetStyleBlendField(parser, &field) || !field) return json::Value();
	Dict dict;
	if (sAIArtStyleParser->GetBlendDictionary(field, dict.d)) return json::Value();
	json::Value raw = DictJson(dict.d);
	json::Value v;
	if (raw.get("Opacity").isNumber()) v["opacity"] = raw.get("Opacity").asNumber() * 100;
	if (raw.get("Mode").isNumber()) {
		int m = raw.get("Mode").asInt();
		v["blendMode"] = m >= 0 && m < kBlendModeCount ? kBlendModes[m] : "other";
	}
	v["raw"] = raw;
	return v;
}

json::Value EffectJson(AIParserLiveEffect effect)
{
	json::Value v;
	const char* name = nullptr;
	ai::int32 major = 0, minor = 0;
	if (!sAIArtStyleParser->GetLiveEffectNameAndVersion(effect, &name, &major, &minor) && name) v["effect"] = name;
	AILiveEffectHandle handle = nullptr;
	const char* title = nullptr;
	if (sAILiveEffect && !sAIArtStyleParser->GetLiveEffectHandle(effect, &handle) && handle && !sAILiveEffect->GetLiveEffectTitle(handle, &title) && title)
		v["title"] = title;
	AIBoolean visible = true;
	if (!sAIArtStyleParser->GetEffectVisible(effect, &visible) && !visible) v["visible"] = false;
	AILiveEffectParameters params = nullptr;
	if (!sAIArtStyleParser->GetLiveEffectParams(effect, &params) && params) v["settings"] = DictJson(params);
	return v;
}

// Text and groups draw their own contents somewhere in the paint stack: the
// Appearance panel's "Characters" / "Contents" row.
bool HasContentsRow(AIArtHandle art)
{
	short type = kUnknownArt;
	sAIArt->GetArtType(art, &type);
	return type == kTextFrameArt || type == kGroupArt;
}

json::Value AppearanceOf(AIArtHandle art)
{
	json::Value v;
	v["id"] = ArtId(art);
	AIArtStyleHandle style = StyleOf(art);
	ai::UnicodeString name;
	AIBoolean anonymous = true;
	if (!sAIArtStyle->GetArtStyleName(style, name, &anonymous) && !anonymous) v["graphicStyle"] = S(name);
	Parser parser;
	Check(sAIArtStyleParser->ParseStyle(parser.p, style), "ParseStyle");
	json::Value paints = json::Value::MakeArray();
	ai::int32 count = sAIArtStyleParser->CountPaintFields(parser.p);
	for (ai::int32 i = 0; i < count; i++) {
		AIParserPaintField field = nullptr;
		if (sAIArtStyleParser->GetNthPaintField(parser.p, i, &field) || !field) continue;
		json::Value f;
		f["index"] = i;
		AIArtStylePaintData data;
		if (sAIArtStyleParser->IsFill(field)) {
			AIFillStyle fill;
			f["kind"] = "fill";
			if (!sAIArtStyleParser->GetFill(field, &fill, &data)) f["color"] = ColorJson(fill.color);
		}
		else if (sAIArtStyleParser->IsStroke(field)) {
			AIStrokeStyle stroke;
			f["kind"] = "stroke";
			if (!sAIArtStyleParser->GetStroke(field, &stroke, &data)) { f["color"] = ColorJson(stroke.color); f["width"] = (double) stroke.width; }
		}
		else f["kind"] = "other";
		AIBoolean visible = true;
		if (!sAIArtStyleParser->GetPaintFieldVisible(field, &visible) && !visible) f["visible"] = false;
		json::Value effects = json::Value::MakeArray();
		ai::int32 n = sAIArtStyleParser->CountEffectsOfPaintField(field);
		for (ai::int32 k = 0; k < n; k++) {
			AIParserLiveEffect e = nullptr;
			if (!sAIArtStyleParser->GetNthEffectOfPaintField(field, k, &e) && e) effects.push(EffectJson(e));
		}
		if (n) f["effects"] = effects;
		paints.push(f);
	}
	v["paints"] = paints;
	if (HasContentsRow(art)) v["contentsAt"] = sAIArtStyleParser->GetGroupContentsPosition(parser.p);
	json::Value effects = json::Value::MakeArray();
	for (int pass = 0; pass < 2; pass++) {
		ai::int32 n = pass == 0 ? sAIArtStyleParser->CountPreEffects(parser.p) : sAIArtStyleParser->CountPostEffects(parser.p);
		for (ai::int32 k = 0; k < n; k++) {
			AIParserLiveEffect e = nullptr;
			if ((pass == 0 ? sAIArtStyleParser->GetNthPreEffect(parser.p, k, &e) : sAIArtStyleParser->GetNthPostEffect(parser.p, k, &e)) || !e) continue;
			json::Value ej = EffectJson(e);
			ej["stage"] = pass == 0 ? "pre" : "post";
			effects.push(ej);
		}
	}
	v["effects"] = effects;
	json::Value blend = BlendJson(parser.p);
	if (!blend.isNull()) v["transparency"] = blend;
	return v;
}

} // namespace

// opacity (0-100) / blendMode on art (art.set and appearance.set). Kit.h.
void SetBlend(AIArtHandle art, const json::Value& p)
{
	if (!p.has("opacity") && !p.has("blendMode") && !p.has("knockout") && !p.has("isolate")) return;
	Restyle(art, [&](AIStyleParser parser) {
		AIParserBlendField field = nullptr;
		Check(sAIArtStyleParser->GetStyleBlendField(parser, &field), "GetStyleBlendField");
		if (!field) Fail(kErrIllustrator, "this art has no transparency settings to change");
		Dict dict;
		Check(sAIArtStyleParser->GetBlendDictionary(field, dict.d), "GetBlendDictionary");
		json::Value set = json::Value::MakeObject();
		if (p.has("opacity")) {
			json::Value o;
			o["type"] = "real";
			o["value"] = std::max(0.0, std::min(100.0, ReqNum(p, "opacity"))) / 100.0;
			set["Opacity"] = o;
		}
		if (p.has("blendMode")) {
			std::string want = ReqStr(p, "blendMode");
			int mode = -1;
			for (int i = 0; i < kBlendModeCount; i++) if (Lower(want) == Lower(kBlendModes[i])) mode = i;
			if (mode < 0) Fail(kErrInvalidParams, "'blendMode' must be normal, multiply, screen, overlay, softLight, hardLight, colorDodge, colorBurn, darken, lighten, difference, exclusion, hue, saturation, color or luminosity");
			set["Mode"] = mode;
		}
		if (p.has("isolate")) set["Isolated"] = p.boolean("isolate", false);
		if (p.has("knockout")) set["Knockout"] = p.boolean("knockout", false) ? 1 : 0;
		JsonIntoDict(set, dict.d);
		Check(sAIArtStyleParser->SetBlendDictionary(field, dict.d), "SetBlendDictionary");
	});
}

namespace {

json::Value AppearanceGet(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) out.push(AppearanceOf(a));
	return out;
}

json::Value AppearanceSet(const json::Value& p)
{
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		SetBlend(a, p);
		if (p.get("contentsAt").isNumber()) {
			if (!HasContentsRow(a)) Fail(kErrInvalidParams, "'contentsAt' is for text and groups (their Characters / Contents row)");
			Restyle(a, [&](AIStyleParser parser) {
				ai::int32 n = std::max(0, std::min(sAIArtStyleParser->CountPaintFields(parser), p.get("contentsAt").asInt()));
				Check(sAIArtStyleParser->MoveGroupContentsPosition(parser, n), "MoveGroupContentsPosition");
			});
		}
		out.push(AppearanceOf(a));
	}
	return out;
}

json::Value AppearanceCopy(const json::Value& p)
{
	AIArtStyleHandle style = StyleOf(ArtById(IdText(Required(p, "from"))));
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p)) { Check(sAIArtStyle->SetArtStyle(a, style), "SetArtStyle"); out.push(ArtSummary(a, 0)); }
	return out;
}

// Paints are numbered from the top of the Appearance panel down (index 0 is
// drawn last, over everything). 'position': top, bottom, or an index.
ai::int32 PaintSlot(AIStyleParser parser, const json::Value& p, ai::int32 fallback)
{
	ai::int32 count = sAIArtStyleParser->CountPaintFields(parser);
	const json::Value& pos = p.get("position");
	if (pos.isNull()) return fallback;
	if (pos.isNumber()) return std::max(0, std::min(count, pos.asInt()));
	if (pos.isString() && pos.asString() == "top") return 0;
	if (pos.isString() && pos.asString() == "bottom") return count;
	Fail(kErrInvalidParams, "'position' must be top, bottom or an index (0 = top, as appearance.get numbers them)");
	return 0;
}

// A fill or stroke with no paint, no effects: the empty pair every object
// carries (hidden in the panel on type until something is added).
bool IsEmptyPaint(AIParserPaintField field)
{
	if (sAIArtStyleParser->CountEffectsOfPaintField(field)) return false;
	AIArtStylePaintData data;
	if (sAIArtStyleParser->IsFill(field)) {
		AIFillStyle fill;
		return !sAIArtStyleParser->GetFill(field, &fill, &data) && fill.color.kind == kNoneColor;
	}
	if (sAIArtStyleParser->IsStroke(field)) {
		AIStrokeStyle stroke;
		return !sAIArtStyleParser->GetStroke(field, &stroke, &data) && stroke.color.kind == kNoneColor;
	}
	return false;
}

// Moving a paint keeps the contents row ("Characters") where it was relative
// to the other paints.
void MovePaint(AIStyleParser parser, AIParserPaintField field, ai::int32 from, ai::int32 to, bool contents)
{
	ai::int32 at = contents ? sAIArtStyleParser->GetGroupContentsPosition(parser) : -1;
	Check(sAIArtStyleParser->RemovePaintField(parser, field, false), "RemovePaintField");
	if (to > from) to--;
	Check(sAIArtStyleParser->InsertNthPaintField(parser, to, field), "InsertNthPaintField");
	if (contents) {
		if (from < at) at--;
		if (to < at || (to == at && from >= at)) at++;
		sAIArtStyleParser->MoveGroupContentsPosition(parser, at);
	}
}

// A new fill or stroke, as the Appearance panel's Add New Fill / Stroke: the
// empty one an object already has is used first, so no "none" rows are left.
json::Value AppearanceAdd(const json::Value& p)
{
	std::string kind = ReqStr(p, "kind");
	if (kind != "fill" && kind != "stroke") Fail(kErrInvalidParams, "'kind' must be fill or stroke");
	bool fill = kind == "fill";
	AIColor color = ParseColor(Required(p, "color"), "color");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		AIColor c = color;
		FitPaintToArt(a, c, p.get("color"));
		bool contents = HasContentsRow(a);
		Restyle(a, [&](AIStyleParser parser) {
			ai::int32 count = sAIArtStyleParser->CountPaintFields(parser);
			AIParserPaintField empty = nullptr;
			ai::int32 emptyAt = -1;
			for (ai::int32 i = 0; i < count && !empty; i++) {
				AIParserPaintField f = nullptr;
				if (sAIArtStyleParser->GetNthPaintField(parser, i, &f) || !f) continue;
				if ((fill ? sAIArtStyleParser->IsFill(f) : sAIArtStyleParser->IsStroke(f)) && IsEmptyPaint(f)) { empty = f; emptyAt = i; }
			}
			AIArtStylePaintData data;
			if (empty && p.boolean("reuseEmpty", true)) {
				if (fill) {
					AIFillStyle style;
					Check(sAIArtStyleParser->GetFill(empty, &style, &data), "GetFill");
					style.color = c;
					Check(sAIArtStyleParser->SetFill(empty, &style, &data), "SetFill");
				}
				else {
					AIStrokeStyle style;
					Check(sAIArtStyleParser->GetStroke(empty, &style, &data), "GetStroke");
					style.color = c;
					style.width = (AIReal) p.num("width", style.width > 0 ? style.width : 1);
					Check(sAIArtStyleParser->SetStroke(empty, &style, &data), "SetStroke");
				}
				ai::int32 to = PaintSlot(parser, p, emptyAt);
				if (to != emptyAt) MovePaint(parser, empty, emptyAt, to, contents);
				return;
			}
			AIParserPaintField field = nullptr;
			if (fill) {
				AIFillStyle style;
				style.color = c;
				style.overprint = false;
				Check(sAIArtStyleParser->NewPaintFieldFill(&style, false, &data, &field), "NewPaintFieldFill");
			}
			else {
				AIPathStyle base;
				sAIPathStyle->GetInitialPathStyle(&base);
				AIStrokeStyle style = base.stroke;
				style.color = c;
				style.width = (AIReal) p.num("width", 1);
				Check(sAIArtStyleParser->NewPaintFieldStroke(&style, &data, &field), "NewPaintFieldStroke");
			}
			ai::int32 at = contents ? sAIArtStyleParser->GetGroupContentsPosition(parser) : -1;
			ai::int32 to = PaintSlot(parser, p, 0);
			Check(sAIArtStyleParser->InsertNthPaintField(parser, to, field), "InsertNthPaintField");
			if (contents && to <= at) sAIArtStyleParser->MoveGroupContentsPosition(parser, at + 1);
		});
		out.push(AppearanceOf(a));
	}
	return out;
}

// Take fills / strokes off: one by index (as appearance.get numbers them), or every empty one.
json::Value AppearanceRemove(const json::Value& p)
{
	bool empties = p.boolean("empty", false);
	if (!empties && !p.get("index").isNumber()) Fail(kErrInvalidParams, "pass 'index' (from appearance.get) or empty=true");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		bool contents = HasContentsRow(a);
		Restyle(a, [&](AIStyleParser parser) {
			ai::int32 count = sAIArtStyleParser->CountPaintFields(parser);
			for (ai::int32 i = count - 1; i >= 0; i--) {
				if (!empties && i != p.get("index").asInt()) continue;
				AIParserPaintField f = nullptr;
				if (sAIArtStyleParser->GetNthPaintField(parser, i, &f) || !f) continue;
				if (empties && !IsEmptyPaint(f)) continue;
				ai::int32 at = contents ? sAIArtStyleParser->GetGroupContentsPosition(parser) : -1;
				Check(sAIArtStyleParser->RemovePaintField(parser, f, true), "RemovePaintField");
				if (contents && i < at) sAIArtStyleParser->MoveGroupContentsPosition(parser, at - 1);
			}
			if (!empties && p.get("index").asInt() >= count) Fail(kErrNotFound, "no paint at index " + std::to_string(p.get("index").asInt()));
		});
		out.push(AppearanceOf(a));
	}
	return out;
}

// Remove effects (all, or those named) - and with paints=true, extra fills / strokes too.
json::Value AppearanceClear(const json::Value& p)
{
	std::string only = p.str("effect", "");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		Restyle(a, [&](AIStyleParser parser) {
			if (only.empty()) sAIArtStyleParser->RemoveAllEffects(parser);
			else {
				for (int pass = 0; pass < 2; pass++) {
					for (ai::int32 k = (pass == 0 ? sAIArtStyleParser->CountPreEffects(parser) : sAIArtStyleParser->CountPostEffects(parser)) - 1; k >= 0; k--) {
						AIParserLiveEffect e = nullptr;
						const char* name = nullptr;
						ai::int32 major = 0, minor = 0;
						if ((pass == 0 ? sAIArtStyleParser->GetNthPreEffect(parser, k, &e) : sAIArtStyleParser->GetNthPostEffect(parser, k, &e)) || !e) continue;
						if (sAIArtStyleParser->GetLiveEffectNameAndVersion(e, &name, &major, &minor) || !name || only != name) continue;
						if (pass == 0) sAIArtStyleParser->RemovePreEffect(parser, e, true);
						else sAIArtStyleParser->RemovePostEffect(parser, e, true);
					}
				}
			}
			if (p.boolean("paints", false)) sAIArtStyleParser->Simplify(parser);
		});
		out.push(AppearanceOf(a));
	}
	return out;
}

// ---- live effects

json::Value EffectList(const json::Value& p)
{
	Need(sAILiveEffect, "The live effect suite");
	std::string search = Lower(p.str("search", ""));
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAILiveEffect->CountLiveEffects(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AILiveEffectHandle e = nullptr;
		const char* name = nullptr;
		const char* title = nullptr;
		if (sAILiveEffect->GetNthLiveEffect(i, &e) || !e || sAILiveEffect->GetLiveEffectName(e, &name) || !name) continue;
		sAILiveEffect->GetLiveEffectTitle(e, &title);
		std::string t = title ? title : "";
		if (!search.empty() && Lower(name).find(search) == std::string::npos && Lower(t).find(search) == std::string::npos) continue;
		json::Value v;
		v["effect"] = name;
		if (!t.empty()) v["title"] = t;
		list.push(v);
	}
	return list;
}

json::Value EffectApply(const json::Value& p)
{
	Need(sAILiveEffect, "The live effect suite");
	std::string name = ReqStr(p, "effect");
	AILiveEffectHandle handle = nullptr;
	if (sAILiveEffect->GetLiveEffectHandleByName(name.c_str(), &handle) || !handle) Fail(kErrNotFound, "no effect named '" + name + "' - see effect.list");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		Restyle(a, [&](AIStyleParser parser) {
			AILiveEffectParameters params = nullptr;
			Check(sAILiveEffect->CreateLiveEffectParameters(&params), "CreateLiveEffectParameters");
			AIParserLiveEffect effect = nullptr;
			try {
				if (p.has("settings")) JsonIntoDict(p.get("settings"), params);
				Check(sAIArtStyleParser->NewParserLiveEffect(handle, params, &effect), "NewParserLiveEffect");
			}
			catch (...) { sAIDictionary->Release(params); throw; }
			sAIDictionary->Release(params);   // the parser's effect holds its own reference
			if (p.get("paint").isNumber()) {   // inside one fill / stroke, as in the Appearance panel
				AIParserPaintField field = nullptr;
				if (sAIArtStyleParser->GetNthPaintField(parser, p.get("paint").asInt(), &field) || !field) {
					sAIArtStyleParser->DisposeParserLiveEffect(effect);
					Fail(kErrNotFound, "no paint at index " + std::to_string(p.get("paint").asInt()) + " - see appearance.get");
				}
				Check(sAIArtStyleParser->InsertNthEffectOfPaintField(parser, field, -1, effect), "InsertNthEffectOfPaintField");
				return;
			}
			bool pre = p.str("stage", "post") == "pre";
			ai::int32 n = pre ? sAIArtStyleParser->CountPreEffects(parser) : sAIArtStyleParser->CountPostEffects(parser);
			Check(pre ? sAIArtStyleParser->InsertNthPreEffect(parser, n, effect) : sAIArtStyleParser->InsertNthPostEffect(parser, n, effect), "InsertEffect");
		});
		out.push(AppearanceOf(a));
	}
	return out;
}

// ---- graphic styles

std::string StyleName(AIArtStyleHandle s)
{
	ai::UnicodeString n;
	AIBoolean anonymous = true;
	return sAIArtStyle->GetArtStyleName(s, n, &anonymous) ? "" : S(n);
}

AIArtStyleHandle NamedStyle(const std::string& name)
{
	Need(sAIArtStyle, "The art style suite");
	ActiveDocument();
	AIArtStyleHandle s = nullptr;
	if (sAIArtStyle->GetArtStyleByName(&s, U(name), false) || !s) Fail(kErrNotFound, "no graphic style named '" + name + "' - see style.list");
	return s;
}

json::Value StyleList(const json::Value&)
{
	Need(sAIArtStyle, "The art style suite");
	ActiveDocument();
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAIArtStyle->CountNamedArtStyles(&count);
	for (ai::int32 i = 0; i < count; i++) {
		AIArtStyleHandle s = nullptr;
		if (!sAIArtStyle->GetNthNamedArtStyle(i, &s) && s) list.push(StyleName(s));
	}
	return list;
}

json::Value StyleApply(const json::Value& p)
{
	AIArtStyleHandle s = NamedStyle(ReqStr(p, "name"));
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) { Check(sAIArtStyle->SetArtStyle(a, s), "SetArtStyle"); out.push(ArtSummary(a, 0)); }
	return out;
}

json::Value StyleCreate(const json::Value& p)
{
	Need(sAIArtStyle, "The art style suite");
	AIArtHandle from = ArtList(p, true).front();
	AIArtStyleHandle named = nullptr;
	Check(sAIArtStyle->AddNamedStyle(StyleOf(from), U(ReqStr(p, "name")), true, &named), "AddNamedStyle");
	json::Value v;
	v["name"] = StyleName(named);
	return v;
}

json::Value StyleRedefine(const json::Value& p)
{
	AIArtStyleHandle named = NamedStyle(ReqStr(p, "name"));
	Check(sAIArtStyle->RedefineNamedStyle(named, StyleOf(ArtList(p, true).front())), "RedefineNamedStyle");
	json::Value v;
	v["name"] = StyleName(named);
	return v;
}

json::Value StyleDelete(const json::Value& p)
{
	std::string name = ReqStr(p, "name");
	AIArtStyleHandle anon = nullptr;
	Check(sAIArtStyle->RemoveNamedStyle(NamedStyle(name), &anon), "RemoveNamedStyle");
	json::Value v;
	v["deleted"] = name;
	return v;
}

} // namespace

void AddAppearanceCommands(CommandTable& t)
{
	t["appearance.get"] = {"Art's appearance (default: the selection): its fills and strokes top to bottom as the Appearance panel lists them "
		"('index' 0 = top; text and groups also give 'contentsAt', where their Characters / Contents row sits), live effects with their settings, "
		"transparency (opacity 0-100, blend mode), and graphic style. A gradient's 'matrix' passed back as is copies it exactly.",
		Params({{"ids", kIds}, {"id", "string"}}), AppearanceGet, false};
	t["appearance.set"] = {"Transparency: opacity (0-100), blendMode, knockout, isolate; for text and groups, contentsAt moves the Characters / "
		"Contents row (it sits above the paint at that index). (art.set takes opacity / blendMode too.)",
		Params({{"ids", kIds}, {"id", "string"}, {"opacity", "number 0-100"}, {"blendMode", "normal | multiply | screen | overlay | softLight | hardLight | colorDodge | colorBurn | darken | lighten | difference | exclusion | hue | saturation | color | luminosity"},
			{"knockout", "boolean"}, {"isolate", "boolean"}, {"contentsAt", "number - text / groups"}}), AppearanceSet, true};
	t["appearance.add"] = {"Add a fill or stroke to art's appearance (default: the selection), as the panel's Add New Fill / Stroke: an empty "
		"one the art already has is used first (reuseEmpty=false always adds). New ones go on top unless 'position' says otherwise.",
		Params({{"ids", kIds}, {"id", "string"}, {"kind", "fill | stroke"}, {"color", kPaint}, {"width", "number - stroke width"},
			{"position", "top | bottom | index (0 = top, as appearance.get numbers them)"}, {"reuseEmpty", "boolean (default true)"}}),
		AppearanceAdd, true};
	t["appearance.remove"] = {"Take a fill or stroke off art's appearance: 'index' as appearance.get numbers them, or empty=true for every one "
		"with no paint and no effects.", Params({{"ids", kIds}, {"id", "string"}, {"index", "number"}, {"empty", "boolean"}}), AppearanceRemove, true};
	t["appearance.clear"] = {"Remove live effects (all, or one 'effect' by name); paints=true also reduces to a basic fill and stroke.",
		Params({{"ids", kIds}, {"id", "string"}, {"effect", "string - only this effect"}, {"paints", "boolean"}}), AppearanceClear, true};
	t["appearance.copy"] = {"Give art the whole appearance of another object (fills, strokes, effects, transparency) - the eyedropper.",
		Params({{"from", "string - art id to copy from"}, {"ids", "string[]"}, {"id", "string"}}), AppearanceCopy, true};
	t["effect.list"] = {"Live effects Illustrator has (Drop Shadow, Offset Path, Round Corners, Gaussian Blur...). 'search' filters.",
		Params({{"search", "string (optional)"}}), EffectList, false};
	t["effect.apply"] = {"Add a live effect to art (default: the selection). 'settings' are the effect's own keys - read them off art that has "
		"the effect with appearance.get, and pass the same shape.",
		Params({{"ids", kIds}, {"id", "string"}, {"effect", "string - a name from effect.list"}, {"settings", "object (optional)"}, {"stage", "post (default) | pre"},
			{"paint", "number - put it inside this fill / stroke instead (index from appearance.get)"}}),
		EffectApply, true};
	t["style.list"] = {"Graphic styles in the document.", Params({}), StyleList, false};
	t["style.apply"] = {"Apply a graphic style to art (default: the selection).", Params({{"name", "string"}, {"ids", kIds}, {"id", "string"}}), StyleApply, true};
	t["style.create"] = {"New graphic style from art's appearance (default: the first selected object).", Params({{"name", "string"}, {"id", "string"}}), StyleCreate, true};
	t["style.redefine"] = {"Redefine a graphic style from art's appearance; art using it updates.", Params({{"name", "string"}, {"id", "string"}}), StyleRedefine, true};
	t["style.delete"] = {"Delete a graphic style (art using it keeps its look).", Params({{"name", "string"}}), StyleDelete, true};
}

} // namespace slippy
