#include "BatchRefs.h"

#include <cassert>
#include <iostream>

int main()
{
	using json::Value;
	Value earlier = Value::MakeArray(), current = Value::MakeArray();
	Value first;
	first["result"]["id"] = "cut-line";
	first["result"]["bounds"]["left"] = 18;
	earlier.push(first);                   // completed before a document switch
	Value second;
	second["result"] = Value::MakeArray();
	Value created;
	created["id"] = "placed-art";
	second["result"].push(created);
	current.push(second);
	Value params;
	params["ids"] = Value::MakeArray();
	params["ids"].push("$0.id");
	params["ids"].push("$1[0].id");
	params["piece"] = "$0.id";
	params["bleed"] = "$0.bounds.left";
	params["literal"] = "price $5.00";
	Value resolved = slippy::ResolveBatchRefs(params, earlier, current, 2);
	assert(resolved.get("ids").asArray()[0].asString() == "cut-line");
	assert(resolved.get("ids").asArray()[1].asString() == "placed-art");
	assert(resolved.get("bleed").asInt() == 18);
	assert(resolved.get("literal").asString() == "price $5.00");
	assert(slippy::ResolveBatchRefs("$1.id", earlier, current, 2).asString() == "placed-art");
	for (const char* bad : {"$2.id", "$0.missing", "$1[8].id", "$999999999999999999999999.id"}) {
		bool threw = false;
		try { slippy::ResolveBatchRefs(bad, earlier, current, 2); }
		catch (const slippy::BatchRefError&) { threw = true; }
		assert(threw);
	}
	std::cout << "batch references OK\n";
}
