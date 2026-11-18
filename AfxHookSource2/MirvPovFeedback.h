#pragma once

#include <Windows.h>

namespace SOURCESDK { namespace CS2 { class IGameEvent; } }

// Resolve unhooked helper call sites before radio detours overwrite their prologues.
void MirvPovFeedback_Prepare(HMODULE clientDll);
void MirvPovFeedback_Initialize(HMODULE clientDll);
void MirvPovFeedback_ResetDeafen();
void MirvPovFeedback_ResetDirections();
void MirvPovFeedback_UpdatePovSelection();
void MirvPovFeedback_ResetPovSelection();
void MirvPovFeedback_HandleGameEvent(SOURCESDK::CS2::IGameEvent * event);
bool MirvPovFeedback_IsLocalPlayerVictim(SOURCESDK::CS2::IGameEvent * event);
bool MirvPovFeedback_IsRealLocalPlayerVictim(SOURCESDK::CS2::IGameEvent * event);
// Returns true when the event's userid resolves to the currently selected POV pawn.
bool MirvPovFeedback_IsCurrentPovVictim(
    SOURCESDK::CS2::IGameEvent * event,
    bool * headshot = nullptr);

class MirvPovFeedbackEvent;
void MirvPovFeedback_HandleGameEventSnapshot(MirvPovFeedbackEvent *event);
bool MirvPovFeedback_IsCurrentPovVictimSnapshot(MirvPovFeedbackEvent *event, bool *headshot = nullptr);
bool MirvPovFeedback_IsLocalPlayerVictimSnapshot(MirvPovFeedbackEvent *event);
bool MirvPovFeedback_IsRealLocalPlayerVictimSnapshot(MirvPovFeedbackEvent *event);
bool MirvPovFeedback_DeafenReady();
bool MirvPovFeedback_DirectionsReady();
bool MirvPovFeedback_AttackSoundReady();
