#ifndef __SLIPPY_PS_SUITES_H__
#define __SLIPPY_PS_SUITES_H__

// Photoshop's suites, acquired once at startup (Slippy stays loaded), and
// what everything that talks to Photoshop shares: string IDs, UTF-8 <-> ZString,
// and errors. Main thread only.

#include "SPBasic.h"
#include "PIActions.h"
#include "ASZStringSuite.h"
#include "PIAliasSuite.h"
#include "PIHandleSuite.h"

#include <string>

namespace slippy {
namespace ps {

extern SPBasicSuite* sBasic;
extern SPPluginRef sSelf;
extern PSActionControlProcs* sControl;
extern PSActionDescriptorProcs* sDesc;
extern PSActionReferenceProcs* sRef;
extern PSActionListProcs* sList;
extern ASZStringSuite2* sZString;
extern PSAliasSuite* sAlias;      // file references on Windows
extern PSHandleSuite2* sHandle;

bool AcquireSuites(SPBasicSuite* basic, SPPluginRef self, std::string& missing);
void ReleaseSuites();

// "layer" -> its type ID (cached). A four-character code works too ("Lyr ").
DescriptorTypeID ID(const char* stringID);
inline DescriptorTypeID ID(const std::string& s) { return ID(s.c_str()); }
// A type ID back to its string ID; a four-character code when it has none.
std::string StrID(DescriptorTypeID id);

std::string FromZ(ASZString z);   // releases z
ASZString ToZ(const std::string& utf8);   // the caller releases it

// A failed Photoshop call, as the command layer reports it.
struct PsError {
	int code;   // JSON-RPC code (Commands.h)
	std::string message;
	int psError = 0;   // Photoshop's own (OSErr), when it gave one
};
[[noreturn]] void Fail(int code, const std::string& message);
std::string ErrorText(OSErr e);   // "not available right now (-25920)"
void Check(OSErr e, const std::string& what);   // throws PsError naming 'what'

} // namespace ps
} // namespace slippy

#endif // __SLIPPY_PS_SUITES_H__
