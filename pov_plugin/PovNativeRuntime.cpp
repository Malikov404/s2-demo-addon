#include "PovDeathFade.h"
// SPDX-License-Identifier: AGPL-3.0-only
#include "PovNativeRuntime.h"
#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"
#include "../AfxHookSource2/MirvPovCore.h"
#include "../AfxHookSource2/MirvPovHud.h"
#include "../AfxHookSource2/MirvPovRadar.h"
#include "../AfxHookSource2/MirvPovTeamHealth.h"
#include "../AfxHookSource2/MirvPovTeamID.h"
#include "../AfxHookSource2/MirvPovVoice.h"
#include "../AfxHookSource2/MirvPovVoiceBan.h"
#include "../AfxHookSource2/MirvPovCvars.h"
#include "../AfxHookSource2/MirvPovFeedback.h"
#include "../AfxHookSource2/MirvPanorama.h"
#include "../AfxHookSource2/SchemaSystem.h"
#include "../shared/ThreadPool.h"
#include "../shared/AfxConsole.h"
#include "../AfxHookSource2/MirvPovDeathPanel.h"
#include "../AfxHookSource2/MirvPovDeathCam.h"
#include "../AfxHookSource2/MirvPovMvpMusic.h"
#include "../AfxHookSource2/MirvPovRadio.h"
#include "../AfxHookSource2/MirvPovKillReward.h"
#include "../AfxHookSource2/MirvPovSoundCircle.h"
#include "../AfxHookSource2/MirvPovScoreboard.h"
#include "../AfxHookSource2/MirvPovPickupPrompt.h"
#include "../AfxHookSource2/MirvPovBuyMenu.h"
#include "../AfxHookSource2/MirvPovFeedbackEvent.h"
#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
bool PovDeathPanel_Initialize(HMODULE client);


SOURCESDK::CS2::ISource2EngineToClient *g_pEngineToClient = nullptr;
advancedfx::CThreadPool *g_pThreadPool = nullptr;

