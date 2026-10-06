#include "IllustratorSDK.h"
#include "CrashLog.h"
#include "SlippyPlugin.h"
#include "Commands.h"
#include "SlippyPanel.h"
#include "Mcp.h"
#include "Overlay.h"
#include "AppContext.hpp"
#include "Platform.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <future>
#include <memory>
#include <mutex>

// ------------------------------------------------------------------ bridge
// Server threads never touch the SDK. They queue a Job, ask the main thread
// (GCD's main queue, or a message window on Windows) to Kick the plug-in, and wait on the job's future. Kick
// activates a one-tick timer; GoTimer runs the queue in a normal plug-in
// context. A job that times out is abandoned - it still runs, its result is
// dropped.

namespace {

struct Job {
	json::Value request;
	std::promise<json::Value> promise;
	json::Value done;   // a batch split at a document open / close: results so far
};

std::mutex gQueueMutex;
std::deque<std::shared_ptr<Job>> gQueue;
SlippyPlugin* gPlugin = nullptr;   // main thread only
std::atomic<bool> gPaused{false};    // the panel's "Pause agents"
std::atomic<bool> gShuttingDown{false};
const char* gLastRunVia = "none";   // app.info reports it: "menu" (undoable) or "timer"

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
	if (request.isObject() && request.get("method").isString() && request.get("method").asString() == "app.lastRunVia") {
		json::Value r;
		r["jsonrpc"] = "2.0";
		r["id"] = request.get("id");
		r["result"] = gLastRunVia;
		return r;
	}
	double seconds = 60;
	const json::Value& first = request.isArray() && request.size() ? request.asArray()[0] : request;
	if (first.isObject() && first.get("timeout").isNumber()) seconds = first.get("timeout").asNumber();
	seconds = std::max(1.0, std::min(seconds, 3600.0));
	if (gShuttingDown)
		return Failure(first.isObject() ? first.get("id") : json::Value(), slippy::kErrUnavailable, "Illustrator is shutting down");
	if (gPaused)
		return Failure(first.isObject() ? first.get("id") : json::Value(), slippy::kErrUnavailable, "Slippy is paused in its Illustrator panel");

	auto job = std::make_shared<Job>();
	job->request = request;
	std::future<json::Value> result = job->promise.get_future();
	{
		std::lock_guard<std::mutex> lock(gQueueMutex);
		gQueue.push_back(job);
	}
	slippy::platform::MainThreadKick();

