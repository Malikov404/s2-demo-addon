#include "stdafx.h"

#include "MirvPovVoice.h"
#include "MirvVoicePolicy.h"

#include "ClientEntitySystem.h"
#include "Globals.h"
#include "MirvPovCore.h"
#include "MirvPovLocalization.h"
#include "MirvPovCvars.h"
#include <atomic>
#include "SchemaSystem.h"

#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"
extern SOURCESDK::CS2::ISource2EngineToClient * g_pEngineToClient;

#include "../shared/AfxConsole.h"
#include "../shared/binutils.h"

#define WIN32_LEAN_AND_MEAN
#include "../deps/release/Detours/src/detours.h"

#include <intrin.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

static constexpr int kMirvPovIsPlayingDemoVtableIndex = 42;
static constexpr int kMirvPovVoiceSeekThresholdTicks = 16;
static constexpr int kMirvPovVoiceSeekClearRenderPasses = 3;
static constexpr float kMirvPovSyntheticSpeakingSeconds = 0.65f;

static std::atomic<bool> g_MirvPovVoiceEnabled{true};
static std::atomic<bool> g_ManualVoiceMuted{false};
static advancedfx::MirvVoicePolicy g_VoicePolicy;
static std::atomic<uint64_t> g_AudibleVoiceMask{advancedfx::MirvVoicePolicy::All};
static std::atomic<uint64_t> g_PendingSpeaking{0};
static std::atomic<bool> g_VoiceManaged{false};
static MirvPovVoiceMode g_MirvPovVoiceMode = MirvPovVoiceMode::Team;
static int g_MirvPovVoiceLastDemoTick = INT_MIN;
static int g_MirvPovVoiceClearRenderPasses = 0;
static bool g_MirvPovVoiceHadDemoFile = false;
static float g_MirvPovSyntheticSpeakingUntil[64] = {};
static size_t g_MirvPovShowSpeakerRetAddr = 0;
static size_t g_MirvPovServerVoiceDataAddr = 0;
static size_t g_MirvPovVoiceStatusGetAddr = 0;
static size_t g_MirvPovVoiceStatusUpdateSpeakerStatusAddr = 0;

typedef bool (*MirvPov_IsPlayingDemo_t)(void * This);
static MirvPov_IsPlayingDemo_t g_Org_MirvPov_IsPlayingDemo = nullptr;
static bool g_bMirvPovIsPlayingDemoHooked = false;

typedef __int64 (__fastcall * MirvPov_ServerVoiceData_t)(__int64 This, __int64 msg);
static MirvPov_ServerVoiceData_t g_Org_MirvPov_ServerVoiceData = nullptr;
static bool g_bMirvPovServerVoiceDataHooked = false;

typedef __int64 (__fastcall * MirvPov_VoiceStatus_Get_t)();
typedef __int64 (__fastcall * MirvPov_VoiceStatus_UpdateSpeakerStatus_t)(__int64 voiceStatus, unsigned int playerSlot, int localSlot, unsigned __int8 talking);

struct MirvPovVoiceMaskCvar {
    const char * name;
    int previousValue = 0;
    bool previousValueSaved = false;
    int lastAppliedValue = 0;
};

static MirvPovVoiceMaskCvar g_MirvPovVoiceMaskLow = { "tv_listen_voice_indices" };
static MirvPovVoiceMaskCvar g_MirvPovVoiceMaskHigh = { "tv_listen_voice_indices_h" };

// Native VoiceStatus keeps three visible rows (24 bytes each), separately
// from its active-speaker list. Only invalidate the cached speaker state;
// the native updater owns and republishes all row/panel contents.
using MirvPov_FormatSpeaker_t = __int64 (__fastcall *)(void *, char *, unsigned int,
    CEntityInstance *, const char *, bool, unsigned int);
using MirvPov_UpdateSpeakers_t = __int64 (__fastcall *)(void *);
using MirvPov_GetSpeakerPawn_t = CEntityInstance * (__fastcall *)(CEntityInstance *);
static MirvPov_FormatSpeaker_t g_OrgFormatSpeaker = nullptr;
static MirvPov_UpdateSpeakers_t g_OrgUpdateSpeakers = nullptr;
static MirvPov_GetSpeakerPawn_t g_GetSpeakerPawn = nullptr;
static bool g_VoiceLocationHooked = false;

