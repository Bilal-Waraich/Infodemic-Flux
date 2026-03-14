// NetworkBuilder.h — Barabási–Albert scale-free graph with zone/platform bias and triangle closing.
//
// BA preferential attachment + three modifications:
//   zone bias (4x), platform bias (2x), triangle closing (p_close=0.048).
// Target at m=5: avg_degree≈16, clustering≈0.10, power-law degree tail.
#pragma once
#include "World.h"
#include <random>

class NetworkBuilder {
public:
    // adjacency_list must already be sized to world.agents.size() before calling.
    static void build(World& world, int m = 6);

    // Connects same-region zone pairs via influence-weighted edge drawing.
    // High-influence nodes are disproportionately the bridges between countries.
    static void addInterZoneEdges(World& world, int edges_per_pair = 80);

    // Bot cliques are fully connected internally and wired to legitimate agents
    // with social_influence > 0.85 to maximise seeding reach.
    static void injectBotnet(World& world, int num_botnets, int bots_per_botnet);

private:
    // Falls back to uniform random if all weights are zero (selected without replacement).
    static uint32_t weightedSelect(
        const std::vector<uint32_t>& candidates,
        const std::vector<float>&    weights,
        std::mt19937& rng);
};
