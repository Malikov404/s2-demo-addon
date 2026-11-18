// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#ifdef _WIN32
#define POV_CALL __cdecl
#ifdef pov_plugin_EXPORTS
#define POV_EXPORT __declspec(dllexport)
#else
#define POV_EXPORT
#endif
#else
#define POV_CALL
#define POV_EXPORT
#endif

#define POV_PLUGIN_API_VERSION 5u
#define POV_PLUGIN_CAP_CVARS UINT64_C(1)
#define POV_PLUGIN_CAP_CORE UINT64_C(2)
#define POV_PLUGIN_CAP_HUD UINT64_C(4)
#define POV_PLUGIN_CAP_RADAR UINT64_C(8)
#define POV_PLUGIN_CAP_TEAM_HEALTH UINT64_C(16)
#define POV_PLUGIN_CAP_TEAM_ID UINT64_C(32)
#define POV_PLUGIN_CAP_VOICE UINT64_C(64)
#define POV_PLUGIN_CAP_VOICE_LOCATION UINT64_C(128)
#define POV_PLUGIN_CAP_VOICE_BAN UINT64_C(256)
#define POV_PLUGIN_CAP_VOICE_AVATARS UINT64_C(512)
#define POV_PLUGIN_CAP_VOICE_HUD UINT64_C(1024)
#define POV_PLUGIN_CAP_DEAFEN UINT64_C(2048)
#define POV_PLUGIN_CAP_DAMAGE_DIRECTION UINT64_C(4096)
#define POV_PLUGIN_CAP_ATTACK_SOUND UINT64_C(8192)
#define POV_PLUGIN_CAP_DEATH_PANEL UINT64_C(16384)
#define POV_PLUGIN_CAP_DEATH_CAMERA UINT64_C(32768)
#define POV_PLUGIN_CAP_RADIO UINT64_C(65536)
#define POV_PLUGIN_CAP_KILL_REWARD UINT64_C(131072)
#define POV_PLUGIN_CAP_MVP_MUSIC UINT64_C(262144)
#define POV_PLUGIN_CAP_PICKUP_PROMPT UINT64_C(524288)
#define POV_PLUGIN_CAP_SOUND_CIRCLE UINT64_C(1048576)
#define POV_PLUGIN_CAP_SCOREBOARD UINT64_C(2097152)
#define POV_PLUGIN_CAP_BUY_MENU UINT64_C(4194304)


enum PovEventKind { POV_EVENT_ROUND_START = 1, POV_EVENT_FLASH, POV_EVENT_BLIND, POV_EVENT_HURT, POV_EVENT_DEATH, POV_EVENT_OTHER };
enum PovEventField { POV_EVENT_HAS_HEALTH = 1, POV_EVENT_HAS_ENTITY = 2, POV_EVENT_HAS_POSITION = 4,
    POV_EVENT_HAS_WEAPON = 8, POV_EVENT_HAS_DAMAGE = 16, POV_EVENT_HAS_MUSIC_KIT = 32, POV_EVENT_HAS_NO_MUSIC = 64 };
enum PovEntityVector { POV_VECTOR_ORIGIN, POV_VECTOR_EYE };
typedef struct PovGameEventV5 {
    uint32_t size, kind, fields, victimPawn, attackerPawn, victimController;
    int32_t userId, entityIndex, damageHealth, damageArmor, health, hitgroup, headshot;
    float position[3];
    char weapon[64];
    char name[48];
    uint32_t attackerController;
    int32_t userSlot, attackerSlot, radioSlot, musicKit, noMusic;
    uint32_t dispatch; // 0 client, 1 server (radio only).
    uint64_t identity; // Opaque dispatch identity, never dereferenced.

} PovGameEventV5;

enum PovEntityField {
    POV_ENTITY_IS_PAWN, POV_ENTITY_IS_CONTROLLER, POV_ENTITY_TEAM, POV_ENTITY_HANDLE,
    POV_ENTITY_PAWN_HANDLE, POV_ENTITY_CONTROLLER_HANDLE, POV_ENTITY_OBSERVER_MODE,
    POV_ENTITY_OBSERVER_TARGET, POV_ENTITY_HEALTH
};
enum PovHudLayoutChange { POV_LAYOUT_LEGEND = 1, POV_LAYOUT_HEALTH_AMMO = 2 };