struct MirvPovVoiceLocationKey {
    int slot;
    uint32_t povHandle;
    uint32_t speakerHandle;
    uint32_t pawnHandle;
    int povTeam;
    char place[18];
};
static MirvPovVoiceLocationKey g_VoiceLocationKeys[3] = {};
static bool g_VoiceLocationWasActive = false;
static bool g_VoiceLocationRefreshPending = false;

static CEntityInstance * MirvPov_GetVoiceLocationPov() {
    if(!g_MirvPovVoiceEnabled || !MIRV_POV_FEATURE_ACTIVE("voice")
        || !MIRV_POV_FEATURE_ACTIVE("voice_location")) return nullptr;
    CEntityInstance * pov = GetCurrentPovPlayerController();
    return pov && (pov->GetTeam() == 2 || pov->GetTeam() == 3) ? pov : nullptr;
}

static void MirvPov_CopySpeakerPlace(CEntityInstance * controller, char (&place)[18],
    uint32_t & pawnHandle) {
    place[0] = '\0';
    pawnHandle = 0xFFFFFFFFu;
    if(!controller || !g_GetSpeakerPawn
        || g_clientDllOffsets.C_CSPlayerPawn.m_szLastPlaceName < 0) return;
    __try {
        CEntityInstance * pawn = g_GetSpeakerPawn(controller);
        if(!pawn || !pawn->IsPlayerPawn()) return;
        pawnHandle = pawn->GetHandle().ToInt();
        const char * source = pawn->GetLastPlaceName();
        size_t length = source ? strnlen_s(source, sizeof(place)) : 0;
        if(length && length < sizeof(place)) memcpy(place, source, length + 1);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        place[0] = '\0';
        pawnHandle = 0xFFFFFFFFu;
    }
}

static __int64 __fastcall New_MirvPov_FormatSpeaker(void * hud, char * output,
    unsigned int capacity, CEntityInstance * speaker, const char * name,
    bool alive, unsigned int team) {
    CEntityInstance * pov = MirvPov_GetVoiceLocationPov();
    if(!pov) return g_OrgFormatSpeaker(hud, output, capacity, speaker, name, alive, team);

    char decorated[1024];
    const char * displayName = name;
    if(alive && team == static_cast<unsigned int>(pov->GetTeam())) {
        char place[18] = {};
        uint32_t pawnHandle;
        MirvPov_CopySpeakerPlace(speaker, place, pawnHandle);
        if(place[0]) {
            char token[20];
            snprintf(token, sizeof(token), "#%s", place);
            const char * localized = MirvPov_LocalizeToken(token);
            if(localized && localized[0] && strcmp(localized, token)) {
                int length = snprintf(decorated, sizeof(decorated),
                    "%s @ <font color=\"#40ff40\">%s</font>", name ? name : "", localized);
                if(0 < length && length < sizeof(decorated)) displayName = decorated;
            }
        }
    }
    // Suppress only the formatter's native location branch to avoid a second
    // suffix from the real spectator context. The row's alive/skull state is
    // separate and is never changed by this argument.
    return g_OrgFormatSpeaker(hud, output, capacity, speaker, displayName, false, team);
}

static __int64 __fastcall New_MirvPov_UpdateSpeakers(void * hud) {
    CEntityInstance * pov = MirvPov_GetVoiceLocationPov();
    bool active = nullptr != pov;
    if(active || g_VoiceLocationWasActive || g_VoiceLocationRefreshPending) {
        for(int row = 0; row < 3; ++row) {
            unsigned char * cache = static_cast<unsigned char *>(hud) + 160 + 24 * row;
            MirvPovVoiceLocationKey key = {};
            key.slot = *reinterpret_cast<int *>(cache);
            if(active) {
                key.povHandle = pov->GetHandle().ToInt();
                key.povTeam = pov->GetTeam();
                if(0 <= key.slot && key.slot < 64) {
                    CEntityInstance * speaker = GetEntityFromIndex(key.slot + 1);
                    if(speaker && speaker->IsPlayerController()) {
                        key.speakerHandle = speaker->GetHandle().ToInt();
                        MirvPov_CopySpeakerPlace(speaker, key.place, key.pawnHandle);
                    }
                }
            }
            const auto & previous = g_VoiceLocationKeys[row];
            if(g_VoiceLocationRefreshPending || active != g_VoiceLocationWasActive
                || key.slot != previous.slot || key.povHandle != previous.povHandle
                || key.speakerHandle != previous.speakerHandle || key.pawnHandle != previous.pawnHandle
                || key.povTeam != previous.povTeam
                || 0 != memcmp(key.place, previous.place, sizeof(key.place))) {
                // Native speaker states are 0..3; -2 forces a native refresh.
                *reinterpret_cast<int *>(cache + 20) = -2;
            }
            g_VoiceLocationKeys[row] = key;
        }
    }
    g_VoiceLocationWasActive = active;
    g_VoiceLocationRefreshPending = false;
    return g_OrgUpdateSpeakers(hud);
}

