#ifndef __KAGE_PLUGIN_H__
#define __KAGE_PLUGIN_H__

#include "KAGESuites.h"
#include "KAGEID.h"
#include "Plugin.hpp"
#include "Server.h"

Plugin* AllocatePlugin(SPPluginRef pluginRef);
void FixupReload(Plugin* plugin);

/**	KAGE: a native bridge that lets agents call Illustrator directly
	(no ExtendScript/JSX). A loopback HTTP server (Server.h) takes JSON-RPC
	calls on its own threads; each call is queued and run on Illustrator's
	main thread inside a timer message - a normal plug-in context, like a menu
	command - by the command table in Commands.cpp.

	Window > Utilities > KAGE opens the docked panel (KAGEPanel.h): live,
	animated status of what agents are doing, pause, copy connection.
*/
class KAGEPlugin : public Plugin
{
public:
	KAGEPlugin(SPPluginRef pluginRef);
	virtual ~KAGEPlugin() {}

	void Kick();   // main thread: make sure queued calls get a timer message

	FIXUP_VTABLE_EX(KAGEPlugin, Plugin);

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
	AIMenuItemHandle fRunItem = nullptr;   // KAGE "clicks" it to run calls like a menu command
	AICommandID fRunCommand = 0;
	AIPanelRef fPanel = nullptr;
	AITimerHandle fTimer = nullptr;
	bool fRunning = false;       // guards against re-entry from a nested event loop
	kage::Server fServer;
	std::string fServerError;

	void RunPending();
	void AddPanel();
	std::string ConnectionInfo() const;
};

#endif // __KAGE_PLUGIN_H__
