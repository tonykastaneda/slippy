#include "PsDescriptor.h"
#include "Commands.h"

#include "Platform.h"

#include <cmath>
#include <filesystem>
#include <fstream>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace slippy {
namespace ps {

namespace {

const DescriptorTypeID kInteger = 'long', kFloat = 'doub', kUnitFloat = 'UntF', kChar = 'TEXT', kBoolean = 'bool',
	kEnumerated = 'enum', kObject = 'Objc', kGlobalObject = 'GlbO', kList = 'VlLs', kReference = 'obj ', kClass = 'type',
	kGlobalClass = 'GlbC', kAlias = 'alis', kPath = 'Pth ', kBookmark = 'bkmk', kRawData = 'tdta', kLargeInteger = 'comp';

const DescriptorFormID kFormEnumerated = 'Enmr', kFormIdentifier = 'Idnt', kFormIndex = 'indx',
	kFormOffset = 'rele', kFormProperty = 'prop', kFormName = 'name';

bool IsWhole(double v) { return std::floor(v) == v && std::fabs(v) < 2147483647.0; }

std::filesystem::path FsPath(const std::string& utf8)
{
#ifdef _WIN32
	return std::filesystem::u8path(utf8);
#else
	return std::filesystem::path(utf8);
#endif
}

bool Exists(const std::string& path)
{
	std::error_code e;
	return std::filesystem::exists(FsPath(path), e);
}

void NeedFile(const std::string& path)
{
	if (!platform::IsAbsolutePath(path)) Fail(kErrInvalidParams, "a path must be absolute: " + path);
	if (!Exists(path)) Fail(kErrNotFound, "no file at " + path);
}

#ifdef __APPLE__
// macOS: files go into descriptors as bookmarks (they need the file to exist).
CFDataRef Bookmark(const std::string& path)
{
	NeedFile(path);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*) path.c_str(), (CFIndex) path.size(), false);
	CFDataRef data = url ? CFURLCreateBookmarkData(nullptr, url, 0, nullptr, nullptr, nullptr) : nullptr;
	if (url) CFRelease(url);
	if (!data) Fail(kErrInvalidParams, "can't refer to " + path);
	return data;
}

std::string PathOf(CFDataRef bookmark)
{
	if (!bookmark) return "";
	Boolean stale = false;
	CFURLRef url = CFURLCreateByResolvingBookmarkData(nullptr, bookmark, kCFBookmarkResolutionWithoutUIMask, nullptr, nullptr, &stale, nullptr);
	std::string out;
	if (url) {
		char buf[4096];
		if (CFURLGetFileSystemRepresentation(url, true, (UInt8*) buf, sizeof buf)) out = buf;
		CFRelease(url);
	}
	return out;
}
#else
// Windows: as aliases, made from the UTF-16 path (the caller disposes it).
// ::Handle - inside slippy, Handle is the request dispatcher (Commands.h).
::Handle Alias(const std::string& path)
{
	NeedFile(path);
	std::u16string w = FsPath(path).u16string();
	AliasHandle alias = nullptr;
	if (sAlias->UnicodePathToAlias((const uint16*) w.c_str(), &alias) || !alias) Fail(kErrInvalidParams, "can't refer to " + path);
	return (::Handle) alias;
}

std::string PathOf(::Handle alias)
{
	if (!alias) return "";
	uint16 buf[2048] = {0};
	if (sAlias->AliasToUnicodePath((AliasHandle) alias, buf, 2047)) return "";
	std::u16string w((const char16_t*) buf);
	return std::filesystem::path(w).u8string();
}
#endif

const std::string& Str(const json::Value& o, const char* key)
{
	const json::Value& v = o.get(key);
	if (!v.isString()) Fail(kErrInvalidParams, std::string("'") + key + "' must be a string in " + o.dump());
	return v.asString();
}

PIActionList ListFrom(const json::Value& array);

