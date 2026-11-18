#include "stdafx.h"

#include "MirvPovVoiceBan.h"
#include "MirvPovCore.h"
#include "MirvPovVoice.h"

#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"
#include "../deps/release/Detours/src/detours.h"
#include "../shared/AfxConsole.h"
#include "../shared/binutils.h"

#include <intrin.h>
#include <stdint.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

extern SOURCESDK::CS2::ISource2EngineToClient * g_pEngineToClient;

namespace {

bool g_Enabled = false;
bool g_ResolveAttempted = false;
bool g_Hooked = false;
size_t g_VoiceBlockReturnAddress = 0;
size_t g_AvatarBlockReturnAddress = 0;
using ReloadAvatars_t = void (*)();
ReloadAvatars_t g_ReloadAvatars = nullptr;
void ** g_AvatarUIEngine = nullptr;
bool g_AvatarBypassActive = false;
using IsPlayerBlocked_t = bool (__fastcall *)(void *, unsigned int);
IsPlayerBlocked_t g_OriginalIsPlayerBlocked = nullptr;

size_t FindUnique(Afx::BinUtils::MemRange text, const char * pattern)
{
    const auto match = Afx::BinUtils::FindPatternString(text, pattern);
    if(match.IsEmpty()) return 0;
    return Afx::BinUtils::FindPatternString({match.Start + 1, text.End}, pattern).IsEmpty() ? match.Start : 0;
}

size_t FindEventSymbol(Afx::BinUtils::MemRange text, Afx::BinUtils::MemRange data, const char * name)
{
    const auto eventName = Afx::BinUtils::FindCString(data, name);
    if(eventName.IsEmpty()) return 0;
    const char * registrationPattern = "48 8D 15 ?? ?? ?? ?? 49 89 43 E0 48 8D 0D ?? ?? ?? ?? 33 C0 45 33 C0";
    while(!text.IsEmpty()) {
        const auto match = Afx::BinUtils::FindPatternString(text, registrationPattern);
        if(match.IsEmpty()) return 0;
        int32_t relative;
        memcpy(&relative, reinterpret_cast<const void *>(match.Start + 3), sizeof(relative));
        if(match.Start + 7 + relative == eventName.Start) {
            memcpy(&relative, reinterpret_cast<const void *>(match.Start + 14), sizeof(relative));
            return match.Start + 18 + relative;
        }
        text.Start = match.Start + 1;
    }
    return 0;
}

bool IsDemoUnmuteEnabled()
{
    return (MIRV_POV_FEATURE_ACTIVE("voiceban") || g_Enabled)
        && g_pEngineToClient && g_pEngineToClient->IsPlayingDemo();
}

void UpdateAvatarCache()
{
    if(!g_ReloadAvatars || !g_AvatarUIEngine || !*g_AvatarUIEngine) return;
    const bool active = g_AvatarBlockReturnAddress && IsDemoUnmuteEnabled()
        && MirvPovDebug_IsFeatureEnabled("voiceban_avatars");
    if(active == g_AvatarBypassActive) return;
    g_AvatarBypassActive = active;
    g_ReloadAvatars(); // Native ReloadAllAvatarImages, once per presentation change.
}

bool __fastcall New_IsPlayerBlocked(void * voiceStatus, unsigned int playerSlot)
{
    const auto caller = reinterpret_cast<size_t>(_ReturnAddress());
    const bool avatar = g_AvatarBlockReturnAddress && caller == g_AvatarBlockReturnAddress;
    if((caller == g_VoiceBlockReturnAddress || avatar) && playerSlot < 64 && IsDemoUnmuteEnabled()) {
        // Hiding an avatar is independent of team routing and manual voice mute.
        if(!avatar) return !MirvPovVoice_AllowsPlayer(playerSlot);
        if(MirvPovDebug_IsFeatureEnabled("voiceban_avatars")) return false;
    }
    return g_OriginalIsPlayerBlocked(voiceStatus, playerSlot);
}

bool ResolveVoiceBan(HMODULE clientDll)
{
    Afx::BinUtils::ImageSectionsReader sections(clientDll);
    if(sections.Eof()) return false;
    const auto text = sections.GetMemRange();
    if(text.IsEmpty()) return false;
    // GetVoiceStatus(), zero-based player slot, IsPlayerBlocked(), bool result.
    const char * pattern = "E8 ?? ?? ?? ?? 8B D7 48 8B C8 E8 ?? ?? ?? ?? 84 C0 0F 85";
    const auto sequence = FindUnique(text, pattern);
    if(!sequence) return false;

    int32_t relative = 0;
    memcpy(&relative, reinterpret_cast<const void *>(sequence + 11), sizeof(relative));
    const size_t target = static_cast<size_t>(static_cast<intptr_t>(sequence + 15) + relative);
    if(target < text.Start || target >= text.End) return false;

    g_VoiceBlockReturnAddress = sequence + 15;
    // Avatar sanitization calls the same slot predicate after resolving its Steam ID.
    const char * avatarPattern = "8B 4C 24 38 8D 51 FF 83 F9 FF 75 ?? BA FF FF FF FF 48 8B CE E8 ?? ?? ?? ?? 48 8B 74 24 30 84 C0";
    const auto avatar = FindUnique(text, avatarPattern);
    if(avatar) {
        memcpy(&relative, reinterpret_cast<const void *>(avatar + 21), sizeof(relative));
        if(avatar + 25 + relative == target) g_AvatarBlockReturnAddress = avatar + 25;
    }
    if(!g_AvatarBlockReturnAddress) advancedfx::Warning("[mirv_voicebanFix] avatar mute bypass unavailable\n");

    // Reuse the native no-argument ReloadAllAvatarImages dispatcher. It queues
    // the normal UI event; it does not reload cosmetics or alter game settings.
    const char * reloadPattern = "40 53 48 83 EC 30 48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 48 8B 03 B9 20 00 00 00 48 89 7C 24 40 48 8B B8 78 01 00 00 E8 ?? ?? ?? ?? 48 85 C0 74 ?? 0F B7 0D ?? ?? ?? ?? 66 89 48 08 48 B9 00 00 00 00 FF FF FF FF 48 89 48 0C";
    size_t reload = 0;
    sections.Next();
    const auto eventSymbol = sections.Eof() ? 0 : FindEventSymbol(text, sections.GetMemRange(), "ReloadAllAvatarImages");
    auto remaining = text;
    while(eventSymbol && !remaining.IsEmpty()) {
        const auto candidate = Afx::BinUtils::FindPatternString(remaining, reloadPattern);
        if(candidate.IsEmpty()) break;
        memcpy(&relative, reinterpret_cast<const void *>(candidate.Start + 0x33), sizeof(relative));
        if(candidate.Start + 0x37 + relative == eventSymbol) { reload = candidate.Start; break; }
        remaining.Start = candidate.Start + 1;
    }
    if(reload && g_AvatarBlockReturnAddress) {
        memcpy(&relative, reinterpret_cast<const void *>(reload + 9), sizeof(relative));
        const auto slot = reload + 13 + relative;
        const auto base = reinterpret_cast<size_t>(clientDll);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(clientDll);
        const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        if(slot >= base && slot - base <= pe->OptionalHeader.SizeOfImage - sizeof(void *) && slot % alignof(void *) == 0) {
            g_AvatarUIEngine = reinterpret_cast<void **>(slot);
            g_ReloadAvatars = reinterpret_cast<ReloadAvatars_t>(reload);
        }
    }
    if(g_AvatarBlockReturnAddress && !g_ReloadAvatars) advancedfx::Warning("[mirv_voicebanFix] avatar cache refresh unavailable\n");
    g_OriginalIsPlayerBlocked = reinterpret_cast<IsPlayerBlocked_t>(target);
    return true;
}
bool InstallHook(HMODULE clientDll) {
    if(!ResolveVoiceBan(clientDll)) return false;
    if(NO_ERROR != DetourTransactionBegin()) return false;
    if(NO_ERROR != DetourUpdateThread(GetCurrentThread())
        || NO_ERROR != DetourAttach(reinterpret_cast<PVOID *>(&g_OriginalIsPlayerBlocked), New_IsPlayerBlocked)) {
        DetourTransactionAbort();
        return false;
    }
    return NO_ERROR == DetourTransactionCommit();
}

} // namespace

void MirvPovVoiceBan_OnRenderPass()
{
    if(!g_ResolveAttempted && (MIRV_POV_FEATURE_ACTIVE("voiceban") || g_Enabled)) {
        const auto clientDll = GetModuleHandleW(L"client.dll");
        if(!clientDll) return;
        MirvPovVoiceBan_Initialize(clientDll);
    }
    if(g_Hooked) UpdateAvatarCache();
}

void MirvPovVoiceBan_SetEnabled(bool enabled) { g_Enabled = enabled; }
bool MirvPovVoiceBan_IsReady() { return g_Hooked; }
bool MirvPovVoiceBan_AvatarsReady() { return g_Hooked && g_AvatarBlockReturnAddress && g_ReloadAvatars; }

void MirvPovVoiceBan_ResetDemoState()
{
    if(g_Hooked) UpdateAvatarCache();
}

void MirvPovVoiceBan_Initialize(HMODULE clientDll) {
    if(g_ResolveAttempted || !clientDll) return;
    g_ResolveAttempted = true;
    g_Hooked = InstallHook(clientDll);
    if(!g_Hooked) advancedfx::Warning("[mirv_voicebanFix] demo voice block hook unavailable\n");
}
