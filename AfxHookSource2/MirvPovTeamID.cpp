#include "stdafx.h"

#include "MirvPovTeamID.h"

#include "ClientEntitySystem.h"
#include "MirvPovCore.h"
#include "MirvPovHookUtils.h"

#include "../shared/AfxConsole.h"
#include "MirvPovCvars.h"
#include "../shared/binutils.h"

#include <Windows.h>
#include <stdint.h>

namespace {

using GetTeamIdContextPlayerFn = CEntityInstance * (__fastcall *)();
using GetTeamIdRelationshipFn = bool (__fastcall *)(CEntityInstance *, CEntityInstance *);

constexpr size_t kTeamIdRelationshipCallCount = 4;
uint8_t * g_TeamIdRelationshipCallSites[kTeamIdRelationshipCallCount] = {};
uint8_t g_TeamIdRelationshipOriginalCalls[kTeamIdRelationshipCallCount][5] = {};
uint8_t * g_TeamIdRelationshipThunk = nullptr;
GetTeamIdRelationshipFn g_GetNativeTeamIdRelationship = nullptr;
GetTeamIdContextPlayerFn g_GetNativeTeamIdDisplayPlayer = nullptr;
DWORD g_TeamIdOriginalProtection[kTeamIdRelationshipCallCount] = {};
bool g_TeamIdPatched = false;
bool g_TeamIdDebug = false;
ULONGLONG g_TeamIdDebugLastLogTime = 0;

bool ShouldLogTeamIdDebug()
{
    if(!g_TeamIdDebug) return false;

    const ULONGLONG now = GetTickCount64();
    if(0 != g_TeamIdDebugLastLogTime && now - g_TeamIdDebugLastLogTime < 1000) return false;
    g_TeamIdDebugLastLogTime = now;
    return true;
}

void LogTeamIdEntity(const char * label, CEntityInstance * entity)
{
    if(nullptr == entity) {
        advancedfx::Message("[mirv_pov_teamid_debug] %s=null\n", label);
        return;
    }

    __try {
        const bool isPawn = entity->IsPlayerPawn();
        const bool isController = entity->IsPlayerController();
        const auto handle = entity->GetHandle();
        const auto controllerHandle = isPawn
            ? entity->GetPlayerControllerHandle()
            : SOURCESDK::CS2::CBaseHandle();
        const auto observerTarget = isPawn
            ? entity->GetObserverTarget()
            : SOURCESDK::CS2::CBaseHandle();
        const char * className = entity->GetClassName();

        advancedfx::Message(
            "[mirv_pov_teamid_debug] %s=%p handle=%08X class=%s team=%d pawn=%d controller=%d observerMode=%u observerTarget=%08X pawnController=%08X\n",
            label,
            entity,
            handle.ToInt(),
            className ? className : "<null>",
            entity->GetTeam(),
            isPawn ? 1 : 0,
            isController ? 1 : 0,
            isPawn ? static_cast<unsigned int>(entity->GetObserverMode()) : 0,
            observerTarget.ToInt(),
            controllerHandle.ToInt());
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        advancedfx::Warning(
            "[mirv_pov_teamid_debug] %s=%p raised an exception while reading entity state.\n",
            label,
            entity);
    }
}

void LogTeamIdCvar(const char * name)
{
    int32_t value;
    if(MirvPovCvars::ReadInteger(name, value))
        advancedfx::Message("[mirv_pov_teamid_debug] cvar %s int=%d\n", name, value);
    else advancedfx::Message("[mirv_pov_teamid_debug] cvar %s unavailable.\n", name);
}

void LogTeamIdRelationship(
    const char * reason,
    bool xray,
    CEntityInstance * nativeContext,
    CEntityInstance * candidate,
    CEntityInstance * povController,
    bool nativeResult,
    bool result)
{
    if(!ShouldLogTeamIdDebug()) return;

    uint8_t observerMode = 0;
    uint32_t observerTarget = 0xFFFFFFFFu;
    const bool haveObserverState = MirvPov_GetObserverState(observerMode, observerTarget);

    advancedfx::Message(
        "[mirv_pov_teamid_debug] reason=%s xray=%d autoSync=%d configuredController=%d observerState=%d observerMode=%u observerTarget=%08X nativeEnemy=%d resultEnemy=%d\n",
        reason,
        xray ? 1 : 0,
        GetFakePovRadarAutoSync() ? 1 : 0,
        GetFakePovRadarControllerIndex(),
        haveObserverState ? 1 : 0,
        static_cast<unsigned int>(observerMode),
        observerTarget,
        nativeResult ? 1 : 0,
        result ? 1 : 0);
    CEntityInstance * displayPlayer = g_GetNativeTeamIdDisplayPlayer
        ? g_GetNativeTeamIdDisplayPlayer()
        : nullptr;
    LogTeamIdEntity("nativeDisplay", displayPlayer);
    LogTeamIdEntity("nativeContext", nativeContext);
    LogTeamIdEntity("candidate", candidate);
    LogTeamIdEntity("povController", povController);
    LogTeamIdEntity("realLocal", GetRealLocalPlayerPawn());
    LogTeamIdEntity("observed", GetObservedPlayerPawn());
    LogTeamIdCvar("cl_teamid_overhead_mode");
    LogTeamIdCvar("cl_teamid_overhead_maxdist_spec");
    LogTeamIdCvar("cl_drawhud_force_teamid_overhead");
    LogTeamIdCvar("sv_teamid_overhead");
    LogTeamIdCvar("sv_teamid_overhead_always_prohibit");
}

bool IsSpectatorXrayEnabled()
{
    int32_t value;
    return !MirvPovCvars::ReadInteger("spec_show_xray", value) || value != 0;
}

bool __fastcall GetTeamIdRelationship(CEntityInstance * nativeContext, CEntityInstance * candidate)
{
    const bool nativeResult = g_GetNativeTeamIdRelationship
        ? g_GetNativeTeamIdRelationship(nativeContext, candidate)
        : false;

    if(!MIRV_POV_FEATURE_ACTIVE("teamid")) return nativeResult;

    __try {
        const bool xray = IsSpectatorXrayEnabled();
        if(xray) {
            LogTeamIdRelationship(
                "xray-native", true, nativeContext, candidate, nullptr, nativeResult, nativeResult);
            return nativeResult;
        }

        CEntityInstance * povController = GetCurrentPovPlayerController();
        if(nullptr == povController || !povController->IsPlayerController()
            || nullptr == candidate || !candidate->IsPlayerPawn()) {
            LogTeamIdRelationship(
                "fallback-native", false, nativeContext, candidate, povController, nativeResult, nativeResult);
            return nativeResult;
        }

        const int povTeam = povController->GetTeam();
        const int candidateTeam = candidate->GetTeam();
        if((2 != povTeam && 3 != povTeam) || (2 != candidateTeam && 3 != candidateTeam)) {
            LogTeamIdRelationship(
                "invalid-team-native", false, nativeContext, candidate, povController, nativeResult, nativeResult);
            return nativeResult;
        }

        const bool result = povTeam != candidateTeam;
        LogTeamIdRelationship(
            "pov-team", false, nativeContext, candidate, povController, nativeResult, result);
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(ShouldLogTeamIdDebug()) {
            advancedfx::Warning(
                "[mirv_pov_teamid_debug] exception in TeamID relationship wrapper; returning native=%d.\n",
                nativeResult ? 1 : 0);
        }
        return nativeResult;
    }
}

} // namespace

