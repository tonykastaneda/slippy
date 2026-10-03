#ifndef __SLIPPY_PS_DESCRIPTOR_H__
#define __SLIPPY_PS_DESCRIPTOR_H__

// Action descriptors <-> JSON, in the shape UXP's batchPlay uses, so a
// descriptor copied from Photoshop's "Copy As JavaScript" works as is:
//   {"_obj": "make", "_target": [{"_ref": "layer"}], "using": {"_obj": "layer", "name": "Logo"}}
// Values: numbers, strings, booleans, lists, {"_obj"} objects,
// {"_unit": "pixelsUnit", "_value": 12}, {"_enum": "ordinal", "_value": "targetEnum"},
// {"_class": "document"}, {"_path": "/abs/file.png"}, and references as arrays of
// {"_ref": "layer", "_id" | "_index" | "_name" | "_enum"+"_value" | "_offset"} / {"_property": "name"}.
// A whole number becomes an integer; write 2.5, or {"_float": 2}, for a double.

#include "Json.h"
#include "PsSuites.h"

#include <memory>
#include <string>

namespace slippy {
namespace ps {

// Owns a descriptor / reference / list and frees it.
struct Desc {
	PIActionDescriptor d = nullptr;
	Desc() { sDesc->Make(&d); }
	explicit Desc(PIActionDescriptor take) : d(take) {}
	~Desc() { if (d) sDesc->Free(d); }
	Desc(const Desc&) = delete;
	Desc& operator=(const Desc&) = delete;
	Desc(Desc&& o) noexcept : d(o.d) { o.d = nullptr; }
	operator PIActionDescriptor() const { return d; }
};

// The event's descriptor ("_obj" names the event; "_target" becomes its target).
Desc DescriptorFrom(const json::Value& object);
PIActionReference ReferenceFrom(const json::Value& refs);   // the caller frees it
json::Value JsonFrom(PIActionDescriptor d, int depth = 0);
json::Value JsonFromReference(PIActionReference r);

// Plays one event, silently (no dialogs); returns the result as JSON.
// 'what' names it in an error ("make a layer").
json::Value Play(const json::Value& descriptor, const std::string& what);
// Gets a property or object: refs as in "_target".
json::Value Get(const json::Value& refs, const std::string& what);

// A {"_path"} must name an existing file; a save makes its target first.
void PrepareSaveTarget(const std::string& path);

// Shorthands for building descriptors in C++.
json::Value Ref(const char* cls, int id);                    // {"_ref": cls, "_id": id}
json::Value RefTarget(const char* cls);                      // {"_ref": cls, "_enum": "ordinal", "_value": "targetEnum"}
json::Value Unit(const char* unit, double value);           // {"_unit", "_value"}
json::Value Enum(const char* type, const char* value);      // {"_enum", "_value"}

} // namespace ps
} // namespace slippy

#endif // __SLIPPY_PS_DESCRIPTOR_H__
