// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../pov_plugin/PovPluginApi.h"
namespace SOURCESDK { namespace CS2 { class IGameEvent; } }
bool MirvPov_ReadNativeEvent(SOURCESDK::CS2::IGameEvent *event, PovGameEventV5 &out);
