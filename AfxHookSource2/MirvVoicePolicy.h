#pragma once
// SPDX-License-Identifier: AGPL-3.0-only

#include <charconv>
#include <cstdint>
#include <cstring>

namespace advancedfx {

// User selection is independent of the observed team. Never feed the effective
// mask back into the selection: a POV/team transition must not lose permissions.
class MirvVoicePolicy {
public:
    static constexpr uint64_t All = UINT64_MAX;

    void Set(uint32_t low, uint32_t high) {
        m_Selected = uint64_t(low) | (uint64_t(high) << 32);
        m_Managed = true;
    }
    void Reset() { m_Selected = All; m_Managed = false; }
    bool IsManaged() const { return m_Managed; }
    uint64_t Resolve(bool pov, uint64_t team) const {
        return m_Selected & (pov ? team : All);
    }
    static bool Allows(uint64_t mask, unsigned int slot) {
        return slot < 64 && (mask & (uint64_t(1) << slot)) != 0;
    }
    static bool ParseWord(const char* text, uint32_t& word) {
        int64_t value = 0;
        const auto end = text + std::strlen(text);
        const auto parsed = std::from_chars(text, end, value);
        if(parsed.ec != std::errc() || parsed.ptr != end
            || value < INT32_MIN || value > UINT32_MAX) return false;
        word = static_cast<uint32_t>(value);
        return true;
    }

private:
    uint64_t m_Selected = All;
    bool m_Managed = false;
};

}