bool MirvPovTeamID_GetDebug()
{
    return g_TeamIdDebug;
}

void MirvPovTeamID_SetDebug(bool value)
{
    g_TeamIdDebug = value;
    g_TeamIdDebugLastLogTime = 0;
    advancedfx::Message("mirv_pov_teamid_debug %d\n", value ? 1 : 0);
}

namespace {
bool ResolveTeamIdTargets(HMODULE clientDll, uint8_t *(&relationshipCallSites)[kTeamIdRelationshipCallCount])
{
    if(!clientDll) return false;
    Afx::BinUtils::MemRange textRange = Afx::BinUtils::MemRange::FromEmpty();
    Afx::BinUtils::ImageSectionsReader sections(clientDll);
    if(!sections.Eof()) textRange = sections.GetMemRange();

    // TeamID obtains its native context player immediately after its display-state player.
    auto sequence = Afx::BinUtils::FindPatternString(
        textRange,
        "E8 ?? ?? ?? ?? 4C 8B F0 48 89 45 ?? E8 ?? ?? ?? ?? 48 8B F0 48 89 45 ?? E8");
    if(sequence.IsEmpty() || !Afx::BinUtils::FindPatternString(
        {sequence.Start + 1, textRange.End},
        "E8 ?? ?? ?? ?? 4C 8B F0 48 89 45 ?? E8 ?? ?? ?? ?? 48 8B F0 48 89 45 ?? E8").IsEmpty()) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] TeamID context call-site was not found.\n");
        return false;
    }

    uint8_t * displayCallSite = reinterpret_cast<uint8_t *>(sequence.Start);
    if(0xE8 != displayCallSite[0]) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] TeamID display call-site has an unexpected opcode.\n");
        return false;
    }

    int32_t displayNativeRelative = 0;
    memcpy(&displayNativeRelative, displayCallSite + 1, sizeof(displayNativeRelative));
    g_GetNativeTeamIdDisplayPlayer = reinterpret_cast<GetTeamIdContextPlayerFn>(
        displayCallSite + 5 + displayNativeRelative);

    auto relationshipSequence = Afx::BinUtils::FindPatternString(
        textRange,
        "45 84 ED 75 ?? 48 8B D3 48 8B CE E8 ?? ?? ?? ?? 84 C0 0F 85");
    if(relationshipSequence.IsEmpty() || !Afx::BinUtils::FindPatternString(
        {relationshipSequence.Start + 1, textRange.End},
        "45 84 ED 75 ?? 48 8B D3 48 8B CE E8 ?? ?? ?? ?? 84 C0 0F 85").IsEmpty()) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] TeamID relationship call-site was not found.\n");
        g_GetNativeTeamIdDisplayPlayer = nullptr;
        return false;
    }

    uint8_t * firstRelationshipCall = reinterpret_cast<uint8_t *>(relationshipSequence.Start) + 11;
    if(0xE8 != firstRelationshipCall[0]) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] TeamID relationship call-site has an unexpected opcode.\n");
        g_GetNativeTeamIdDisplayPlayer = nullptr;
        return false;
    }

    int32_t nativeRelative = 0;
    memcpy(&nativeRelative, firstRelationshipCall + 1, sizeof(nativeRelative));
    uint8_t * nativeRelationshipAddress = firstRelationshipCall + 5 + nativeRelative;
    if(reinterpret_cast<size_t>(nativeRelationshipAddress) < textRange.Start
        || reinterpret_cast<size_t>(nativeRelationshipAddress) >= textRange.End
        || reinterpret_cast<size_t>(g_GetNativeTeamIdDisplayPlayer) < textRange.Start
        || reinterpret_cast<size_t>(g_GetNativeTeamIdDisplayPlayer) >= textRange.End
        || firstRelationshipCall < reinterpret_cast<uint8_t *>(sequence.Start)
        || firstRelationshipCall + 5 > reinterpret_cast<uint8_t *>(sequence.Start) + 0x1000) return false;
    g_GetNativeTeamIdRelationship = reinterpret_cast<GetTeamIdRelationshipFn>(nativeRelationshipAddress);

    size_t relationshipCallCount = 0;
    uint8_t * scan = reinterpret_cast<uint8_t *>(sequence.Start);
    uint8_t * scanEnd = scan + 0x1000;
    if(reinterpret_cast<size_t>(scanEnd) > textRange.End) {
        scanEnd = reinterpret_cast<uint8_t *>(textRange.End);
    }
    for(uint8_t * cursor = scan; cursor + 5 <= scanEnd; ++cursor) {
        if(0xE8 != cursor[0]) continue;
        int32_t relative = 0;
        memcpy(&relative, cursor + 1, sizeof(relative));
        if(cursor + 5 + relative != nativeRelationshipAddress) continue;
        if(relationshipCallCount >= kTeamIdRelationshipCallCount) {
            relationshipCallCount = kTeamIdRelationshipCallCount + 1;
            break;
        }
        relationshipCallSites[relationshipCallCount++] = cursor;
    }
    if(kTeamIdRelationshipCallCount != relationshipCallCount) {
        MIRV_POV_DIAGNOSTIC_WARNING(
            "[mirv_pov_teamid] Expected %u TeamID relationship calls, found %u.\n",
            static_cast<unsigned int>(kTeamIdRelationshipCallCount),
            static_cast<unsigned int>(relationshipCallCount));
        g_GetNativeTeamIdDisplayPlayer = nullptr;
        g_GetNativeTeamIdRelationship = nullptr;
        return false;
    }

    return true;
}

