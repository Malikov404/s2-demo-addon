// SPDX-License-Identifier: AGPL-3.0-only
#include "MirvPovPawnHook.h"
#include "MirvPovCore.h"
#include "../deps/release/Detours/src/detours.h"
#include <intrin.h>
#include <atomic>
namespace {
struct Hook {
    void *target = nullptr, *original = nullptr;
    bool hasSlot=false;
    std::atomic<MirvPovPawnHook::Override> callbacks[3]{};
};
Hook g_Hooks[4];
CEntityInstance *Invoke(unsigned int index, int slot, void *caller) {
    auto &hook = g_Hooks[index];
    const bool hasSlot=hook.hasSlot;
    void *previous = MirvPov_PushHookReturnAddress(caller);
    CEntityInstance *pawn = nullptr;
    __try {
        pawn = hasSlot ? reinterpret_cast<CEntityInstance *(__fastcall *)(int)>(hook.original)(slot)
            : reinterpret_cast<CEntityInstance *(__fastcall *)()>(hook.original)();
        for(auto &entry : hook.callbacks)
            if(auto callback=entry.load(std::memory_order_acquire)) pawn = callback(pawn, MirvPov_GetHookReturnAddress(), slot);
    } __finally { MirvPov_PopHookReturnAddress(previous); }
    return pawn;
}
template<unsigned int I> CEntityInstance *__fastcall NoSlot() {return Invoke(I,0,_ReturnAddress());}
template<unsigned int I> CEntityInstance *__fastcall WithSlot(int slot) {return Invoke(I,slot,_ReturnAddress());}
void *g_NoSlot[]={reinterpret_cast<void *>(NoSlot<0>),reinterpret_cast<void *>(NoSlot<1>),reinterpret_cast<void *>(NoSlot<2>),reinterpret_cast<void *>(NoSlot<3>)};
void *g_WithSlot[]={reinterpret_cast<void *>(WithSlot<0>),reinterpret_cast<void *>(WithSlot<1>),reinterpret_cast<void *>(WithSlot<2>),reinterpret_cast<void *>(WithSlot<3>)};
}
bool MirvPovPawnHook::Ensure(void *target, bool hasSlot, Override callback) {
    if(!target || !callback) return false;
    unsigned int index=0;
    for(;index<4;++index)if(g_Hooks[index].target==target)break;
    if(index==4)for(index=0;index<4;++index)if(!g_Hooks[index].target)break;
    if(index==4)return false;
    auto &hook=g_Hooks[index];
    if(hook.target&&hook.hasSlot!=hasSlot)return false;
    for(auto &registered : hook.callbacks) if(registered.load() == callback) return true;
    std::atomic<Override> *free = nullptr;
    for(auto &registered : hook.callbacks) if(!registered.load()) { free = &registered; break; }
    if(!free) return false;
    if(!hook.target) {
        hook.original = target;hook.hasSlot=hasSlot;
        if(DetourTransactionBegin() != NO_ERROR) return false;
        if(DetourUpdateThread(GetCurrentThread()) != NO_ERROR
            || DetourAttach(&hook.original, hasSlot ? g_WithSlot[index] : g_NoSlot[index]) != NO_ERROR) {
            DetourTransactionAbort(); return false;
        }
        if(DetourTransactionCommit() != NO_ERROR) { hook.original = nullptr; return false; }
        hook.target = target;
    }
    free->store(callback,std::memory_order_release);
    return true;
}