static bool MirvPov_ResolveVoiceLocation(HMODULE clientDll) {
    Afx::BinUtils::ImageSectionsReader sections(clientDll);
    sections.Next(IMAGE_SCN_MEM_EXECUTE);
    if(sections.Eof()) return false;
    auto text = sections.GetMemRange();
    auto findUnique = [&](const char * pattern) -> size_t {
        auto found = Afx::BinUtils::FindPatternString(text, pattern);
        if(found.IsEmpty() || !Afx::BinUtils::FindPatternString(
            Afx::BinUtils::MemRange(found.Start + 1, text.End), pattern).IsEmpty()) return 0;
        return found.Start;
    };
    size_t formatter = findUnique("48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 54 41 56 41 57 48 81 EC ?? ?? ?? ?? 49 8B F1");
    size_t updater = findUnique("4C 8B DC 55 53 56 57 41 55 41 56 41 57 49 8D AB 68 F9 FF FF 48 81 EC 60 07 00 00");
    if(!formatter || !updater || formatter + 0x270 > text.End || updater + 0x800 > text.End) return false;
    // Verify the three-row cache layout, the exact formatter call and the
    // serial-checked speaker-pawn helper before touching cached row state.
    const unsigned char cacheLayout[] = {0x49,0x8d,0x86,0xa0,0,0,0,0x33,0xd2,0x90,
        0x83,0x38,0xff,0x74,0x10,0xff,0xc1,0x48,0xff,0xc2,0x48,0x83,0xc0,0x18,0x83,0xf9,3,0x72,0xed};
    const unsigned char stateCompare[] = {0x41,0x8b,0x84,0xce,0xb4,0,0,0,0x39,0x43,0x14};
    auto callTarget = [&](size_t call) -> size_t {
        if(*reinterpret_cast<unsigned char *>(call) != 0xe8) return 0;
        size_t target = call + 5 + *reinterpret_cast<int32_t *>(call + 1);
        return text.Start <= target && target < text.End ? target : 0;
    };
    size_t pawnGetter = callTarget(formatter + 0x60);
    if(memcmp(reinterpret_cast<void *>(updater + 0x5d6), cacheLayout, sizeof(cacheLayout))
        || memcmp(reinterpret_cast<void *>(updater + 0x6cd), stateCompare, sizeof(stateCompare))
        || callTarget(updater + 0x72b) != formatter || !pawnGetter) return false;
    g_GetSpeakerPawn = reinterpret_cast<MirvPov_GetSpeakerPawn_t>(pawnGetter);
    g_OrgFormatSpeaker = reinterpret_cast<MirvPov_FormatSpeaker_t>(formatter);
    g_OrgUpdateSpeakers = reinterpret_cast<MirvPov_UpdateSpeakers_t>(updater);
    return true;
}
static bool MirvPov_HookVoiceLocation(HMODULE clientDll) {
    if(g_VoiceLocationHooked) return true;
    if(!MirvPov_ResolveVoiceLocation(clientDll)) return false;
    if(NO_ERROR != DetourTransactionBegin()) return false;
    if(NO_ERROR != DetourUpdateThread(GetCurrentThread())
        || NO_ERROR != DetourAttach(&(PVOID&)g_OrgFormatSpeaker, New_MirvPov_FormatSpeaker)
        || NO_ERROR != DetourAttach(&(PVOID&)g_OrgUpdateSpeakers, New_MirvPov_UpdateSpeakers)) {
        DetourTransactionAbort(); return false;
    }
    if(NO_ERROR != DetourTransactionCommit()) return false;
    g_VoiceLocationHooked = true;
    return true;
}

static bool MirvPov_SetVoiceMaskCvar(MirvPovVoiceMaskCvar &state, int value) {
    int32_t current;
    if(!MirvPovCvars::ReadInteger(state.name, current)) return false;
    if(!state.previousValueSaved) { state.previousValue = current; state.previousValueSaved = true; }
    if(current != value && !MirvPovCvars::WriteInteger(state.name, value)) return false;
    state.lastAppliedValue = value;
    return true;
}

