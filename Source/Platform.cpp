#include "Platform.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>

#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& s)
{
	if (s.empty()) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
	std::wstring w((size_t) n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n);
	return w;
}

std::string Utf8(const std::wstring& w)
{
	if (w.empty()) return "";
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t) n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int) w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

fs::path Path(const std::string& s) { return fs::path(Wide(s)); }

// Main-thread messages go to a message-only window made on the main thread.
const UINT kKickMessage = WM_APP + 1;
HWND gWindow = nullptr;
std::function<void()> gKick;
std::map<UINT_PTR, std::function<void()>> gLater;
UINT_PTR gNextTimer = 1;

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == kKickMessage) {
		if (gKick) gKick();
		return 0;
	}
	if (msg == WM_TIMER) {
		KillTimer(hwnd, wp);
		auto it = gLater.find(wp);
		if (it != gLater.end()) {
			std::function<void()> work = std::move(it->second);
			gLater.erase(it);
			if (work) work();
		}
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

HINSTANCE ThisModule()
{
	HMODULE module = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) &MainProc, &module);
	return module;
}

} // namespace

namespace slippy {
namespace platform {

std::string SupportDir()
{
	PWSTR appData = nullptr;
	std::string dir;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) dir = Utf8(appData);
	CoTaskMemFree(appData);
	if (dir.empty()) {
		const char* env = getenv("APPDATA");
		dir = env ? env : "C:\\";
	}
	return JoinPath(dir, "Slippy");
}

std::string JoinPath(const std::string& dir, const std::string& name) { return dir + "\\" + name; }

void MakeDirs(const std::string& dir)
{
	std::error_code ignored;
	fs::create_directories(Path(dir), ignored);
}

void WritePrivateFile(const std::string& path, const std::string& contents)
{
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(Path(tmp), std::ios::trunc | std::ios::binary);
		out << contents;
	}
	// %APPDATA% is already the user's own; nothing more to lock down.
	MoveFileExW(Wide(tmp).c_str(), Wide(path).c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool ReadFirstWord(const std::string& path, std::string& word)
{
	std::ifstream in(Path(path));
	return (bool) (in >> word);
}

void RemoveFile(const std::string& path) { DeleteFileW(Wide(path).c_str()); }

bool Readable(const std::string& path)
{
	std::ifstream in(Path(path), std::ios::binary);
	return in.good();
}

bool IsAbsolutePath(const std::string& path) { return !path.empty() && Path(path).is_absolute(); }

int ProcessId() { return (int) GetCurrentProcessId(); }

void RandomBytes(unsigned char* out, size_t count)
{
	BCryptGenRandom(nullptr, out, (ULONG) count, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

bool EnvFlag(const char* name, const char* value)
{
	const char* v = getenv(name);
	return v && !strcmp(v, value);
}

void MainThreadInit(std::function<void()> kick)
{
	gKick = std::move(kick);
	if (gWindow) return;
	WNDCLASSEXW wc = {sizeof wc};
	wc.lpfnWndProc = MainProc;
	wc.hInstance = ThisModule();
	wc.lpszClassName = L"SlippyMainThread";
	RegisterClassExW(&wc);
	gWindow = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
}

void MainThreadKick()
{
	if (HWND w = gWindow) PostMessageW(w, kKickMessage, 0, 0);
}

void MainThreadAfter(double seconds, std::function<void()> work)
{
	if (!gWindow) return;
	UINT_PTR id = gNextTimer++;
	gLater[id] = std::move(work);
	SetTimer(gWindow, id, (UINT) std::max(0.0, seconds * 1000), nullptr);
}

void MainThreadShutdown()
{
	gKick = nullptr;
	gLater.clear();
	if (gWindow) {
		HWND w = gWindow;
		gWindow = nullptr;
		DestroyWindow(w);
		UnregisterClassW(L"SlippyMainThread", ThisModule());
	}
}

} // namespace platform
} // namespace slippy

#else // macOS

#include <dispatch/dispatch.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::function<void()> gKick;   // main thread only
void KickMain(void*) { if (gKick) gKick(); }
}

namespace slippy {
namespace platform {

std::string SupportDir()
{
	const char* home = getenv("HOME");
	return std::string(home ? home : "/tmp") + "/Library/Application Support/Slippy";
}

std::string JoinPath(const std::string& dir, const std::string& name) { return dir + "/" + name; }

void MakeDirs(const std::string& dir)
{
	for (size_t slash = dir.find('/', 1); ; slash = dir.find('/', slash + 1)) {   // mkdir -p
		mkdir(dir.substr(0, slash).c_str(), 0700);
		if (slash == std::string::npos) break;
	}
}

void WritePrivateFile(const std::string& path, const std::string& contents)
{
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::trunc);
		out << contents;
	}
	chmod(tmp.c_str(), 0600);
	rename(tmp.c_str(), path.c_str());
}

bool ReadFirstWord(const std::string& path, std::string& word)
{
	std::ifstream in(path);
	return (bool) (in >> word);
}

void RemoveFile(const std::string& path) { unlink(path.c_str()); }
bool Readable(const std::string& path) { return access(path.c_str(), R_OK) == 0; }
bool IsAbsolutePath(const std::string& path) { return !path.empty() && path[0] == '/'; }
int ProcessId() { return (int) getpid(); }
void RandomBytes(unsigned char* out, size_t count) { arc4random_buf(out, count); }

bool EnvFlag(const char* name, const char* value)
{
	const char* v = getenv(name);
	return v && !strcmp(v, value);
}

void MainThreadInit(std::function<void()> kick) { gKick = std::move(kick); }
void MainThreadKick() { dispatch_async_f(dispatch_get_main_queue(), nullptr, KickMain); }

void MainThreadAfter(double seconds, std::function<void()> work)
{
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t) (seconds * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ work(); });
}

void MainThreadShutdown() { gKick = nullptr; }

} // namespace platform
} // namespace slippy

#endif
