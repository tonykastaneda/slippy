#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

static const Value kNull;

bool Value::asBool() const
{
	if (fType != Type::Bool) throw Error("expected a boolean");
	return fBool;
}

double Value::asNumber() const
{
	if (fType != Type::Number) throw Error("expected a number");
	return fNum;
}

const std::string& Value::asString() const
{
	if (fType != Type::String) throw Error("expected a string");
	return *fStr;
}

const Array& Value::asArray() const&
{
	if (fType != Type::Array) throw Error("expected an array");
	return *fArr;
}

Array& Value::asArray() &
{
	if (fType != Type::Array) throw Error("expected an array");
	return *fArr;
}

const Object& Value::asObject() const&
{
	if (fType != Type::Object) throw Error("expected an object");
	return *fObj;
}

Object& Value::asObject() &
{
	if (fType != Type::Object) throw Error("expected an object");
	return *fObj;
}

bool Value::has(const std::string& key) const
{
	if (fType != Type::Object) return false;
	for (auto& kv : *fObj) if (kv.first == key) return true;
	return false;
}

const Value& Value::get(const std::string& key) const
{
	if (fType != Type::Object) return kNull;
	for (auto& kv : *fObj) if (kv.first == key) return kv.second;
	return kNull;
}

Value& Value::operator[](const std::string& key)
{
	if (fType == Type::Null) *this = MakeObject();
	Object& o = asObject();
	for (auto& kv : o) if (kv.first == key) return kv.second;
	o.emplace_back(key, Value());
	return o.back().second;
}

void Value::push(Value v)
{
	if (fType == Type::Null) *this = MakeArray();
	asArray().push_back(std::move(v));
}

size_t Value::size() const
{
	if (fType == Type::Array) return fArr->size();
	if (fType == Type::Object) return fObj->size();
	return 0;
}

std::string Value::str(const std::string& key, const std::string& def) const
{
	const Value& v = get(key);
	if (v.isNull()) return def;
	if (!v.isString()) throw Error("'" + key + "' must be a string");
	return v.asString();
}

double Value::num(const std::string& key, double def) const
{
	const Value& v = get(key);
	if (v.isNull()) return def;
	if (!v.isNumber()) throw Error("'" + key + "' must be a number");
	return v.asNumber();
}

bool Value::boolean(const std::string& key, bool def) const
{
	const Value& v = get(key);
	if (v.isNull()) return def;
	if (!v.isBool()) throw Error("'" + key + "' must be true or false");
	return v.asBool();
}

// ---------------------------------------------------------------- writer

static void Escape(std::string& out, const std::string& s)
{
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof buf, "\\u%04x", c);
				out += buf;
			}
			else out += (char) c;
		}
	}
	out += '"';
}

static void Newline(std::string& out, int indent, int depth)
{
	if (indent < 0) return;
	out += '\n';
	out.append((size_t) (indent * depth), ' ');
}

void Value::dumpTo(std::string& out, int indent, int depth) const
{
	switch (fType) {
	case Type::Null: out += "null"; break;
	case Type::Bool: out += fBool ? "true" : "false"; break;
	case Type::Number: {
		if (!std::isfinite(fNum)) { out += "null"; break; }
		char buf[32];
		if (fNum == std::floor(fNum) && std::fabs(fNum) < 1e15) snprintf(buf, sizeof buf, "%.0f", fNum);
		else snprintf(buf, sizeof buf, "%.10g", fNum);
		out += buf;
		break;
	}
	case Type::String: Escape(out, *fStr); break;
	case Type::Array: {
		if (fArr->empty()) { out += "[]"; break; }
		out += '[';
		for (size_t i = 0; i < fArr->size(); i++) {
			if (i) out += ',';
			Newline(out, indent, depth + 1);
			(*fArr)[i].dumpTo(out, indent, depth + 1);
		}
		Newline(out, indent, depth);
		out += ']';
		break;
	}
	case Type::Object: {
		if (fObj->empty()) { out += "{}"; break; }
		out += '{';
		for (size_t i = 0; i < fObj->size(); i++) {
			if (i) out += ',';
			Newline(out, indent, depth + 1);
			Escape(out, (*fObj)[i].first);
			out += indent < 0 ? ":" : ": ";
			(*fObj)[i].second.dumpTo(out, indent, depth + 1);
		}
		Newline(out, indent, depth);
		out += '}';
		break;
	}
	}
}