static bool MirvPov_RestoreVoiceMaskCvar(MirvPovVoiceMaskCvar &state) {
    if(!state.previousValueSaved) return true;
    int32_t target = state.previousValue;
    if(g_ManualVoiceMuted) target = 0;
    else if(g_VoicePolicy.IsManaged()) {
        const uint64_t selected = g_VoicePolicy.Resolve(false, 0);
        target = static_cast<int32_t>(&state == &g_MirvPovVoiceMaskLow ? selected : selected >> 32);
    }
    int32_t current;
    if(!MirvPovCvars::ReadInteger(state.name, current)
        || (current != target && !MirvPovCvars::WriteInteger(state.name, target))) return false;
    state.previousValueSaved = false;
    return true;
}

static void MirvPov_CheckManualVoiceMute() {
    if(!g_MirvPovVoiceEnabled || !g_MirvPovVoiceMaskLow.previousValueSaved
        || g_MirvPovVoiceMaskLow.lastAppliedValue == 0) return;
    int32_t low;
    if(!MirvPovCvars::ReadInteger(g_MirvPovVoiceMaskLow.name, low) || low != 0) return;

    // A user mute disables the feature too. Clear the high slots and relinquish
    // both masks before resetting, so restoration cannot undo the mute.
    g_MirvPovVoiceEnabled = false;
    g_ManualVoiceMuted = true;
    if(MirvPov_SetVoiceMaskCvar(g_MirvPovVoiceMaskHigh, 0)) {
        g_MirvPovVoiceMaskLow.previousValueSaved = false;
        g_MirvPovVoiceMaskHigh.previousValueSaved = false;
    }
    if(g_pEngineToClient) g_pEngineToClient->ExecuteClientCmd(0, "servervoice_clear", true);
    MirvPov_ResetVoiceHud();
}

static bool MirvPov_IsVoiceHudReady() {
    return g_MirvPovVoiceStatusGetAddr && g_MirvPovVoiceStatusUpdateSpeakerStatusAddr;
}

static void MirvPov_SetSyntheticSpeaking(unsigned int playerSlot, bool speaking) {
    if(64 <= playerSlot || !MirvPov_IsVoiceHudReady()) return;
    __int64 voiceStatus = ((MirvPov_VoiceStatus_Get_t)g_MirvPovVoiceStatusGetAddr)();
    if(!voiceStatus) return;

    ((MirvPov_VoiceStatus_UpdateSpeakerStatus_t)g_MirvPovVoiceStatusUpdateSpeakerStatusAddr)(voiceStatus, playerSlot, -1, speaking ? 1 : 0);
    g_MirvPovSyntheticSpeakingUntil[playerSlot] = speaking ? MirvPovVoice_GameTime() + kMirvPovSyntheticSpeakingSeconds : 0.0f;
}

static void MirvPov_ForceClearSyntheticSpeaking() {
    for(int i = 0; i < 64; ++i) {
        if(0.0f < g_MirvPovSyntheticSpeakingUntil[i]) MirvPov_SetSyntheticSpeaking(i, false);
        g_MirvPovSyntheticSpeakingUntil[i] = 0.0f;
    }
}

static void MirvPov_RequestFullVoiceClear() {
    if(g_MirvPovVoiceClearRenderPasses < kMirvPovVoiceSeekClearRenderPasses) {
        g_MirvPovVoiceClearRenderPasses = kMirvPovVoiceSeekClearRenderPasses;
    }
}

static bool MirvPov_TryClearAllSpeakingSlots() {
    if(!MirvPov_IsVoiceHudReady()) return false;
    __int64 voiceStatus = ((MirvPov_VoiceStatus_Get_t)g_MirvPovVoiceStatusGetAddr)();
    if(!voiceStatus) return false;

    auto updateSpeakerStatus = (MirvPov_VoiceStatus_UpdateSpeakerStatus_t)g_MirvPovVoiceStatusUpdateSpeakerStatusAddr;
    for(unsigned int playerSlot = 0; playerSlot < 64; ++playerSlot) {
        updateSpeakerStatus(voiceStatus, playerSlot, -1, 0);
        g_MirvPovSyntheticSpeakingUntil[playerSlot] = 0.0f;
    }
    return true;
}

void MirvPov_ClearSyntheticSpeaking() {
    MirvPov_ForceClearSyntheticSpeaking();
}

static bool MirvPov_IsTeamVoiceActive() {
    return g_MirvPovVoiceEnabled && MIRV_POV_FEATURE_ACTIVE("voice");
}

