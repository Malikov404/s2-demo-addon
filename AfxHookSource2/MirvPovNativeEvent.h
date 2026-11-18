// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../deps/release/prop/cs2/sdk_src/public/playerslot.h"
#include "../deps/release/prop/cs2/sdk_src/public/igameevents.h"
// The pinned POV SDK omits default-value arguments present in the native
// getters. Keep this adaptation local; never pass an SDK proxy to the game.
class MirvPovNativeEvent {
    SOURCESDK::CS2::IGameEvent *m_Event;
    template<class R> R Value(size_t slot, const SOURCESDK::CS2::CKV3MemberName &key, R fallback) const {
        using Fn=R(__fastcall *)(void *,const SOURCESDK::CS2::CKV3MemberName &,R);
        return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(m_Event))[slot])(m_Event,key,fallback);
    }
public:
    explicit MirvPovNativeEvent(SOURCESDK::CS2::IGameEvent *event):m_Event(event){}
    const char *GetName() const {return m_Event->GetName();}
    bool HasKey(const SOURCESDK::CS2::CKV3MemberName &key) {return m_Event->HasKey(key);}
    bool GetBool(const SOURCESDK::CS2::CKV3MemberName &key) {return Value<bool>(6,key,false);}
    int GetInt(const SOURCESDK::CS2::CKV3MemberName &key) {return Value<int>(7,key,0);}
    float GetFloat(const SOURCESDK::CS2::CKV3MemberName &key) {return Value<float>(9,key,0.0f);}
    const char *GetString(const SOURCESDK::CS2::CKV3MemberName &key) {return Value<const char *>(10,key,"");}
    auto GetPlayerSlot(const SOURCESDK::CS2::CKV3MemberName &key) {return m_Event->GetPlayerSlot(key);}
};
