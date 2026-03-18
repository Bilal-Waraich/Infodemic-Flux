// Simulation.cpp — 12-phase hybrid tick + scenario system + platform mechanics.
#include "Simulation.h"
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include <thread>
#include <cmath>

// ── ScenarioConfig::make ───────────────────────────────────────────────────
ScenarioConfig ScenarioConfig::make(ScenarioType t) {
    ScenarioConfig cfg;
    cfg.type = t;
    switch (t) {
        case ScenarioType::CONTROLLED_BURN:
            cfg.name             = "Controlled Burn";
            cfg.emotional_valence = 0.25f;
            cfg.plausibility      = 0.45f;
            cfg.via_influencer    = false;
            // Fact-check arrives early via Twitter — watch partial correction
            cfg.events = {
                {80, ScenarioEvent::Type::FACTCHECK, 0.80f, (int)Platform::TWITTER}
            };
            break;

        case ScenarioType::SILENT_WILDFIRE:
            cfg.name             = "Silent Wildfire";
            cfg.emotional_valence = 0.55f;
            cfg.plausibility      = 0.70f;
            cfg.via_influencer    = false;
            // Botnet activates at tick 50; WhatsApp correction at 150 (near-useless)
            cfg.events = {
                {50,  ScenarioEvent::Type::BOTNET,    0.0f,  0},
                {150, ScenarioEvent::Type::FACTCHECK, 0.60f, (int)Platform::WHATSAPP}
            };
            break;

        case ScenarioType::PERFECT_STORM:
            cfg.name             = "Perfect Storm";
            cfg.emotional_valence = 0.85f;
            cfg.plausibility      = 0.80f;
            cfg.via_influencer    = true;
            // Crisis spikes fear → bots amplify → late correction
            cfg.events = {
                {50,  ScenarioEvent::Type::CRISIS,    0.65f, 80},
                {60,  ScenarioEvent::Type::BOTNET,    0.0f,  0},
                {200, ScenarioEvent::Type::FACTCHECK, 0.60f, (int)Platform::TWITTER}
            };
            break;

        default:
            cfg.name = "Custom";
            break;
    }
    return cfg;
}

// ── Constructor ────────────────────────────────────────────────────────────
Simulation::Simulation(World& w) : world(w) {
    size_t N = w.agents.size();
    current_states.resize(N);
    next_states.resize(N);
    exposure_timers.resize(N, 0);
    for (size_t i = 0; i < N; ++i)
        current_states[i] = w.agents[i].state;
    current_packet.active = false;
}