// Puts one value under 'key' in a descriptor, or (d == nullptr) appends it to 'list'.
void Put(PIActionDescriptor d, PIActionList list, DescriptorKeyID key, const json::Value& v)
{
	auto put = [&](auto descFn, auto listFn) { if (d) descFn(); else listFn(); };
	if (v.isBool()) { put([&] { sDesc->PutBoolean(d, key, v.asBool()); }, [&] { sList->PutBoolean(list, v.asBool()); }); return; }
	if (v.isNumber()) {
		double n = v.asNumber();
		if (IsWhole(n)) put([&] { sDesc->PutInteger(d, key, (int32) n); }, [&] { sList->PutInteger(list, (int32) n); });
		else put([&] { sDesc->PutFloat(d, key, n); }, [&] { sList->PutFloat(list, n); });
		return;
	}
	if (v.isString()) {
		ASZString z = ToZ(v.asString());
		put([&] { sDesc->PutZString(d, key, z); }, [&] { sList->PutZString(list, z); });
		sZString->Release(z);
		return;
	}
	if (v.isArray()) {
		bool isRef = v.size() && v.asArray()[0].isObject() && (v.asArray()[0].has("_ref") || v.asArray()[0].has("_property"));
		if (isRef) {
			PIActionReference r = ReferenceFrom(v);
			put([&] { sDesc->PutReference(d, key, r); }, [&] { sList->PutReference(list, r); });
			sRef->Free(r);
		}
		else {
			PIActionList l = ListFrom(v);
			put([&] { sDesc->PutList(d, key, l); }, [&] { sList->PutList(list, l); });
			sList->Free(l);
		}
		return;
	}
	if (v.isNull()) Fail(kErrInvalidParams, "null isn't a descriptor value");
	// Objects: the special shapes, then a nested object.
	if (v.has("_unit")) {
		DescriptorUnitID unit = ID(Str(v, "_unit"));
		double n = v.num("_value");
		put([&] { sDesc->PutUnitFloat(d, key, unit, n); }, [&] { sList->PutUnitFloat(list, unit, n); });
		return;
	}
	if (v.has("_enum")) {
		DescriptorEnumTypeID type = ID(Str(v, "_enum"));
		DescriptorEnumID value = ID(Str(v, "_value"));
		put([&] { sDesc->PutEnumerated(d, key, type, value); }, [&] { sList->PutEnumerated(list, type, value); });
		return;
	}
	if (v.has("_class")) {
		DescriptorClassID c = ID(Str(v, "_class"));
		put([&] { sDesc->PutClass(d, key, c); }, [&] { sList->PutClass(list, c); });
		return;
	}
	if (v.has("_path")) {
#ifdef __APPLE__
		CFDataRef b = Bookmark(Str(v, "_path"));
		put([&] { sDesc->PutBookmark(d, key, b); }, [&] { sList->PutBookmark(list, b); });
		CFRelease(b);
#else
		::Handle a = Alias(Str(v, "_path"));
		put([&] { sDesc->PutAlias(d, key, a); }, [&] { sList->PutAlias(list, a); });
		sHandle->Dispose(a);
#endif
		return;
	}
	if (v.has("_float")) {
		double n = v.num("_float");
		put([&] { sDesc->PutFloat(d, key, n); }, [&] { sList->PutFloat(list, n); });
		return;
	}
	if (v.has("_ref") || v.has("_property")) {   // a single reference
		PIActionReference r = ReferenceFrom(json::Array{v});
		put([&] { sDesc->PutReference(d, key, r); }, [&] { sList->PutReference(list, r); });
		sRef->Free(r);
		return;
	}
	Desc sub = DescriptorFrom(v);
	DescriptorClassID cls = v.has("_obj") ? ID(Str(v, "_obj")) : ID("object");
	put([&] { sDesc->PutObject(d, key, cls, sub); }, [&] { sList->PutObject(list, cls, sub); });
}

PIActionList ListFrom(const json::Value& array)
{
	PIActionList l = nullptr;
	sList->Make(&l);
	for (const json::Value& v : array.asArray()) Put(nullptr, l, 0, v);
	return l;
}

json::Value ValueAt(PIActionDescriptor d, PIActionList l, DescriptorKeyID key, uint32 index, int depth);

json::Value ListJson(PIActionList l, int depth)
{
	json::Value out = json::Value::MakeArray();
	uint32 n = 0;
	sList->GetCount(l, &n);
	for (uint32 i = 0; i < n; i++) out.push(ValueAt(nullptr, l, 0, i, depth));
	return out;
}

