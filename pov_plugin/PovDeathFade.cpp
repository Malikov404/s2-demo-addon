// SPDX-License-Identifier: AGPL-3.0-only
// Native Fade tracking/cleanup from WangChuDi/advancedfx RenderSystemDX11Hooks.
// Death presentation itself stays in the native postprocess updater.
#include "PovDeathFade.h"
#include "PovNativeRuntime.h"
#include "../AfxHookSource2/MirvPovCore.h"
#include "../AfxHookSource2/MirvTime.h"
#include "../AfxHookSource2/Globals.h"
#include "../deps/release/Detours/src/detours.h"
#include <atomic>
namespace {
using NativeFade=__int64(__fastcall *)(void *,void *,void *,void *,void *,void *,void *,void *,void *);
using GetEffects=void *(__fastcall *)(int);
NativeFade g_Original=nullptr;
GetEffects g_GetEffects=nullptr;
bool g_Hooked=false,g_Known=false;
uint8_t g_Mode=0;uint32_t g_Target=UINT32_MAX;
int g_Tick=-1;
std::atomic<bool> g_Active{false},g_ClearPending{false};
__int64 __fastcall OnFade(void *message,void *a2,void *a3,void *a4,void *a5,void *a6,void *a7,void *a8,void *a9) {
    if(message&&MirvPov_IsEnabled()) {
        __try {
            auto *bytes=static_cast<unsigned char *>(message);
            const auto flags=*reinterpret_cast<uint16_t *>(bytes+0x50);
            if(bytes[0x54]<8&&bytes[0x55]<8&&bytes[0x56]<8&&(flags&0xA))g_Active.store(true);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return g_Original(message,a2,a3,a4,a5,a6,a7,a8,a9);
}
bool Clear() {
    if(!g_ClearPending.load())return true;
    if(!g_GetEffects)return false;
    __try {
        auto *effects=g_GetEffects(-1);
        if(!effects)return false;
        auto **vtable=*reinterpret_cast<void ***>(effects);
        if(!vtable||!vtable[10])return false;
        reinterpret_cast<void(__fastcall *)(void *)>(vtable[10])(effects);
        g_ClearPending.store(false);g_Active.store(false);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
bool PovDeathFade_Initialize(HMODULE client) {
    if(g_Hooked)return true;
    g_GetEffects=reinterpret_cast<GetEffects>(getAddress(client,
        "53 48 83 EC 20 65 48 8B 04 25 58 00 00 00 8B D9 8B 0D ?? ?? ?? ?? BA 68 00 00 00 48 8B 04 C8 8B 04 02"));
    g_Original=reinterpret_cast<NativeFade>(getAddress(client,
        "48 83 EC 38 0F B7 41 48 66 89 44 24 ?? 0F B7 41 4C 66 89 44 24 ?? 0F B7 41 50 66 89 44 24 ?? 8B 41 54 8B 0D ?? ?? ?? ??"));
    if(!g_GetEffects||!g_Original||DetourTransactionBegin()!=NO_ERROR)return false;
    if(DetourUpdateThread(GetCurrentThread())!=NO_ERROR||DetourAttach(&(PVOID&)g_Original,OnFade)!=NO_ERROR) {
        DetourTransactionAbort();return false;
    }
    g_Hooked=DetourTransactionCommit()==NO_ERROR;return g_Hooked;
}
void PovDeathFade_ObserverChanged() {if(g_Hooked)g_ClearPending.store(true);}
void PovDeathFade_Frame() {
    int tick=-1;g_MirvTime.GetCurrentDemoTick(tick);
    uint8_t mode=0;uint32_t target=UINT32_MAX;
    const bool known=MirvPov_GetObserverState(mode,target);
    if((g_Tick>=0&&(tick<g_Tick||tick-g_Tick>2))
        ||(g_Known&&(!known||mode!=g_Mode||target!=g_Target)))PovDeathFade_ObserverChanged();
    g_Tick=tick;g_Known=known;g_Mode=mode;g_Target=target;
    Clear(); // Game/frame thread; never mutate CViewEffects inside its message callback.
}
bool PovDeathFade_Reset() {
    if(g_Active.load())g_ClearPending.store(true);
    g_Known=false;g_Tick=-1;
    return Clear(); // Retain pending ownership if cleanup fails.
}
