#include "Server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace kage {

namespace {

const size_t kMaxHeader = 64 * 1024;
const size_t kMaxBody = 64 * 1024 * 1024;

std::string NewToken()
{
	unsigned char bytes[24];
	arc4random_buf(bytes, sizeof bytes);
	static const char* hex = "0123456789abcdef";
	std::string s;
	for (unsigned char b : bytes) { s += hex[b >> 4]; s += hex[b & 15]; }
	return s;
}

bool SameToken(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) return false;
	unsigned char diff = 0;
	for (size_t i = 0; i < a.size(); i++) diff |= (unsigned char) (a[i] ^ b[i]);
	return diff == 0;
}

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) tolower(c); });
	return s;
}

std::string Trim(const std::string& s)
{
	size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
	return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool SendAll(int fd, const std::string& data)
{
	size_t sent = 0;
	while (sent < data.size()) {
		ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return false;
		sent += (size_t) n;
	}
	return true;
}

void Respond(int fd, int status, const json::Value& body)
{
	const char* reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 400 ? "Bad Request" : status == 401 ? "Unauthorized"
		: status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed"
		: status == 413 ? "Payload Too Large" : "Error";
	std::string text = status == 202 ? "" : body.dump();
	std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Content-Length: " + std::to_string(text.size()) + "\r\n"
		"Cache-Control: no-store\r\n" +
		std::string(status == 405 ? "Allow: POST\r\n" : "") +
		"Connection: close\r\n\r\n";
	SendAll(fd, head + text);
}

json::Value ErrorBody(const std::string& message)
{
	json::Value v;
	v["error"]["code"] = "http";
	v["error"]["message"] = message;
	return v;
}

} // namespace

static std::string SupportDir()
{
	const char* home = getenv("HOME");
	return std::string(home ? home : "/tmp") + "/Library/Application Support/KAGE";
}

static void MakeDirs(const std::string& dir)
{
	for (size_t slash = dir.find('/', 1); ; slash = dir.find('/', slash + 1)) {   // mkdir -p
		mkdir(dir.substr(0, slash).c_str(), 0700);
		if (slash == std::string::npos) break;
	}
}

std::string Server::SessionFilePath() { return SupportDir() + "/session.json"; }
std::string Server::TokenFilePath() { return SupportDir() + "/token"; }

// The kept token, or a new one saved for next time. Delete the file to rotate it.
static std::string LoadOrCreateToken()
{
	std::string path = Server::TokenFilePath();
	{
		std::ifstream in(path);
		std::string t;
		if (in >> t && t.size() >= 32 && t.find_first_not_of("0123456789abcdef") == std::string::npos) return t;
	}
	MakeDirs(SupportDir());
	std::string t = NewToken();
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::trunc);
		out << t << "\n";
	}
	chmod(tmp.c_str(), 0600);
	rename(tmp.c_str(), path.c_str());
	return t;
}

bool Server::Start(Handler rpc, Handler mcp, int firstPort, const std::string& version, std::string& error)
{
	fHandler = std::move(rpc);
	fMcp = std::move(mcp);
	fVersion = version;
	fToken = LoadOrCreateToken();
	fStopping = false;

	for (int port = firstPort; port < firstPort + 10; port++) {
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) { error = strerror(errno); return false; }
		int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons((uint16_t) port);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // never reachable from the network
		if (::bind(fd, (sockaddr*) &addr, sizeof addr) == 0 && listen(fd, 16) == 0) {
			fListenFd = fd;
			fPort = port;
			break;
		}
		error = "port " + std::to_string(port) + ": " + strerror(errno);
		close(fd);
	}
	if (fListenFd < 0) return false;
	error.clear();
	WriteSessionFile();
	fAcceptThread = std::thread([this] { AcceptLoop(); });
	return true;
}

void Server::Stop()
{
	if (fListenFd < 0) return;
	fStopping = true;
	shutdown(fListenFd, SHUT_RDWR);
	close(fListenFd);
	fListenFd = -1;
	if (fAcceptThread.joinable()) fAcceptThread.join();
	// Connection threads are detached; give in-flight ones a moment to finish
	// before the plug-in's code goes away.
	for (int i = 0; i < 300 && fActive > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	unlink(SessionFilePath().c_str());
}

void Server::WriteSessionFile()
{
	std::string path = SessionFilePath();
	MakeDirs(SupportDir());
	json::Value s;
	s["name"] = "KAGE";
	s["version"] = fVersion;
	s["url"] = "http://127.0.0.1:" + std::to_string(fPort) + "/rpc";
	s["mcp"] = "http://127.0.0.1:" + std::to_string(fPort) + "/mcp";
	s["port"] = fPort;
	s["token"] = fToken;
	s["pid"] = (int) getpid();
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::trunc);
		out << s.dump(2) << "\n";
	}
	chmod(tmp.c_str(), 0600);
	rename(tmp.c_str(), path.c_str());
}