// ── Old network-only tick (preserved, not called in country mode) ──────────
void Simulation::tick() {
    size_t N = world.agents.size();
    newly_infected.clear();
    cross_zone_events.clear();
    next_states = current_states;

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic, 256)
    #endif
    for (int i = 0; i < (int)N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;
        const Agent& ai = world.agents[i];
        thread_local std::mt19937 rng(
            std::random_device{}() ^ (std::hash<std::thread::id>{}(
                std::this_thread::get_id()) * 0x9e3779b9u));
        thread_local std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (uint32_t j : world.adjacency_list[i]) {
            if (current_states[j] != BeliefState::SUSCEPTIBLE) continue;
            const Agent& aj = world.agents[j];
            Platform tx = Platform::UNKNOWN;
            for (int pa = 0; pa < ai.platform_count && tx == Platform::UNKNOWN; ++pa)
                for (int pb = 0; pb < aj.platform_count; ++pb)
                    if (ai.platforms[pa] == aj.platforms[pb]) { tx = ai.platforms[pa]; break; }
            if (tx == Platform::UNKNOWN) tx = ai.platforms[0];
            const PlatformProfile& prof = world.platform_profiles.at(tx);
            float p = ai.infectionProbability(current_packet.emotional_valence,
                                              prof.trust_multiplier, prof.algorithm_amp);
            if (tx == Platform::WHATSAPP) p *= 1.5f;
            if (tx == Platform::TWITTER)  p *= 0.6f;
            if (tx == Platform::REDDIT)   p *= 0.5f;
            if (dist(rng) < p) {
                next_states[j] = BeliefState::EXPOSED;
                #ifdef _OPENMP
                #pragma omp atomic
                #endif
                world.transmissions_by_platform[(int)tx]++;
                world.agents[j].overload_timer++;
                if (world.agents[i].zone_id != world.agents[j].zone_id)
                    cross_zone_events.push_back({(uint16_t)world.agents[i].zone_id,
                                                 (uint16_t)world.agents[j].zone_id});
            }
        }
    }

    // TikTok FYP
    if (current_packet.active) {
        int tiktok_infected = 0;
        for (size_t i = 0; i < N; ++i) {
            if (current_states[i] != BeliefState::INFECTED) continue;
            for (int p = 0; p < world.agents[i].platform_count; ++p)
                if (world.agents[i].platforms[p] == Platform::TIKTOK) { tiktok_infected++; break; }
        }
        if (tiktok_infected > 0) {
            const PlatformProfile& tprof = world.platform_profiles.at(Platform::TIKTOK);
            int expected = (int)(tiktok_infected * 0.001f * tprof.algorithm_amp);
            if (expected > 0) {
                if (current_tick % 20 == 0 || susceptible_pool.empty()) rebuildSusceptiblePool();
                std::mt19937 rng(current_tick ^ 0xDEADBEEF);
                for (int k = 0; k < expected && !susceptible_pool.empty(); ++k) {
                    uint32_t target = weightedSelectFromPool(rng);
                    next_states[target] = BeliefState::EXPOSED;
                    world.transmissions_by_platform[(int)Platform::TIKTOK]++;
                }
            }
        }
    }

    // YouTube entrenchment
    for (size_t i = 0; i < N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;
        Agent& a = world.agents[i];
        for (int p = 0; p < a.platform_count; ++p)
            if (a.platforms[p] == Platform::YOUTUBE) {
                a.correction_resistance = std::min(1.0f, a.correction_resistance + 0.0015f);
                break;
            }
    }

    // Exposure resolution
    for (size_t i = 0; i < N; ++i) {
        if (next_states[i] != BeliefState::EXPOSED) continue;
        exposure_timers[i]++;
        if (exposure_timers[i] >= EXPOSURE_WINDOW) {
            Agent& a = world.agents[i];
            float p_infect = a.infectionProbability(current_packet.emotional_valence, 1.0f, 1.0f);
            std::mt19937 rng(i ^ current_tick);
            if (std::uniform_real_distribution<float>(0.f,1.f)(rng) < p_infect)
                next_states[i] = BeliefState::INFECTED;
            else { next_states[i] = BeliefState::SUSCEPTIBLE; a.prior_exposure_count++; }
            exposure_timers[i] = 0;
        }
    }

    // Recovery
    for (size_t i = 0; i < N; ++i) {
        if (next_states[i] != BeliefState::INFECTED) continue;
        Agent& a = world.agents[i];
        float p_recover = RECOVERY_RATE * a.literacy_score * (1.0f - a.correction_resistance);
        std::mt19937 rng(i ^ (current_tick * 1000003u));
        if (std::uniform_real_distribution<float>(0.f,1.f)(rng) < p_recover)
            next_states[i] = BeliefState::RECOVERED;
    }

    for (auto& a : world.agents) a.overload_timer = 0;

    if (crisis_remaining_ticks > 0) {
        for (auto& a : world.agents)
            a.emotional_state = std::max(0.0f, a.emotional_state - crisis_decay_rate);
        crisis_remaining_ticks--;
    }

    for (size_t i = 0; i < N; ++i)
        if (next_states[i] == BeliefState::INFECTED &&
            current_states[i] != BeliefState::INFECTED)
            newly_infected.push_back((uint32_t)i);

    std::swap(current_states, next_states);
    for (size_t i = 0; i < N; ++i) world.agents[i].state = current_states[i];
    current_tick++;
}

