// SPDX-License-Identifier: AGPL-3.0-only
#include "PovPluginApi.h"
#include "PovNativeRuntime.h"
#include "../AfxHookSource2/MirvPovVoice.h"
#include "../AfxHookSource2/MirvPovCvars.h"
#include "../AfxHookSource2/MirvPovFeatures.h"
#include "../AfxHookSource2/MirvPovDeathPanel.h"
#include "../AfxHookSource2/MirvPovDeathCam.h"
#include "../AfxHookSource2/MirvPovMvpMusic.h"
#include "../AfxHookSource2/MirvPovRadio.h"
#include "../AfxHookSource2/MirvPovKillReward.h"
#include "../AfxHookSource2/MirvPovSoundCircle.h"
#include "../AfxHookSource2/MirvPovScoreboard.h"
#include "../AfxHookSource2/MirvPovPickupPrompt.h"
#include "../AfxHookSource2/MirvPovBuyMenu.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include "../AfxHookSource2/MirvPovFeedback.h"
#include "../AfxHookSource2/MirvPovFeedbackEvent.h"

namespace {
PovPluginHostV5 g_Host{};
DWORD g_GameThread = 0;
std::atomic<int32_t> g_State{POV_PLUGIN_UNINITIALIZED};
const auto g_FeatureDefinitions = MirvPovFeatures::Create();
auto g_Features = g_FeatureDefinitions;
bool g_Dirty = false;
bool g_RestorePending = false;
std::atomic<uint64_t> g_ActiveFeatures{0};

void PublishFeatures() {
    uint64_t mask = 0;
    for(size_t i = 0; i < g_Features.size(); ++i)
        if(g_Features[i].active) mask |= UINT64_C(1) << i;
    g_ActiveFeatures.store(mask, std::memory_order_release);
}
bool Supported(const char *name) {
    auto *feature=MirvPovFeatures::Find(g_Features,name);
    return feature && std::strcmp(feature->name,"voice_script");
}

bool OnGameThread() { return g_GameThread && g_GameThread == GetCurrentThreadId(); }
struct Cvars {
    bool Read(const char *name, int32_t &value, int32_t &defaults) {
        return g_Host.ReadCvar(name, &value, &defaults) != 0;
    }
    bool Write(const char *name, int32_t value) { return g_Host.WriteCvar(name, value) != 0; }
};

int32_t Apply(bool enabled) {
    Cvars access;
    const int result = MirvPovCvars::Apply(access, enabled);
    if(result < 0) g_Dirty = true;
    if(result > 0) g_Dirty = enabled;
    return result > 0 ? POV_PLUGIN_OK : result < 0 ? POV_PLUGIN_RESTORE_FAILED : POV_PLUGIN_UNAVAILABLE;
}

int32_t POV_CALL Initialize(const PovPluginHostV5 *host) {
    if(g_State.load() != POV_PLUGIN_UNINITIALIZED) return POV_PLUGIN_INVALID_API;
    if(!host || host->version != POV_PLUGIN_API_VERSION || host->size < sizeof(*host)
        || !host->ReadCvar || !host->WriteCvar) return POV_PLUGIN_INVALID_API;
    if(g_Host.size && std::memcmp(&g_Host, host, sizeof(g_Host))) return POV_PLUGIN_INVALID_API;
    if(!g_Host.size) { g_Host = *host; PovNativeRuntime::Bind(&g_Host); }
    g_GameThread = GetCurrentThreadId();
    g_Features = MirvPovFeatures::Create();
    g_Dirty = false;
    g_RestorePending = false;
    PublishFeatures();
    g_State.store(POV_PLUGIN_DISABLED);
    return POV_PLUGIN_OK;
}

int32_t POV_CALL SetEnabled(uint32_t enabled) {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    if(enabled > 1) return POV_PLUGIN_INVALID_API;
    const bool wasEnabled = g_State.load() == POV_PLUGIN_ENABLED;
    if(enabled && wasEnabled && !g_RestorePending) return POV_PLUGIN_OK;
    if(g_RestorePending) {
        if(!PovNativeRuntime::Disable()) return POV_PLUGIN_RESTORE_FAILED;
        const int32_t restored = Apply(false);
        if(restored != POV_PLUGIN_OK) return restored;
        g_RestorePending = false;
    }
    auto *cvars = MirvPovFeatures::Find(g_Features, "cvars");
    const bool active = enabled && cvars->configured;
    // An explicit off selects the spectator baseline even on a fresh process.
    if(!enabled) {
        g_State.store(POV_PLUGIN_DISABLED);
        const bool stopped = PovNativeRuntime::Disable();
        const int32_t result = Apply(false);
        if(!stopped || result != POV_PLUGIN_OK) {
            g_RestorePending = true;
            g_State.store(wasEnabled ? POV_PLUGIN_ENABLED : POV_PLUGIN_DISABLED);
            return !stopped ? POV_PLUGIN_RESTORE_FAILED : result;
        }
        return POV_PLUGIN_OK;
    }
    const int32_t result = active || g_Dirty ? Apply(active) : POV_PLUGIN_OK;
    if(result != POV_PLUGIN_OK) return result;
    if(enabled) for(auto &feature : g_Features) feature.active = feature.configured;
    PublishFeatures();
    g_State.store(enabled ? POV_PLUGIN_ENABLED : POV_PLUGIN_DISABLED);
    PovNativeRuntime::Enable();
    return POV_PLUGIN_OK;
}

int32_t POV_CALL SetFeature(const char *name, uint32_t enabled) {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    if(enabled > 1) return POV_PLUGIN_INVALID_API;
    auto *feature = MirvPovFeatures::Find(g_Features, name);
    if(!feature || !Supported(name)) return POV_PLUGIN_UNSUPPORTED;
    feature->configured = enabled != 0;
    if(feature->immediate) {
        feature->active = feature->configured;
        PublishFeatures();
        if(enabled && !std::strcmp(feature->name, "round_damage")
            && g_State.load() == POV_PLUGIN_ENABLED && !g_RestorePending) PovNativeRuntime::Enable();
        if(g_State.load() == POV_PLUGIN_ENABLED && !g_RestorePending)
            PovNativeRuntime::FeatureChanged(feature->name, enabled != 0);
    }
    return POV_PLUGIN_OK;
}

int32_t POV_CALL OnLevelInit(uint32_t phase) {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    if(phase > 1) return POV_PLUGIN_INVALID_API;
    if(phase == 0) { PovNativeRuntime::LevelInit(); return POV_PLUGIN_OK; }
    if(g_RestorePending) return POV_PLUGIN_RESTORE_FAILED;
    return g_State.load() == POV_PLUGIN_ENABLED && MirvPovFeatures::Find(g_Features, "cvars")->active
        ? Apply(true) : POV_PLUGIN_OK;
}

int32_t POV_CALL Shutdown() {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    const int32_t previous = g_State.exchange(POV_PLUGIN_DISABLED);
    if(!PovNativeRuntime::Disable()) {
        g_RestorePending = true; g_State.store(previous); return POV_PLUGIN_RESTORE_FAILED;
    }
    if(g_Dirty) {
        const int32_t result = Apply(false);
        if(result != POV_PLUGIN_OK) { g_RestorePending = true; g_State.store(previous); return result; }
    }
    g_RestorePending = false;
    g_GameThread = 0;
    g_State.store(POV_PLUGIN_UNINITIALIZED);
    return POV_PLUGIN_OK;
}

int32_t POV_CALL GetState() { return g_State.load(); }
uint64_t POV_CALL GetReadyCapabilities() {
    return g_State.load() == POV_PLUGIN_UNINITIALIZED ? 0 : POV_PLUGIN_CAP_CVARS | PovNativeRuntime::Ready();
}
int32_t POV_CALL OnFrame() {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    if(g_State.load() == POV_PLUGIN_ENABLED && !g_RestorePending && !PovNativeRuntime::Frame()) {
        g_RestorePending = true;
        PovNativeRuntime::Disable();
        return POV_PLUGIN_RESTORE_FAILED;
    }
    return POV_PLUGIN_OK;
}
int32_t POV_CALL OnHudLayoutChanged(uint32_t kinds) {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    if(kinds & ~(POV_LAYOUT_LEGEND | POV_LAYOUT_HEALTH_AMMO)) return POV_PLUGIN_INVALID_API;
    if(g_RestorePending) return POV_PLUGIN_OK;
    PovNativeRuntime::LayoutChanged(kinds);
    return POV_PLUGIN_OK;
}
int32_t POV_CALL SetVoiceMode(uint32_t mode) {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    if(mode > 3) return POV_PLUGIN_INVALID_API;
    if(g_RestorePending) return POV_PLUGIN_RESTORE_FAILED;
    if(mode == 0) MirvPovVoice_SetEnabled(false);
    else MirvPovVoice_SetMode(static_cast<MirvPovVoiceMode>(mode - 1));
    return POV_PLUGIN_OK;
}
int32_t POV_CALL SetVoiceSelection(uint32_t low, uint32_t high) {
    if(!OnGameThread()) return POV_PLUGIN_WRONG_THREAD;
    MirvPovVoice_SetSelection(low, high);
    return POV_PLUGIN_OK;
}
int32_t POV_CALL OnGameEvent(const PovGameEventV5 *event) {
    if(!OnGameThread())return POV_PLUGIN_WRONG_THREAD;
    if(!event||event->size<sizeof(*event)||event->kind<POV_EVENT_ROUND_START||event->kind>POV_EVENT_OTHER
        ||(event->fields&~127u)||event->dispatch>1||!std::memchr(event->name,0,sizeof(event->name))||!std::memchr(event->weapon,0,sizeof(event->weapon)))return POV_PLUGIN_INVALID_API;
    if((event->fields&POV_EVENT_HAS_POSITION)&&(!std::isfinite(event->position[0])
        ||!std::isfinite(event->position[1])||!std::isfinite(event->position[2])))return POV_PLUGIN_INVALID_API;
    if(g_RestorePending)return POV_PLUGIN_RESTORE_FAILED;
    return PovNativeRuntime::BeginEvent(*event);
}
int32_t POV_CALL EndGameEvent() {
    if(!OnGameThread())return POV_PLUGIN_WRONG_THREAD;
    return PovNativeRuntime::EndEvent();
}
int32_t POV_CALL Configure(const char *name,int32_t value) {
    if(!OnGameThread())return POV_PLUGIN_WRONG_THREAD;
    if(!name)return POV_PLUGIN_INVALID_API;
    if(g_RestorePending)return POV_PLUGIN_RESTORE_FAILED;
    if(!strcmp(name,"scoreboard")||!strcmp(name,"buymenu")) {
        if(value!=0&&value!=1)return POV_PLUGIN_INVALID_API;
        if(!strcmp(name,"scoreboard"))MirvPovScoreboard_SetEnabled(value!=0);
        else MirvPovBuyMenu_SetEnabled(value!=0);
        if(g_State.load()==POV_PLUGIN_ENABLED)PovNativeRuntime::Enable();
        return POV_PLUGIN_OK;
    }
    return MirvPovRadio_Configure(name,value)?POV_PLUGIN_OK:POV_PLUGIN_INVALID_API;
}
int32_t POV_CALL OnEntityAdded(uint32_t handle) {
    if(!OnGameThread())return POV_PLUGIN_WRONG_THREAD;
    if(g_RestorePending)return POV_PLUGIN_RESTORE_FAILED;
    const SOURCESDK::CS2::CBaseHandle key(handle);
    auto *entity=key.IsValid()?GetEntityFromIndex(key.GetEntryIndex()):nullptr;
    if(entity&&entity->GetHandle().ToInt()==handle&&MirvPov_IsEnabled())MirvPovRadio_HandleEntityAdded(entity,handle);
    return POV_PLUGIN_OK;
}
const PovPluginApiV5 g_Api{sizeof(PovPluginApiV5), POV_PLUGIN_API_VERSION,
    POV_PLUGIN_CAP_CVARS | POV_PLUGIN_CAP_CORE | POV_PLUGIN_CAP_HUD | POV_PLUGIN_CAP_RADAR | POV_PLUGIN_CAP_TEAM_HEALTH | POV_PLUGIN_CAP_TEAM_ID
        | POV_PLUGIN_CAP_VOICE | POV_PLUGIN_CAP_VOICE_LOCATION | POV_PLUGIN_CAP_VOICE_BAN
        | POV_PLUGIN_CAP_VOICE_AVATARS | POV_PLUGIN_CAP_VOICE_HUD
        | POV_PLUGIN_CAP_DEAFEN | POV_PLUGIN_CAP_DAMAGE_DIRECTION | POV_PLUGIN_CAP_ATTACK_SOUND
        | POV_PLUGIN_CAP_DEATH_PANEL
        | POV_PLUGIN_CAP_DEATH_CAMERA
        | POV_PLUGIN_CAP_RADIO
        | POV_PLUGIN_CAP_KILL_REWARD
        | POV_PLUGIN_CAP_MVP_MUSIC
        | POV_PLUGIN_CAP_PICKUP_PROMPT
        | POV_PLUGIN_CAP_SOUND_CIRCLE
        | POV_PLUGIN_CAP_SCOREBOARD
        | POV_PLUGIN_CAP_BUY_MENU,
    Initialize, SetEnabled, SetFeature, OnLevelInit, Shutdown, GetState, GetReadyCapabilities, OnFrame, OnHudLayoutChanged, SetVoiceMode, SetVoiceSelection, OnGameEvent, EndGameEvent, Configure, OnEntityAdded};
}

bool PovPlugin_FeatureActive(const char *name) {
    const auto mask = g_ActiveFeatures.load(std::memory_order_acquire);
    for(size_t i = 0; name && i < g_FeatureDefinitions.size(); ++i)
        if(!_stricmp(g_FeatureDefinitions[i].name, name)) return (mask & (UINT64_C(1) << i)) != 0;
    return false;
}
bool MirvPov_IsEnabled() {
    return g_State.load() == POV_PLUGIN_ENABLED && (PovNativeRuntime::Ready() & POV_PLUGIN_CAP_CORE) != 0;
}
bool MirvPovDebug_IsFeatureEnabled(const char *name) {
    return PovPlugin_FeatureActive(name) && PovNativeRuntime::EnabledFeature(name);
}
extern "C" const PovPluginApiV5 *POV_CALL PovPlugin_GetApi(uint32_t version, uint32_t size) {
    return version == POV_PLUGIN_API_VERSION && size >= sizeof(PovPluginApiV5) ? &g_Api : nullptr;
}
