// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <Windows.h>
bool PovDeathFade_Initialize(HMODULE client);
void PovDeathFade_ObserverChanged();
void PovDeathFade_Frame();
bool PovDeathFade_Reset();
