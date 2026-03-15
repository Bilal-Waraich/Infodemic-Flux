// Simulation.cpp — 9-phase tick loop and interventions.
#include "Simulation.h"
#include <algorithm>
#include <numeric>
#include <random>
#include <iostream>
#include <thread>

Simulation::Simulation(World& w) : world(w) {
    size_t N = w.agents.size();
    current_states.resize(N);
    next_states.resize(N);
    exposure_timers.resize(N, 0);
    for (size_t i = 0; i < N; ++i)
        current_states[i] = w.agents[i].state;
    current_packet.active = false;
}

void Simulation::tick() {
    size_t N = world.agents.size();

    // Phase 1: copy current → next so phases 2-6 only write changed agents.
    next_states = current_states;

    // Phase 2: network propagation (hot path, parallelised).
    // Each infected agent tries to expose its susceptible neighbours.
    // Reads current_states; writes next_states. thread_local RNG avoids mutex contention.
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

            // Use first platform shared by sender and receiver; cross-platform transmission is rare.
            Platform tx = Platform::UNKNOWN;
            for (int pa = 0; pa < ai.platform_count && tx == Platform::UNKNOWN; ++pa)
                for (int pb = 0; pb < aj.platform_count; ++pb)
                    if (ai.platforms[pa] == aj.platforms[pb]) {
                        tx = ai.platforms[pa];
                        break;
                    }
            if (tx == Platform::UNKNOWN) tx = ai.platforms[0];

            const PlatformProfile& prof = world.platform_profiles.at(tx);

            float p = ai.infectionProbability(
                current_packet.emotional_valence,
                prof.trust_multiplier,
                prof.algorithm_amp);

            // WhatsApp: +50% — messages arrive from known contacts (high intimacy trust).
            // Twitter:  -40% — adversarial public context raises skepticism.
            // Reddit:   -50% — downvoting and community moderation suppress low-quality claims.
            if (tx == Platform::WHATSAPP) p *= 1.5f;
            if (tx == Platform::TWITTER)  p *= 0.6f;
            if (tx == Platform::REDDIT)   p *= 0.5f;

            if (dist(rng) < p) {
                next_states[j] = BeliefState::EXPOSED;
                #ifdef _OPENMP
                #pragma omp atomic
                #endif
                world.transmissions_by_platform[(int)tx]++;
                // overload_timer write is approximate under OpenMP — benign; timer is capped
                world.agents[j].overload_timer++;
            }
        }
    }

    // Phase 3: TikTok viral reach.
    // TikTok's FYP bypasses the social graph entirely — modelling this as graph traversal
    // would be wrong. Instead: 0.001 * tiktok_infected * algorithm_amp = expected reach/tick.
    if (current_packet.active) {
        int tiktok_infected = 0;
        for (size_t i = 0; i < N; ++i) {
            if (current_states[i] != BeliefState::INFECTED) continue;
            const Agent& a = world.agents[i];
            for (int p = 0; p < a.platform_count; ++p)
                if (a.platforms[p] == Platform::TIKTOK) { tiktok_infected++; break; }
        }

        if (tiktok_infected > 0) {
            const PlatformProfile& tprof = world.platform_profiles.at(Platform::TIKTOK);
            int expected = (int)(tiktok_infected * 0.001f * tprof.algorithm_amp);

            if (expected > 0) {
                if (current_tick % 20 == 0 || susceptible_pool.empty())
                    rebuildSusceptiblePool();

                std::mt19937 rng(current_tick ^ 0xDEADBEEF);
                for (int k = 0; k < expected && !susceptible_pool.empty(); ++k) {
                    uint32_t target = weightedSelectFromPool(rng);
                    next_states[target] = BeliefState::EXPOSED;
                    world.transmissions_by_platform[(int)Platform::TIKTOK]++;
                }
            }
        }
    }

    // Phase 4: YouTube entrenchment.
    // Recommendation rabbit-hole hardens beliefs: correction_resistance ratchets up
    // monotonically while infected on YouTube, never resets.
    for (size_t i = 0; i < N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;
        Agent& a = world.agents[i];
        for (int p = 0; p < a.platform_count; ++p) {
            if (a.platforms[p] == Platform::YOUTUBE) {
                a.correction_resistance = std::min(1.0f, a.correction_resistance + 0.0015f);
                break;
            }
        }
    }

    // Phase 5: exposure resolution.
    // After EXPOSURE_WINDOW ticks, agent decides to accept or reject.
    // Rejection increments prior_exposure_count — primes future credulity.
    for (size_t i = 0; i < N; ++i) {
        if (next_states[i] != BeliefState::EXPOSED) continue;

        exposure_timers[i]++;
        if (exposure_timers[i] >= EXPOSURE_WINDOW) {
            Agent& a = world.agents[i];
            float p_infect = a.infectionProbability(
                current_packet.emotional_valence, 1.0f, 1.0f);

            std::mt19937 rng(i ^ current_tick);
            std::uniform_real_distribution<float> d(0.0f, 1.0f);

            if (d(rng) < p_infect)
                next_states[i] = BeliefState::INFECTED;
            else {
                next_states[i] = BeliefState::SUSCEPTIBLE;
                a.prior_exposure_count++;
            }
            exposure_timers[i] = 0;
        }
    }

    // Phase 6: natural recovery — rate scales with literacy, suppressed by correction_resistance.
    for (size_t i = 0; i < N; ++i) {
        if (next_states[i] != BeliefState::INFECTED) continue;
        Agent& a = world.agents[i];
        float p_recover = RECOVERY_RATE * a.literacy_score * (1.0f - a.correction_resistance);
        std::mt19937 rng(i ^ (current_tick * 1000003u));
        if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < p_recover)
            next_states[i] = BeliefState::RECOVERED;
    }

    // Phase 7: clear overload timers so they accumulate freshly next tick.
    for (auto& a : world.agents)
        a.overload_timer = 0;

    // Phase 8: crisis decay.
    if (crisis_remaining_ticks > 0) {
        for (auto& a : world.agents)
            a.emotional_state = std::max(0.0f, a.emotional_state - crisis_decay_rate);
        crisis_remaining_ticks--;
    }

    // Phase 9: swap buffers; sync authoritative state back to world.agents for renderer.
    std::swap(current_states, next_states);
    for (size_t i = 0; i < N; ++i)
        world.agents[i].state = current_states[i];

    current_tick++;
}