static uint64_t MirvPov_GetVoiceTeamMask() {
    auto watchedController = GetCurrentPovPlayerController();
    const int team = watchedController && watchedController->IsPlayerController()
        ? watchedController->GetTeam() : 0;
    if(g_MirvPovVoiceMode != MirvPovVoiceMode::All && team != 2 && team != 3) return 0;

    uint64_t mask = 0;
    for(unsigned int slot = 0; slot < 64; ++slot) {
        auto controller = GetEntityFromIndex(static_cast<int>(slot) + 1);
        if(!controller || !controller->IsPlayerController()) continue;
        const int voiceTeam = controller->GetTeam();
        const bool allowed = g_MirvPovVoiceMode == MirvPovVoiceMode::All
            || ((voiceTeam == 2 || voiceTeam == 3)
                && (g_MirvPovVoiceMode == MirvPovVoiceMode::Enemy
                    ? voiceTeam != team : voiceTeam == team));
        if(allowed)
            mask |= uint64_t(1) << slot;
    }
    return mask;
}

static bool MirvPov_ApplyVoiceSelection() {
    const bool pov = MirvPov_IsTeamVoiceActive();
    if(!pov && !g_VoicePolicy.IsManaged() && !g_ManualVoiceMuted) {
        const bool low = MirvPov_RestoreVoiceMaskCvar(g_MirvPovVoiceMaskLow);
        const bool high = MirvPov_RestoreVoiceMaskCvar(g_MirvPovVoiceMaskHigh);
        if(low && high) { g_AudibleVoiceMask = advancedfx::MirvVoicePolicy::All; g_VoiceManaged = false; }
        return low && high;
    }

    const auto effective = g_ManualVoiceMuted ? uint64_t(0) : g_VoicePolicy.Resolve(pov, pov ? MirvPov_GetVoiceTeamMask() : 0);
    int32_t previousLow, previousHigh;
    if(!MirvPovCvars::ReadInteger(g_MirvPovVoiceMaskLow.name, previousLow)
        || !MirvPovCvars::ReadInteger(g_MirvPovVoiceMaskHigh.name, previousHigh)) return false;
    if(!MirvPov_SetVoiceMaskCvar(g_MirvPovVoiceMaskLow, static_cast<int32_t>(effective))) return false;
    if(!MirvPov_SetVoiceMaskCvar(g_MirvPovVoiceMaskHigh, static_cast<int32_t>(effective >> 32))) {
        if(MirvPovCvars::WriteInteger(g_MirvPovVoiceMaskLow.name, previousLow))
            g_MirvPovVoiceMaskLow.lastAppliedValue = previousLow;
        return false; // Both saved baselines remain owned if rollback also failed.
    }
    const auto removed = g_AudibleVoiceMask.exchange(effective) & ~effective;
    g_VoiceManaged = true;
    for(unsigned int slot = 0; slot < 64; ++slot) {
        if(advancedfx::MirvVoicePolicy::Allows(removed, slot))
            MirvPov_SetSyntheticSpeaking(slot, false);
    }
    return true;
}

bool MirvPov_UpdateVoiceTeam() {
    MirvPov_CheckManualVoiceMute();
    return MirvPov_ApplyVoiceSelection();
}
static void MirvPov_UpdateSyntheticSpeakingExpiry() {
    float curTime = MirvPovVoice_GameTime();
    for(int i = 0; i < 64; ++i) {
        if(0.0f < g_MirvPovSyntheticSpeakingUntil[i] && g_MirvPovSyntheticSpeakingUntil[i] <= curTime) {
            MirvPov_SetSyntheticSpeaking(i, false);
        }
    }
}

static bool New_MirvPov_IsPlayingDemo(void * This) {
    void * ret = _ReturnAddress();
    bool result = g_Org_MirvPov_IsPlayingDemo(This);
    if(g_MirvPovVoiceEnabled
        && MIRV_POV_FEATURE_ACTIVE("voice")
        && g_MirvPovShowSpeakerRetAddr
        && (size_t)ret == g_MirvPovShowSpeakerRetAddr) return false;
    return result;
}

static __int64 __fastcall New_MirvPov_ServerVoiceData(__int64 This, __int64 msg) {
    unsigned int playerSlot = 0xFFFFFFFF;
    if(msg) {
        const unsigned int fields = *(unsigned int *)(msg + 0x40);
        // Native ServerVoiceData prefers the new entity index when present;
        // older demo messages can still supply the zero-based player slot.
        if(fields & 0x100) {
            const unsigned int entityIndex = *(unsigned int *)(msg + 0x6c);
            if(1 <= entityIndex && entityIndex <= 64) playerSlot = entityIndex - 1;
        } else if(fields & 0x80) {
            playerSlot = *(unsigned int *)(msg + 0x68);
        }
    }
    __int64 result = g_Org_MirvPov_ServerVoiceData(This, msg);
    if(playerSlot < 64
        && g_MirvPovVoiceEnabled
        && MIRV_POV_FEATURE_ACTIVE("voice")
        && MirvPovVoice_AllowsPlayer(playerSlot)) {
        g_PendingSpeaking.fetch_or(uint64_t(1) << playerSlot, std::memory_order_release);
    }
    return result;
}

