// SPDX-License-Identifier: MIT
#pragma once
#include "PovPluginApi.h"
#include <cstring>

template<class Event, class Key, class Pawn, class Controller>
bool PovReadGameEvent(Event *event, Key key, Pawn pawn, Controller controller, PovGameEventV5 &out) {
    if(!event || !event->GetName()) return false;
    const char *name = event->GetName();
    uint32_t kind = 0;
    if(!std::strcmp(name,"round_start")) kind=POV_EVENT_ROUND_START;
    else if(!std::strcmp(name,"flashbang_detonate")) kind=POV_EVENT_FLASH;
    else if(!std::strcmp(name,"player_blind")) kind=POV_EVENT_BLIND;
    else if(!std::strcmp(name,"player_hurt")) kind=POV_EVENT_HURT;
    else if(!std::strcmp(name,"player_death")) kind=POV_EVENT_DEATH;
    else kind=POV_EVENT_OTHER;
    out={}; out.size=sizeof(out); out.kind=kind;
    const size_t nameSize=strnlen_s(name,sizeof(out.name));
    if(nameSize==sizeof(out.name))return false;
    std::memcpy(out.name,name,nameSize+1);
    out.identity=static_cast<uint64_t>(reinterpret_cast<uintptr_t>(event));
    out.victimPawn=out.attackerPawn=out.victimController=out.attackerController=UINT32_MAX;
    out.userSlot=out.attackerSlot=-1;
    out.userId=out.entityIndex=-1;
    if(kind==POV_EVENT_ROUND_START) return true;
    const auto entity=key("entityid");
    if(event->HasKey(entity)) { out.fields|=POV_EVENT_HAS_ENTITY; out.entityIndex=event->GetInt(entity); }
    if(kind==POV_EVENT_FLASH) {
        const auto x=key("x"),y=key("y"),z=key("z");
        if(event->HasKey(x)&&event->HasKey(y)&&event->HasKey(z)) {
            out.fields|=POV_EVENT_HAS_POSITION;
            out.position[0]=event->GetFloat(x);out.position[1]=event->GetFloat(y);out.position[2]=event->GetFloat(z);
        }
        return true;
    }
    const auto user=key("userid"),attacker=key("attacker");
    out.userId=event->GetInt(user);out.victimPawn=pawn(user);out.victimController=controller(user);
    out.userSlot=event->GetPlayerSlot(user).Get();
    out.radioSlot=event->GetInt(key("slot"));
    const auto kit=key("musickitid"),mute=key("nomusic");
    if(event->HasKey(kit)) {out.fields|=POV_EVENT_HAS_MUSIC_KIT;out.musicKit=event->GetInt(kit);}
    if(event->HasKey(mute)) {out.fields|=POV_EVENT_HAS_NO_MUSIC;out.noMusic=event->GetInt(mute);}
    if(kind==POV_EVENT_BLIND) return true;
    out.attackerPawn=pawn(attacker);out.attackerController=controller(attacker);
    out.attackerSlot=event->GetPlayerSlot(attacker).Get();
    const auto damage=key("dmg_health"),health=key("health"),weapon=key("weapon");
    if(event->HasKey(damage)) {out.fields|=POV_EVENT_HAS_DAMAGE;out.damageHealth=event->GetInt(damage);}
    if(event->HasKey(health)) {out.fields|=POV_EVENT_HAS_HEALTH;out.health=event->GetInt(health);}
    if(event->HasKey(weapon)) {
        const char *value=event->GetString(weapon);
        const size_t length=value?strnlen_s(value,sizeof(out.weapon)):0;
        if(value&&length<sizeof(out.weapon)) {out.fields|=POV_EVENT_HAS_WEAPON;std::memcpy(out.weapon,value,length+1);}
    }
    out.damageArmor=event->GetInt(key("dmg_armor"));out.hitgroup=event->GetInt(key("hitgroup"));
    out.headshot=event->GetBool(key("headshot"))?1:0;
    return true;
}
