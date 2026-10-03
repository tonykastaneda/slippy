#ifndef __SLIPPY_JSON_H__
#define __SLIPPY_JSON_H__

// Small self-contained JSON value, parser and writer. No third-party
// dependency, so the plug-in bundle carries nothing but itself.

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;   // keeps key order

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
	Value() : fType(Type::Null) {}
	Value(std::nullptr_t) : fType(Type::Null) {}
	Value(bool b) : fType(Type::Bool), fBool(b) {}
	Value(int n) : fType(Type::Number), fNum(n) {}
	Value(long n) : fType(Type::Number), fNum((double) n) {}
	Value(long long n) : fType(Type::Number), fNum((double) n) {}
	Value(unsigned n) : fType(Type::Number), fNum(n) {}
	Value(unsigned long n) : fType(Type::Number), fNum((double) n) {}
	Value(double n) : fType(Type::Number), fNum(n) {}
	Value(const char* s) : fType(Type::String), fStr(std::make_shared<std::string>(s)) {}
	Value(std::string s) : fType(Type::String), fStr(std::make_shared<std::string>(std::move(s))) {}
	Value(Array a) : fType(Type::Array), fArr(std::make_shared<Array>(std::move(a))) {}
	Value(Object o) : fType(Type::Object), fObj(std::make_shared<Object>(std::move(o))) {}

	static Value MakeArray() { return Value(Array{}); }
	static Value MakeObject() { return Value(Object{}); }

	Type type() const { return fType; }
	bool isNull() const { return fType == Type::Null; }
	bool isBool() const { return fType == Type::Bool; }
	bool isNumber() const { return fType == Type::Number; }
	bool isString() const { return fType == Type::String; }
	bool isArray() const { return fType == Type::Array; }
	bool isObject() const { return fType == Type::Object; }

	bool asBool() const;
	double asNumber() const;
	int asInt() const { return (int) asNumber(); }
	const std::string& asString() const;
	// Only on a value that outlives the use: `for (x : Make().asArray())`
	// would walk a destroyed array, so that doesn't compile.
	const Array& asArray() const&;
	Array& asArray() &;
	const Array& asArray() const&& = delete;
	const Object& asObject() const&;
	Object& asObject() &;
	const Object& asObject() const&& = delete;

	// Object access. get() returns a shared null for a missing key.
	bool has(const std::string& key) const;
	const Value& get(const std::string& key) const;
	Value& operator[](const std::string& key);   // inserts (turns null into an object)
	void push(Value v);                          // appends (turns null into an array)
	size_t size() const;

	// Typed lookups with defaults; throw json::Error on a wrong type.
	std::string str(const std::string& key, const std::string& def = "") const;
	double num(const std::string& key, double def = 0) const;
	bool boolean(const std::string& key, bool def = false) const;

	std::string dump(int indent = -1) const;

private:
	Type fType;
	bool fBool = false;
	double fNum = 0;
	std::shared_ptr<std::string> fStr;
	std::shared_ptr<Array> fArr;
	std::shared_ptr<Object> fObj;
	void dumpTo(std::string& out, int indent, int depth) const;
};

struct Error : std::runtime_error {
	using std::runtime_error::runtime_error;
};

Value Parse(const std::string& text);   // throws json::Error

} // namespace json

#endif // __SLIPPY_JSON_H__
