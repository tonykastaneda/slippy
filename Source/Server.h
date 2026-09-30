#ifndef __SLIPPY_SERVER_H__
#define __SLIPPY_SERVER_H__

// Loopback HTTP server for agents. Runs entirely on its own threads and never
// touches the Illustrator SDK: every request goes to the handler, which hands
// it to Illustrator's main thread (see Bridge in SlippyPlugin.cpp).
//
//   GET  /health  -> {"ok":true,...}           no token needed, reveals nothing
//   POST /rpc     -> {"method":..,"params":..}  JSON-RPC, needs the token
//   POST /mcp     -> MCP (streamable HTTP, JSON responses), needs the token
//
// The token is made once and kept (~/Library/Application Support/Slippy/token,
// mode 0600; %APPDATA%\Slippy\token on Windows) so agents' saved MCP configs
// keep working across launches; the session file next to it tells clients the
// port and token. Requests carrying an Origin header (browsers) are refused, so
// a web page can't drive Illustrator.

#include "Json.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace slippy {

class Server {
public:
	using Handler = std::function<json::Value(const json::Value& request)>;

	// Binds 127.0.0.1 on the first free port from firstPort (up to 10 tries).
	// mcp returns null for a message that gets no response (a notification).
	bool Start(Handler rpc, Handler mcp, int firstPort, const std::string& version, std::string& error);
	void Stop();

	int Port() const { return fPort; }
	const std::string& Token() const { return fToken; }
	static std::string SessionFilePath();
	static std::string TokenFilePath();

private:
	Handler fHandler;
	Handler fMcp;
	intptr_t fListenFd = -1;   // a socket (SOCKET on Windows)
	int fPort = 0;
	std::string fToken;
	std::string fVersion;
	std::thread fAcceptThread;
	std::atomic<bool> fStopping{false};
	std::atomic<int> fActive{0};
	bool fWinsock = false;

	void AcceptLoop();
	void Serve(intptr_t fd);
	void WriteSessionFile();
};

} // namespace slippy

#endif // __SLIPPY_SERVER_H__