bool WriteTeamIdCall(size_t index, const uint8_t *bytes)
{
    auto *site = g_TeamIdRelationshipCallSites[index];
    DWORD oldProtect;
    if(!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    if(!g_TeamIdOriginalProtection[index]) g_TeamIdOriginalProtection[index] = oldProtect;
    memcpy(site, bytes, 5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, 5) != 0;
    DWORD unused;
    const bool protectedAgain = VirtualProtect(site, 5, g_TeamIdOriginalProtection[index], &unused) != 0;
    return flushed && protectedAgain && memcmp(site, bytes, 5) == 0;
}
} // namespace

void MirvPovTeamID_ApplyPatches(HMODULE clientDll)
{
    if(g_TeamIdPatched) return;
    if(!MirvPovTeamID_IsClean()) {
        MirvPovTeamID_RemovePatches();
        if(!MirvPovTeamID_IsClean()) return;
    }
    uint8_t *relationshipCallSites[kTeamIdRelationshipCallCount] = {};
    if(!ResolveTeamIdTargets(clientDll, relationshipCallSites)) return;
    uint8_t *firstRelationshipCall = relationshipCallSites[0];
    uint8_t * thunk = MirvPovHookUtils::AllocateNear(firstRelationshipCall, 16);
    if(nullptr == thunk) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] Could not allocate the TeamID relationship thunk.\n");
        g_GetNativeTeamIdDisplayPlayer = nullptr;
        g_GetNativeTeamIdRelationship = nullptr;
        return;
    }

    // mov rax, GetTeamIdRelationship; jmp rax
    thunk[0] = 0x48;
    thunk[1] = 0xB8;
    uint64_t wrapperAddress = reinterpret_cast<uint64_t>(&GetTeamIdRelationship);
    memcpy(thunk + 2, &wrapperAddress, sizeof(wrapperAddress));
    thunk[10] = 0xFF;
    thunk[11] = 0xE0;
    g_TeamIdRelationshipThunk = thunk;
    if(!FlushInstructionCache(GetCurrentProcess(), thunk, 12)) {
        MirvPovTeamID_RemovePatches();
        return;
    }

    int32_t thunkRelatives[kTeamIdRelationshipCallCount] = {};
    for(size_t i = 0; i < kTeamIdRelationshipCallCount; ++i) {
        if(!MirvPovHookUtils::CalcRel32(relationshipCallSites[i] + 5, thunk, thunkRelatives[i])) {
            MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] TeamID relationship thunk is out of range.\n");
            MirvPovTeamID_RemovePatches();
            return;
        }
    }

    for(size_t i = 0; i < kTeamIdRelationshipCallCount; ++i) {
        uint8_t replacement[5] = {0xE8, 0, 0, 0, 0};
        memcpy(replacement + 1, &thunkRelatives[i], sizeof(thunkRelatives[i]));
        memcpy(g_TeamIdRelationshipOriginalCalls[i], relationshipCallSites[i], sizeof(replacement));
        g_TeamIdRelationshipCallSites[i] = relationshipCallSites[i];
        if(!WriteTeamIdCall(i, replacement)) {
            MirvPovTeamID_RemovePatches();
            return;
        }
    }

    g_TeamIdPatched = true;
}

void MirvPovTeamID_RemovePatches()
{
    if(!g_TeamIdRelationshipThunk) return;
    g_TeamIdPatched = false;
    bool restored = true;
    for(size_t i = 0; i < kTeamIdRelationshipCallCount; ++i) {
        if(!g_TeamIdRelationshipCallSites[i]) continue;
        if(!WriteTeamIdCall(i, g_TeamIdRelationshipOriginalCalls[i])) {
            restored = false;
            continue;
        }
        g_TeamIdRelationshipCallSites[i] = nullptr;
        g_TeamIdOriginalProtection[i] = 0;
    }
    if(!restored || !VirtualFree(g_TeamIdRelationshipThunk, 0, MEM_RELEASE)) {
        MIRV_POV_DIAGNOSTIC_WARNING("[mirv_pov_teamid] Cleanup incomplete; retaining the thunk for retry.\n");
        return;
    }
    g_TeamIdRelationshipThunk = nullptr;
    g_GetNativeTeamIdDisplayPlayer = nullptr;
    g_GetNativeTeamIdRelationship = nullptr;
}

bool MirvPovTeamID_IsReady() { return g_TeamIdPatched; }
bool MirvPovTeamID_IsClean() { return g_TeamIdRelationshipThunk == nullptr; }
