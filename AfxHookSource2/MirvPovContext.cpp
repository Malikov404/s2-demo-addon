// SPDX-License-Identifier: AGPL-3.0-only
#include "MirvPovCore.h"
#include "ClientEntitySystem.h"
#include <atomic>

namespace {
std::atomic<int> g_FakePovRadarControllerIndex{0};
std::atomic<bool> g_MirvPovAutoSync{false};
thread_local void *g_MirvPovHookReturnAddress = nullptr;
CEntityInstance * GetPawnFromController(CEntityInstance * controller)
{
    if(nullptr == controller || !controller->IsPlayerController()) return nullptr;
    auto pawnHandle = controller->GetPlayerPawnHandle();
    if(!pawnHandle.IsValid()) return nullptr;
    CEntityInstance * pawn = GetEntityFromIndex(pawnHandle.GetEntryIndex());
    return nullptr != pawn && pawn->GetHandle() == pawnHandle && pawn->IsPlayerPawn() ? pawn : nullptr;
}

CEntityInstance * ResolveObservedPlayerPawn()
{
    CEntityInstance * realPawn = GetPawnFromController(GetRealSplitScreenPlayer(0));
    if(nullptr == realPawn || 0 == realPawn->GetObserverMode()) return nullptr;

    auto targetHandle = realPawn->GetObserverTarget();
    if(!targetHandle.IsValid()) return nullptr;

    CEntityInstance * targetPawn = GetEntityFromIndex(targetHandle.GetEntryIndex());
    return nullptr != targetPawn && targetPawn->GetHandle() == targetHandle && targetPawn->IsPlayerPawn() ? targetPawn : nullptr;
}

CEntityInstance * ResolveObservedPlayerController()
{
    CEntityInstance * targetPawn = ResolveObservedPlayerPawn();
    if(nullptr == targetPawn) return nullptr;

    auto controllerHandle = targetPawn->GetPlayerControllerHandle();
    if(!controllerHandle.IsValid()) return nullptr;

    CEntityInstance * targetController = GetEntityFromIndex(controllerHandle.GetEntryIndex());
    return nullptr != targetController && targetController->GetHandle() == controllerHandle && targetController->IsPlayerController() ? targetController : nullptr;
}

CEntityInstance * ResolveConfiguredPovPlayerController()
{
    if(g_MirvPovAutoSync) return ResolveObservedPlayerController();
    if(g_FakePovRadarControllerIndex <= 0) return nullptr;

    CEntityInstance * controller = GetEntityFromIndex(g_FakePovRadarControllerIndex);
    return nullptr != controller && controller->IsPlayerController() ? controller : nullptr;
}
}

CEntityInstance * GetCurrentPovPlayerController()
{
    return MirvPov_IsEnabled() ? ResolveConfiguredPovPlayerController() : nullptr;
}

CEntityInstance * GetCurrentPovPlayerPawn()
{
    if(!MirvPov_IsEnabled()) return nullptr;
    if(g_MirvPovAutoSync) return ResolveObservedPlayerPawn();
    return GetPawnFromController(ResolveConfiguredPovPlayerController());
}

CEntityInstance * GetObservedPlayerPawn()
{
    __try {
        return ResolveObservedPlayerPawn();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

CEntityInstance * GetObservedPlayerController()
{
    __try {
        return ResolveObservedPlayerController();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool MirvPov_GetObserverState(uint8_t & observerMode, uint32_t & observerTarget)
{
    observerMode = 0;
    observerTarget = 0xFFFFFFFFu;

    __try {
        CEntityInstance * realPawn = GetRealLocalPlayerPawn();
        if(nullptr == realPawn || !realPawn->IsPlayerPawn()) return false;

        observerMode = static_cast<uint8_t>(realPawn->GetObserverMode());
        const auto targetHandle = realPawn->GetObserverTarget();
        if(targetHandle.IsValid()) observerTarget = targetHandle.ToInt();
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        observerMode = 0;
        observerTarget = 0xFFFFFFFFu;
        return false;
    }
}

CEntityInstance * GetRealLocalPlayerPawn()
{
    __try {
        return GetPawnFromController(GetRealSplitScreenPlayer(0));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

CEntityInstance * GetFakePovRadarController()
{
    return ResolveConfiguredPovPlayerController();
}

CEntityInstance * GetEffectiveSplitScreenPlayer(int slot)
{
    if(0 == slot) {
        if(CEntityInstance * povController = GetCurrentPovPlayerController()) return povController;
    }
    return GetRealSplitScreenPlayer(slot);
}

void SetFakePovRadarControllerIndex(int index)
{
    g_FakePovRadarControllerIndex = 0 < index ? index : 0;
    g_MirvPovAutoSync = false;
}

void SetFakePovRadarAutoSync(bool enabled)
{
    g_MirvPovAutoSync = enabled;
    if(enabled) g_FakePovRadarControllerIndex = -1;
}

bool GetFakePovRadarAutoSync()
{
    return g_MirvPovAutoSync;
}

int GetFakePovRadarControllerIndex()
{
    return g_FakePovRadarControllerIndex;
}

void * MirvPov_PushHookReturnAddress(void * returnAddress)
{
    void * previous = g_MirvPovHookReturnAddress;
    if(nullptr == previous) g_MirvPovHookReturnAddress = returnAddress;
    return previous;
}

void * MirvPov_GetHookReturnAddress()
{
    return g_MirvPovHookReturnAddress;
}

void MirvPov_PopHookReturnAddress(void * previous)
{
    g_MirvPovHookReturnAddress = previous;
}
