// SPDX-License-Identifier: AGPL-3.0-only
#include "MirvPovFeedbackEvent.h"
#include "MirvPovNativeEvent.h"
#include "Globals.h"
#include "../shared/binutils.h"
#include "../pov_plugin/PovGameEvent.h"
bool MirvPov_ReadNativeEvent(SOURCESDK::CS2::IGameEvent *event,PovGameEventV5 &out) {
    using Hash=unsigned int(__fastcall *)(const char *,unsigned int,unsigned int);
    static Hash hash=nullptr;
    if(!hash) hash=reinterpret_cast<Hash>(getAddress(GetModuleHandleW(L"client.dll"),
        "48 83 EC 28 45 8B D0 4C 8B C9 48 83 FA 04 0F 82 ?? ?? ?? ?? 0F B6 09 48 89 5C 24 20 8D 41 BF 3C 19 77 03 80 C1 20"));
    if(!hash||!event)return false;
    auto key=[&](const char *name) {const auto size=static_cast<unsigned int>(std::strlen(name));
        return SOURCESDK::CS2::CKV3MemberName(static_cast<int>(hash(name,size,size^0x31415926)),-1,name);};
    auto controller=[&](const auto &symbol){auto *entity=reinterpret_cast<CEntityInstance *>(event->GetPlayerController(symbol));
        return entity&&entity->IsPlayerController()?entity->GetHandle().ToInt():UINT32_MAX;};
    auto pawn=[&](const auto &symbol)->uint32_t{auto *entity=reinterpret_cast<CEntityInstance *>(event->GetPlayerPawn(symbol));
        if(entity&&entity->IsPlayerPawn())return entity->GetHandle().ToInt();
        auto *player=reinterpret_cast<CEntityInstance *>(event->GetPlayerController(symbol));
        if(!player||!player->IsPlayerController())return UINT32_MAX;
        const auto handle=player->GetPlayerPawnHandle();if(!handle.IsValid())return UINT32_MAX;
        entity=GetEntityFromIndex(handle.GetEntryIndex());
        return entity&&entity->IsPlayerPawn()&&entity->GetHandle().ToInt()==handle.ToInt()?handle.ToInt():UINT32_MAX;};
    MirvPovNativeEvent native(event);
    __try {const bool valid=PovReadGameEvent(&native,key,pawn,controller,out);
        out.identity=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(event));return valid;} __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
