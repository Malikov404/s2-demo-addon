// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

namespace MirvPovCvars {
bool ReadInteger(const char *name, int32_t &value);
bool WriteInteger(const char *name, int32_t value);
bool ReadNumeric(const char *name, float &value);
inline constexpr const char *Names[] = {
    "cl_radar_square_when_spectating", "cl_radar_square_always",
    "cl_radar_show_all_players_when_spectating", "cl_trueview_show_status",
    "r_show_build_info"
};

// Read every dependency before changing state. Restore already written values
// if a later write fails. Return -1 if rollback itself could not finish.
template<class Access> int Apply(Access &access, bool enabled) {
    int32_t previous[5]{}, defaults[5]{};
    for(unsigned int i = 0; i < 5; ++i)
        if(!access.Read(Names[i], previous[i], defaults[i])) return 0;
    for(unsigned int i = 0; i < 5; ++i) {
        const int32_t target = enabled ? 0 : i == 2 ? 1 : defaults[i];
        if(previous[i] == target || access.Write(Names[i], target)) continue;
        bool restored = true;
        for(unsigned int j = 0; j < i; ++j)
            if(!access.Write(Names[j], previous[j])) restored = false;
        return restored ? 0 : -1;
    }
    return 1;
}
}