// One value from a descriptor (by key) or a list (by index).
json::Value ValueAt(PIActionDescriptor d, PIActionList l, DescriptorKeyID key, uint32 index, int depth)
{
	DescriptorTypeID type = 0;
	if (d) sDesc->GetType(d, key, &type);
	else sList->GetType(l, index, &type);
	switch (type) {
	case kInteger: { int32 v = 0; d ? sDesc->GetInteger(d, key, &v) : sList->GetInteger(l, index, &v); return json::Value((int) v); }
	case kFloat: { real64 v = 0; d ? sDesc->GetFloat(d, key, &v) : sList->GetFloat(l, index, &v); return json::Value((double) v); }
	case kUnitFloat: {
		DescriptorUnitID unit = 0;
		real64 v = 0;
		d ? sDesc->GetUnitFloat(d, key, &unit, &v) : sList->GetUnitFloat(l, index, &unit, &v);
		json::Value o;
		o["_unit"] = StrID(unit);
		o["_value"] = (double) v;
		return o;
	}
	case kChar: {
		ASZString z = nullptr;
		d ? sDesc->GetZString(d, key, &z) : sList->GetZString(l, index, &z);
		return json::Value(FromZ(z));
	}
	case kBoolean: { Boolean v = false; d ? sDesc->GetBoolean(d, key, &v) : sList->GetBoolean(l, index, &v); return json::Value((bool) v); }
	case kEnumerated: {
		DescriptorEnumTypeID t = 0;
		DescriptorEnumID v = 0;
		d ? sDesc->GetEnumerated(d, key, &t, &v) : sList->GetEnumerated(l, index, &t, &v);
		json::Value o;
		o["_enum"] = StrID(t);
		o["_value"] = StrID(v);
		return o;
	}
	case kObject: case kGlobalObject: {
		DescriptorClassID cls = 0;
		PIActionDescriptor sub = nullptr;
		if (d) (type == kObject ? sDesc->GetObject : sDesc->GetGlobalObject)(d, key, &cls, &sub);
		else (type == kObject ? sList->GetObject : sList->GetGlobalObject)(l, index, &cls, &sub);
		Desc owned(sub);
		json::Value o = depth > 12 ? json::Value("…") : JsonFrom(sub, depth + 1);
		if (o.isObject()) {
			json::Value withClass;
			withClass["_obj"] = StrID(cls);
			for (auto& kv : o.asObject()) withClass[kv.first] = kv.second;
			return withClass;
		}
		return o;
	}
	case kList: {
		PIActionList sub = nullptr;
		d ? sDesc->GetList(d, key, &sub) : sList->GetList(l, index, &sub);
		json::Value out = depth > 12 ? json::Value("…") : ListJson(sub, depth + 1);
		sList->Free(sub);
		return out;
	}
	case kReference: {
		PIActionReference r = nullptr;
		d ? sDesc->GetReference(d, key, &r) : sList->GetReference(l, index, &r);
		json::Value out = JsonFromReference(r);
		sRef->Free(r);
		return out;
	}
	case kClass: case kGlobalClass: {
		DescriptorClassID c = 0;
		if (d) (type == kClass ? sDesc->GetClass : sDesc->GetGlobalClass)(d, key, &c);
		else (type == kClass ? sList->GetClass : sList->GetGlobalClass)(l, index, &c);
		json::Value o;
		o["_class"] = StrID(c);
		return o;
	}
	case kAlias: case kPath: case kBookmark: {
		json::Value o;
#ifdef __APPLE__
		CFDataRef b = nullptr;
		d ? sDesc->GetBookmark(d, key, &b) : sList->GetBookmark(l, index, &b);
		o["_path"] = PathOf(b);
		if (b) CFRelease(b);
#else
		::Handle a = nullptr;
		d ? sDesc->GetAlias(d, key, &a) : sList->GetAlias(l, index, &a);
		o["_path"] = PathOf(a);
		if (a) sHandle->Dispose(a);
#endif
		return o;
	}
	case kRawData: { json::Value o; o["_rawData"] = true; return o; }
	case kLargeInteger: { real64 v = 0; d ? sDesc->GetFloat(d, key, &v) : sList->GetFloat(l, index, &v); return json::Value((double) v); }
	}
	json::Value o;
	o["_type"] = StrID(type);
	return o;
}

} // namespace

Desc DescriptorFrom(const json::Value& object)
{
	if (!object.isObject()) Fail(kErrInvalidParams, "a descriptor must be an object: " + object.dump());
	Desc d;
	for (const auto& kv : object.asObject()) {
		const std::string& k = kv.first;
		if (k == "_obj" || k == "_options" || k == "_isCommand") continue;
		if (k == "_target") {
			PIActionReference r = ReferenceFrom(kv.second.isArray() ? kv.second : json::Value(json::Array{kv.second}));
			sDesc->PutReference(d, ID("null"), r);
			sRef->Free(r);
			continue;
		}
		Put(d, nullptr, ID(k), kv.second);
	}
	return d;
}

PIActionReference ReferenceFrom(const json::Value& refs)
{
	PIActionReference r = nullptr;
	sRef->Make(&r);
	if (!refs.isArray()) { sRef->Free(r); Fail(kErrInvalidParams, "a reference must be an array of {_ref...}"); }
	for (const json::Value& e : refs.asArray()) {
		if (!e.isObject()) continue;
		DescriptorClassID cls = e.has("_ref") ? ID(Str(e, "_ref")) : ID("property");
		if (e.has("_property")) sRef->PutProperty(r, cls, ID(Str(e, "_property")));
		else if (e.has("_id")) sRef->PutIdentifier(r, cls, (uint32) e.num("_id"));
		else if (e.has("_index")) sRef->PutIndex(r, cls, (uint32) e.num("_index"));
		else if (e.has("_name")) { ASZString z = ToZ(Str(e, "_name")); sRef->PutNameZString(r, cls, z); sZString->Release(z); }
		else if (e.has("_enum")) sRef->PutEnumerated(r, cls, ID(Str(e, "_enum")), ID(Str(e, "_value")));
		else if (e.has("_offset")) sRef->PutOffset(r, cls, (int32) e.num("_offset"));
		else sRef->PutClass(r, cls);
	}
	return r;
}

