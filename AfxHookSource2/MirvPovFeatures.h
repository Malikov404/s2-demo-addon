// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstring>

namespace MirvPovFeatures {
struct State { const char *name; bool configured; bool active; bool immediate = false; };
inline std::array<State, 40> Create() {
    return {{
        {"voice_script", true, true},
        {"cvars", true, true},
        {"teamid", true, true},
        {"soundcircle", true, true},
        {"hud", true, true},
        {"buyzone", true, true, true},
        {"round_damage", true, true, true},
        {"teamhealth", true, true},
        {"hud_target_highlight", false, false, true},
        {"buymenu", true, true},
        {"buymenu_promo_hide", true, true, true},
        {"buymenu_layout", true, true, true},
        {"buymenu_input_passthrough", true, true, true},
        {"buymenu_gloves", true, true, true},
        {"radar", true, true},
        {"voice", true, true},
        {"voice_location", true, true, true},
        {"scoreboard", true, true},
        {"feedback", true, true},
        {"hit_sound", true, true, true},
        {"kill_sound", true, true, true},
        {"deafen", true, true, true},
        {"mvp_music", true, true, true},
        {"damage_direction", true, true, true},
        {"deathpanel_slide", true, true, true},
        {"death_screen", true, true, true},
        {"deathcam", true, true},
        {"pickupprompt", true, true},
        {"killreward", true, true},
        {"radio", true, true},
        {"radio_text_dispatch", true, true},
        {"radio_text_handler", true, true},
        {"radio_sendaudio_dispatch", true, true},
        {"radio_sendaudio_emitter", true, true},
        {"radio_sendaudio_parser", true, true},
        {"radio_rawaudio_dispatch", true, true},
        {"radio_rawaudio_formatter", true, true},
        {"framestage", true, true},
        {"voiceban", true, true},
        {"voiceban_avatars", true, true, true}
    }};
}
inline State *Find(std::array<State, 40> &features, const char *name) {
    if(name) for(auto &feature : features) if(!_stricmp(feature.name, name)) return &feature;
    return nullptr;
}
}
