// Simulation.h — hybrid tick: proximity + social graph + platform mechanics + scenario system.
//
// Tick pipeline (tickProximity):
//   1. Buffer copy
//   2. Proximity spread  (screen distance < radius)
//   3. Graph spread      (social graph edges — platform-mediated, async)
//   4. TikTok FYP        (algorithm bypasses graph, reaches random susceptibles)
//   5. YouTube hardening (correction_resistance ratchets up while infected)
//   6. Virality burst    (low-prob spontaneous viral event)
//   7. Exposure resolution
//   8. Natural recovery
//   9. Overload timer reset
//  10. Crisis decay
//  11. Scenario auto-events
//  12. Newly-infected collection + buffer swap
#pragma once
#include "World.h"
#include <SFML/System/Vector2.hpp>
#include <vector>
#include <fstream>
#include <string>
#include <random>
#include <unordered_map>

// ── Scenario system ────────────────────────────────────────────────────────
enum class ScenarioType : uint8_t {
    NONE = 0, CONTROLLED_BURN = 1, SILENT_WILDFIRE = 2, PERFECT_STORM = 3
};

struct ScenarioEvent {
    int  tick   = 0;
    enum class Type : uint8_t { CRISIS=0, FACTCHECK=1, BOTNET=2 } type;
    float param1 = 0.f;   // crisis: intensity;  factcheck: effectiveness
    int   param2 = 0;     // crisis: duration;   factcheck: (int)Platform
};

struct ScenarioConfig {
    ScenarioType type            = ScenarioType::NONE;
    const char*  name            = "Custom";
    float        emotional_valence = 0.70f;
    float        plausibility      = 0.65f;
    bool         via_influencer    = false;
    std::vector<ScenarioEvent> events;

    static ScenarioConfig make(ScenarioType t);
};

// ── Misinformation packet ──────────────────────────────────────────────────
struct MisInfoPacket {
    float emotional_valence  = 0.0f;
    float plausibility       = 0.0f;
    bool  active             = false;
};

// ── Simulation event (read by main loop → Renderer) ───────────────────────
struct SimEvent { std::string msg; bool is_alert; };

// ── Simulation ─────────────────────────────────────────────────────────────
class Simulation {
public:
    World& world;
    MisInfoPacket  current_packet;
    ScenarioConfig active_scenario;

    std::vector<BeliefState> current_states;
    std::vector<BeliefState> next_states;
    std::vector<uint8_t>     exposure_timers;

    uint32_t current_tick = 0;

    static constexpr int   EXPOSURE_WINDOW    = 8;
    static constexpr int   OVERLOAD_THRESHOLD = 5;
    static constexpr float RECOVERY_RATE      = 0.015f;

    float crisis_decay_rate      = 0.0f;
    int   crisis_remaining_ticks = 0;

    // Debunking half-life: each successive fact-check on same platform is 20% weaker.
    std::unordered_map<int, int> factcheck_deploy_count;

    // Set each tick — tells renderer to flash the sim panel this frame.
    bool virality_flash_this_tick = false;

    // Pre-allocated bot IDs populated by main after NetworkBuilder runs.
    std::vector<uint32_t> bot_agent_ids;

    // Auto-events fired this tick by scenario system; cleared at start of each tick.
    std::vector<SimEvent> auto_events_this_tick;

    explicit Simulation(World& w);

    // Legacy graph-only tick; superseded by tickProximity() which runs both graph and proximity spread.
    void tick();

    // Hybrid tick used by country mode. Runs all 12 phases.
    void tickProximity(const std::vector<sf::Vector2f>& positions, float radius);

    // Interventions
    void injectMisinformation(uint32_t patient_zero_id,
                              float emotional_valence,
                              float plausibility,
                              bool via_influencer);

    // Applies debunking half-life automatically.
    void injectFactCheck(float effectiveness, Platform reach_platform);

    void triggerCrisisEvent(float intensity, int duration_ticks);

    // Marks all bot_agent_ids as INFECTED immediately.
    void activateBotnet();

    // Resets simulation state and re-injects misinformation per scenario config.
    void loadScenario(ScenarioType t);

    void logStats(std::ofstream& log_file);
    std::string platformName(Platform p) const;

    // Populated each tick — consumed by main loop for visual effects.
    std::vector<uint32_t> newly_infected;

    struct CrossZoneEvent { uint16_t src_zone; uint16_t dst_zone; };
    std::vector<CrossZoneEvent> cross_zone_events;

    // src == UINT32_MAX means TikTok FYP (no source agent).
    struct TransmissionRecord { uint32_t src; uint32_t dst; Platform plat; };
    std::vector<TransmissionRecord> transmissions_this_tick;

private:
    void processScenarioEvents();

    std::vector<uint32_t> susceptible_pool;
    uint32_t              last_pool_rebuild_tick = 999999;
    void     rebuildSusceptiblePool();
    uint32_t weightedSelectFromPool(std::mt19937& rng);
};
