#pragma once

#include <Windows.h>

void MirvPovTeamHealth_Initialize(HMODULE clientDll);
bool MirvPovTeamHealth_IsReady();
bool MirvPovTeamHealth_OverridesVisibility(void *returnAddress);
