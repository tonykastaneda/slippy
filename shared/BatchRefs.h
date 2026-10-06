#ifndef __SLIPPY_BATCH_REFS_H__
#define __SLIPPY_BATCH_REFS_H__

#include "Json.h"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace slippy {

struct BatchRefError : std::runtime_error { using std::runtime_error::runtime_error; };

// Replaces whole-string references to earlier batch results. A single-item
// array result can be addressed as an object because art commands often
// return arrays even for a single supplied id.
inline json::Value ResolveBatchRefs(const json::Value& value, const json::Value& previous,
	const json::Value& current, size_t step)
{
	if (value.isArray()) {
		json::Value out = json::Value::MakeArray();
		for (const json::Value& v : value.asArray()) out.push(ResolveBatchRefs(v, previous, current, step));
		return out;
	}
	if (value.isObject()) {
		json::Value out = json::Value::MakeObject();
		for (const auto& kv : value.asObject()) out[kv.first] = ResolveBatchRefs(kv.second, previous, current, step);
		return out;
	}
	if (!value.isString()) return value;
	const std::string& s = value.asString();
	if (s.size() < 2 || s[0] != '$' || !std::isdigit((unsigned char) s[1])) return value;
	size_t pos = 1, index = 0;
	while (pos < s.size() && std::isdigit((unsigned char) s[pos])) {
		size_t digit = (size_t) (s[pos++] - '0');
		if (index > (std::numeric_limits<size_t>::max() - digit) / 10)
			throw BatchRefError("batch reference '" + s + "' is out of range");
		index = index * 10 + digit;
	}
	if (index >= step) throw BatchRefError("batch reference '" + s + "' must point to an earlier step");
	if (!previous.isArray() || !current.isArray() || index >= previous.size() + current.size())
		throw BatchRefError("batch reference '" + s + "' has no saved result");
	const json::Value& response = index < previous.size() ? previous.asArray()[index] : current.asArray()[index - previous.size()];
	if (response.has("error")) throw BatchRefError("batch reference '" + s + "' points to a failed step");
	json::Value found = response.get("result");
	while (pos < s.size()) {
		if (s[pos] == '.') {
			pos++;
			size_t begin = pos;
			while (pos < s.size() && (std::isalnum((unsigned char) s[pos]) || s[pos] == '_')) pos++;
			if (begin == pos) throw BatchRefError("invalid batch reference '" + s + "'");
			if (found.isArray() && found.size() == 1) found = found.asArray()[0];
			std::string key = s.substr(begin, pos - begin);
			if (!found.isObject() || !found.has(key))
				throw BatchRefError("batch reference '" + s + "' has no field '" + key + "'");
			found = found.get(key);
		}
		else if (s[pos] == '[') {
			pos++;
			size_t n = 0, begin = pos;
			while (pos < s.size() && std::isdigit((unsigned char) s[pos])) {
				size_t digit = (size_t) (s[pos++] - '0');
				if (n > (std::numeric_limits<size_t>::max() - digit) / 10)
					throw BatchRefError("invalid batch array reference '" + s + "'");
				n = n * 10 + digit;
			}
			if (begin == pos || pos >= s.size() || s[pos++] != ']' || !found.isArray() || n >= found.size())
				throw BatchRefError("invalid batch array reference '" + s + "'");
			found = found.asArray()[n];
		}
		else throw BatchRefError("invalid batch reference '" + s + "'");
	}
	return found;
}

} // namespace slippy

#endif
