// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PovPluginApi.h"
#include <cstdint>
namespace PovNativeRuntime {
int32_t BeginEvent(const PovGameEventV5 &event);
int32_t EndEvent();
void Bind(const PovPluginHostV5 *host);
bool Prepare();
void Enable();
bool Disable();
bool Frame();
void LevelInit();
void LayoutChanged(uint32_t kinds);
void FeatureChanged(const char *name, bool enabled);
uint64_t Ready();
bool EnabledFeature(const char *name);
}
bool MirvPov_IsEnabled();
bool MirvPovDebug_IsFeatureEnabled(const char *name);
bool PovPlugin_FeatureActive(const char *name);