void Server::AcceptLoop()
{
	while (!fStopping) {
		int fd = accept(fListenFd, nullptr, nullptr);
		if (fd < 0) {
			if (errno == EINTR) continue;
			break;   // closed by Stop()
		}
		int one = 1;
		setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
		timeval tv = {30, 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		fActive++;
		std::thread([this, fd] {
			Serve(fd);
			close(fd);
			fActive--;
		}).detach();
	}
}

void Server::Serve(int fd)
{
	// Read the header block.
	std::string buf;
	size_t headerEnd = std::string::npos;
	char chunk[8192];
	while (headerEnd == std::string::npos) {
		ssize_t n = recv(fd, chunk, sizeof chunk, 0);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return;
		buf.append(chunk, (size_t) n);
		headerEnd = buf.find("\r\n\r\n");
		if (headerEnd == std::string::npos && buf.size() > kMaxHeader) { Respond(fd, 413, ErrorBody("headers too large")); return; }
	}

	std::string head = buf.substr(0, headerEnd);
	std::string body = buf.substr(headerEnd + 4);
	size_t lineEnd = head.find("\r\n");
	std::string requestLine = head.substr(0, lineEnd);
	std::string method = requestLine.substr(0, requestLine.find(' '));
	size_t p1 = requestLine.find(' '), p2 = requestLine.rfind(' ');
	std::string target = p1 != std::string::npos && p2 > p1 ? requestLine.substr(p1 + 1, p2 - p1 - 1) : "";

	std::string origin, token, contentLength;
	size_t pos = lineEnd == std::string::npos ? head.size() : lineEnd + 2;
	while (pos < head.size()) {
		size_t end = head.find("\r\n", pos);
		if (end == std::string::npos) end = head.size();
		std::string line = head.substr(pos, end - pos);
		pos = end + 2;
		size_t colon = line.find(':');
		if (colon == std::string::npos) continue;
		std::string key = Lower(Trim(line.substr(0, colon)));
		std::string value = Trim(line.substr(colon + 1));
		if (key == "origin") origin = value;
		else if (key == "x-kage-token") token = value;
		else if (key == "authorization" && Lower(value).rfind("bearer ", 0) == 0) token = Trim(value.substr(7));
		else if (key == "content-length") contentLength = value;
	}

	if (!origin.empty()) { Respond(fd, 403, ErrorBody("browser requests are not accepted")); return; }

	if (method == "GET" && target == "/health") {
		json::Value v;
		v["ok"] = true;
		v["name"] = "KAGE";
		v["version"] = fVersion;
		Respond(fd, 200, v);
		return;
	}
	// Query strings don't matter here.
	std::string path = target.substr(0, target.find('?'));
	if (path != "/rpc" && path != "/mcp") { Respond(fd, 404, ErrorBody("use POST /rpc or POST /mcp")); return; }
	// MCP's optional GET (server-sent events) and DELETE (end session) aren't offered.
	if (method != "POST") { Respond(fd, 405, ErrorBody("use POST " + path)); return; }
	if (!SameToken(token, fToken)) {
		Respond(fd, 401, ErrorBody("missing or wrong token - read it from " + SessionFilePath()));
		return;
	}

	size_t length = contentLength.empty() ? 0 : (size_t) strtoull(contentLength.c_str(), nullptr, 10);
	if (length > kMaxBody) { Respond(fd, 413, ErrorBody("body too large")); return; }
	while (body.size() < length) {
		ssize_t n = recv(fd, chunk, sizeof chunk, 0);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return;
		body.append(chunk, (size_t) n);
	}
	body.resize(length);

	json::Value request;
	try {
		request = json::Parse(body);
	}
	catch (const json::Error& e) {
		Respond(fd, 400, ErrorBody(e.what()));
		return;
	}
	if (path == "/rpc") { Respond(fd, 200, fHandler(request)); return; }

	// MCP: one message or an array; notifications get 202 and no body.
	json::Value reply;
	if (request.isArray()) {
		json::Value out = json::Value::MakeArray();
		for (const json::Value& m : request.asArray()) {
			json::Value r = fMcp(m);
			if (!r.isNull()) out.push(r);
		}
		if (out.size()) reply = out;
	}
	else reply = fMcp(request);
	if (reply.isNull()) Respond(fd, 202, json::Value());
	else Respond(fd, 200, reply);
}

} // namespace kage
