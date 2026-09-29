#ifndef __SLIPPY_PLUGIN_H__
#define __SLIPPY_PLUGIN_H__

#include "SlippySuites.h"
#include "SlippyID.h"
#include "Plugin.hpp"
#include "Server.h"

Plugin* AllocatePlugin(SPPluginRef pluginRef);
void FixupReload(Plugin* plugin);

/**	Slippy: a native bridge that lets agents call Illustrator directly
	(no ExtendScript/JSX). A loopback HTTP server (Server.h) takes JSON-RPC
	calls on its own threads; each call is queued and run on Illustrator's
	main thread inside a timer message - a normal plug-in context, like a menu
	command - by the command table in Commands.cpp.

	Window > Utilities > Slippy opens the docked panel (SlippyPanel.h): live,
	animated status of what agents are doing, pause, copy connection.
*/
class SlippyPlugin : public Plugin
{
public:
	SlippyPlugin(SPPluginRef pluginRef);
	virtual ~SlippyPlugin() {}

	void Kick();   // main thread: make sure queued calls get a timer message

	FIXUP_VTABLE_EX(SlippyPlugin, Plugin);

protected:
	virtual ASErr StartupPlugin(SPInterfaceMessage* message);
	virtual ASErr PostStartupPlugin();
	virtual ASErr PreShutdownPlugin();
	virtual ASErr ShutdownPlugin(SPInterfaceMessage* message);
	virtual ASErr GoMenuItem(AIMenuMessage* message);
	virtual ASErr GoTimer(AITimerMessage* message);
	virtual ASErr Message(char* caller, char* selector, void* message);   // + the overlay's annotator

private:
	AIMenuItemHandle fPanelItem = nullptr;
	AIMenuItemHandle fRunItem = nullptr;   // Slippy "clicks" it to run calls like a menu command
	AICommandID fRunCommand = 0;
	AIPanelRef fPanel = nullptr;
	AITimerHandle fTimer = nullptr;
	bool fRunning = false;       // guards against re-entry from a nested event loop
	slippy::Server fServer;
	std::string fServerError;

	void RunPending();
	void AddPanel();
	std::string ConnectionInfo() const;
};

#endif // __SLIPPY_PLUGIN_H__