void Simulation::rebuildSusceptiblePool() {
    susceptible_pool.clear();
    for (size_t i = 0; i < world.agents.size(); ++i)
        if (current_states[i] == BeliefState::SUSCEPTIBLE)
            susceptible_pool.push_back((uint32_t)i);
    last_pool_rebuild_tick = current_tick;
}

uint32_t Simulation::weightedSelectFromPool(std::mt19937& rng) {
    float total = 0.0f;
    for (uint32_t idx : susceptible_pool)
        total += world.zones[world.agents[idx].zone_id].internet_penetration + 0.01f;

    std::uniform_real_distribution<float> d(0.0f, total);
    float threshold = d(rng);
    float cumul = 0.0f;

    for (size_t k = 0; k < susceptible_pool.size(); ++k) {
        cumul += world.zones[world.agents[susceptible_pool[k]].zone_id].internet_penetration + 0.01f;
        if (cumul >= threshold) {
            uint32_t selected = susceptible_pool[k];
            // swap-remove to keep pool contiguous without shifting
            susceptible_pool[k] = susceptible_pool.back();
            susceptible_pool.pop_back();
            return selected;
        }
    }
    uint32_t selected = susceptible_pool.back();
    susceptible_pool.pop_back();
    return selected;
}

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
        for (const auto& z : world.zones)
            log_file << ",\"" << z.name << "_infected\"";
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

void Simulation::injectMisinformation(uint32_t patient_zero_id,
                                      float emotional_valence,
                                      float plausibility,
                                      bool via_influencer) {
    current_packet.emotional_valence = emotional_valence;
    current_packet.plausibility = plausibility;
    current_packet.active = true;

    current_states[patient_zero_id] = BeliefState::INFECTED;
    world.agents[patient_zero_id].state = BeliefState::INFECTED;

    int extra_infected = 0;

    if (via_influencer) {
        // Seed top-5 influence neighbours to model a coordinated influencer drop —
        // prevents the packet from dying in the network's periphery at launch.
        auto& nbrs = world.adjacency_list[patient_zero_id];
        std::vector<uint32_t> sorted_nbrs = nbrs;
        std::sort(sorted_nbrs.begin(), sorted_nbrs.end(),
            [&](uint32_t a, uint32_t b) {
                return world.agents[a].social_influence > world.agents[b].social_influence;
            });

        int top_n = std::min((int)sorted_nbrs.size(), 5);
        for (int i = 0; i < top_n; ++i) {
            uint32_t idx = sorted_nbrs[i];
            world.agents[idx].state = BeliefState::INFECTED;
            current_states[idx]     = BeliefState::INFECTED;
            extra_infected++;
        }
    }

    std::cout << "Tick " << current_tick << ": Misinformation injected\n"
              << "  Patient zero: agent " << patient_zero_id << "\n"
              << "  emotional_valence=" << emotional_valence
              << "  plausibility=" << plausibility << "\n"
              << "  via_influencer=" << via_influencer
              << " (+" << extra_infected << " neighbours seeded)\n";
}

void Simulation::injectFactCheck(float effectiveness, Platform reach_platform) {
    const PlatformProfile& prof = world.platform_profiles.at(reach_platform);
    float base_correction = effectiveness * prof.correction_reach;

    int recovered_count = 0;
    size_t N = world.agents.size();

    for (size_t i = 0; i < N; ++i) {
        if (current_states[i] != BeliefState::INFECTED) continue;

        const Agent& a = world.agents[i];

        float recovery_chance = base_correction * a.literacy_score * (1.0f - a.correction_resistance);

        // WhatsApp: E2E encryption means no feed surface for correction labels.
        if (reach_platform == Platform::WHATSAPP)
            recovery_chance *= 0.08f;
        // TikTok: corrections spread but compete with 50x more confirming content.
        if (reach_platform == Platform::TIKTOK)
            recovery_chance *= 0.4f;

        std::mt19937 rng(i ^ current_tick ^ 0xC0FFEE);
        std::uniform_real_distribution<float> d(0.0f, 1.0f);

        if (d(rng) < recovery_chance) {
            current_states[i]     = BeliefState::RECOVERED;
            world.agents[i].state = BeliefState::RECOVERED;
            recovered_count++;
        }
    }

    std::cout << "Tick " << current_tick
              << ": Fact-check injected via " << platformName(reach_platform)
              << "\n  " << recovered_count << " agents recovered immediately\n";
}

void Simulation::triggerCrisisEvent(float intensity, int duration_ticks) {
    for (auto& a : world.agents)
        a.emotional_state = std::min(1.0f, a.emotional_state + intensity * 0.45f);

    // 0.45 scaling: prevents a full-intensity crisis from pinning every agent at 1.0.
    crisis_decay_rate      = (intensity * 0.45f) / (float)duration_ticks;
    crisis_remaining_ticks = duration_ticks;

    std::cout << "Tick " << current_tick << ": Crisis event triggered\n"
              << "  intensity=" << intensity
              << "  duration=" << duration_ticks << " ticks\n";
}