json::Value JsonFrom(PIActionDescriptor d, int depth)
{
	json::Value out = json::Value::MakeObject();
	if (!d) return out;
	uint32 n = 0;
	sDesc->GetCount(d, &n);
	for (uint32 i = 0; i < n; i++) {
		DescriptorKeyID key = 0;
		if (sDesc->GetKey(d, i, &key)) continue;
		std::string name = StrID(key);
		out[name == "null" ? "_target" : name] = ValueAt(d, nullptr, key, 0, depth);
	}
	return out;
}

json::Value JsonFromReference(PIActionReference r)
{
	json::Value out = json::Value::MakeArray();
	PIActionReference cur = r;
	bool owned = false;
	for (int guard = 0; cur && guard < 16; guard++) {
		DescriptorFormID form = 0;
		DescriptorClassID cls = 0;
		if (sRef->GetForm(cur, &form) || sRef->GetDesiredClass(cur, &cls)) break;
		json::Value e;
		if (form != kFormProperty) e["_ref"] = StrID(cls);
		if (form == kFormProperty) { DescriptorKeyID p = 0; sRef->GetProperty(cur, &p); e["_property"] = StrID(p); }
		else if (form == kFormIdentifier) { uint32 v = 0; sRef->GetIdentifier(cur, &v); e["_id"] = (double) v; }
		else if (form == kFormIndex) { uint32 v = 0; sRef->GetIndex(cur, &v); e["_index"] = (double) v; }
		else if (form == kFormOffset) { int32 v = 0; sRef->GetOffset(cur, &v); e["_offset"] = (int) v; }
		else if (form == kFormName) { ASZString z = nullptr; sRef->GetNameZString(cur, &z); e["_name"] = FromZ(z); }
		else if (form == kFormEnumerated) {
			DescriptorEnumTypeID t = 0;
			DescriptorEnumID v = 0;
			sRef->GetEnumerated(cur, &t, &v);
			e["_enum"] = StrID(t);
			e["_value"] = StrID(v);
		}
		out.push(e);
		PIActionReference next = nullptr;
		if (sRef->GetContainer(cur, &next) || !next) break;
		if (owned) sRef->Free(cur);
		cur = next;
		owned = true;
	}
	if (owned && cur) sRef->Free(cur);
	return out;
}

json::Value Play(const json::Value& descriptor, const std::string& what)
{
	if (!descriptor.isObject() || !descriptor.get("_obj").isString())
		Fail(kErrInvalidParams, "each descriptor needs \"_obj\", the event to play (\"make\", \"set\", \"gaussianBlur\"...)");
	Desc d = DescriptorFrom(descriptor);
	PIActionDescriptor result = nullptr;
	OSErr e = sControl->Play(&result, ID(descriptor.get("_obj").asString()), d, plugInDialogSilent);
	Desc owned(result);
	Check(e, what);
	return JsonFrom(result);
}

json::Value Get(const json::Value& refs, const std::string& what)
{
	PIActionReference r = ReferenceFrom(refs);
	PIActionDescriptor result = nullptr;
	OSErr e = sControl->Get(&result, r);
	sRef->Free(r);
	Desc owned(result);
	Check(e, what);
	return JsonFrom(result);
}

void PrepareSaveTarget(const std::string& path)
{
	if (!platform::IsAbsolutePath(path)) Fail(kErrInvalidParams, "'path' must be absolute");
	if (Exists(path)) return;
	std::ofstream touch(FsPath(path), std::ios::app);
	if (!touch) Fail(kErrInvalidParams, "can't write to " + path + " (does the folder exist?)");
}

json::Value Ref(const char* cls, int id)
{
	json::Value r;
	r["_ref"] = cls;
	r["_id"] = id;
	return r;
}

json::Value RefTarget(const char* cls)
{
	json::Value r;
	r["_ref"] = cls;
	r["_enum"] = "ordinal";
	r["_value"] = "targetEnum";
	return r;
}

json::Value Unit(const char* unit, double value)
{
	json::Value u;
	u["_unit"] = unit;
	u["_value"] = value;
	return u;
}

json::Value Enum(const char* type, const char* value)
{
	json::Value e;
	e["_enum"] = type;
	e["_value"] = value;
	return e;
}

} // namespace ps
} // namespace slippy