std::string Value::dump(int indent) const
{
	std::string out;
	dumpTo(out, indent, 0);
	return out;
}

// ---------------------------------------------------------------- parser

namespace {

struct Parser {
	const std::string& s;
	size_t i = 0;
	int depth = 0;

	explicit Parser(const std::string& text) : s(text) {}

	[[noreturn]] void Fail(const std::string& what)
	{
		throw Error("JSON parse error at offset " + std::to_string(i) + ": " + what);
	}

	void Skip()
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
	}

	bool Eat(char c)
	{
		Skip();
		if (i < s.size() && s[i] == c) { i++; return true; }
		return false;
	}

	void Expect(const char* word)
	{
		for (const char* p = word; *p; p++, i++)
			if (i >= s.size() || s[i] != *p) Fail(std::string("expected '") + word + "'");
	}

	static void AppendUTF8(std::string& out, unsigned cp)
	{
		if (cp < 0x80) out += (char) cp;
		else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) {
			out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F));
		}
		else {
			out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F));
			out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F));
		}
	}

	unsigned Hex4()
	{
		if (i + 4 > s.size()) Fail("short \\u escape");
		unsigned v = 0;
		for (int k = 0; k < 4; k++) {
			char c = s[i++];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= (unsigned) (c - '0');
			else if (c >= 'a' && c <= 'f') v |= (unsigned) (c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= (unsigned) (c - 'A' + 10);
			else Fail("bad \\u escape");
		}
		return v;
	}

	std::string String()
	{
		if (!Eat('"')) Fail("expected a string");
		std::string out;
		while (true) {
			if (i >= s.size()) Fail("unterminated string");
			char c = s[i++];
			if (c == '"') break;
			if (c != '\\') { out += c; continue; }
			if (i >= s.size()) Fail("unterminated escape");
			char e = s[i++];
			switch (e) {
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u': {
				unsigned cp = Hex4();
				if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
					i += 2;
					unsigned lo = Hex4();
					if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					else { AppendUTF8(out, 0xFFFD); cp = lo; }
				}
				AppendUTF8(out, cp);
				break;
			}
			default: Fail("bad escape");
			}
		}
		return out;
	}

	Value Number()
	{
		size_t start = i;
		if (s[i] == '-') i++;
		while (i < s.size() && (isdigit((unsigned char) s[i]) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) i++;
		std::string tok = s.substr(start, i - start);
		char* end = nullptr;
		double d = strtod(tok.c_str(), &end);
		if (tok.empty() || *end) Fail("bad number");
		return Value(d);
	}

	Value Any()
	{
		if (++depth > 256) Fail("nested too deeply");
		Skip();
		if (i >= s.size()) Fail("unexpected end");
		Value v;
		char c = s[i];
		if (c == '{') {
			i++;
			v = Value::MakeObject();
			if (!Eat('}')) {
				do {
					Skip();
					std::string key = String();
					if (!Eat(':')) Fail("expected ':'");
					v.asObject().emplace_back(std::move(key), Any());
				} while (Eat(','));
				if (!Eat('}')) Fail("expected '}'");
			}
		}
		else if (c == '[') {
			i++;
			v = Value::MakeArray();
			if (!Eat(']')) {
				do v.asArray().push_back(Any()); while (Eat(','));
				if (!Eat(']')) Fail("expected ']'");
			}
		}
		else if (c == '"') v = Value(String());
		else if (c == 't') { Expect("true"); v = Value(true); }
		else if (c == 'f') { Expect("false"); v = Value(false); }
		else if (c == 'n') { Expect("null"); v = Value(); }
		else if (c == '-' || isdigit((unsigned char) c)) v = Number();
		else Fail(std::string("unexpected '") + c + "'");
		depth--;
		return v;
	}
};

} // namespace

Value Parse(const std::string& text)
{
	Parser p(text);
	Value v = p.Any();
	p.Skip();
	if (p.i != text.size()) p.Fail("trailing characters");
	return v;
}

} // namespace json
