// NetworkBuilder.cpp — BA preferential attachment + triangle closing.
//
// Why BA: produces the power-law degree distribution seen in real social networks.
// Popular nodes attract more connections ("the rich get richer"), which matters
// because hubs are both super-spreaders and the highest-value fact-check targets.
//
// Three modifications to pure BA (which gives clustering ≈ 0.01):
//   zone bias (4x)    — same-country agents connect more, producing country clusters.
//   platform bias (2x) — shared-platform agents connect more, producing sub-communities.
//   triangle closing   — post-BA pass connects neighbour pairs at p_close=0.048,
//                        raising clustering to ~0.10 without changing degree distribution.
//   p_close tuned empirically: lower values give clustering < 0.08; higher values
//   blow up avg_degree past 20 at m=6. Current m=5 + p_close=0.048 → avg≈16, cc≈0.10.
#include "NetworkBuilder.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>

void NetworkBuilder::build(World &world, int m) {
  int N = (int)world.agents.size();
  world.adjacency_list.assign(N, {});

  std::vector<uint32_t> degree(N, 0);
  uint64_t degree_sum = 0;

  // Seed: fully connect first m+1 nodes so BA loop always has non-zero degree_sum.
  int seed_end = std::min(m + 1, N);
  for (int i = 0; i < seed_end; ++i) {
    for (int j = i + 1; j < seed_end; ++j) {
      world.adjacency_list[i].push_back(j);
      world.adjacency_list[j].push_back(i);
      degree[i]++;
      degree[j]++;
      degree_sum += 2;
    }
  }

  std::mt19937 rng(12345);
  auto t_start = std::chrono::steady_clock::now();

  for (int i = seed_end; i < N; ++i) {
    // Candidate vectors rebuilt per-node — O(N²) total, acceptable at N=21700.
    std::vector<uint32_t> candidates;
    std::vector<float> weights;
    candidates.reserve(i);
    weights.reserve(i);

    const Agent &ai = world.agents[i];

    for (int j = 0; j < i; ++j) {
      if (degree_sum == 0)
        break;
      float w = (float)degree[j] / (float)degree_sum;

      const Agent &aj = world.agents[j];

      if (ai.zone_id == aj.zone_id)
        w *= 4.0f;

      for (int pa = 0; pa < ai.platform_count; ++pa) {
        for (int pb = 0; pb < aj.platform_count; ++pb) {
          if (ai.platforms[pa] == aj.platforms[pb]) {
            w *= 2.0f;
            goto done_platform_check;
          }
        }
      }
    done_platform_check:

      if (aj.is_bot)
        w *= 3.0f;

      candidates.push_back((uint32_t)j);
      weights.push_back(w);
    }

    int targets_to_pick = std::min(m, (int)candidates.size());
    for (int k = 0; k < targets_to_pick; ++k) {
      uint32_t t = weightedSelect(candidates, weights, rng);

      world.adjacency_list[i].push_back(t);
      world.adjacency_list[t].push_back(i);
      degree[i]++;
      degree[t]++;
      degree_sum += 2;

      // Zero weight instead of erase to preserve index alignment with candidates[].
      for (size_t w_idx = 0; w_idx < candidates.size(); ++w_idx) {
        if (candidates[w_idx] == t) {
          weights[w_idx] = 0.0f;
          break;
        }
      }
    }

    if (i % 5000 == 0 && i > 0) {
      auto now = std::chrono::steady_clock::now();
      double elapsed = std::chrono::duration<double>(now - t_start).count();
      std::cout << "  Network: " << i << "/" << N << "  (" << (int)elapsed
                << "s elapsed)\r" << std::flush;
    }
  }

  // Triangle-closing pass — adds short-range clustering without altering degree distribution.
  // deg_cap=20 prevents O(max_degree²) blowup on influencer nodes with degree 800+.
  const float p_close = 0.048f;
  std::uniform_real_distribution<float> coin(0.0f, 1.0f);
  uint64_t triangles_closed = 0;

  for (int v = 0; v < N; ++v) {
    auto &nbrs = world.adjacency_list[v];
    int deg = (int)nbrs.size();
    if (deg < 2)
      continue;

    int deg_cap = std::min(deg, 20);

    for (int a = 0; a < deg_cap; ++a) {
      for (int b = a + 1; b < deg_cap; ++b) {
        uint32_t na = nbrs[a];
        uint32_t nb = nbrs[b];

        auto &check_list = world.adjacency_list[na];
        bool already_connected = std::find(check_list.begin(), check_list.end(),
                                           nb) != check_list.end();
        if (already_connected)
          continue;

        if (coin(rng) < p_close) {
          world.adjacency_list[na].push_back(nb);
          world.adjacency_list[nb].push_back(na);
          degree[na]++;
          degree[nb]++;
          degree_sum += 2;
          triangles_closed++;
        }
      }
    }
  }

  std::cout << "  Triangles closed: " << triangles_closed << "\n";

  // Clustering estimated from 1000 random nodes — exact computation is O(N*avg_degree²).
  std::cout << "\n";

  double avg_degree = (double)degree_sum / N;
  uint32_t max_degree = *std::max_element(degree.begin(), degree.end());
  uint32_t max_agent = (uint32_t)(std::max_element(degree.begin(), degree.end()) - degree.begin());

  std::vector<uint32_t> sorted_deg = degree;
  std::sort(sorted_deg.begin(), sorted_deg.end());
  uint32_t p95 = sorted_deg[(size_t)(sorted_deg.size() * 0.95)];

  std::uniform_int_distribution<int> node_dist(0, N - 1);
  double cc_sum = 0.0;
  int cc_samples = std::min(1000, N);
  for (int s = 0; s < cc_samples; ++s) {
    int v = node_dist(rng);
    auto &nbrs = world.adjacency_list[v];
    int deg_v = (int)nbrs.size();
    if (deg_v < 2) { cc_sum += 0.0; continue; }
    int triangles = 0;
    for (int a = 0; a < deg_v; ++a) {
      for (int b = a + 1; b < deg_v; ++b) {
        uint32_t na = nbrs[a], nb = nbrs[b];
        auto &na_list = world.adjacency_list[na];
        if (std::find(na_list.begin(), na_list.end(), nb) != na_list.end())
          triangles++;
      }
    }
    int possible = deg_v * (deg_v - 1) / 2;
    cc_sum += (double)triangles / possible;
  }
  double cc = cc_sum / cc_samples;

  std::cout << "Network construction complete\n";
  std::cout << "  Nodes:           " << N << "\n";
  std::cout << "  Edges:           " << degree_sum / 2 << "\n";
  std::cout << "  Avg degree:      " << avg_degree << "\n";
  std::cout << "  Max degree:      " << max_degree << " (agent " << max_agent << ")\n";
  std::cout << "  p95 degree:      " << p95 << "\n";
  std::cout << "  Clustering coef: " << cc << "\n";
}