static MirvPov_VoiceStatus_UpdateSpeakerStatus_t g_OrgUpdateSpeakerStatus = nullptr;

static __int64 __fastcall New_MirvPov_UpdateSpeakerStatus(
    __int64 voiceStatus, unsigned int playerSlot, int localSlot, unsigned __int8 talking) {
    if(g_VoiceManaged.load(std::memory_order_acquire)
        && !MirvPovVoice_AllowsPlayer(playerSlot))
        talking = 0;
    return g_OrgUpdateSpeakerStatus(voiceStatus, playerSlot, localSlot, talking);
}

static bool MirvPov_ResolveVoiceHud(HMODULE clientDll) {
    if(!clientDll) return false;

    if(!g_MirvPovShowSpeakerRetAddr) {
        size_t matchAddr = getAddress(clientDll, "48 63 ?? 48 8D 0D ?? ?? ?? ?? C6 84 08 ?? ?? ?? ?? 01 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 84 C0 0F 85");
        if(matchAddr && *reinterpret_cast<const uint32_t *>(matchAddr + 0x1e)
            == kMirvPovIsPlayingDemoVtableIndex * sizeof(void *)) g_MirvPovShowSpeakerRetAddr = matchAddr + 0x22;
    }
    if(!g_MirvPovServerVoiceDataAddr) {
        g_MirvPovServerVoiceDataAddr = getAddress(clientDll, "48 89 4C 24 08 55 53 57 41 54 41 55 41 56 48 8D 6C 24 D1 48 81 EC C8 00 00 00 44 8B 42 40 45 33 F6 41 8B C0 48 8B DA C1 E8 08");
    }
    if(!g_MirvPovVoiceStatusGetAddr) {
        g_MirvPovVoiceStatusGetAddr = getAddress(clientDll, "48 8B 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 48 8D 05");
    }
    if(!g_MirvPovVoiceStatusUpdateSpeakerStatusAddr) {
        g_MirvPovVoiceStatusUpdateSpeakerStatusAddr = getAddress(clientDll, "48 89 5C 24 08 55 56 57 48 83 EC 30 48 8B 05 ?? ?? ?? ?? 48 8B F1 41 0F B6 E9 49 63 F8 48 63 DA 83 78 58 00");
    }
    return g_MirvPovShowSpeakerRetAddr && g_MirvPovServerVoiceDataAddr && MirvPov_IsVoiceHudReady();
}

static bool MirvPov_InstallVoiceHudDetours(void *target) {
    if(g_bMirvPovServerVoiceDataHooked) return true;
    g_Org_MirvPov_IsPlayingDemo = reinterpret_cast<MirvPov_IsPlayingDemo_t>(target);
    g_Org_MirvPov_ServerVoiceData = reinterpret_cast<MirvPov_ServerVoiceData_t>(g_MirvPovServerVoiceDataAddr);
    g_OrgUpdateSpeakerStatus = reinterpret_cast<MirvPov_VoiceStatus_UpdateSpeakerStatus_t>(g_MirvPovVoiceStatusUpdateSpeakerStatusAddr);
    if(NO_ERROR != DetourTransactionBegin()) return false;
    if(NO_ERROR != DetourUpdateThread(GetCurrentThread())
        || NO_ERROR != DetourAttach(&(PVOID&)g_Org_MirvPov_IsPlayingDemo, New_MirvPov_IsPlayingDemo)
        || NO_ERROR != DetourAttach(&(PVOID&)g_Org_MirvPov_ServerVoiceData, New_MirvPov_ServerVoiceData)
        || NO_ERROR != DetourAttach(&(PVOID&)g_OrgUpdateSpeakerStatus, New_MirvPov_UpdateSpeakerStatus)) {
        DetourTransactionAbort(); return false;
    }
    if(NO_ERROR != DetourTransactionCommit()) return false;
    g_bMirvPovIsPlayingDemoHooked = g_bMirvPovServerVoiceDataHooked = true;
    return true;
}
static bool MirvPov_EnsureVoiceHudHook(HMODULE clientDll) {
    if(!MirvPovVoice_TimeReady() || !MirvPov_ResolveVoiceHud(clientDll)) return false;

    if(g_bMirvPovServerVoiceDataHooked) return true;
    if(!g_pEngineToClient) return false;
    void **vtable = *reinterpret_cast<void ***>(g_pEngineToClient);
    if(!vtable || !vtable[kMirvPovIsPlayingDemoVtableIndex]) return false;
    bool executable = false;
    const auto target = reinterpret_cast<size_t>(vtable[kMirvPovIsPlayingDemoVtableIndex]);
    if(const auto engine = GetModuleHandleW(L"engine2.dll"))
        for(Afx::BinUtils::ImageSectionsReader sections(engine); !sections.Eof(); sections.Next()) {
            const auto range = sections.GetMemRange();
            if((sections.Get()->Characteristics & IMAGE_SCN_MEM_EXECUTE) && range.Start <= target && target < range.End) executable = true;
        }
    if(!executable) return false;
    return MirvPov_InstallVoiceHudDetours(reinterpret_cast<void *>(target));
}

