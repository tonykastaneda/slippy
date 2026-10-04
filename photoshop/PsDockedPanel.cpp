#include "PsDockedPanel.h"
#include "PsDescriptor.h"
#include "PIUXPSuite.h"
#include "Version.h"

namespace slippy {
namespace ps {
namespace docked {

namespace {

const char* kPanelId = "com.slippy.photoshop.panel";   // panel/manifest.json

PsUXPSuite1* sUxp = nullptr;
Callbacks gCallbacks;
bool gConnected = false;
std::string gStatus = "Starting…";
bool gListening = false;
bool gPaused = false;

void Send(const json::Value& message)
{
	if (!sUxp || !gConnected) return;
	Desc d;
	ASZString z = ToZ(message.dump());
	sDesc->PutZString(d, ID("slippyJSON"), z);
	sZString->Release(z);
	sUxp->SendUXPMessage(sSelf, kPanelId, d);
}

void SendState()
{
	json::Value m;
	m["type"] = "state";
	m["status"] = gStatus;
	m["listening"] = gListening;
	m["paused"] = gPaused;
	m["version"] = kSlippyVersion;
	m["connection"] = gCallbacks.connection ? gCallbacks.connection() : "";
	Send(m);
}

// Messages from the panel: {slippyJSON: "{\"type\": \"hello\" | \"pause\", ...}"}.
void Heard(PIActionDescriptor descriptor)
{
	try {
		json::Value outer = JsonFrom(descriptor);
		const json::Value& text = outer.get("slippyJSON");
		if (!text.isString()) return;
		json::Value m = json::Parse(text.asString());
		std::string type = m.str("type");
		if (type == "hello") {
			gConnected = true;
			SendState();
			if (gCallbacks.onHello) gCallbacks.onHello();
		}
		else if (type == "pause" && gCallbacks.onPause) gCallbacks.onPause(m.boolean("paused"));
	}
	catch (...) {}
}

} // namespace

void Init(Callbacks callbacks)
{
	gCallbacks = std::move(callbacks);
	const void* p = nullptr;
	if (sBasic->AcquireSuite(kPSUXPSuite, kPSUXPSuiteVersion1, &p) || !p) return;
	sUxp = (PsUXPSuite1*) p;
	if (sUxp->AddUXPMessageListener(sSelf, Heard)) {
		sBasic->ReleaseSuite(kPSUXPSuite, kPSUXPSuiteVersion1);
		sUxp = nullptr;
	}
}

void Shutdown()
{
	if (!sUxp) return;
	sUxp->RemoveUXPMessageListener(sSelf);
	sBasic->ReleaseSuite(kPSUXPSuite, kPSUXPSuiteVersion1);
	sUxp = nullptr;
	gConnected = false;
}

bool Connected() { return gConnected; }

void SetStatus(const std::string& text, bool listening)
{
	gStatus = text;
	gListening = listening;
	SendState();
}

void SetPaused(bool paused)
{
	gPaused = paused;
	SendState();
}

void Call(const std::string& method, bool ok, bool changesDocument, double milliseconds, const std::string& line, const std::string& agent)
{
	json::Value m;
	m["type"] = "call";
	m["method"] = method;
	m["ok"] = ok;
	m["edit"] = changesDocument;
	m["ms"] = milliseconds;
	m["line"] = line;
	m["agent"] = agent;
	Send(m);
}

} // namespace docked
} // namespace ps
} // namespace slippy