// ── Hybrid tick ────────────────────────────────────────────────────────────
void Simulation::tickProximity(const std::vector<sf::Vector2f>& positions, float radius) {
    size_t N = world.agents.size();
    transmissions_this_tick.clear();
    newly_infected.clear();
    auto_events_this_tick.clear();
    virality_flash_this_tick = false;

    // Phase 1: copy
    next_states = current_states;

    std::mt19937 rng(current_tick ^ 0xABCDEF01);
    std::uniform_real_distribution<float> dist(0.f, 1.f);

    // Phase 2: proximity spread (face-to-face / physical community)
    const float r2 = radius * radius;
    for (size_t i = 0; i < N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;
        const Agent& ai = world.agents[i];

        for (size_t j = 0; j < N; ++j) {
            if (i == j) continue;
            if (current_states[j] != BeliefState::SUSCEPTIBLE) continue;

            if (i < positions.size() && j < positions.size()) {
                float dx = positions[i].x - positions[j].x;
                float dy = positions[i].y - positions[j].y;
                if (dx*dx + dy*dy > r2) continue;
            }

            // Find shared platform (same logic as graph spread)
            const Agent& aj = world.agents[j];
            Platform tx = Platform::UNKNOWN;
            for (int pa = 0; pa < ai.platform_count && tx == Platform::UNKNOWN; ++pa)
                for (int pb = 0; pb < aj.platform_count; ++pb)
                    if (ai.platforms[pa] == aj.platforms[pb]) { tx = ai.platforms[pa]; break; }
            if (tx == Platform::UNKNOWN) tx = ai.platforms[0];

            const PlatformProfile& prof = world.platform_profiles.at(tx);
            float p = ai.infectionProbability(current_packet.emotional_valence,
                                              prof.trust_multiplier, prof.algorithm_amp);
            if (tx == Platform::WHATSAPP) p *= 1.5f;
            if (tx == Platform::TWITTER)  p *= 0.6f;
            if (tx == Platform::REDDIT)   p *= 0.5f;

            if (dist(rng) < p) {
                next_states[j] = BeliefState::EXPOSED;
                world.transmissions_by_platform[(int)tx]++;
                transmissions_this_tick.push_back({(uint32_t)i, (uint32_t)j, tx});
            }
        }
    }

    // Phase 3: social graph spread (platform-mediated, distance-independent)
    // Scaled down vs proximity — digital async contact carries less immediacy.
    if (current_packet.active) {
        for (size_t i = 0; i < N; ++i) {
            if (current_states[i] != BeliefState::INFECTED) continue;
            const Agent& ai = world.agents[i];

            for (uint32_t j : world.adjacency_list[i]) {
                if (j >= N) continue;
                if (current_states[j] != BeliefState::SUSCEPTIBLE) continue;

                // Find best shared platform
                Platform tx = Platform::UNKNOWN;
                for (int pa = 0; pa < ai.platform_count && tx == Platform::UNKNOWN; ++pa)
                    for (int pb = 0; pb < world.agents[j].platform_count; ++pb)
                        if (ai.platforms[pa] == world.agents[j].platforms[pb]) {
                            tx = ai.platforms[pa]; break;
                        }
                if (tx == Platform::UNKNOWN) tx = ai.platforms[0];

                const PlatformProfile& prof = world.platform_profiles.at(tx);
                float p = ai.infectionProbability(current_packet.emotional_valence,
                                                  prof.trust_multiplier, prof.algorithm_amp);

                // Graph spread is attenuated (async digital vs in-person)
                // WhatsApp retains high trust; Twitter and Reddit still suppressed.
                p *= 0.18f;
                if (tx == Platform::WHATSAPP) p *= 1.5f;
                if (tx == Platform::TWITTER)  p *= 0.6f;
                if (tx == Platform::REDDIT)   p *= 0.5f;

                if (dist(rng) < p) {
                    next_states[j] = BeliefState::EXPOSED;
                    world.transmissions_by_platform[(int)tx]++;
                    world.agents[j].overload_timer++;
                    transmissions_this_tick.push_back({(uint32_t)i, (uint32_t)j, tx});
                }
            }
        }
    }

    // Phase 4: TikTok FYP — algorithmic reach bypasses social graph entirely.
    // Models the For You Page delivering content to strangers.
    if (current_packet.active) {
        int tiktok_infected = 0;
        for (size_t i = 0; i < N; ++i) {
            if (current_states[i] != BeliefState::INFECTED) continue;
            for (int p = 0; p < world.agents[i].platform_count; ++p)
                if (world.agents[i].platforms[p] == Platform::TIKTOK) { tiktok_infected++; break; }
        }

        // 10% chance per tick of reaching one susceptible agent when ≥1 TikTok infected.
        if (tiktok_infected > 0 && dist(rng) < 0.10f * tiktok_infected) {
            if (current_tick % 5 == 0 || susceptible_pool.empty()) rebuildSusceptiblePool();
            if (!susceptible_pool.empty()) {
                uint32_t target = weightedSelectFromPool(rng);
                next_states[target] = BeliefState::EXPOSED;
                world.transmissions_by_platform[(int)Platform::TIKTOK]++;
                // src = UINT32_MAX signals FYP (no source agent)
                transmissions_this_tick.push_back({UINT32_MAX, target, Platform::TIKTOK});
                virality_flash_this_tick = true;
            }
        }
    }

    // Phase 5: YouTube entrenchment — rabbit-hole hardens beliefs over time.
    // correction_resistance ratchets monotonically while infected on YouTube.
    for (size_t i = 0; i < N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;
        Agent& a = world.agents[i];
        for (int p = 0; p < a.platform_count; ++p)
            if (a.platforms[p] == Platform::YOUTUBE) {
                a.correction_resistance = std::min(1.0f, a.correction_resistance + 0.002f);
                break;
            }
    }

    // Phase 6: spontaneous virality burst — rare but dramatically visible.
    // Higher chance during crisis (fear/anger drives sharing).
    if (current_packet.active) {
        float burst_p = crisis_remaining_ticks > 0 ? 0.025f : 0.004f;
        if (dist(rng) < burst_p) {
            // Instantly expose all susceptible TikTok and Twitter users
            for (size_t i = 0; i < N; ++i) {
                if (next_states[i] != BeliefState::SUSCEPTIBLE) continue;
                for (int p = 0; p < world.agents[i].platform_count; ++p) {
                    Platform plat = world.agents[i].platforms[p];
                    if (plat == Platform::TIKTOK || plat == Platform::TWITTER) {
                        next_states[i] = BeliefState::EXPOSED;
                        world.transmissions_by_platform[(int)plat]++;
                        virality_flash_this_tick = true;
                        break;
                    }
                }
            }
            auto_events_this_tick.push_back({"Viral burst — post went mass-reach", true});
        }
    }

    // Phase 7: exposure resolution
    for (size_t i = 0; i < N; ++i) {
        if (next_states[i] != BeliefState::EXPOSED) continue;
        exposure_timers[i]++;
        if (exposure_timers[i] >= EXPOSURE_WINDOW) {
            Agent& a = world.agents[i];
            float p_infect = a.infectionProbability(current_packet.emotional_valence, 1.f, 1.f);
            std::mt19937 r2(i ^ current_tick);
            if (std::uniform_real_distribution<float>(0.f,1.f)(r2) < p_infect)
                next_states[i] = BeliefState::INFECTED;
            else {
                next_states[i] = BeliefState::SUSCEPTIBLE;
                a.prior_exposure_count++;
            }
            exposure_timers[i] = 0;
        }
    }

    // Phase 8: natural recovery
    // Agents with high literacy who've been exposed multiple times develop
    // epistemic immunity — they can never be re-infected.
    for (size_t i = 0; i < N; ++i) {
        if (next_states[i] != BeliefState::INFECTED) continue;
        Agent& a = world.agents[i];
        float p_recover = RECOVERY_RATE * a.literacy_score * (1.f - a.correction_resistance);
        std::mt19937 r3(i ^ (current_tick * 1000003u));
        if (std::uniform_real_distribution<float>(0.f,1.f)(r3) < p_recover) {
            // IMMUNE: prior multi-exposure + high literacy = permanently inoculated
            if (a.prior_exposure_count >= 2 && a.literacy_score > 0.65f)
                next_states[i] = BeliefState::IMMUNE;
            else
                next_states[i] = BeliefState::RECOVERED;
        }
    }

    // Phase 9: overload timer reset
    for (auto& a : world.agents) a.overload_timer = 0;

    // Phase 10: crisis decay
    if (crisis_remaining_ticks > 0) {
        for (auto& a : world.agents)
            a.emotional_state = std::max(0.f, a.emotional_state - crisis_decay_rate);
        crisis_remaining_ticks--;
    }

    // Phase 11: scenario auto-events
    processScenarioEvents();

    // Phase 12: collect newly infected, swap buffers
    for (size_t i = 0; i < N; ++i)
        if (next_states[i] == BeliefState::INFECTED &&
            current_states[i] != BeliefState::INFECTED)
            newly_infected.push_back((uint32_t)i);

    std::swap(current_states, next_states);
    for (size_t i = 0; i < N; ++i) world.agents[i].state = current_states[i];
    current_tick++;
}

