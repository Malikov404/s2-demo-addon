// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
class CEntityInstance;
namespace MirvPovPawnHook {
using Override = CEntityInstance *(*)(CEntityInstance *nativePawn, void *caller, int slot);
// One owner per native signature; consumers override only their own contexts.
bool Ensure(void *target, bool hasSlot, Override callback);
}