void NetworkBuilder::addInterZoneEdges(World &world, int edges_per_pair) {
  std::mt19937 rng(42);
  int pairs_connected = 0;

  for (size_t a = 0; a < world.zones.size(); ++a) {
    for (size_t b = a + 1; b < world.zones.size(); ++b) {
      if (world.zones[a].region != world.zones[b].region)
        continue;

      // +0.01f floor so every agent has a non-zero selection chance.
      std::vector<uint32_t> cands_a, cands_b;
      std::vector<float> wts_a, wts_b;

      uint32_t start_a = world.zones[a].agent_start_idx;
      uint32_t end_a = start_a + world.zones[a].agent_count;
      for (uint32_t i = start_a; i < end_a; ++i) {
        cands_a.push_back(i);
        wts_a.push_back(world.agents[i].social_influence + 0.01f);
      }

      uint32_t start_b = world.zones[b].agent_start_idx;
      uint32_t end_b = start_b + world.zones[b].agent_count;
      for (uint32_t i = start_b; i < end_b; ++i) {
        cands_b.push_back(i);
        wts_b.push_back(world.agents[i].social_influence + 0.01f);
      }

      if (cands_a.empty() || cands_b.empty())
        continue;

      for (int e = 0; e < edges_per_pair; ++e) {
        uint32_t src = weightedSelect(cands_a, wts_a, rng);
        uint32_t tgt = weightedSelect(cands_b, wts_b, rng);
        world.adjacency_list[src].push_back(tgt);
        world.adjacency_list[tgt].push_back(src);
      }
      pairs_connected++;
    }
  }
  std::cout << "Inter-zone edges added (" << pairs_connected << " zone pairs connected)\n";
}

