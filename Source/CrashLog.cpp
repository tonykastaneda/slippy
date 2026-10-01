#include "CrashLog.h"

#if defined(__APPLE__)
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#endif

namespace slippy {
namespace crashlog {

#if defined(__APPLE__)

namespace {

const int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
const int kCount = sizeof kSignals / sizeof kSignals[0];
struct sigaction gOld[kCount];
bool gInstalled = false;

// Everything the handler needs is ready beforehand: it only writes.
char gPath[1024];
char gCall[1024];
char gImage[1024];
uintptr_t gBase = 0;
volatile sig_atomic_t gWriting = 0;

void Put(int fd, const char* s) { if (write(fd, s, strlen(s)) < 0) {} }

void PutHex(int fd, uintptr_t v)
{
	char b[24];
	int i = sizeof b;
	b[--i] = 0;
	do { b[--i] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v && i > 2);
	b[--i] = 'x';
	b[--i] = '0';
	Put(fd, b + i);
}

void PutDec(int fd, long v)
{
	char b[24];
	int i = sizeof b;
	b[--i] = 0;
	bool neg = v < 0;
	unsigned long u = neg ? (unsigned long) -v : (unsigned long) v;
	do { b[--i] = (char) ('0' + u % 10); u /= 10; } while (u && i > 1);
	if (neg) b[--i] = '-';
	Put(fd, b + i);
}

const char* SignalName(int sig)
{
	switch (sig) {
	case SIGSEGV: return "SIGSEGV (bad memory access)";
	case SIGBUS: return "SIGBUS (bad memory access)";
	case SIGILL: return "SIGILL (illegal instruction)";
	case SIGFPE: return "SIGFPE (arithmetic)";
	case SIGABRT: return "SIGABRT (abort)";
	case SIGTRAP: return "SIGTRAP (trap)";
	}
	return "signal";
}

void Restore()
{
	for (int i = 0; i < kCount; i++) sigaction(kSignals[i], &gOld[i], nullptr);
	gInstalled = false;
}

void Handler(int sig, siginfo_t* info, void* context)
{
	if (!gWriting) {
		gWriting = 1;
		int fd = open(gPath, O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (fd >= 0) {
			Put(fd, "\n=== Illustrator crashed ===\ntime: ");
			PutDec(fd, (long) time(nullptr));
			Put(fd, " (unix seconds)\nsignal: ");
			Put(fd, SignalName(sig));
			Put(fd, "\naddress: ");
			PutHex(fd, (uintptr_t) (info ? info->si_addr : nullptr));
			Put(fd, "\nthread: ");
			Put(fd, pthread_main_np() ? "main" : "other");
			Put(fd, "\nslippy call: ");
			Put(fd, gCall[0] ? gCall : "(none)");
			Put(fd, "\nslippy binary: ");
			Put(fd, gImage);
			Put(fd, " loaded at ");
			PutHex(fd, gBase);
			// The instruction that crashed (a function that calls nothing else
			// doesn't show up in the walked stack below).
			void* pc = nullptr;
			if (ucontext_t* uc = (ucontext_t*) context) {
#if defined(__arm64__) || defined(__aarch64__)
				if (uc->uc_mcontext) pc = (void*) __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
#elif defined(__x86_64__)
				if (uc->uc_mcontext) pc = (void*) uc->uc_mcontext->__ss.__rip;
#endif
			}
			Put(fd, "\ncrashed at:\n");
			if (pc) backtrace_symbols_fd(&pc, 1, fd);
			Put(fd, "stack:\n");
			void* frames[128];
			int n = backtrace(frames, 128);
			backtrace_symbols_fd(frames, n, fd);
			Put(fd, "=== end ===\n");
			close(fd);
		}
	}
	// Then on to whoever handled it before (Adobe's crash reporter, or the
	// system). Slippy steps aside first, so a repeat lands there directly.
	int index = -1;
	for (int i = 0; i < kCount; i++) if (kSignals[i] == sig) index = i;
	if (index < 0) return;
	struct sigaction old = gOld[index];
	Restore();
	if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction) old.sa_sigaction(sig, info, context);
	else if (!(old.sa_flags & SA_SIGINFO) && old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN) old.sa_handler(sig);
	// Default: returning runs the fault again, which now ends the process as usual.
}

} // namespace

void Install(const std::string& path)
{
	if (gInstalled) return;
	snprintf(gPath, sizeof gPath, "%s", path.c_str());
	Dl_info dl;
	if (dladdr((const void*) &Install, &dl) && dl.dli_fname) {
		snprintf(gImage, sizeof gImage, "%s", dl.dli_fname);
		gBase = (uintptr_t) dl.dli_fbase;
	}
	void* warm[4];
	backtrace(warm, 4);   // loads what backtrace needs now, not mid-crash
	// Room to run when the crash is a stack overflow (unless Illustrator has its own).
	stack_t current;
	if (!sigaltstack(nullptr, &current) && (current.ss_flags & SS_DISABLE)) {
		static char altStack[256 * 1024];
		stack_t ss = {};
		ss.ss_sp = altStack;
		ss.ss_size = sizeof altStack;
		sigaltstack(&ss, nullptr);
	}
	struct sigaction sa = {};
	sa.sa_sigaction = Handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	for (int i = 0; i < kCount; i++) sigaction(kSignals[i], &sa, &gOld[i]);
	gInstalled = true;
}

void Uninstall()
{
	if (!gInstalled) return;
	// Only where Slippy's handler is still the one in place.
	for (int i = 0; i < kCount; i++) {
		struct sigaction current;
		if (!sigaction(kSignals[i], nullptr, &current) && (current.sa_flags & SA_SIGINFO) && current.sa_sigaction == Handler)
			sigaction(kSignals[i], &gOld[i], nullptr);
	}
	gInstalled = false;
}

void SetCurrentCall(const std::string& method, const std::string& params)
{
	snprintf(gCall, sizeof gCall, "%s %s", method.c_str(), params.c_str());
}

void ClearCurrentCall() { gCall[0] = 0; }

#else

void Install(const std::string&) {}
void Uninstall() {}
void SetCurrentCall(const std::string&, const std::string&) {}
void ClearCurrentCall() {}

#endif

} // namespace crashlog
} // namespace slippy
