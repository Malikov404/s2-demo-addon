// SPDX-License-Identifier: AGPL-3.0-only
#include "PovNativeRuntime.h"
#include "../AfxHookSource2/MirvPovCvars.h"
#include "../deps/release/prop/AfxHookSource/SourceSdkShared.h"
#include "../AfxHookSource2/ClientEntitySystem.h"
#include "../AfxHookSource2/SchemaSystem.h"
#include "../shared/AfxConsole.h"
#include "../shared/binutils.h"
#include <cstdarg>
#include <cstdio>
#include <Windows.h>
#include <limits>

namespace {
const PovPluginHostV5 *g_Host = nullptr;
void Print(bool warning, const char *format, va_list args) {
    char buffer[2048]; vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
    if(g_Host && g_Host->Log) g_Host->Log(warning, buffer);
}
void Message(const char *format, ...) { va_list args; va_start(args, format); Print(false, format, args); va_end(args); }
void Warning(const char *format, ...) { va_list args; va_start(args, format); Print(true, format, args); va_end(args); }
uint64_t Read(CEntityInstance *entity, uint32_t field, uint64_t fallback = 0) {
    uint64_t value;
    return g_Host && g_Host->ReadEntity && g_Host->ReadEntity(entity, field, &value) ? value : fallback;
}
}
namespace PovNativeRuntime {
void Bind(const PovPluginHostV5 *host) {
    g_Host = host;
    advancedfx::Message = Message;
    advancedfx::Warning = Warning;
}
const PovPluginHostV5 *Host() { return g_Host; }
}