void MirvPov_HookVoiceHud(HMODULE clientDll, bool location) {
    if(clientDll && location && !MirvPov_HookVoiceLocation(clientDll)) {
        advancedfx::Warning("[mirv_pov_voice] speaker location hooks unavailable\n");
    }
    if(!MirvPov_EnsureVoiceHudHook(clientDll)) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_voice_hud] voice HUD patterns or hooks not available\n");
    }
}

static void MirvPov_UpdateVoiceRuntime() {
    if(!g_pEngineToClient) return;

    SOURCESDK::CS2::IDemoFile * pDemoFile = g_pEngineToClient->GetDemoFile();
    if(!pDemoFile) {
        if(g_MirvPovVoiceHadDemoFile) {
            g_pEngineToClient->ExecuteClientCmd(0, "servervoice_clear", true);
            MirvPov_RequestFullVoiceClear();
        }
        g_MirvPovVoiceHadDemoFile = false;
        g_MirvPovVoiceLastDemoTick = INT_MIN;
        return;
    }

    g_MirvPovVoiceHadDemoFile = true;
    int curTick = pDemoFile->GetDemoTick();
    if(g_MirvPovVoiceLastDemoTick != INT_MIN) {
        int delta = curTick - g_MirvPovVoiceLastDemoTick;
        if(delta < 0 || delta > kMirvPovVoiceSeekThresholdTicks) {
            g_pEngineToClient->ExecuteClientCmd(0, "servervoice_clear", true);
            MirvPov_RequestFullVoiceClear();
        }
    }
    g_MirvPovVoiceLastDemoTick = curTick;
    MirvPov_UpdateSyntheticSpeakingExpiry();
}

bool MirvPovVoice_AfterRenderPass()
{
    // Match the POV committed by the native frame, including camera/team changes.
    if(!MirvPov_UpdateVoiceTeam()) return false;
    const uint64_t speaking = g_PendingSpeaking.exchange(0, std::memory_order_acq_rel);
    if(g_MirvPovVoiceClearRenderPasses == 0 && MirvPov_IsTeamVoiceActive())
        for(unsigned int slot = 0; slot < 64; ++slot)
            if(advancedfx::MirvVoicePolicy::Allows(speaking, slot) && MirvPovVoice_AllowsPlayer(slot))
                MirvPov_SetSyntheticSpeaking(slot, true);
    if(MirvPov_IsTeamVoiceActive()) MirvPov_UpdateVoiceRuntime();
    if(0 < g_MirvPovVoiceClearRenderPasses && MirvPov_TryClearAllSpeakingSlots()) {
        --g_MirvPovVoiceClearRenderPasses;
    }
    return true;
}

void MirvPov_ResetVoiceHud() {
    g_VoiceLocationRefreshPending = true;
    g_PendingSpeaking.store(0, std::memory_order_release);
    MirvPov_CheckManualVoiceMute();
    g_MirvPovVoiceLastDemoTick = INT_MIN;
    g_MirvPovVoiceHadDemoFile = false;
    MirvPov_RequestFullVoiceClear();
}

bool MirvPovVoice_IsEnabled()
{
    return g_MirvPovVoiceEnabled;
}

MirvPovVoiceMode MirvPovVoice_GetMode()
{
    return g_MirvPovVoiceMode;
}