namespace PovNativeRuntime {
const PovPluginHostV5 *Host();
namespace {
// A single Release log per initialization; no timing work in frame/hooks.
class StartupTimings {
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_Start = Clock::now(), m_Last = m_Start;
    char m_Details[640]{};
    size_t m_Length = 0;
public:
    bool active = false;
    void Mark(const char *phase) {
        const auto now = Clock::now();
        const double elapsed = std::chrono::duration<double, std::milli>(now - m_Last).count();
        m_Last = now;
        const size_t remaining = sizeof(m_Details) - m_Length;
        const int written = std::snprintf(m_Details + m_Length, remaining, " %s=%.2f", phase, elapsed);
        m_Length += written > 0 && static_cast<size_t>(written) < remaining
            ? static_cast<size_t>(written) : remaining - 1;
    }
    ~StartupTimings() {
        if(active) advancedfx::Message("[pov_plugin] init_ms=%.2f%s\n",
            std::chrono::duration<double, std::milli>(Clock::now() - m_Start).count(), m_Details);
    }
};
std::atomic<uint64_t> g_Ready{0};
bool g_SchemaAttempted = false, g_HudAttempted = false, g_HudActive = false;
bool g_RadarAttempted = false, g_Pinned = false;
bool g_TeamHealthAttempted = false, g_TeamIdAttempted = false, g_VoiceAttempted = false, g_VoiceLocationAttempted = false;
bool g_VoiceSessionActive = false, g_FeedbackAttempted = false, g_FeedbackPrepared = false;
int g_FeedbackDemoTick = -1;
uint64_t g_RemainingAttempts=0;
bool g_PickupAttempted=false;
bool g_DeathPanelReady=false,g_DeathFadeReady=false;
thread_local bool g_Initializing=false;
struct EventScope {MirvPovMvpMusicTrace trace{};uint32_t previous=UINT32_MAX;bool reapply=false;};
std::array<EventScope,16> g_EventScopes;
size_t g_EventDepth=0;
ULONGLONG g_NextRetry = 0;
// Explicit ownership: a global smart-pointer destructor would join workers
// under the loader lock during process termination. The OS reclaims on exit.
advancedfx::CThreadPool *g_Pool = nullptr;

bool PinModule() {
    if(g_Pinned) return true;
    HMODULE module;
    g_Pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&PinModule), &module) != 0;
    return g_Pinned;
}
}
uint64_t Ready() { return g_Ready.load(std::memory_order_acquire); }
bool EnabledFeature(const char *name) {
    const uint64_t ready = Ready();
    if(g_Initializing)return true;
    if(!std::strcmp(name,"deathcam"))return (ready&(POV_PLUGIN_CAP_DEATH_PANEL|POV_PLUGIN_CAP_DEATH_CAMERA))!=0;
    if(!std::strcmp(name,"deathpanel_slide"))return (ready&POV_PLUGIN_CAP_DEATH_PANEL)!=0;
    if(!std::strcmp(name,"death_screen"))return (ready&POV_PLUGIN_CAP_DEATH_CAMERA)!=0;
    if(!std::strcmp(name,"killreward"))return (ready&POV_PLUGIN_CAP_KILL_REWARD)!=0;
    if(!std::strncmp(name,"radio",5))return (ready&POV_PLUGIN_CAP_RADIO)!=0;
    if(!std::strcmp(name,"mvp_music"))return (ready&POV_PLUGIN_CAP_MVP_MUSIC)!=0;
    if(!std::strcmp(name,"soundcircle"))return (ready&POV_PLUGIN_CAP_SOUND_CIRCLE)!=0;
    if(!std::strcmp(name,"pickupprompt"))return (ready&POV_PLUGIN_CAP_PICKUP_PROMPT)!=0;
    if(!std::strcmp(name,"scoreboard"))return (ready&POV_PLUGIN_CAP_SCOREBOARD)!=0;
    if(!std::strncmp(name,"buymenu",7))return (ready&POV_PLUGIN_CAP_BUY_MENU)!=0;
    if(!std::strcmp(name,"feedback"))return (ready&(POV_PLUGIN_CAP_DEAFEN|POV_PLUGIN_CAP_DAMAGE_DIRECTION|POV_PLUGIN_CAP_ATTACK_SOUND))!=0;
    if(!std::strcmp(name,"deafen"))return (ready&POV_PLUGIN_CAP_DEAFEN)!=0;
    if(!std::strcmp(name,"damage_direction"))return (ready&POV_PLUGIN_CAP_DAMAGE_DIRECTION)!=0;
    if(!std::strcmp(name,"hit_sound")||!std::strcmp(name,"kill_sound"))return (ready&POV_PLUGIN_CAP_ATTACK_SOUND)!=0;
    if(!std::strcmp(name, "voice")) return (ready & POV_PLUGIN_CAP_VOICE) != 0;
    if(!std::strcmp(name, "voice_location")) return (ready & POV_PLUGIN_CAP_VOICE_LOCATION) != 0;
    if(!std::strcmp(name, "voiceban")) return (ready & POV_PLUGIN_CAP_VOICE_BAN) != 0;
    if(!std::strcmp(name, "voiceban_avatars")) return (ready & POV_PLUGIN_CAP_VOICE_AVATARS) != 0;
    if(!std::strcmp(name, "teamhealth") || !std::strcmp(name, "hud_target_highlight"))
        return (ready & POV_PLUGIN_CAP_TEAM_HEALTH) != 0;
    if(!std::strcmp(name, "teamid")) return (ready & POV_PLUGIN_CAP_TEAM_ID) != 0;
    if(!std::strcmp(name, "radar")) return (ready & POV_PLUGIN_CAP_RADAR) != 0;
    if(!std::strcmp(name, "hud") || !std::strcmp(name, "buyzone") || !std::strcmp(name, "round_damage"))
        return (ready & POV_PLUGIN_CAP_HUD) != 0;
    return (ready & POV_PLUGIN_CAP_CORE) != 0;
}
bool Prepare() {
    const auto *host = Host();
    if(!host || !host->GetNativeContext || !host->GetEntity || !host->ReadEntity
        || !host->GetLocalController || !host->GetEntityClass || !host->GetHighestEntityIndex) return false;
    PovNativeContextV5 context{}; context.size = sizeof(context);
    if(!host->GetNativeContext(&context) || !context.engineToClient) return false;
    HMODULE schema = GetModuleHandleW(L"schemasystem.dll");
    if(schema && !g_SchemaAttempted && (PovPlugin_FeatureActive("hud") || PovPlugin_FeatureActive("feedback") || PovPlugin_FeatureActive("deathcam") || PovPlugin_FeatureActive("radio") || PovPlugin_FeatureActive("buymenu") || (PovPlugin_FeatureActive("voice") && PovPlugin_FeatureActive("voice_location")))) {
        g_SchemaAttempted = true;
        g_clientDllOffsets.C_CSGameRulesProxy.m_pGameRules = -1;
        HookSchemaSystem(schema); // Discovery only: does not install a native hook.
    }
    if(g_pEngineToClient != context.engineToClient)
        g_pEngineToClient = static_cast<SOURCESDK::CS2::ISource2EngineToClient *>(context.engineToClient);
    MirvPanorama_SetHudPanel(context.hudPanel);
    MirvPanorama_SetUIEngine(context.uiEngine);
    if(CS2::PanoramaUIEngine::makeSymbol != context.makeSymbolOffset)
        CS2::PanoramaUIEngine::makeSymbol = context.makeSymbolOffset;
    if(!GetFakePovRadarAutoSync()) SetFakePovRadarAutoSync(true);
    g_Ready.fetch_or(POV_PLUGIN_CAP_CORE, std::memory_order_release);
    return true;
}
void Enable() {
    StartupTimings timings;
    if(!Prepare()) return;
    const auto client = GetModuleHandleW(L"client.dll");
    const auto panorama = GetModuleHandleW(L"panorama.dll");
    if(!client || !PinModule()) return;
    timings.active = true;timings.Mark("prepare_schema");
    if(PovPlugin_FeatureActive("hud") && PovPlugin_FeatureActive("round_damage") && !g_Pool) {
        g_Pool = new advancedfx::CThreadPool(1);
        g_pThreadPool = g_Pool;
    }
    struct Initializing {Initializing(){g_Initializing=true;}~Initializing(){g_Initializing=false;}} initializing;
    // Cache native helper targets even if Feedback is configured off initially;
    // Radio must not hide discovery when Feedback is enabled later in this process.
    if(!g_FeedbackPrepared) {
        g_FeedbackPrepared = true;
        MirvPovFeedback_Prepare(client);
    }
    timings.Mark("feedback_discovery");
    const auto attempt=[&](uint64_t bit,bool enabled,auto action) {
        if(enabled&&!(g_RemainingAttempts&bit)) {g_RemainingAttempts|=bit;action();}
    };
    // Discover money/getter contexts before HUD installs its panel hook.
    attempt(1,PovPlugin_FeatureActive("killreward")||PovPlugin_FeatureActive("radio"),[&]{MirvPovKillReward_Initialize(client);});
    timings.Mark("money");
    attempt(2,PovPlugin_FeatureActive("deathcam")||PovPlugin_FeatureActive("mvp_music"),[&]{g_DeathPanelReady=PovDeathPanel_Initialize(client);});
    timings.Mark("deathpanel_mvp");
    attempt(4,PovPlugin_FeatureActive("deathcam"),[&]{MirvPovDeathCam_Initialize(client);g_DeathFadeReady=PovDeathFade_Initialize(client);});
    timings.Mark("deathcam_fade");
    attempt(8,PovPlugin_FeatureActive("soundcircle")||PovPlugin_FeatureActive("radio"),[&]{MirvPovSoundCircle_Initialize(client);});
    timings.Mark("sound");
    attempt(16,PovPlugin_FeatureActive("radio"),[&]{MirvPovRadio_Initialize(client);});
    timings.Mark("radio");
    attempt(32,PovPlugin_FeatureActive("scoreboard"),[&]{MirvPovScoreboard_Initialize(client);});
    timings.Mark("scoreboard");
    if(PovPlugin_FeatureActive("pickupprompt")&&!g_PickupAttempted) {
        g_PickupAttempted=true;MirvPovPickupPrompt_Initialize(client);
    }
    timings.Mark("pickup");
    attempt(64,PovPlugin_FeatureActive("buymenu")&&MirvPovBuyMenu_IsEnabled(),[&]{MirvPovBuyMenu_Initialize(client);});
    timings.Mark("buymenu");
    if(PovPlugin_FeatureActive("hud") && panorama && CS2::PanoramaUIEngine::makeSymbol) {
        if(!g_HudAttempted) {
            g_HudAttempted = true;
            MirvPanorama_InitStyleProperties(panorama);
            MirvPovHud_OnPanoramaDllLoaded(panorama);
        }
        if(!g_HudActive) { MirvPovHud_ApplyPatches(client); g_HudActive = true; }
        if(MirvPovHud_IsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_HUD);
    }
    timings.Mark("hud");
    if(PovPlugin_FeatureActive("radar") && !g_RadarAttempted) {
        g_RadarAttempted = true;
        MirvPov_ApplyRadarPatches(client);
        if(MirvPovRadar_IsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_RADAR);
    }
    timings.Mark("radar");
    if(PovPlugin_FeatureActive("teamhealth")) {
        if(!g_TeamHealthAttempted) {
            g_TeamHealthAttempted = true;
            MirvPovTeamHealth_Initialize(client);
        }
        if(MirvPovTeamHealth_IsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_TEAM_HEALTH);
    }
    timings.Mark("teamhealth");
    if(PovPlugin_FeatureActive("teamid") && !g_TeamIdAttempted) {
        g_TeamIdAttempted = true;
        MirvPovTeamID_ApplyPatches(client);
        if(MirvPovTeamID_IsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_TEAM_ID);
    }
    timings.Mark("teamid");
    if(PovPlugin_FeatureActive("voice")) {
        if(!g_VoiceSessionActive) { MirvPov_ResetVoiceHud(); g_VoiceSessionActive = true; }
        int32_t low, high;
        if(MirvPovCvars::ReadInteger("tv_listen_voice_indices", low)
            && MirvPovCvars::ReadInteger("tv_listen_voice_indices_h", high)) g_Ready.fetch_or(POV_PLUGIN_CAP_VOICE);
        if(!g_VoiceAttempted || (PovPlugin_FeatureActive("voice_location") && !g_VoiceLocationAttempted)) {
            g_VoiceAttempted = true;
            const bool location = PovPlugin_FeatureActive("voice_location");
            MirvPov_HookVoiceHud(client, location);
            if(location) g_VoiceLocationAttempted = true;
        }
        if(Host()->GetGameTime && MirvPovVoice_IsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_VOICE_HUD);
        if(PovPlugin_FeatureActive("voice_location") && MirvPovVoice_LocationReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_VOICE_LOCATION);
    }
    timings.Mark("voice");
    if(PovPlugin_FeatureActive("voiceban")) {
        MirvPovVoiceBan_Initialize(client);
        if(MirvPovVoiceBan_IsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_VOICE_BAN);
        if(MirvPovVoiceBan_AvatarsReady()) g_Ready.fetch_or(POV_PLUGIN_CAP_VOICE_AVATARS);
    }
    timings.Mark("voiceban");
    if(PovPlugin_FeatureActive("feedback")&&Host()->GetFrameCount&&Host()->ReadEntityVector&&Host()->IsEventStreamReady&&Host()->IsEventStreamReady()) {
        if(!g_FeedbackAttempted) {g_FeedbackAttempted=true;MirvPovFeedback_Initialize(client);}
        if(MirvPovFeedback_DeafenReady())g_Ready.fetch_or(POV_PLUGIN_CAP_DEAFEN);
        if(MirvPovFeedback_DirectionsReady())g_Ready.fetch_or(POV_PLUGIN_CAP_DAMAGE_DIRECTION);
        if(MirvPovFeedback_AttackSoundReady())g_Ready.fetch_or(POV_PLUGIN_CAP_ATTACK_SOUND);
    }
    timings.Mark("feedback");
    if(PovPlugin_FeatureActive("deathcam")&&g_DeathPanelReady)g_Ready.fetch_or(POV_PLUGIN_CAP_DEATH_PANEL);
    if(PovPlugin_FeatureActive("deathcam")&&g_MirvPovDeathPanelState.localPawnHooked&&g_DeathFadeReady&&MirvPovDeathCam_IsHooked())g_Ready.fetch_or(POV_PLUGIN_CAP_DEATH_CAMERA);
    if(PovPlugin_FeatureActive("mvp_music")&&g_MirvPovDeathPanelState.localPawnHooked&&MirvPovMvpMusic_IsReady())g_Ready.fetch_or(POV_PLUGIN_CAP_MVP_MUSIC);
    if(PovPlugin_FeatureActive("radio")&&MirvPovRadio_IsAvailable())g_Ready.fetch_or(POV_PLUGIN_CAP_RADIO);
    if(PovPlugin_FeatureActive("killreward")&&MirvPovKillReward_IsAvailable())g_Ready.fetch_or(POV_PLUGIN_CAP_KILL_REWARD);
    if(PovPlugin_FeatureActive("soundcircle")&&MirvPovSoundCircle_IsHooked())g_Ready.fetch_or(POV_PLUGIN_CAP_SOUND_CIRCLE);
    if(PovPlugin_FeatureActive("scoreboard")&&MirvPovScoreboard_IsReady())g_Ready.fetch_or(POV_PLUGIN_CAP_SCOREBOARD);
    if(PovPlugin_FeatureActive("pickupprompt")&&MirvPovPickupPrompt_IsReady())g_Ready.fetch_or(POV_PLUGIN_CAP_PICKUP_PROMPT);
    if(PovPlugin_FeatureActive("buymenu")&&MirvPovBuyMenu_IsReady())g_Ready.fetch_or(POV_PLUGIN_CAP_BUY_MENU);
    MirvPovKillReward_ApplyHudChatDemoBypass(PovPlugin_FeatureActive("radio")||PovPlugin_FeatureActive("killreward"));
    MirvPovHud_ReapplyPanelState();
    timings.Mark("finalize");
}
bool Disable() {
    g_Ready.store(0, std::memory_order_release);
    MirvPovDeathPanel_Clear();MirvPovDeathCam_Reset();
    MirvPovRadio_ResetDemoState();MirvPovSoundCircle_ResetDemoState();
    MirvPovKillReward_Reset("POV disabled");
    MirvPovScoreboard_Reset(false);MirvPovBuyMenu_Reset();
    MirvPovPickupPrompt_RemovePatches();
    if(MirvPovPickupPrompt_IsClean())g_PickupAttempted=false;
    const bool deathClean=PovDeathFade_Reset();
    const bool chatClean=!MirvPovKillReward_IsHudChatDemoBypassApplied()||MirvPovKillReward_ApplyHudChatDemoBypass(false);
    MirvPovFeedback_ResetPovSelection();
    g_FeedbackDemoTick=-1;
    const bool voiceClean = MirvPovVoice_Cleanup();
    g_VoiceSessionActive = false;
    MirvPovVoiceBan_ResetDemoState();
    if(g_HudAttempted) {
        MirvPovHud_OnLevelInitPreEntity();
        MirvPovHud_RemovePatches();
    }
    g_HudActive = false;
    MirvPovTeamID_RemovePatches();
    if(MirvPovTeamID_IsClean()) g_TeamIdAttempted = false;
    if(!MirvPovTeamHealth_IsReady()) g_TeamHealthAttempted = false;
    MirvPov_RemoveRadarPatches();
    if(MirvPovRadar_IsClean()) g_RadarAttempted = false;
    g_pThreadPool = nullptr;
    delete g_Pool; // File tasks are cancelled above; join outside DllMain.
    g_Pool = nullptr;
    return deathClean && chatClean && MirvPovPickupPrompt_IsClean() && voiceClean && MirvPovRadar_IsClean() && MirvPovTeamID_IsClean();
}
bool Frame() {
    if(!(Ready() & POV_PLUGIN_CAP_CORE)) {
        if(GetTickCount64() < g_NextRetry) return true;
        g_NextRetry = GetTickCount64() + 1000;
        Enable();
    }
    if(MirvPovDebug_IsFeatureEnabled("feedback")) {
        auto *demo=g_pEngineToClient?g_pEngineToClient->GetDemoFile():nullptr;
        const int tick=demo?demo->GetDemoTick():-1;
        if(g_FeedbackDemoTick>=0&&(tick<g_FeedbackDemoTick||tick-g_FeedbackDemoTick>16))MirvPovFeedback_ResetPovSelection();
        g_FeedbackDemoTick=tick;
        MirvPovFeedback_UpdatePovSelection();
    }
    if(Ready()&POV_PLUGIN_CAP_CORE) {
        auto *demo=g_pEngineToClient?g_pEngineToClient->GetDemoFile():nullptr;
        const int tick=demo?demo->GetDemoTick():-1;
        PovDeathFade_Frame();
        MirvPovDeathCam_UpdateDemoTick(tick);MirvPovDeathCam_UpdateLifecycle();
        MirvPovDeathPanel_Update();MirvPovRadio_OnDemoTick(tick);MirvPovKillReward_OnDemoTick(tick);
        MirvPovScoreboard_Update();MirvPovBuyMenu_Update();
    }
    if(!MirvPovDebug_IsFeatureEnabled("framestage")) return true;
    if(Ready() & POV_PLUGIN_CAP_CORE) {
        if(!MirvPovVoice_AfterRenderPass()) return false;
        MirvPovVoiceBan_OnRenderPass();
    }
    if(PovPlugin_FeatureActive("hud") && !g_HudAttempted
        && (Ready() & POV_PLUGIN_CAP_CORE) && !(Ready() & POV_PLUGIN_CAP_HUD)
        && GetTickCount64() >= g_NextRetry) {
        g_NextRetry = GetTickCount64() + 1000;
        Enable(); // Late Panorama availability; missing signatures are latched.
    }
    if(Ready() & POV_PLUGIN_CAP_HUD) {
        MirvPovHud_ReapplyPanelState();
        MirvPovHud_UpdateRoundDamage();
        if(g_pEngineToClient && g_pEngineToClient->GetDemoFile())
            MirvPovHud_UpdateSeekDetection(g_pEngineToClient->GetDemoFile()->GetDemoTick());
    }
    if(PovPlugin_FeatureActive("voice") && PovPlugin_FeatureActive("voice_location")
        && g_VoiceLocationAttempted && !(Ready() & POV_PLUGIN_CAP_VOICE_LOCATION)
        && GetTickCount64() >= g_NextRetry) {
        g_NextRetry = GetTickCount64() + 1000;
        Enable(); // Late localization readiness; installed/missing hook scans stay latched.
    }
    if(PovPlugin_FeatureActive("feedback")&&!g_FeedbackAttempted&&GetTickCount64()>=g_NextRetry) {
        g_NextRetry=GetTickCount64()+1000;Enable();
    }
    return true;
}
void LevelInit() {
    PovDeathFade_Reset();
    MirvPovDeathPanel_Clear();MirvPovDeathCam_Reset();
    MirvPovRadio_ResetDemoState();MirvPovSoundCircle_ResetDemoState();
    MirvPovKillReward_Reset("level changed");MirvPovScoreboard_Reset(false);MirvPovBuyMenu_Reset();
    MirvPovFeedback_ResetPovSelection();
    g_FeedbackDemoTick=-1;
    MirvPovVoice_OnLevelInit();
    MirvPovVoiceBan_ResetDemoState();
    if(g_HudAttempted) MirvPovHud_OnLevelInitPreEntity();
}
void LayoutChanged(uint32_t kinds) {
    if(!g_HudAttempted) return;
    if(kinds & 1) MirvPovHud_OnPanoramaLayoutFileLoaded("panorama\\layout\\hud\\hudlegend.xml");
    if(kinds & 2) MirvPovHud_OnPanoramaLayoutFileLoaded("panorama\\layout\\hud\\hudhealthammocenter.xml");
}
void FeatureChanged(const char *name, bool enabled) {
    if(!std::strcmp(name,"death_screen")) {MirvPovDeathCam_ResetPresentation();if(!enabled)PovDeathFade_Reset();}
    if(!std::strcmp(name,"deathpanel_slide"))MirvPovDeathPanel_Update();
    if(!std::strcmp(name,"mvp_music")&&!enabled)MirvPovMvpMusic_PopEvent(UINT32_MAX);
    if(!std::strcmp(name,"buymenu_input_passthrough"))MirvPovBuyMenu_ReapplyInput();
    if(!std::strcmp(name,"buymenu_gloves"))MirvPovBuyMenu_ReapplyGloves();
    if(!std::strncmp(name,"buymenu",7))MirvPovBuyMenu_Update();
    if(!std::strcmp(name,"deafen"))MirvPovFeedback_ResetDeafen();
    if(!std::strcmp(name,"damage_direction"))MirvPovFeedback_ResetDirections();
    if(enabled && !std::strcmp(name, "voice_location")) Enable();
    if(!std::strcmp(name, "voiceban_avatars")) MirvPovVoiceBan_OnRenderPass();
    if(!g_HudAttempted) return;
    if(!std::strcmp(name, "round_damage")) {
        if(!enabled) MirvPovHud_CancelPendingWork();
        MirvPovHud_UpdateRoundDamage();
    }
    if(!std::strcmp(name, "buyzone")) MirvPovHud_ReapplyPanelState();
}
}