// ── processScenarioEvents ──────────────────────────────────────────────────
void Simulation::processScenarioEvents() {
    if (active_scenario.type == ScenarioType::NONE) return;
    for (const auto& ev : active_scenario.events) {
        if ((int)current_tick != ev.tick) continue;
        switch (ev.type) {
            case ScenarioEvent::Type::CRISIS:
                triggerCrisisEvent(ev.param1, ev.param2);
                auto_events_this_tick.push_back({"Scenario: crisis event fired", true});
                break;
            case ScenarioEvent::Type::FACTCHECK: {
                Platform fp = (Platform)ev.param2;
                injectFactCheck(ev.param1, fp);
                auto_events_this_tick.push_back(
                    {"Scenario: fact-check via " + platformName(fp), false});
                break;
            }
            case ScenarioEvent::Type::BOTNET:
                activateBotnet();
                auto_events_this_tick.push_back({"Scenario: botnet activated", true});
                break;
        }
    }
}

// ── Interventions ──────────────────────────────────────────────────────────
void Simulation::injectMisinformation(uint32_t patient_zero_id,
                                      float emotional_valence,
                                      float plausibility,
                                      bool via_influencer) {
    current_packet.emotional_valence = emotional_valence;
    current_packet.plausibility      = plausibility;
    current_packet.active            = true;

    if (patient_zero_id < current_states.size()) {
        current_states[patient_zero_id]     = BeliefState::INFECTED;
        world.agents[patient_zero_id].state = BeliefState::INFECTED;
    }

    int extra = 0;
    if (via_influencer) {
        // Seed top-5 influence neighbours for coordinated influencer launch.
        auto& nbrs = world.adjacency_list[patient_zero_id];
        std::vector<uint32_t> sorted = nbrs;
        std::sort(sorted.begin(), sorted.end(), [&](uint32_t a, uint32_t b) {
            return world.agents[a].social_influence > world.agents[b].social_influence;
        });
        int top_n = std::min((int)sorted.size(), 5);
        for (int i = 0; i < top_n; ++i) {
            world.agents[sorted[i]].state = BeliefState::INFECTED;
            current_states[sorted[i]]     = BeliefState::INFECTED;
            extra++;
        }
    }

    std::cout << "Tick " << current_tick << ": Misinformation injected"
              << "  valence=" << emotional_valence
              << "  plausibility=" << plausibility
              << "  via_influencer=" << via_influencer
              << " (+" << extra << " neighbours)\n";
}

