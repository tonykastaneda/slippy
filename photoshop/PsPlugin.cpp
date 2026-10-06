// Slippy for Photoshop: the plug-in's entry point and the bridge from the
// server's threads to Photoshop's main thread.
//
// An automation plug-in that stays loaded (Persistent) and starts with
// Photoshop. Server threads never touch Photoshop: they queue a Job, ask the
// main thread (GCD's main queue) to run the queue, and wait on the job's
// future. On the main thread the calls play action descriptors directly -
// Photoshop accepts them there, document or not - and Commands groups edits
// into History steps. File > Automate > Slippy (and Window > Slippy) shows the panel.

#include "SPBasic.h"
#include "SPInterf.h"
#include "PIActionsPlugin.h"

#include "Commands.h"
#include "CrashLog.h"
#include "Mcp.h"
#include "Platform.h"
#include "PsDockedPanel.h"
#include "PsPanel.h"
#include "PsSuites.h"
#include "Server.h"
#include "Version.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <future>
#include <memory>
#include <mutex>

namespace {

const int kDefaultPort = 7332;   // Illustrator's Slippy has 7331; SLIPPY_PS_PORT overrides

struct Job {
	json::Value request;
	std::promise<json::Value> promise;
};

std::mutex gQueueMutex;
std::deque<std::shared_ptr<Job>> gQueue;
std::atomic<bool> gPaused{false};
std::atomic<bool> gShuttingDown{false};
bool gRunning = false;   // main thread only
slippy::Server* gServer = nullptr;
std::string gServerError;

json::Value Failure(const json::Value& id, int code, const std::string& message)
{
	json::Value r;
	r["jsonrpc"] = "2.0";
	r["id"] = id;
	r["error"]["code"] = code;
	r["error"]["message"] = message;
	return r;
}

// Called on a server thread.
json::Value Submit(const json::Value& request)
{
	double seconds = 60;
	const json::Value& first = request.isArray() && request.size() ? request.asArray()[0] : request;
	if (first.isObject() && first.get("timeout").isNumber()) seconds = first.get("timeout").asNumber();
	seconds = std::max(1.0, std::min(seconds, 3600.0));
	json::Value id = first.isObject() ? first.get("id") : json::Value();
	if (gShuttingDown) return Failure(id, slippy::kErrUnavailable, "Photoshop is shutting down");
	if (gPaused) return Failure(id, slippy::kErrUnavailable, "Slippy is paused in its Photoshop panel (right-click it to resume)");

	auto job = std::make_shared<Job>();
	job->request = request;
	std::future<json::Value> result = job->promise.get_future();
	{
		std::lock_guard<std::mutex> lock(gQueueMutex);
		gQueue.push_back(job);
	}
	slippy::platform::MainThreadKick();
	if (result.wait_for(std::chrono::duration<double>(seconds)) != std::future_status::ready) {
		return Failure(id, slippy::kErrTimeout, "Photoshop didn't run the call within " + std::to_string((int) seconds) +
			"s (a dialog open? a long operation?). It still runs when Photoshop is free.");
	}
	return result.get();
}

std::shared_ptr<Job> PopJob()
{
	std::lock_guard<std::mutex> lock(gQueueMutex);
	if (gQueue.empty()) return nullptr;
	auto job = gQueue.front();
	gQueue.pop_front();
	return job;
}

bool QueueEmpty()
{
	std::lock_guard<std::mutex> lock(gQueueMutex);
	return gQueue.empty();
}

void FailQueue()
{
	gShuttingDown = true;
	std::lock_guard<std::mutex> lock(gQueueMutex);
	for (auto& job : gQueue) job->promise.set_value(Failure(json::Value(), slippy::kErrUnavailable, "Photoshop is shutting down"));
	gQueue.clear();
}

// Main thread. A call can spin Photoshop's event loop (a long filter), and a
// kick arriving then must not start a second run inside the first.
void RunPending()
{
	if (gRunning || gShuttingDown) return;
	gRunning = true;
	while (auto job = PopJob()) {
		json::Value response;
		try { response = slippy::Handle(job->request); }
		catch (const slippy::ps::PsError& e) { response = Failure(json::Value(), e.code, e.message); }
		catch (...) { response = Failure(json::Value(), slippy::kErrInternal, "internal error"); }
		job->promise.set_value(std::move(response));
	}
	gRunning = false;
	if (!QueueEmpty()) slippy::platform::MainThreadKick();   // arrived during a nested loop
}

std::string ConnectionInfo()
{
	return gServer ? "http://127.0.0.1:" + std::to_string(gServer->Port()) + "/mcp/" + gServer->Token() : "";
}

slippy::McpHost PhotoshopHost()
{
	slippy::McpHost h;
	h.title = "Slippy for Adobe Photoshop";
	h.app = "Photoshop";
	h.instructions =
		"Slippy drives Adobe Photoshop directly through a native plug-in (no JavaScript). Everyday commands are tools here; find the rest "
		"with slippy_find (\"layer\", \"select\", \"document\", \"text\") and run them with slippy_call. ps_batchplay plays any Photoshop action "
		"descriptor (batchPlay's JSON: {_obj, _target, ...}) for anything else, and ps_get reads any property. "
		"Coordinates are pixels from the document's top-left, y down; call document_info first for the size. Layer ids are numbers from "
		"layer_tree; commands that take an id act on the active layer when none is given. Every edit is one History step named after what "
		"it did; slippy_batch makes several edits one step. Saves, exports, opens and closes run on their own and never show a dialog; "
		"document_close discards changes unless save=true.";
	h.findExamples = "(\"layer\", \"select\", \"blur\", \"perspective\", \"export\")";
	h.callExamples = "(e.g. \"layer.perspective\", \"select.ellipse\")";
	h.batchExamples = "(select.rect, filter.blur, layer.set, ...)";
	h.batchReferences = false;
	h.commandCount = "~35";
	h.everyday = {"document.info", "document.open", "document.export", "layer.tree", "layer.get", "layer.set", "layer.create",
		"layer.transform", "text.create", "ps.batchplay", "ps.get", "history.undo"};
	return h;
}

void Startup()
{
	using namespace slippy;
	platform::SetSupportSubdir("Photoshop");   // its own token, session file and logs
	platform::MakeDirs(platform::SupportDir());
	crashlog::Install(platform::JoinPath(platform::SupportDir(), "crash.log"), "Photoshop");
	platform::MainThreadInit([] { RunPending(); });
	SetMcpHost(PhotoshopHost());

	// Two front ends, both fed everything: the docked panel (UXP) and the
	// floating one, which stands aside while the docked one is open.
	auto setPaused = [](bool paused) {
		gPaused = paused;
		PanelSetPaused(paused);
		ps::docked::SetPaused(paused);
	};
	ps::docked::Callbacks dc;
	dc.connection = [] { return ConnectionInfo(); };
	dc.onPause = setPaused;
	dc.onHello = [] { PanelHide(); };
	ps::docked::Init(dc);
	PanelCallbacks cb;
	cb.connectionInfo = [] { return ConnectionInfo(); };
	cb.onPause = setPaused;
	cb.docked = [] { return ps::docked::Connected(); };
	PanelInit(cb);
	SetCallObserver([](const std::string& method, bool ok, bool changes, double ms, const std::string& line, const std::string& agent) {
		PanelCall(method, ok, changes, ms, line, agent);
		ps::docked::Call(method, ok, changes, ms, line, agent);
	});

	int port = kDefaultPort;
	if (const char* env = getenv("SLIPPY_PS_PORT")) if (atoi(env) > 0) port = atoi(env);
	gServer = new Server();
	gServer->urlHint = "it's on the clipboard after they click Copy connection in Photoshop's Slippy panel (Window > Slippy), "
		"and it has the token in it.";
	auto mcp = [](const json::Value& message, const Server::Caller& caller) {
		return HandleMcp(message, Submit, caller.query.find("tools=all") != std::string::npos, caller.userAgent);
	};
	// Scripts (curl, the slippy client) get named from their User-Agent.
	auto rpc = [](const json::Value& request, const Server::Caller& caller) {
		std::string agent = AgentName(caller.userAgent);
		if (agent.empty()) agent = caller.userAgent;
		json::Value tagged = request;
		if (tagged.isArray()) { for (json::Value& c : tagged.asArray()) if (c.isObject() && !c.has("agent")) c["agent"] = agent; }
		else if (tagged.isObject() && !tagged.has("agent")) tagged["agent"] = agent;
		return Submit(tagged);
	};
	bool listening = gServer->Start(rpc, mcp, port, kSlippyVersion, gServerError);
	std::string status = listening ? "Listening on 127.0.0.1:" + std::to_string(gServer->Port())
		: "Not listening: " + (gServerError.empty() ? std::string("the server didn't start") : gServerError);
	PanelSetStatus(status, listening);
	ps::docked::SetStatus(status, listening);
}

void Shutdown()
{
	using namespace slippy;
	FailQueue();
	if (gServer) { gServer->Stop(); delete gServer; gServer = nullptr; }
	SetCallObserver(nullptr);
	ps::docked::Shutdown();
	PanelShutdown();
	platform::MainThreadShutdown();
	crashlog::Uninstall();
	ps::ReleaseSuites();
}

} // namespace

#ifdef _WIN32
#define SLIPPY_EXPORT extern "C" __declspec(dllexport)
#else
#define SLIPPY_EXPORT extern "C" __attribute__((visibility("default")))
#endif

SLIPPY_EXPORT SPAPI SPErr AutoPluginMain(const char* caller, const char* selector, void* message)
{
	SPMessageData* data = (SPMessageData*) message;
	SPBasicSuite* basic = data->basic;
	if (basic->IsEqual(caller, kSPInterfaceCaller)) {
		if (basic->IsEqual(selector, kSPInterfaceStartupSelector)) {
			std::string missing;
			if (!slippy::ps::AcquireSuites(basic, data->self, missing)) return kSPNoError;   // stays quiet rather than half-working
			try { Startup(); } catch (...) {}
		}
		else if (basic->IsEqual(selector, kSPInterfaceShutdownSelector)) Shutdown();
		return kSPNoError;
	}
	// File > Automate > Slippy: show the panel.
	if (basic->IsEqual(caller, kPSPhotoshopCaller) && basic->IsEqual(selector, kPSDoIt)) PanelShow();
	return kSPNoError;
}
