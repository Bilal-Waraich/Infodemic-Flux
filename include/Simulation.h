// Simulation.h — tick loop, belief state propagation, interventions.
//
// Double-buffered: agents read current_states, write to next_states. Swap at end of tick.
// Makes the OpenMP parallel loop race-free without mutex locks.
#pragma once
#include "World.h"
#include <vector>
#include <fstream>
#include <string>
#include <random>

struct MisInfoPacket {
    float emotional_valence  = 0.0f;
    float plausibility       = 0.0f;
    bool  active             = false;
};

class Simulation {
public:
    World& world;
    MisInfoPacket current_packet;

    std::vector<BeliefState> current_states;
    std::vector<BeliefState> next_states;
    std::vector<uint8_t>     exposure_timers;

    uint32_t current_tick = 0;

    static constexpr int   EXPOSURE_WINDOW    = 8;
    static constexpr int   OVERLOAD_THRESHOLD = 5;
    static constexpr float RECOVERY_RATE      = 0.015f;

    float crisis_decay_rate      = 0.0f;
    int   crisis_remaining_ticks = 0;

    explicit Simulation(World& w);

    void tick();

    // If via_influencer=true, also seeds the 5 highest-influence neighbours immediately.
    void injectMisinformation(uint32_t patient_zero_id,
                              float emotional_valence,
                              float plausibility,
                              bool via_influencer);

    // P(recover) = effectiveness * correction_reach * literacy * (1 - correction_resistance).
    // WhatsApp and TikTok apply additional reach penalties (see Simulation.cpp).
    void injectFactCheck(float effectiveness, Platform reach_platform);

    void triggerCrisisEvent(float intensity, int duration_ticks);

    // Writes header on tick 1. Zone names are quoted to prevent comma-in-name CSV corruption.
    void logStats(std::ofstream& log_file);

    std::string platformName(Platform p) const;

private:
    // Cache of susceptible agent indices for TikTok batch reach. Rebuilt every 20 ticks.
    std::vector<uint32_t> susceptible_pool;
    uint32_t              last_pool_rebuild_tick = 999999;

    void rebuildSusceptiblePool();

    // Selects proportional to internet_penetration; removes selected agent to prevent double-count.
    uint32_t weightedSelectFromPool(std::mt19937& rng);
};
