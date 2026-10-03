#include "PsSuites.h"
#include "Commands.h"

#include <cstring>
#include <map>

namespace slippy {
namespace ps {

SPBasicSuite* sBasic = nullptr;
SPPluginRef sSelf = nullptr;
PSActionControlProcs* sControl = nullptr;
PSActionDescriptorProcs* sDesc = nullptr;
PSActionReferenceProcs* sRef = nullptr;
PSActionListProcs* sList = nullptr;
ASZStringSuite2* sZString = nullptr;
PSAliasSuite* sAlias = nullptr;
PSHandleSuite2* sHandle = nullptr;

namespace {

template <typename T>
bool Acquire(const char* name, int32 version, T*& suite)
{
	const void* p = nullptr;
	if (sBasic->AcquireSuite(name, version, &p) || !p) { suite = nullptr; return false; }
	suite = (T*) p;
	return true;
}

template <typename T>
void Release(const char* name, int32 version, T*& suite)
{
	if (suite && sBasic) sBasic->ReleaseSuite(name, version);
	suite = nullptr;
}

std::map<std::string, DescriptorTypeID> gIds;

} // namespace

bool AcquireSuites(SPBasicSuite* basic, SPPluginRef self, std::string& missing)
{
	sBasic = basic;
	sSelf = self;
	if (!Acquire(kPSActionControlSuite, kPSActionControlSuiteVersion, sControl)) missing += " ActionControl";
	if (!Acquire(kPSActionDescriptorSuite, kPSActionDescriptorSuiteVersion, sDesc)) missing += " ActionDescriptor";
	if (!Acquire(kPSActionReferenceSuite, kPSActionReferenceSuiteVersion, sRef)) missing += " ActionReference";
	if (!Acquire(kPSActionListSuite, kPSActionListSuiteVersion, sList)) missing += " ActionList";
	if (!Acquire(kASZStringSuite, kASZStringSuiteVersion2, sZString)) missing += " ZString";
	bool alias = Acquire(kPSAliasSuite, kPSAliasSuiteVersion2, sAlias), handle = Acquire(kPSHandleSuite, kPSHandleSuiteVersion2, sHandle);
#ifdef _WIN32
	if (!alias || !handle) missing += " Alias/Handle";   // paths on Windows go through them
#else
	(void) alias; (void) handle;
#endif
	return missing.empty();
}

void ReleaseSuites()
{
	Release(kPSActionControlSuite, kPSActionControlSuiteVersion, sControl);
	Release(kPSActionDescriptorSuite, kPSActionDescriptorSuiteVersion, sDesc);
	Release(kPSActionReferenceSuite, kPSActionReferenceSuiteVersion, sRef);
	Release(kPSActionListSuite, kPSActionListSuiteVersion, sList);
	Release(kASZStringSuite, kASZStringSuiteVersion2, sZString);
	Release(kPSAliasSuite, kPSAliasSuiteVersion2, sAlias);
	Release(kPSHandleSuite, kPSHandleSuiteVersion2, sHandle);
}

DescriptorTypeID ID(const char* stringID)
{
	auto it = gIds.find(stringID);
	if (it != gIds.end()) return it->second;
	DescriptorTypeID id = 0;
	size_t n = strlen(stringID);
	// A four-character code ("Lyr ", "null") is its own ID when Photoshop has
	// no string ID by that name.
	if (sControl->StringIDToTypeID(stringID, &id) || !id) {
		if (n == 4) id = ((DescriptorTypeID) (unsigned char) stringID[0] << 24) | ((unsigned char) stringID[1] << 16) |
			((unsigned char) stringID[2] << 8) | (unsigned char) stringID[3];
	}
	gIds[stringID] = id;
	return id;
}

std::string StrID(DescriptorTypeID id)
{
	char buf[256] = {0};
	if (!sControl->TypeIDToStringID(id, buf, sizeof buf) && buf[0]) return buf;
	char code[5] = {(char) (id >> 24), (char) (id >> 16), (char) (id >> 8), (char) id, 0};
	return code;
}

std::string FromZ(ASZString z)
{
	if (!z) return "";
	std::string out;
	ASUInt32 units = sZString->LengthAsUnicodeCString(z);
	if (units) {
		std::u16string w(units, u'\0');
		if (!sZString->AsUnicodeCString(z, (ASUnicode*) &w[0], units, false)) {
			while (!w.empty() && w.back() == 0) w.pop_back();
			for (size_t i = 0; i < w.size(); i++) {   // UTF-16 -> UTF-8
				uint32_t c = w[i];
				if (c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) c = 0x10000 + ((c - 0xD800) << 10) + (w[++i] - 0xDC00);
				if (c < 0x80) out += (char) c;
				else if (c < 0x800) { out += (char) (0xC0 | (c >> 6)); out += (char) (0x80 | (c & 0x3F)); }
				else if (c < 0x10000) { out += (char) (0xE0 | (c >> 12)); out += (char) (0x80 | ((c >> 6) & 0x3F)); out += (char) (0x80 | (c & 0x3F)); }
				else { out += (char) (0xF0 | (c >> 18)); out += (char) (0x80 | ((c >> 12) & 0x3F)); out += (char) (0x80 | ((c >> 6) & 0x3F)); out += (char) (0x80 | (c & 0x3F)); }
			}
		}
	}
	sZString->Release(z);
	return out;
}

ASZString ToZ(const std::string& s)
{
	std::u16string w;   // UTF-8 -> UTF-16
	for (size_t i = 0; i < s.size();) {
		unsigned char b = (unsigned char) s[i];
		uint32_t c;
		int extra = b < 0x80 ? 0 : b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
		c = extra == 0 ? b : extra == 1 ? (b & 0x1F) : extra == 2 ? (b & 0x0F) : (b & 0x07);
		for (int k = 1; k <= extra && i + k < s.size(); k++) c = (c << 6) | ((unsigned char) s[i + k] & 0x3F);
		i += 1 + extra;
		if (c >= 0x10000) { c -= 0x10000; w += (char16_t) (0xD800 + (c >> 10)); w += (char16_t) (0xDC00 + (c & 0x3FF)); }
		else w += (char16_t) c;
	}
	ASZString z = nullptr;
	sZString->MakeFromUnicode((ASUnicode*) w.c_str(), w.size(), &z);   // UTF-16 units, despite the "byteCount" name
	return z;
}

void Fail(int code, const std::string& message)
{
	throw PsError{code, message};
}

std::string ErrorText(OSErr e)
{
	const char* what = nullptr;
	switch (e) {
	case -25920: what = "not available right now (wrong state, layer kind or selection)"; break;
	case -25921: case -25930: what = "Photoshop is busy"; break;
	case -25922: what = "that layer, document or item wasn't found"; break;
	case -25923: what = "invalid parameters"; break;
	case -128: what = "cancelled"; break;
	case -108: what = "out of memory"; break;
	case -43: what = "file not found"; break;
	case -1: what = "Photoshop refused it"; break;
	}
	return std::string(what ? what : "Photoshop error") + " (" + std::to_string(e) + ")";
}

void Check(OSErr e, const std::string& what)
{
	if (!e) return;
	int code = e == -25922 ? kErrNotFound : e == -25923 ? kErrInvalidParams : kErrHost;
	throw PsError{code, "Couldn't " + what + ": " + ErrorText(e), e};
}

} // namespace ps
} // namespace slippy