namespace PovNativeRuntime {
int32_t BeginEvent(const PovGameEventV5 &event) {
    if(g_EventDepth==g_EventScopes.size())return POV_PLUGIN_UNAVAILABLE;
    auto &scope=g_EventScopes[g_EventDepth++];scope={};
    MirvPovFeedbackEvent snapshot(event);
    MirvPovFeedbackEvent *active=MirvPov_IsEnabled()&&!event.dispatch?&snapshot:nullptr;
    MirvPovMvpMusic_BeginTraceSnapshot(active,scope.trace);
    scope.previous=MirvPovMvpMusic_PushEventSnapshot(active);
    if(MirvPov_IsEnabled()) {
        MirvPovRadio_HandleGameEventSnapshot(&snapshot);
        if(!event.dispatch) {
            MirvPovFeedback_HandleGameEventSnapshot(&snapshot);
            MirvPovDeathCam_HandleGameEventSnapshot(&snapshot);
            MirvPovKillReward_HandleGameEventSnapshot(&snapshot);
            if(event.kind==POV_EVENT_ROUND_START) {MirvPovDeathPanel_Clear();PovDeathFade_Reset();}
            if(!std::strcmp(event.name,"spec_target_updated"))PovDeathFade_ObserverChanged();
            scope.reapply=event.kind==POV_EVENT_DEATH;
        }
    }
    return POV_PLUGIN_OK;
}
int32_t EndEvent() {
    if(!g_EventDepth)return POV_PLUGIN_INVALID_API;
    auto &scope=g_EventScopes[--g_EventDepth];
    MirvPovMvpMusic_PopEvent(scope.previous);MirvPovMvpMusic_EndTrace(scope.trace);
    if(scope.reapply&&MirvPov_IsEnabled())MirvPovDeathPanel_Reapply("post-dispatch");
    return POV_PLUGIN_OK;
}
}
bool MirvPov_IsDeathFeedbackEnabled() {return MirvPov_IsEnabled();}
