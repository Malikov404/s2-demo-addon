// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ClientEntitySystem.h"
#include "../pov_plugin/PovPluginApi.h"
#include <cstring>
class MirvPovFeedbackEvent {
    const PovGameEventV5 &m_Event;
    CEntityInstance *Entity(uint32_t handle) const {
        const SOURCESDK::CS2::CBaseHandle value(handle);
        if(!value.IsValid()) return nullptr;
        auto *entity=GetEntityFromIndex(value.GetEntryIndex());
        return entity&&entity->GetHandle().ToInt()==handle?entity:nullptr;
    }
public:
    explicit MirvPovFeedbackEvent(const PovGameEventV5 &event):m_Event(event){}
    const char *GetName() const {
        if(m_Event.name[0])return m_Event.name;
        switch(m_Event.kind) {
        case POV_EVENT_ROUND_START:return "round_start";case POV_EVENT_FLASH:return "flashbang_detonate";
        case POV_EVENT_BLIND:return "player_blind";case POV_EVENT_HURT:return "player_hurt";case POV_EVENT_DEATH:return "player_death";
        default:return "";
        }
    }
    bool HasKey(const char *key) const {
        const uint32_t bit=!std::strcmp(key,"health")?POV_EVENT_HAS_HEALTH:!std::strcmp(key,"entityid")?POV_EVENT_HAS_ENTITY
            :!std::strcmp(key,"musickitid")?POV_EVENT_HAS_MUSIC_KIT:!std::strcmp(key,"nomusic")?POV_EVENT_HAS_NO_MUSIC
            :!std::strcmp(key,"weapon")?POV_EVENT_HAS_WEAPON:!std::strcmp(key,"dmg_health")?POV_EVENT_HAS_DAMAGE
            :(!std::strcmp(key,"x")||!std::strcmp(key,"y")||!std::strcmp(key,"z"))?POV_EVENT_HAS_POSITION:0;
        return bit&&(m_Event.fields&bit)!=0;
    }
    int GetInt(const char *key) const {
        if(!std::strcmp(key,"userid"))return m_Event.userId;if(!std::strcmp(key,"entityid"))return m_Event.entityIndex;
        if(!std::strcmp(key,"health"))return m_Event.health;if(!std::strcmp(key,"dmg_health"))return m_Event.damageHealth;
        if(!std::strcmp(key,"dmg_armor"))return m_Event.damageArmor;if(!std::strcmp(key,"hitgroup"))return m_Event.hitgroup;
        if(!std::strcmp(key,"slot"))return m_Event.radioSlot;
        if(!std::strcmp(key,"musickitid"))return m_Event.musicKit;
        if(!std::strcmp(key,"nomusic"))return m_Event.noMusic;
        return !std::strcmp(key,"headshot")?m_Event.headshot:0;
    }
    bool GetBool(const char *key) const {return GetInt(key)!=0;}
    float GetFloat(const char *key) const {return m_Event.position[!std::strcmp(key,"x")?0:!std::strcmp(key,"y")?1:2];}
    const char *GetString(const char *) const {return m_Event.weapon;}
    CEntityInstance *GetPlayerPawn(const char *key) const {return Entity(!std::strcmp(key,"attacker")?m_Event.attackerPawn:m_Event.victimPawn);}
    CEntityInstance *GetPlayerController(const char *key) const {return Entity(!std::strcmp(key,"attacker")?m_Event.attackerController:m_Event.victimController);}
    uint64_t Identity() const {return m_Event.identity;}
    template<class Key>
    bool IsEmpty(const Key &key) const {return !HasKey(key.GetString());}
    template<class Key>
    auto GetInt(const Key &key) const -> decltype(key.GetString(),int{}) {return GetInt(key.GetString());}
    template<class Key>
    auto GetString(const Key &key) const -> decltype(key.GetString(),static_cast<const char *>(nullptr)) {return GetString(key.GetString());}
    template<class Key>
    auto GetPlayerPawn(const Key &key) const -> decltype(key.GetString(),static_cast<CEntityInstance *>(nullptr)) {return GetPlayerPawn(key.GetString());}
    template<class Key>
    auto GetPlayerController(const Key &key) const -> decltype(key.GetString(),static_cast<CEntityInstance *>(nullptr)) {return GetPlayerController(key.GetString());}
    struct Slot {int value;int Get() const {return value;}};
    template<class Key>
    Slot GetPlayerSlot(const Key &key) const {
        return {!std::strcmp(key.GetString(),"attacker")?m_Event.attackerSlot:m_Event.userSlot};
    }
};
int MirvPovFeedback_FrameCount();
