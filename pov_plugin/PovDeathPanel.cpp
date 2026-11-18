// SPDX-License-Identifier: AGPL-3.0-only
// Extracted from WangChuDi/advancedfx DeathMsg.cpp native POV integration.
#include "PovNativeRuntime.h"
#include "../AfxHookSource2/MirvPovDeathPanel.h"
#include "../AfxHookSource2/MirvPovDeathCam.h"
#include "../AfxHookSource2/MirvPovMvpMusic.h"
#include "../AfxHookSource2/MirvPovFeedback.h"
#include "../AfxHookSource2/MirvPovFeedbackEvent.h"
#include "../AfxHookSource2/MirvPovEventReader.h"
#include "../AfxHookSource2/MirvPovPawnHook.h"
#include "../AfxHookSource2/MirvPovCore.h"
#include "../AfxHookSource2/MirvTime.h"
#include "../deps/release/Detours/src/detours.h"
namespace {
bool g_Resolved=false, g_Hooked=false;
CEntityInstance *Override(CEntityInstance *native,void *caller,int slot) {
    if(slot!=0&&slot!=-1)return native;
    if(g_MirvPovDeathPanelLocalPawnOverride)return g_MirvPovDeathPanelLocalPawnOverride;
    if(auto *pawn=MirvPovMvpMusic_GetLocalPawn(caller))return pawn;
    if(auto *pawn=MirvPovDeathCam_GetEffectPawn(caller))return pawn;
    if(auto *pawn=MirvPovDeathPanel_GetAnimationPawn(caller))return pawn;
    return native;
}
unsigned char *__fastcall Construct(unsigned char *panel) {
    auto *result=g_MirvPovDeathPanelState.originalConstructor(panel);
    if(result)g_MirvPovDeathPanelState.nativeInstance=result;
    return result;
}
unsigned char *__fastcall Destruct(unsigned char *panel,unsigned int flags) {
    // Clear ownership before the game's destructor invalidates the object.
    auto &state=g_MirvPovDeathPanelState;
    if(state.reapplyPanel==panel)MirvPovDeathPanel_Clear();
    if(state.nativeInstance==panel)state.nativeInstance=nullptr;
    if(state.lastPanel==panel)state.lastPanel=nullptr;
    return state.originalDestructor(panel,flags);
}
unsigned char *__fastcall OnDeath(unsigned char *listener,SOURCESDK::CS2::IGameEvent *event) {
    auto &state=g_MirvPovDeathPanelState;
    if(!listener||!event)return state.originalHandlePlayerDeath(listener,event);
    state.lastPanel=state.nativeInstance=listener-0x20;
    PovGameEventV5 data{};
    if(!MIRV_POV_FEATURE_ACTIVE("deathcam")||!MirvPov_ReadNativeEvent(event,data)||data.kind!=POV_EVENT_DEATH)
        return state.originalHandlePlayerDeath(listener,event);
    MirvPovFeedbackEvent snapshot(data);
    auto *pawn=snapshot.GetPlayerPawn("userid");
    if(!pawn||!MirvPovFeedback_IsCurrentPovVictimSnapshot(&snapshot))
        return state.originalHandlePlayerDeath(listener,event);
    // The game always consumes its own native event, never a proxy SDK object.
    auto *previous=g_MirvPovDeathPanelLocalPawnOverride;
    unsigned char *result=nullptr;
    g_MirvPovDeathPanelLocalPawnOverride=pawn;
    __try {
        result=state.originalHandlePlayerDeath(listener,event);
        state.show(state.lastPanel);
        unsigned int actions=0;unsigned long exception=0;
        DeathPanel_ForceVisibility(state.lastPanel,actions,exception);
        state.reapplyPanel=state.lastPanel;state.reapplyPawn=pawn;
        state.reapplyPawnHandle=pawn->GetHandle().ToInt();
        state.reapplyFrame=g_MirvTime.framecount_get();state.lastRefreshFrame=-1;state.reapplyArmed=true;
    } __finally {g_MirvPovDeathPanelLocalPawnOverride=previous;}
    return result;
}
}
bool PovDeathPanel_Initialize(HMODULE client) {
    auto &state=g_MirvPovDeathPanelState;
    if(!g_Resolved) {
        g_Resolved=true;MirvPovDeathPanel_ResolveAddresses(client);
        MirvPovMvpMusic_ResolveAddresses(client,reinterpret_cast<void *>(state.originalGetLocalPawn));
    }
    if(g_Hooked)return true;
    if(!state.originalGetLocalPawn||!MirvPovPawnHook::Ensure(reinterpret_cast<void *>(state.originalGetLocalPawn),true,Override))return false;
    state.localPawnHooked=true;
    if(!state.originalHandlePlayerDeath||!state.hide||!state.show
        ||!state.originalConstructor||!state.originalDestructor||!state.animationPawnReturnAddress)return false;
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    if(DetourUpdateThread(GetCurrentThread())!=NO_ERROR
        ||DetourAttach(&(PVOID&)state.originalHandlePlayerDeath,OnDeath)!=NO_ERROR
        ||DetourAttach(&(PVOID&)state.originalConstructor,Construct)!=NO_ERROR
        ||DetourAttach(&(PVOID&)state.originalDestructor,Destruct)!=NO_ERROR
        ||DetourAttach(&(PVOID&)state.hide,MirvPovDeathPanel_HideWhileAlive)!=NO_ERROR) {
        DetourTransactionAbort();return false;
    }
    g_Hooked=DetourTransactionCommit()==NO_ERROR;
    state.constructorHooked=state.destructorHooked=state.hideHooked=g_Hooked;
    return g_Hooked;
}
