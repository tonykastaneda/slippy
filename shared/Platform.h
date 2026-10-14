#ifndef __SLIPPY_PLATFORM_H__
#define __SLIPPY_PLATFORM_H__

// The little bit of macOS / Windows difference outside the panel: files,
// randomness, and waking Illustrator's main thread. No SDK. Paths are UTF-8.

#include <functional>
#include <string>

namespace slippy {
namespace platform {

// ~/Library/Application Support/Slippy, or %APPDATA%\Slippy - plus the
// subfolder, when set (Slippy for Photoshop keeps its token apart in "Photoshop").
std::string SupportDir();
void SetSupportSubdir(const std::string& name);
std::string JoinPath(const std::string& dir, const std::string& name);
void MakeDirs(const std::string& dir);
// Writes through a temp file and renames it into place; only the user can read it.
void WritePrivateFile(const std::string& path, const std::string& contents);
bool ReadFirstWord(const std::string& path, std::string& word);
// Appends a line; past maxBytes the file moves to <path>.1 and starts over.
void AppendLine(const std::string& path, const std::string& line, size_t maxBytes);
std::string ReadFile(const std::string& path);
void RemoveFile(const std::string& path);
bool Readable(const std::string& path);
bool IsAbsolutePath(const std::string& path);
// Where to write a file an agent named: an absolute path as given, a bare
// file name on the Desktop; "" for anything else (a relative path, or a path
// from another computer, like an agent's own sandbox).
std::string OutputPath(const std::string& path);
std::string DesktopDir();
// The error when OutputPath gives "".
#ifdef _WIN32
constexpr const char* kOutputPathHelp = "'path' must be a full Windows path on this computer, like C:\\Users\\you\\Desktop\\art.png, "
#else
constexpr const char* kOutputPathHelp = "'path' must be a full path on this Mac, like /Users/you/Desktop/art.png, "
#endif
	"or just a file name (saved to the Desktop). To see the picture yourself, leave 'path' out: the image comes back in the reply.";
int ProcessId();
void RandomBytes(unsigned char* out, size_t count);
bool EnvFlag(const char* name, const char* value);   // getenv(name) == value

// Main thread. Init on the main thread first; Kick is safe from any thread
// and runs 'kick' on the main thread soon after. After runs work later on
// the main thread.
void MainThreadInit(std::function<void()> kick);
void MainThreadKick();
void MainThreadAfter(double seconds, std::function<void()> work);
void MainThreadShutdown();

} // namespace platform
} // namespace slippy

#endif // __SLIPPY_PLATFORM_H__
