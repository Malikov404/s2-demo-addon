// SPDX-License-Identifier: AGPL-3.0-only
#include <Windows.h>
#include <cstddef>
#include "MirvPovLocalization.h"

namespace {
// localize.dll's Localize_001 interface. The current CLocalize vtable uses
// +0x78 for Find(const char *), while +0x88 is FindSafe and returns the input
// token when no translation exists. Use Find so missing tokens remain null.
constexpr size_t kLocalizationFindVtableIndex = 0x78 / sizeof(void *);
using CreateInterface_t = void * (__cdecl *)(const char * name, int * returnCode);
using LocalizationFind_t = const char * (__fastcall *)(
    void * localize,
    const char * token);

HMODULE g_LocalizeDll = nullptr;
void * g_Localize = nullptr;
LocalizationFind_t g_LocalizationFind = nullptr;
bool ResolveLocalization()
{
    if(nullptr != g_LocalizationFind) return true;

    HMODULE localizeDll = g_LocalizeDll;
    if(nullptr == localizeDll) localizeDll = GetModuleHandleA("localize.dll");
    if(nullptr == localizeDll) return false;

    __try {
        auto createInterface = reinterpret_cast<CreateInterface_t>(
            GetProcAddress(localizeDll, "CreateInterface"));
        if(nullptr == createInterface) return false;

        void * localize = createInterface("Localize_001", nullptr);
        if(nullptr == localize) return false;

        void ** vtable = *reinterpret_cast<void ***>(localize);
        if(nullptr == vtable || nullptr == vtable[kLocalizationFindVtableIndex])
            return false;

        g_LocalizeDll = localizeDll;
        g_Localize = localize;
        g_LocalizationFind = reinterpret_cast<LocalizationFind_t>(
            vtable[kLocalizationFindVtableIndex]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_LocalizeDll = nullptr;
        g_Localize = nullptr;
        g_LocalizationFind = nullptr;
        return false;
    }
}

} // namespace

const char * MirvPov_LocalizeToken(const char * token)
{
    if(nullptr == token || '\0' == token[0] || !ResolveLocalization())
        return nullptr;

    __try {
        const char * localized = g_LocalizationFind(g_Localize, token);
        return nullptr != localized && '\0' != localized[0] ? localized : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}


bool MirvPov_LocalizationReady() { return ResolveLocalization(); }
