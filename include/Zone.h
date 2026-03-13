// Zone.h — one country: demographics, platform mix, agent index range, live state.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "PlatformProfile.h"

struct Zone {
    uint16_t    id;
    std::string name;    // full country name; some contain commas (e.g. "Yemen, Rep.") — logStats() quotes them
    std::string region;  // World Bank region; drives inter-zone edge bias and renderer background colour

    // Population averages from agents_config.csv — agent traits are sampled from
    // truncated normals centred on these values.
    float avg_literacy;
    float avg_press_freedom;
    float avg_polarization;
    float avg_institutional_trust;
    float internet_penetration;   // used by TikTok viral-reach selector

    Platform dominant_platforms[3];
    float    platform_penetration[3]; // platform[k] assigned to agent if noisy sample > 0.5

    std::vector<uint16_t> neighbor_zone_ids; // populated by NetworkBuilder::addInterZoneEdges()

    // All agents for this zone are contiguous: agents[agent_start_idx .. agent_start_idx+agent_count-1].
    uint32_t agent_start_idx;
    uint32_t agent_count;

    uint32_t infected_count;
    uint32_t recovered_count;
    float    spread_velocity; // delta infected over last 10 ticks; used by renderer heat map
};