void Simulation::injectFactCheck(float effectiveness, Platform reach_platform) {
    // Debunking half-life: each re-deployment is 20% less effective.
    int& count = factcheck_deploy_count[(int)reach_platform];
    float adj_eff = effectiveness * std::pow(0.80f, (float)count);
    count++;

    if (adj_eff < 0.01f) {
        std::cout << "Tick " << current_tick << ": Fact-check audience exhausted on "
                  << platformName(reach_platform) << " (deploy #" << count << ")\n";
        return;
    }

    const PlatformProfile& prof = world.platform_profiles.at(reach_platform);
    float base = adj_eff * prof.correction_reach;
    int recovered = 0;
    size_t N = world.agents.size();

    for (size_t i = 0; i < N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;
        const Agent& a = world.agents[i];
        float rc = base * a.literacy_score * (1.f - a.correction_resistance);
        if (reach_platform == Platform::WHATSAPP) rc *= 0.08f;
        if (reach_platform == Platform::TIKTOK)   rc *= 0.40f;

        std::mt19937 rng(i ^ current_tick ^ 0xC0FFEE);
        if (std::uniform_real_distribution<float>(0.f,1.f)(rng) < rc) {
            current_states[i]     = BeliefState::RECOVERED;
            world.agents[i].state = BeliefState::RECOVERED;
            recovered++;
        }
    }

    std::cout << "Tick " << current_tick
              << ": Fact-check via " << platformName(reach_platform)
              << " (deploy #" << count << ", eff=" << adj_eff << ")"
              << "  " << recovered << " recovered\n";
}

