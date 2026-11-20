// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace MirvPovCvars {
bool ReadInteger(const char *name, int32_t &value);
bool WriteInteger(const char *name, int32_t value);
bool ReadNumeric(const char *name, float &value);
inline constexpr std::array Names{
    "cl_radar_square_when_spectating", "cl_radar_square_always",
    "cl_radar_show_all_players_when_spectating", "cl_trueview_show_status",
    "r_show_build_info"
};

class Settings {
    static constexpr std::size_t kRadarCount = 3;
    using Values = std::array<int32_t, Names.size()>;
    std::optional<Values> beforePov_;

public:
    // Snapshot the overlay preferences once. Radar keeps its spectator defaults.
    // Return -1 if a failed write could not be rolled back.
    template<class Access> int Apply(Access &access, bool enabled) {
        Values previous{}, targets{};
        const auto count = enabled || beforePov_ ? previous.size() : kRadarCount;
        for(std::size_t i = 0; i < count; ++i)
            if(!access.Read(Names[i], previous[i], targets[i])) return 0;

        const bool rememberOverlay = enabled && !beforePov_;
        if(rememberOverlay) beforePov_ = previous;
        if(!enabled) {
            targets[2] = 1;
            for(std::size_t i = kRadarCount; i < count; ++i) targets[i] = (*beforePov_)[i];
        }
        for(std::size_t i = 0; i < count; ++i) {
            const int32_t target = enabled ? 0 : targets[i];
            if(previous[i] == target || access.Write(Names[i], target)) continue;
            bool restored = true;
            for(std::size_t j = 0; j < i; ++j)
                if(!access.Write(Names[j], previous[j])) restored = false;
            if(restored && rememberOverlay) beforePov_.reset();
            return restored ? 0 : -1;
        }
        if(!enabled) beforePov_.reset();
        return 1;
    }
};
}