void NetworkBuilder::injectBotnet(World &world, int num_botnets, int bots_per_botnet) {
  std::mt19937 rng(99);

  for (int b = 0; b < num_botnets; ++b) {
    uint32_t bot_start = (uint32_t)world.agents.size();
    uint16_t zone_id = (uint16_t)(b % world.zones.size());
    const Zone &z = world.zones[zone_id];

    for (int k = 0; k < bots_per_botnet; ++k) {
      Agent bot{};
      bot.id = (uint32_t)world.agents.size();
      bot.zone_id = zone_id;
      bot.literacy_score = 0.01f;
      bot.press_freedom = z.avg_press_freedom;
      bot.polarization = 1.0f;
      bot.institutional_trust = 0.1f;
      bot.sharing_habit = 1.0f;
      bot.confirmation_bias = 0.9f;
      bot.emotional_state = 1.0f;
      bot.social_influence = 0.5f;
      bot.correction_resistance = 0.99f;
      bot.is_bot = true;
      bot.state = BeliefState::SUSCEPTIBLE;
      for (int p = 0; p < 3; ++p)
        bot.platforms[p] = z.dominant_platforms[p];
      bot.platform_count = 3;
      world.agents.push_back(bot);
      world.adjacency_list.push_back({});
    }

    // Fully-connected clique: activating one bot immediately exposes all others.
    for (uint32_t i = bot_start; i < world.agents.size(); ++i)
      for (uint32_t j = bot_start; j < world.agents.size(); ++j)
        if (i != j)
          world.adjacency_list[i].push_back(j);

    std::vector<uint32_t> influencers;
    for (uint32_t i = 0; i < bot_start; ++i)
      if (world.agents[i].social_influence > 0.85f)
        influencers.push_back(i);

    std::shuffle(influencers.begin(), influencers.end(), rng);
    int connect_n = std::min((int)influencers.size(), bots_per_botnet * 3);
    for (int i = 0; i < connect_n; ++i) {
      uint32_t bot_idx = bot_start + (i % bots_per_botnet);
      uint32_t inf_idx = influencers[i];
      world.adjacency_list[bot_idx].push_back(inf_idx);
      world.adjacency_list[inf_idx].push_back(bot_idx);
    }
  }

  std::cout << "Injected " << num_botnets << " botnets ("
            << num_botnets * bots_per_botnet << " bots total)\n";
}

uint32_t NetworkBuilder::weightedSelect(const std::vector<uint32_t> &candidates,
                                        const std::vector<float> &weights,
                                        std::mt19937 &rng) {
  float total = 0.0f;
  for (float w : weights)
    total += w;

  if (total <= 0.0f) {
    std::uniform_int_distribution<size_t> d(0, candidates.size() - 1);
    return candidates[d(rng)];
  }

  std::uniform_real_distribution<float> d(0.0f, total);
  float threshold = d(rng);
  float cumulative = 0.0f;
  for (size_t i = 0; i < candidates.size(); ++i) {
    cumulative += weights[i];
    if (cumulative >= threshold)
      return candidates[i];
  }
  return candidates.back();
}