	if (result.wait_for(std::chrono::duration<double>(seconds)) != std::future_status::ready) {
		return Failure(first.isObject() ? first.get("id") : json::Value(), slippy::kErrTimeout,
			"Illustrator didn't run the call within " + std::to_string((int) seconds) +
			"s (a modal dialog open? a long operation?). It still runs when Illustrator is free.");
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

void PushFront(std::shared_ptr<Job> job)
{
	std::lock_guard<std::mutex> lock(gQueueMutex);
	gQueue.push_front(std::move(job));
}

// When a run stopped at a document open / close with calls to go, nothing
// runs again before this (steady-clock seconds): not a kick from a new
// request, not the fallback timer. A scheduled kick resumes just after.
const double kResumeDelay = 0.05;
std::atomic<double> gResumeAt{0};

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool Waiting() { return Now() < gResumeAt.load(); }

bool QueueEmpty()
{
	std::lock_guard<std::mutex> lock(gQueueMutex);
	return gQueue.empty();
}

// Fails whatever is still queued (shutdown), so waiting threads return.
void FailQueue()
{
	gShuttingDown = true;
	std::lock_guard<std::mutex> lock(gQueueMutex);
	for (auto& job : gQueue) job->promise.set_value(Failure(json::Value(), slippy::kErrUnavailable, "Illustrator is shutting down"));
	gQueue.clear();
}

} // namespace

// ------------------------------------------------------------------ plug-in

Plugin* AllocatePlugin(SPPluginRef pluginRef)
{
	return new SlippyPlugin(pluginRef);
}

void FixupReload(Plugin* plugin)
{
	SlippyPlugin::FixupVTable((SlippyPlugin*) plugin);
}

SlippyPlugin::SlippyPlugin(SPPluginRef pluginRef)
	: Plugin(pluginRef)
{
	strncpy(fPluginName, kSlippyPluginName, kMaxStringLength);
}

ASErr SlippyPlugin::StartupPlugin(SPInterfaceMessage* message)
{
	ASErr error = Plugin::StartupPlugin(message);
	if (error) return error;
	SlippyAcquireNewerSuites();
	slippy::platform::MainThreadInit([] { if (gPlugin) gPlugin->Kick(); });
	AIPlatformAddMenuItemDataUS menuData;
	menuData.groupName = kSlippyMenuGroup;
	menuData.itemText = ai::UnicodeString::FromUTF8(kSlippyMenuItemText);
	if (sAIMenu->AddMenuItem(fPluginRef, kSlippyMenuItemName, &menuData, 0, &fPanelItem)) fPanelItem = nullptr;
	AIPlatformAddMenuItemDataUS runData;
	runData.groupName = kSlippyMenuGroup;
	runData.itemText = ai::UnicodeString::FromUTF8(kSlippyRunItemName);
	if (sAIMenu->AddMenuItem(fPluginRef, kSlippyRunItemName, &runData, 0, &fRunItem)) fRunItem = nullptr;
	AddPanel();   // never fails startup: without it Slippy still serves agents
	slippy::overlay::Init(fPluginRef);   // likewise optional
	return kNoErr;
}

namespace {
const ai::uint32 kPauseItem = 1;
AIPanelFlyoutMenuRef gFlyout = nullptr;

// The panel's flyout menu: Pause agents (refuses calls until unticked).
void AIAPI FlyoutChose(AIPanelRef, ai::uint32 item)
{
	if (item != kPauseItem) return;
	gPaused = !gPaused;
	if (gFlyout && sAIPanelFlyoutMenu)
		sAIPanelFlyoutMenu->SetItemMark(gFlyout, kPauseItem, gPaused ? kAIPanelFlyoutMenuItemMark_CHECK : kAIPanelFlyoutMenuItemMark_NONE);
	PanelSetPaused(gPaused);
}
} // namespace

void SlippyPlugin::AddPanel()
{
	if (!sAIPanel) return;
	if (sAIPanelFlyoutMenu && !sAIPanelFlyoutMenu->Create(gFlyout))
		sAIPanelFlyoutMenu->AppendItem(gFlyout, kPauseItem, ai::UnicodeString::FromUTF8("Pause agents"));
	AISize minSize = {240, 300};
	if (sAIPanel->Create(fPluginRef, ai::UnicodeString::FromUTF8("Slippy"), ai::UnicodeString::FromUTF8("Slippy"), 1, minSize, true, gFlyout, this, fPanel)) {
		fPanel = nullptr;
		return;
	}
	if (gFlyout) sAIPanel->SetFlyoutMenuProc(fPanel, FlyoutChose);
	sAIPanel->SetSVGIconResourceID(fPanel, kSlippyPanelIconID, kSlippyPanelDarkIconID);
	PanelCallbacks cb;
	cb.connectionInfo = [this] { return ConnectionInfo(); };
	PanelAttach(fPanel, cb);
	slippy::SetCallObserver([](const std::string& method, bool ok, bool changes, double ms, const std::string& line, const std::string& agent) {
		PanelCall(method, ok, changes, ms, line, agent);
	});
}

// What "Copy connection" puts on the clipboard: Slippy's URL, token and all -
// the one thing to hand any agent, whatever MCP client it runs in.
std::string SlippyPlugin::ConnectionInfo() const
{
	return "http://127.0.0.1:" + std::to_string(fServer.Port()) + "/mcp/" + fServer.Token();
}

// After every plug-in has started: open the door for agents.
ASErr SlippyPlugin::PostStartupPlugin()
{
	ASErr error = Plugin::PostStartupPlugin();
	gPlugin = this;
	// Last in line for crashes, so it sees them first; it passes them on.
	slippy::platform::MakeDirs(slippy::platform::SupportDir());
	slippy::crashlog::Install(slippy::platform::JoinPath(slippy::platform::SupportDir(), "crash.log"));
	if (fRunItem && sAICommandManager && sAICommandManager->GetCommandIDFromName(kSlippyRunItemName, &fRunCommand)) fRunCommand = 0;
	// The run command is Slippy's own plumbing: keep it out of the Window menu.
	// (Again a little later, in case Illustrator builds the menu after startup.)
	HideMenuItemTitled(kSlippyRunItemName);
	slippy::platform::MainThreadAfter(3, [] { HideMenuItemTitled(kSlippyRunItemName); });
	int port = kSlippyDefaultPort;
	if (const char* env = getenv("SLIPPY_PORT")) if (atoi(env) > 0) port = atoi(env);
	auto mcp = [](const json::Value& message, const slippy::Server::Caller& caller) {
		return slippy::HandleMcp(message, Submit, caller.query.find("tools=all") != std::string::npos, caller.userAgent);
	};
	// Scripts (client/slippy, curl) get named from their User-Agent too.
	auto rpc = [](const json::Value& request, const slippy::Server::Caller& caller) {
		std::string agent = slippy::AgentName(caller.userAgent);
		if (agent.empty()) agent = caller.userAgent;
		json::Value tagged = request;
		if (tagged.isArray()) { for (json::Value& c : tagged.asArray()) if (c.isObject() && !c.has("agent")) c["agent"] = agent; }
		else if (tagged.isObject() && !tagged.has("agent")) tagged["agent"] = agent;
		return Submit(tagged);
	};
	if (fServer.Start(rpc, mcp, port, kSlippyVersion, fServerError))
		PanelSetStatus("Listening on 127.0.0.1:" + std::to_string(fServer.Port()), true);
	else
		PanelSetStatus("Not listening: " + (fServerError.empty() ? std::string("the server didn't start") : fServerError), false);
	return error;
}

ASErr SlippyPlugin::PreShutdownPlugin()
{
	FailQueue();
	fServer.Stop();
	gPlugin = nullptr;
	return Plugin::PreShutdownPlugin();
}

ASErr SlippyPlugin::ShutdownPlugin(SPInterfaceMessage* message)
{
	FailQueue();
	fServer.Stop();
	gPlugin = nullptr;
	slippy::SetCallObserver(nullptr);
	slippy::crashlog::Uninstall();
	slippy::overlay::Shutdown();
	PanelDetach();
	if (fPanel && sAIPanel) { sAIPanel->Destroy(fPanel); fPanel = nullptr; }
	if (gFlyout && sAIPanelFlyoutMenu) { sAIPanelFlyoutMenu->Destroy(gFlyout); gFlyout = nullptr; }
	slippy::platform::MainThreadShutdown();
	SlippyReleaseNewerSuites();
	return Plugin::ShutdownPlugin(message);
}

ASErr SlippyPlugin::GoMenuItem(AIMenuMessage* message)
{
	if (message->menuItem == fRunItem) {
		gLastRunVia = "menu";
		if (!fRunning && !Waiting()) RunPending();
		return kNoErr;
	}
	if (message->menuItem != fPanelItem) return kNoErr;
	if (fPanel) { sAIPanel->Show(fPanel, true); return kNoErr; }
	// No panel suite: say where things stand instead.
	if (!sAIUser) return kNoErr;
	std::string text = fServer.Port() ? ConnectionInfo() : "Slippy isn't listening: " + fServerError;
	sAIUser->MessageAlert(ai::UnicodeString::FromUTF8(text));
	return kNoErr;
}

void SlippyPlugin::Kick()
{
	if (QueueEmpty() || Waiting()) return;   // waiting: the scheduled kick comes
	AppContext context(fPluginRef);
	// Preferred: run the calls as our own menu command - Illustrator gives
	// those a standard undo context, so agents' edits land on Edit > Undo.
	// (Timer messages are silent, and 30.2's timer suite can't change that.)
	if (fRunCommand && !fRunning) {
		sAIMenu->InvokeMenuAction(fRunCommand);
		if (QueueEmpty() || Waiting()) return;   // done, or the rest waits for a fresh event
	}
	// Added on first use: adding a timer during startup makes Illustrator
	// refuse the plug-in (found building RAGE).
	if (!fTimer && SlippyAddTimer(fPluginRef, "Slippy Calls", 1, &fTimer)) fTimer = nullptr;
	if (fTimer) SlippySetTimerActive(fTimer, true);
	else RunPending();   // no timer suite: run here, inside the app context
}

ASErr SlippyPlugin::Message(char* caller, char* selector, void* message)
{
	if (!strcmp(caller, kCallerAIAnnotation)) return slippy::overlay::Annotate(selector, (AIAnnotatorMessage*) message);
	return Plugin::Message(caller, selector, message);
}

ASErr SlippyPlugin::GoTimer(AITimerMessage* message)
{
	if (slippy::overlay::IsTimer(message->timer)) {
		slippy::overlay::Tick();
		return kNoErr;
	}
	if (fTimer && message->timer == fTimer) {
		if (fRunning) return kNoErr;   // nested event loop; the outer run drains the queue
		SlippySetTimerActive(fTimer, false);
		if (Waiting()) return kNoErr;   // the scheduled kick resumes
		gLastRunVia = "timer";
		RunPending();
	}
	return kNoErr;
}

void SlippyPlugin::RunPending()
{
	fRunning = true;
	// Timer messages come in a silent undo context: edits there never reach
	// Edit > Undo. Ask for a standard one so agents' changes can be undone.
	if (sAIUndo) {
		sAIUndo->SetSilent(false);
		sAIUndo->SetKind(kAIStandardUndoContext);
	}
	// No modal alerts while agents run ("Cut is not available"...): one would
	// block Illustrator until someone clicks it. The call reports the failure.
	ASInteractionAllowed interaction = kASInteractWithAll;
	if (sASUserInteraction) {
		interaction = sASUserInteraction->GetInteractionAllowed();
		sASUserInteraction->SetInteractionAllowed(kASInteractWithNone);
	}
	bool resume = false, ran = false;
	while (auto job = PopJob()) {
		if (ran && slippy::StartsRun(job->request)) {   // a file write gets an event of its own
			PushFront(job);
			resume = true;
			break;
		}
		ran = true;
		json::Value response, rest;
		try {
			response = slippy::Handle(job->request, &rest, &job->done);
		}
		catch (...) {
			response = Failure(json::Value(), slippy::kErrInternal, "internal error");
		}
		if (job->done.isArray()) {   // a split batch: earlier results first
			if (response.isArray()) for (const json::Value& r : response.asArray()) job->done.push(r);
			else job->done.push(response);
			response = job->done;
		}
		if (rest.isArray() && rest.size()) {
			// The batch reached a document open / close: the rest runs on a
			// later event, after Illustrator has finished with it.
			job->done = response;
			job->request = rest;
			PushFront(job);
			resume = true;
			break;
		}
		job->promise.set_value(std::move(response));
		if (slippy::EndsRun(job->request) && !QueueEmpty()) { resume = true; break; }
	}
	if (sASUserInteraction) sASUserInteraction->SetInteractionAllowed(interaction);
	fRunning = false;
	if (resume) {
		// A little after this event (and Illustrator's own work after it) ends.
		gResumeAt = Now() + kResumeDelay;
		slippy::platform::MainThreadAfter(kResumeDelay + 0.01, [] { slippy::platform::MainThreadKick(); });
	}
}