const char * MirvPovVoice_GetModeName()
{
    if(!g_MirvPovVoiceEnabled) return "off";
    switch(g_MirvPovVoiceMode) {
    case MirvPovVoiceMode::All: return "all";
    case MirvPovVoiceMode::Enemy: return "enemy";
    default: return "team";
    }
}

void MirvPovVoice_ResetDemoState()
{
    // Resetting hadDemoFile first would bypass the disconnect clear in
    // UpdateVoiceRuntime, leaving the previous demo's queued voice audio.
    if(g_MirvPovVoiceHadDemoFile && g_pEngineToClient) {
        g_pEngineToClient->ExecuteClientCmd(0, "servervoice_clear", true);
    }
    MirvPov_ResetVoiceHud();
    memset(g_MirvPovSyntheticSpeakingUntil, 0, sizeof(g_MirvPovSyntheticSpeakingUntil));
}

void MirvPovVoice_SetEnabled(bool enabled)
{
    MirvPov_CheckManualVoiceMute();
    if(enabled) g_ManualVoiceMuted = false;
    if(g_MirvPovVoiceEnabled == enabled) return;

    g_MirvPovVoiceEnabled = enabled;
    if(MIRV_POV_FEATURE_ACTIVE("voice") && g_pEngineToClient) {
        g_pEngineToClient->ExecuteClientCmd(0, "servervoice_clear", true);
    }
    MirvPov_ResetVoiceHud();

    if(enabled && MIRV_POV_FEATURE_ACTIVE("voice"))
        MirvPov_HookVoiceHud(GetModuleHandleW(L"client.dll"));
    MirvPov_ApplyVoiceSelection();
}

void MirvPovVoice_SetMode(MirvPovVoiceMode mode)
{
    MirvPov_CheckManualVoiceMute();
    if(g_MirvPovVoiceMode == mode) {
        MirvPovVoice_SetEnabled(true);
        return;
    }
    g_MirvPovVoiceMode = mode;
    const bool wasEnabled = g_MirvPovVoiceEnabled;
    MirvPovVoice_SetEnabled(true);
    if(wasEnabled && MIRV_POV_FEATURE_ACTIVE("voice") && g_pEngineToClient) {
        g_pEngineToClient->ExecuteClientCmd(0, "servervoice_clear", true);
        MirvPov_ResetVoiceHud();
    }
    MirvPov_ApplyVoiceSelection();
}
bool MirvPovVoice_AllowsPlayer(unsigned int playerSlot)
{
    return !g_ManualVoiceMuted && advancedfx::MirvVoicePolicy::Allows(g_AudibleVoiceMask, playerSlot);
}

void MirvPovVoice_OnLevelInit() {
    MirvPov_CheckManualVoiceMute();
    g_VoicePolicy.Reset();
    MirvPovVoice_ResetDemoState();
    MirvPov_RestoreVoiceMaskCvar(g_MirvPovVoiceMaskLow);
    MirvPov_RestoreVoiceMaskCvar(g_MirvPovVoiceMaskHigh);
    g_AudibleVoiceMask = 0;
}


void MirvPovVoice_SetSelection(uint32_t low, uint32_t high) {
    g_ManualVoiceMuted = false;
    g_VoicePolicy.Set(low, high);
    // The host command just applied this user selection. Start fresh ownership
    // so a zero low word with selected high slots is not mistaken for global mute.
    g_MirvPovVoiceMaskLow.previousValueSaved = false;
    g_MirvPovVoiceMaskHigh.previousValueSaved = false;
}
bool MirvPovVoice_IsReady() { return g_bMirvPovIsPlayingDemoHooked && g_bMirvPovServerVoiceDataHooked; }
bool MirvPovVoice_LocationReady() { return g_VoiceLocationHooked && g_clientDllOffsets.C_CSPlayerPawn.m_szLastPlaceName >= 0 && MirvPov_LocalizationReady(); }
bool MirvPovVoice_Cleanup() {
    g_PendingSpeaking.store(0);
    MirvPovVoice_ResetDemoState();
    MirvPov_TryClearAllSpeakingSlots();
    g_MirvPovVoiceClearRenderPasses = 0;
    const bool low = MirvPov_RestoreVoiceMaskCvar(g_MirvPovVoiceMaskLow);
    const bool high = MirvPov_RestoreVoiceMaskCvar(g_MirvPovVoiceMaskHigh);
    if(low && high) { g_AudibleVoiceMask = g_ManualVoiceMuted ? 0 : g_VoicePolicy.Resolve(false, 0); g_VoiceManaged = false; }
    return low && high;
}