// Borrowed game interfaces/globals, never HLAE C++ state or owned allocations.
typedef struct PovNativeContextV5 {
    uint32_t size;
    uint32_t makeSymbolOffset;
    void *engineToClient;
    void **hudPanel;
    void **uiEngine;
} PovNativeContextV5;

enum PovPluginResult {
    POV_PLUGIN_OK = 0,
    POV_PLUGIN_INVALID_API = -1,
    POV_PLUGIN_UNAVAILABLE = -2,
    POV_PLUGIN_WRONG_THREAD = -3,
    POV_PLUGIN_UNSUPPORTED = -4,
    POV_PLUGIN_RESTORE_FAILED = -5
};

enum PovPluginState {
    POV_PLUGIN_UNINITIALIZED = 0,
    POV_PLUGIN_DISABLED = 1,
    POV_PLUGIN_ENABLED = 2
};

// Cvar callbacks and mutating API calls run on the host game thread. The host
// owns its native hooks. Native query results are borrowed engine addresses,
// not host wrapper objects. No allocation ownership or C++ ABI crosses this
// interface. Initialize copies the table. Entity queries may run synchronously
// inside native presentation hooks; mutations remain game-thread-only.
typedef struct PovPluginHostV5 {
    uint32_t size;
    uint32_t version;
    int32_t (POV_CALL *ReadCvar)(const char *name, int32_t *current, int32_t *defaults);
    int32_t (POV_CALL *WriteCvar)(const char *name, int32_t value);
    int32_t (POV_CALL *GetNativeContext)(PovNativeContextV5 *context);
    void *(POV_CALL *GetEntity)(int32_t index);
    void *(POV_CALL *GetLocalController)(int32_t slot);
    int32_t (POV_CALL *ReadEntity)(void *entity, uint32_t field, uint64_t *value);
    const char *(POV_CALL *GetEntityClass)(void *entity);
    int32_t (POV_CALL *GetHighestEntityIndex)(void);
    void (POV_CALL *Log)(uint32_t warning, const char *message);
    float (POV_CALL *GetGameTime)(void);
    int32_t (POV_CALL *GetFrameCount)(void);
    int32_t (POV_CALL *IsEventStreamReady)(void);
    int32_t (POV_CALL *ReadEntityVector)(void *entity, uint32_t field, float out[3]);
    int32_t (POV_CALL *ReadNumericCvar)(const char *name, float *value);

} PovPluginHostV5;

typedef struct PovPluginApiV5 {
    uint32_t size;
    uint32_t version;
    uint64_t capabilities;
    int32_t (POV_CALL *Initialize)(const PovPluginHostV5 *host);
    int32_t (POV_CALL *SetEnabled)(uint32_t enabled);
    int32_t (POV_CALL *SetFeature)(const char *name, uint32_t enabled);
    int32_t (POV_CALL *OnLevelInit)(uint32_t phase); // 0 before native teardown, 1 after init
    int32_t (POV_CALL *Shutdown)(void);
    // These two atomic queries are permitted from other threads.
    int32_t (POV_CALL *GetState)(void);
    uint64_t (POV_CALL *GetReadyCapabilities)(void);
    int32_t (POV_CALL *OnFrame)(void);
    int32_t (POV_CALL *OnHudLayoutChanged)(uint32_t kinds);
    int32_t (POV_CALL *SetVoiceMode)(uint32_t mode); // 0 off, 1 team, 2 all, 3 enemy
    int32_t (POV_CALL *SetVoiceSelection)(uint32_t low, uint32_t high);
    int32_t (POV_CALL *OnGameEvent)(const PovGameEventV5 *event);
    int32_t (POV_CALL *EndGameEvent)(void);
    int32_t (POV_CALL *Configure)(const char *name, int32_t value);
    int32_t (POV_CALL *OnEntityAdded)(uint32_t handle);

} PovPluginApiV5;

typedef const PovPluginApiV5 *(POV_CALL *PovPluginGetApiFn)(uint32_t version, uint32_t size);

#ifdef __cplusplus
extern "C" {
#endif
POV_EXPORT const PovPluginApiV5 *POV_CALL PovPlugin_GetApi(uint32_t version, uint32_t size);
#ifdef __cplusplus
}
#endif