void Simulation::triggerCrisisEvent(float intensity, int duration_ticks) {
    for (auto& a : world.agents)
        a.emotional_state = std::min(1.0f, a.emotional_state + intensity * 0.45f);
    crisis_decay_rate      = (intensity * 0.45f) / (float)duration_ticks;
    crisis_remaining_ticks = duration_ticks;
    std::cout << "Tick " << current_tick << ": Crisis  intensity=" << intensity
              << "  duration=" << duration_ticks << " ticks\n";
}

void Simulation::activateBotnet() {
    for (uint32_t idx : bot_agent_ids) {
        if (idx >= current_states.size()) continue;
        current_states[idx]       = BeliefState::INFECTED;
        world.agents[idx].state   = BeliefState::INFECTED;
        newly_infected.push_back(idx);
    }
    std::cout << "Tick " << current_tick << ": Botnet activated ("
              << bot_agent_ids.size() << " bots)\n";
}

void Simulation::loadScenario(ScenarioType t) {
    active_scenario = ScenarioConfig::make(t);
    size_t N = world.agents.size();

    // Reset all agent belief states and decay fields
    for (size_t i = 0; i < N; ++i) {
        current_states[i]           = BeliefState::SUSCEPTIBLE;
        next_states[i]              = BeliefState::SUSCEPTIBLE;
        world.agents[i].state       = BeliefState::SUSCEPTIBLE;
        exposure_timers[i]          = 0;
        world.agents[i].overload_timer      = 0;
        world.agents[i].correction_resistance = 0.f;
        world.agents[i].emotional_state     = 0.2f;
        world.agents[i].prior_exposure_count = 0;
    }

    // Perfect Storm: amplify polarization and confirmation bias
    if (t == ScenarioType::PERFECT_STORM) {
        for (auto& a : world.agents) {
            if (a.is_bot) continue;
            a.polarization      = std::min(1.f, a.polarization + 0.30f);
            a.confirmation_bias = std::min(1.f, a.confirmation_bias + 0.25f);
        }
    }

    current_tick            = 0;
    crisis_remaining_ticks  = 0;
    crisis_decay_rate       = 0.f;
    factcheck_deploy_count.clear();
    auto_events_this_tick.clear();
    newly_infected.clear();
    transmissions_this_tick.clear();
    cross_zone_events.clear();
    susceptible_pool.clear();
    last_pool_rebuild_tick = 999999;
    std::fill(std::begin(world.transmissions_by_platform),
              std::end(world.transmissions_by_platform), 0);

    // Inject misinformation with scenario parameters
    injectMisinformation(0, active_scenario.emotional_valence,
                            active_scenario.plausibility,
                            active_scenario.via_influencer);

    std::cout << "Scenario loaded: " << active_scenario.name
              << "  events=" << active_scenario.events.size() << "\n";
}

