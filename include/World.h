// World.h — owns agents, zones, adjacency list, and platform profiles.
#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include "Agent.h"
#include "Zone.h"

class World {
public:
    // flat contiguous array — never store pointers; reallocation invalidates them.
    std::vector<Agent> agents;

    std::vector<Zone> zones;

    // Sparse adjacency list. Never use an adjacency matrix — for 100k agents that's 10 GB.
    std::vector<std::vector<uint32_t>> adjacency_list;

    // Populated once at startup by buildPlatformProfiles(); read-only during simulation.
    std::unordered_map<Platform, PlatformProfile> platform_profiles;

    // Indexed by (int)Platform — must stay in sync with Platform enum size (9 entries).
    uint64_t transmissions_by_platform[9] = {};

    void reserveCapacity(size_t num_agents, size_t num_zones) {
        agents.reserve(num_agents);
        zones.reserve(num_zones);
        adjacency_list.reserve(num_agents);
    }

    void buildPlatformProfiles() {
        for (int i = 0; i <= static_cast<int>(Platform::UNKNOWN); ++i) {
            Platform p = static_cast<Platform>(i);
            platform_profiles[p] = PlatformProfile::defaults(p);
        }
    }

    // O(N) full scan — avoid inside the hot propagation loop.
    int countByState(BeliefState s) const {
        int count = 0;
        for (const auto& a : agents)
            if (a.state == s) ++count;
        return count;
    }

    int countByStateInZone(BeliefState s, uint16_t zone_id) const {
        const Zone& z = zones[zone_id];
        int count = 0;
        for (uint32_t i = z.agent_start_idx;
             i < z.agent_start_idx + z.agent_count; ++i)
            if (agents[i].state == s) ++count;
        return count;
    }

    Agent& getAgent(uint32_t id) { return agents[id]; }
    const Agent& getAgent(uint32_t id) const { return agents[id]; }

    uint64_t totalTransmissions() const {
        uint64_t total = 0;
        for (auto t : transmissions_by_platform) total += t;
        return total;
    }
};