CEntityInstance *GetEntityFromIndex(int index) {
    return g_Host && g_Host->GetEntity ? static_cast<CEntityInstance *>(g_Host->GetEntity(index)) : nullptr;
}
CEntityInstance *GetRealSplitScreenPlayer(int slot) {
    return g_Host && g_Host->GetLocalController ? static_cast<CEntityInstance *>(g_Host->GetLocalController(slot)) : nullptr;
}
int GetHighestEntityIndex() { return g_Host && g_Host->GetHighestEntityIndex ? g_Host->GetHighestEntityIndex() : -1; }
bool CEntityInstance::IsPlayerPawn() { return Read(this, POV_ENTITY_IS_PAWN) != 0; }
bool CEntityInstance::IsPlayerController() { return Read(this, POV_ENTITY_IS_CONTROLLER) != 0; }
int CEntityInstance::GetTeam() { return static_cast<int>(Read(this, POV_ENTITY_TEAM)); }
unsigned int CEntityInstance::GetHealth() { return static_cast<unsigned int>(Read(this, POV_ENTITY_HEALTH)); }
SOURCESDK::CS2::CBaseHandle CEntityInstance::GetHandle() { return SOURCESDK::CS2::CBaseHandle(static_cast<uint32_t>(Read(this, POV_ENTITY_HANDLE, UINT32_MAX))); }
SOURCESDK::CS2::CBaseHandle CEntityInstance::GetPlayerPawnHandle() { return SOURCESDK::CS2::CBaseHandle(static_cast<uint32_t>(Read(this, POV_ENTITY_PAWN_HANDLE, UINT32_MAX))); }
SOURCESDK::CS2::CBaseHandle CEntityInstance::GetPlayerControllerHandle() { return SOURCESDK::CS2::CBaseHandle(static_cast<uint32_t>(Read(this, POV_ENTITY_CONTROLLER_HANDLE, UINT32_MAX))); }
uint8_t CEntityInstance::GetObserverMode() { return static_cast<uint8_t>(Read(this, POV_ENTITY_OBSERVER_MODE)); }
SOURCESDK::CS2::CBaseHandle CEntityInstance::GetObserverTarget() { return SOURCESDK::CS2::CBaseHandle(static_cast<uint32_t>(Read(this, POV_ENTITY_OBSERVER_TARGET, UINT32_MAX))); }
const char *CEntityInstance::GetClassName() { return g_Host && g_Host->GetEntityClass ? g_Host->GetEntityClass(this) : nullptr; }
bool CEntityInstance::GetInBuyZone(bool &available) {
    available = false;
    if(!IsPlayerPawn() || g_clientDllOffsets.C_CSPlayerPawn.m_bInBuyZone < 0) return false;
    __try {
        available = true;
        return *reinterpret_cast<unsigned char *>(reinterpret_cast<unsigned char *>(this) + g_clientDllOffsets.C_CSPlayerPawn.m_bInBuyZone) != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { available = false; return false; }
}

size_t getAddress(HMODULE module, const char *pattern) {
    if(!module) return 0;
    size_t found = 0;
    for(Afx::BinUtils::ImageSectionsReader sections(module); !sections.Eof(); sections.Next()) {
        if(!(sections.Get()->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto range = sections.GetMemRange();
        const auto match = Afx::BinUtils::FindPatternString(range, pattern);
        if(match.IsEmpty()) continue;
        if(found || !Afx::BinUtils::FindPatternString({match.Start + 1, range.End}, pattern).IsEmpty()) return 0;
        found = match.Start;
    }
    return found;
}
void ErrorBox(const char *message) { advancedfx::Warning("[pov_plugin] %s\n", message ? message : "native dependency unavailable"); }
void ErrorBox() { ErrorBox(nullptr); }

bool MirvPovCvars::ReadInteger(const char *name, int32_t &value) {
    int32_t defaults;
    return g_Host && g_Host->ReadCvar && g_Host->ReadCvar(name, &value, &defaults) != 0;
}

bool MirvPovCvars::WriteInteger(const char *name, int32_t value) {
    return g_Host && g_Host->WriteCvar && g_Host->WriteCvar(name, value) != 0;
}
float MirvPovVoice_GameTime() { return g_Host && g_Host->GetGameTime ? g_Host->GetGameTime() : 0; }
bool MirvPovVoice_TimeReady() { return g_Host && g_Host->GetGameTime; }
const char *CEntityInstance::GetLastPlaceName() {
    if(!IsPlayerPawn() || g_clientDllOffsets.C_CSPlayerPawn.m_szLastPlaceName < 0) return nullptr;
    return reinterpret_cast<const char *>(this) + g_clientDllOffsets.C_CSPlayerPawn.m_szLastPlaceName;
}

int MirvPovFeedback_FrameCount() { return g_Host&&g_Host->GetFrameCount?g_Host->GetFrameCount():0; }
void CEntityInstance::GetOrigin(float &x,float &y,float &z) {
    float out[3];
    if(g_Host&&g_Host->ReadEntityVector&&g_Host->ReadEntityVector(this,POV_VECTOR_ORIGIN,out)) {x=out[0];y=out[1];z=out[2];}
    else x=y=z=std::numeric_limits<float>::quiet_NaN();
}
void CEntityInstance::GetRenderEyeOrigin(float out[3]) {
    if(!g_Host||!g_Host->ReadEntityVector||!g_Host->ReadEntityVector(this,POV_VECTOR_EYE,out))
        out[0]=out[1]=out[2]=std::numeric_limits<float>::quiet_NaN();
}

const char * CEntityInstance::GetPlayerName(){
    if (!IsPlayerController()) return nullptr;
    // m_iszPlayerName is an inline char[128] network field, not a pointer.
    return (const char *)((u_char*)(this) + g_clientDllOffsets.CBasePlayerController.m_iszPlayerName);
}
bool CEntityInstance::GetCurrentRoundKillReward(int & outValue) {
    if (!IsPlayerController()
        || g_clientDllOffsets.CCSPlayerController.m_pActionTrackingServices < 0
        || g_clientDllOffsets.CCSPlayerController_ActionTrackingServices.m_perRoundStats < 0
        || g_clientDllOffsets.CSPerRoundStats_t.m_iKillReward < 0
        || g_clientDllOffsets.CSPerRoundStats_t.size
            <= (size_t)g_clientDllOffsets.CSPerRoundStats_t.m_iKillReward) return false;

    void * actionTrackingServices = *(void **)(
        (unsigned char *)this
        + g_clientDllOffsets.CCSPlayerController.m_pActionTrackingServices);
    if(nullptr == actionTrackingServices) return false;

    unsigned char * perRoundStats = (unsigned char *)actionTrackingServices
        + g_clientDllOffsets.CCSPlayerController_ActionTrackingServices.m_perRoundStats;
    int count = *(int *)perRoundStats;
    unsigned char * data = *(unsigned char **)(perRoundStats + 8);
    if(count <= 0 || 128 < count || nullptr == data) return false;

    unsigned char * currentRound = data
        + (count - 1) * g_clientDllOffsets.CSPerRoundStats_t.size;
    outValue = *(int *)(currentRound + g_clientDllOffsets.CSPerRoundStats_t.m_iKillReward);
    return true;
}

bool MirvPovCvars::ReadNumeric(const char *name, float &value) {
    return g_Host && g_Host->ReadNumericCvar && g_Host->ReadNumericCvar(name, &value);
}
#include "../AfxHookSource2/MirvTime.h"
#include "../deps/release/prop/cs2/sdk_src/public/cdll_int.h"
extern SOURCESDK::CS2::ISource2EngineToClient *g_pEngineToClient;
CMirvTime g_MirvTime;
float CMirvTime::curtime_get() {return MirvPovVoice_GameTime();}
int CMirvTime::framecount_get() {return MirvPovFeedback_FrameCount();}
bool CMirvTime::GetCurrentDemoTick(int &tick) {
    auto *demo=g_pEngineToClient?g_pEngineToClient->GetDemoFile():nullptr;
    tick=demo?demo->GetDemoTick():-1;return tick>=0;
}
