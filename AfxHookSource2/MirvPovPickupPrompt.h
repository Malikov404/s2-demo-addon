#pragma once

#include <Windows.h>

void MirvPovPickupPrompt_Initialize(HMODULE clientDll);
void MirvPovPickupPrompt_RemovePatches();

bool MirvPovPickupPrompt_IsReady();
bool MirvPovPickupPrompt_IsClean();
