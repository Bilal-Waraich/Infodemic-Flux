// Agent.h — 64-byte agent struct, belief states, and infection probability formula.
#pragma once

#include <cstdint>
#include <algorithm>
#include <array>
#include "PlatformProfile.h"

enum class BeliefState : uint8_t {
    SUSCEPTIBLE=0, EXPOSED=1, INFECTED=2, RECOVERED=3, IMMUNE=4
};

// without pack: 2-byte padding after zone_id(uint16) before first float pushes struct past 64 bytes
#pragma pack(push, 1)
struct alignas(64) Agent {
    // --- Identity: 6 bytes ---
    uint32_t id;
    uint16_t zone_id;

    // --- Psychological profile: 28 bytes ---
    float literacy_score;       // 0-1; World Bank adult literacy for this zone
    float press_freedom;        // 0-1; RSF press freedom; low = state controls narrative
    float polarization;         // 0-1; V-Dem score; high = society split into hostile camps
    float institutional_trust;  // derived: literacy*0.4 + press_freedom*0.6
    float sharing_habit;        // 0-1; compulsive re-sharing, independent of literacy
    float confirmation_bias;    // 0-1; seeded from agent's own polarization, not zone average
    float emotional_state;      // 0-1; elevated by crisis events; decays each tick

    // --- Social profile: 8 bytes ---
    float social_influence;       // 0-1; log-normal distributed — produces influencer power-law tail
    float correction_resistance;  // 0-1; YouTube entrenchment ratchets this up by 0.0015/tick while infected

    // --- Belief state: 4 bytes ---
    BeliefState state;
    uint8_t exposure_timer;       // ticks since becoming EXPOSED
    uint8_t overload_timer;       // messages received this tick; >5 triggers reflexive sharing
    uint8_t prior_exposure_count; // times previously exposed then rejected — primes future credulity

    // --- Platform membership: 4 bytes (3B + 1B) ---
    Platform platforms[3];
    uint8_t platform_count;

    // --- Flags: 1 byte ---
    bool is_bot; // bots: sharing_habit=1, literacy=0.01, correction_resistance=0.99

    // --- Padding: 13 bytes to reach exactly 64 bytes (one cache line on M-series / x86-64) ---
    uint8_t _pad[13];

    // Infection probability formula:
    //   base      = (1 - literacy) * trust_mult * algo_amp
    //   emotional = 1 + (valence * emotional_state * 0.6)
    //   habit     = 1 + (sharing_habit * 0.4)
    //   bias      = 1 + (confirmation_bias * 0.3)
    //   trust_pen = institutional_trust * 0.35   [subtractive: competing heuristic, not a scale factor]
    //   overload  = 1.3 if overload_timer > 5    [information fatigue → reflexive acceptance]
    //   result    = clamp(base*emotional*habit*bias*overload - trust_pen, 0, 1)
    float infectionProbability(
        float emotional_valence,
        float platform_trust_mult,
        float platform_algorithm_amp) const noexcept
    {
        float base         = (1.0f - literacy_score) * platform_trust_mult * platform_algorithm_amp;
        float emotional_amp = 1.0f + (emotional_valence * emotional_state * 0.6f);
        float habit_amp     = 1.0f + (sharing_habit * 0.4f);
        float bias_amp      = 1.0f + (confirmation_bias * 0.3f);
        float trust_penalty = institutional_trust * 0.35f;
        float overload_amp  = (overload_timer > 5) ? 1.3f : 1.0f;
        float result = base * emotional_amp * habit_amp * bias_amp * overload_amp - trust_penalty;
        return std::clamp(result, 0.0f, 1.0f);
    }
};
#pragma pack(pop)

static_assert(sizeof(Agent) == 64, "Agent must be exactly 64 bytes for cache line alignment");