// ── Pool helpers ───────────────────────────────────────────────────────────
void Simulation::rebuildSusceptiblePool() {
    susceptible_pool.clear();
    for (size_t i = 0; i < world.agents.size(); ++i)
        if (current_states[i] == BeliefState::SUSCEPTIBLE)
            susceptible_pool.push_back((uint32_t)i);
    last_pool_rebuild_tick = current_tick;
}

uint32_t Simulation::weightedSelectFromPool(std::mt19937& rng) {
    float total = 0.f;
    for (uint32_t idx : susceptible_pool)
        total += world.zones[world.agents[idx].zone_id].internet_penetration + 0.01f;

    std::uniform_real_distribution<float> d(0.f, total);
    float threshold = d(rng), cumul = 0.f;

    for (size_t k = 0; k < susceptible_pool.size(); ++k) {
        cumul += world.zones[world.agents[susceptible_pool[k]].zone_id].internet_penetration + 0.01f;
        if (cumul >= threshold) {
            uint32_t sel = susceptible_pool[k];
            susceptible_pool[k] = susceptible_pool.back();
            susceptible_pool.pop_back();
            return sel;
        }
    }
    uint32_t sel = susceptible_pool.back();
    susceptible_pool.pop_back();
    return sel;
}

// ── platformName / logStats ────────────────────────────────────────────────
std::string Simulation::platformName(Platform p) const {
    switch(p) {
        case Platform::FACEBOOK:  return "Facebook";
        case Platform::WHATSAPP:  return "WhatsApp";
        case Platform::TIKTOK:    return "TikTok";
        case Platform::TWITTER:   return "Twitter";
        case Platform::YOUTUBE:   return "YouTube";
        case Platform::REDDIT:    return "Reddit";
        case Platform::WECHAT:    return "WeChat";
        case Platform::TELEGRAM:  return "Telegram";
        default:                  return "Unknown";
    }
}

void Simulation::logStats(std::ofstream& log_file) {
    if (!log_file.is_open()) return;
    if (current_tick == 1) {
        log_file << "tick,susceptible,exposed,infected,recovered";
        for (const auto& z : world.zones) log_file << ",\"" << z.name << "_infected\"";
        for (int p = 0; p <= (int)Platform::UNKNOWN; ++p)
            log_file << ",tx_" << platformName((Platform)p);
        log_file << "\n";
    }
    log_file << current_tick
             << "," << world.countByState(BeliefState::SUSCEPTIBLE)
             << "," << world.countByState(BeliefState::EXPOSED)
             << "," << world.countByState(BeliefState::INFECTED)
             << "," << world.countByState(BeliefState::RECOVERED);
    for (const auto& z : world.zones)
        log_file << "," << world.countByStateInZone(BeliefState::INFECTED, z.id);
    for (int p = 0; p <= (int)Platform::UNKNOWN; ++p)
        log_file << "," << world.transmissions_by_platform[p];
    log_file << "\n";
    if (current_tick % 50 == 0) log_file.flush();
}
