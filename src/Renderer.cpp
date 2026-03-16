// Renderer.cpp — SFML visualiser implementation.
#include "Renderer.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include <vector>
#include <string>
#include <cstdint>

// Uses substring matching because region strings include parentheticals
// (e.g. "Middle East & North Africa (MENA)") that would break exact equality.
static int regionIndex(const std::string& region) {
    if (region.find("East Asia") != std::string::npos)       return 0;
    if (region.find("Europe") != std::string::npos)          return 1;
    if (region.find("Latin America") != std::string::npos)   return 2;
    if (region.find("Middle East") != std::string::npos)     return 3;
    if (region.find("North America") != std::string::npos)   return 4;
    if (region.find("South Asia") != std::string::npos)      return 5;
    if (region.find("Sub-Saharan") != std::string::npos)     return 6;
    return 0;
}

// Colours must stay in sync with the drawCurve() lambdas in drawDashboard().
static sf::Color stateToColor(BeliefState state, bool is_bot) {
    if (is_bot) return sf::Color(180, 50, 180);
    switch (state) {
        case BeliefState::SUSCEPTIBLE: return sf::Color(60, 179, 113);
        case BeliefState::EXPOSED:     return sf::Color(255, 165, 0);
        case BeliefState::INFECTED:    return sf::Color(220, 20, 60);
        case BeliefState::RECOVERED:   return sf::Color(70, 130, 180);
        case BeliefState::IMMUNE:      return sf::Color(100, 200, 100);
        default:                       return sf::Color(128, 128, 128);
    }
}

Renderer::Renderer() {}

void Renderer::init(const World& world, sf::RenderWindow& window) {
    sf::Vector2u sz = window.getSize();
    win_w = sz.x;
    win_h = sz.y;

    computeZoneRects(world);
    loadSprites();
    rebuildAgentVertices(world);

    // Try project-relative font first; macOS system font as fallback.
    font_loaded = false;
    if (font.loadFromFile("assets/fonts/Roboto-Regular.ttf")) {
        font_loaded = true;
    } else if (font.loadFromFile("/System/Library/Fonts/Helvetica.ttc")) {
        font_loaded = true;
    }
}

void Renderer::computeZoneRects(const World& world) {
    const int N = static_cast<int>(world.zones.size());
    if (N == 0) return;

    std::vector<int> order(N);
    for (int i = 0; i < N; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return world.zones[a].region < world.zones[b].region;
    });

    const int PANEL_W = 880;
    const int PANEL_H = 720;

    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(N))));
    int rows = static_cast<int>(std::ceil(static_cast<double>(N) / cols));

    float cell_w = std::max(40.f, static_cast<float>(PANEL_W) / cols);
    float cell_h = std::max(40.f, static_cast<float>(PANEL_H) / rows);

    zone_rects.resize(N);

    float min_ip = 1.0f, max_ip = 0.0f;
    for (const auto& z : world.zones) {
        min_ip = std::min(min_ip, z.internet_penetration);
        max_ip = std::max(max_ip, z.internet_penetration);
    }
    float ip_range = (max_ip > min_ip) ? (max_ip - min_ip) : 1.0f;

    for (int idx = 0; idx < N; ++idx) {
        int zi = order[idx];
        const Zone& zone = world.zones[zi];

        int grid_col = idx % cols;
        int grid_row = idx / cols;

        float cell_x = grid_col * cell_w;
        float cell_y = grid_row * cell_h;

        // Zone width scaled to internet_penetration: better-connected zones get more screen area.
        float t = (ip_range > 0.f) ? (zone.internet_penetration - min_ip) / ip_range : 1.0f;
        float zone_w = cell_w * (0.6f + t * 0.4f);

        zone_rects[zi] = sf::FloatRect(cell_x, cell_y, zone_w, cell_h);
    }

    // Agent positions fixed at construction using agent.id as RNG seed — stable across redraws.
    const size_t total_agents = world.agents.size();
    agent_positions.resize(total_agents);

    for (const auto& zone : world.zones) {
        const sf::FloatRect& rect = zone_rects[zone.id];
        for (uint32_t ai = zone.agent_start_idx;
             ai < zone.agent_start_idx + zone.agent_count && ai < total_agents; ++ai) {
            std::mt19937 rng(world.agents[ai].id ^ 0xDEADBEEF);
            std::uniform_real_distribution<float> dx(rect.left, rect.left + rect.width);
            std::uniform_real_distribution<float> dy(rect.top,  rect.top  + rect.height);
            agent_positions[ai] = sf::Vector2f(dx(rng), dy(rng));
        }
    }
}

bool Renderer::loadSprites() {
    static const char* sprite_files[] = {
        "assets/sprites/susceptible.png",
        "assets/sprites/exposed.png",
        "assets/sprites/infected.png",
        "assets/sprites/recovered.png",
        "assets/sprites/immune.png",
        "assets/sprites/bot.png"
    };

    any_sprites_loaded = false;
    for (int i = 0; i < 6; ++i) {
        texture_loaded[i] = state_textures[i].loadFromFile(sprite_files[i]);
        if (texture_loaded[i]) {
            state_textures[i].setSmooth(false); // nearest-neighbour preserves pixel-art edges
            any_sprites_loaded = true;
            std::cout << "Loaded sprite: " << sprite_files[i] << "\n";
        } else {
            std::cout << "Missing sprite: " << sprite_files[i] << " -- will use coloured point\n";
        }
    }
    return any_sprites_loaded;
}

void Renderer::rebuildAgentVertices(const World& world) {
    const size_t N = world.agents.size();
    if (N == 0) return;

    if (any_sprites_loaded) {
        vertex_array.setPrimitiveType(sf::Quads);
        vertex_array.resize(N * 4);
    } else {
        vertex_array.setPrimitiveType(sf::Points);
        vertex_array.resize(N);
    }

    // 32 px matches the sprite size produced by make_spritesheet.py.
    const float DRAW_SIZE = 32.0f;

    if (any_sprites_loaded) {
        for (size_t i = 0; i < N; ++i) {
            const Agent& a   = world.agents[i];
            sf::Vector2f pos = agent_positions[i];

            // tile 0-4 = BeliefState; tile 5 = bot sprite
            int tile = a.is_bot ? 5 : (int)a.state;
            tile = std::min(tile, 5);

            sf::Vertex* quad = &vertex_array[i * 4];
            quad[0].position = {pos.x,             pos.y            };
            quad[1].position = {pos.x + DRAW_SIZE,  pos.y            };
            quad[2].position = {pos.x + DRAW_SIZE,  pos.y + DRAW_SIZE};
            quad[3].position = {pos.x,              pos.y + DRAW_SIZE};

            if (texture_loaded[tile]) {
                sf::Vector2u sz = state_textures[tile].getSize();
                quad[0].texCoords = {0.f,         0.f        };
                quad[1].texCoords = {(float)sz.x, 0.f        };
                quad[2].texCoords = {(float)sz.x, (float)sz.y};
                quad[3].texCoords = {0.f,          (float)sz.y};
                for (int v = 0; v < 4; ++v) quad[v].color = sf::Color::White;
            } else {
                sf::Color col = stateToColor(a.state, a.is_bot);
                for (int v = 0; v < 4; ++v) {
                    quad[v].texCoords = {0.f, 0.f};
                    quad[v].color     = col;
                }
            }
        }
    } else {
        for (size_t i = 0; i < N; ++i) {
            const Agent& a = world.agents[i];
            vertex_array[i].position = agent_positions[i];
            vertex_array[i].color    = stateToColor(a.state, a.is_bot);
        }
    }
}

void Renderer::drawZoneBackgrounds(const World& world, sf::RenderWindow& window) {
    for (const auto& zone : world.zones) {
        if (zone.id >= static_cast<int>(zone_rects.size())) continue;
        const sf::FloatRect& r = zone_rects[zone.id];
        sf::RectangleShape shape({r.width, r.height});
        shape.setPosition(r.left, r.top);
        shape.setFillColor(REGION_COLOURS[regionIndex(zone.region)]);
        window.draw(shape);
    }
}

void Renderer::drawZoneBorders(const World& world, sf::RenderWindow& window) {
    for (const auto& zone : world.zones) {
        if (zone.id >= static_cast<int>(zone_rects.size())) continue;
        const sf::FloatRect& r = zone_rects[zone.id];
        sf::RectangleShape shape({r.width, r.height});
        shape.setPosition(r.left, r.top);
        shape.setFillColor(sf::Color::Transparent);
        shape.setOutlineColor(sf::Color(80, 80, 100, 120));
        shape.setOutlineThickness(1.f);
        window.draw(shape);
    }
}

void Renderer::drawDashboard(const Simulation& sim, sf::RenderWindow& window) {
    sf::RectangleShape bg({400.f, 720.f});
    bg.setPosition(880.f, 0.f);
    bg.setFillColor(sf::Color(18, 18, 36));
    window.draw(bg);

    const World& world = sim.world;
    const size_t total_agents = world.agents.size();

    SIRSample sample;
    sample.susceptible = world.countByState(BeliefState::SUSCEPTIBLE);
    sample.exposed     = world.countByState(BeliefState::EXPOSED);
    sample.infected    = world.countByState(BeliefState::INFECTED);
    sample.recovered   = world.countByState(BeliefState::RECOVERED);

    sir_buf[sir_head] = sample;
    sir_head = (sir_head + 1) % SIR_HISTORY;
    if (sir_count < SIR_HISTORY) ++sir_count;

    const float CHART_X0 = 880.f, CHART_X1 = 1280.f;
    const float CHART_Y0 = 20.f,  CHART_Y1 = 340.f;
    const float CHART_W  = CHART_X1 - CHART_X0;
    const float CHART_H  = CHART_Y1 - CHART_Y0;
    const float ymax     = (total_agents > 0) ? static_cast<float>(total_agents) : 1.f;

    if (font_loaded) {
        sf::Text title("Belief State Dynamics", font, 13);
        title.setPosition(890.f, 5.f);
        title.setFillColor(sf::Color::White);
        window.draw(title);
    }

    auto drawCurve = [&](auto getter, sf::Color colour) {
        if (sir_count < 2) return;
        sf::VertexArray line(sf::LineStrip, sir_count);
        for (int i = 0; i < sir_count; ++i) {
            int buf_i = (sir_head - sir_count + i + SIR_HISTORY) % SIR_HISTORY;
            float val = static_cast<float>(getter(sir_buf[buf_i]));
            float x = CHART_X0 + (static_cast<float>(i) / (sir_count - 1)) * CHART_W;
            float y = CHART_Y1 - (val / ymax) * CHART_H;
            line[i].position = {x, y};
            line[i].color    = colour;
        }
        window.draw(line);
    };

    drawCurve([](const SIRSample& s){ return s.susceptible; }, sf::Color(60,  179, 113));
    drawCurve([](const SIRSample& s){ return s.exposed;     }, sf::Color(255, 165,   0));
    drawCurve([](const SIRSample& s){ return s.infected;    }, sf::Color(220,  20,  60));
    drawCurve([](const SIRSample& s){ return s.recovered;   }, sf::Color(70,  130, 180));

    if (font_loaded) {
        sf::Text ptitle("Transmission by Platform", font, 13);
        ptitle.setPosition(890.f, 365.f);
        ptitle.setFillColor(sf::Color::White);
        window.draw(ptitle);
    }

    struct PlatEntry { std::string name; uint64_t count; };
    std::vector<PlatEntry> plat_entries;
    for (int p = 0; p <= static_cast<int>(Platform::UNKNOWN); ++p) {
        uint64_t cnt = world.transmissions_by_platform[p];
        if (cnt > 0)
            plat_entries.push_back({sim.platformName(static_cast<Platform>(p)), cnt});
    }
    std::sort(plat_entries.begin(), plat_entries.end(),
              [](const PlatEntry& a, const PlatEntry& b){ return a.count > b.count; });
    if (plat_entries.size() > 5) plat_entries.resize(5);

    uint64_t total_tx = 0;
    for (const auto& pe : plat_entries) total_tx += pe.count;

    static const sf::Color BAR_COLOURS[] = {
        {70, 130, 180}, {220, 20, 60}, {60, 179, 113}, {255, 165, 0}, {180, 50, 180}
    };

    const float BAR_MAX_W = 360.f;
    const float BAR_H     = 28.f;
    const float BAR_GAP   = 4.f;
    const float BAR_X0    = 890.f;
    const float BAR_Y0    = 385.f;

    // Bars normalised to the highest-count platform so the top bar always spans full width.
    // Percentage label uses the true total so relative shares are accurate.
    uint64_t max_cnt = plat_entries.empty() ? 1 : plat_entries[0].count;

    for (size_t i = 0; i < plat_entries.size(); ++i) {
        float share = (max_cnt > 0) ? static_cast<float>(plat_entries[i].count) / max_cnt : 0.f;
        float bw    = share * BAR_MAX_W;
        float by    = BAR_Y0 + i * (BAR_H + BAR_GAP);

        sf::RectangleShape bar({bw, BAR_H});
        bar.setPosition(BAR_X0, by);
        bar.setFillColor(BAR_COLOURS[i % 5]);
        window.draw(bar);

        if (font_loaded) {
            float pct = (total_tx > 0)
                        ? 100.f * static_cast<float>(plat_entries[i].count) / total_tx : 0.f;
            char label_buf[64];
            std::snprintf(label_buf, sizeof(label_buf), "%s %.0f%%",
                          plat_entries[i].name.c_str(), pct);
            sf::Text label(label_buf, font, 12);
            label.setPosition(BAR_X0 + 4.f, by + 7.f);
            label.setFillColor(sf::Color::White);
            window.draw(label);
        }
    }

    if (!font_loaded) return;

    const float STATS_X = 890.f;
    const float STATS_Y = 570.f;
    const float LINE_H  = 22.f;

    { sf::Text t("Tick: " + std::to_string(sim.current_tick), font, 13);
      t.setPosition(STATS_X, STATS_Y); t.setFillColor(sf::Color::White); window.draw(t); }

    { float pct = (total_agents > 0)
                  ? 100.f * static_cast<float>(sample.infected) / total_agents : 0.f;
      char buf[64]; std::snprintf(buf, sizeof(buf), "Infected: %.1f%%", pct);
      sf::Text t(buf, font, 13); t.setPosition(STATS_X, STATS_Y + LINE_H);
      t.setFillColor(sf::Color(220, 20, 60)); window.draw(t); }

    // Velocity: delta infected over the last 10 ring-buffer samples.
    { int delta = 0;
      if (sir_count >= 10) {
          int cur_idx  = (sir_head - 1 + SIR_HISTORY) % SIR_HISTORY;
          int prev_idx = (sir_head - 10 + SIR_HISTORY) % SIR_HISTORY;
          delta = sir_buf[cur_idx].infected - sir_buf[prev_idx].infected;
      }
      char buf[64]; std::snprintf(buf, sizeof(buf), "Velocity: +%d/10t", delta);
      sf::Text t(buf, font, 13); t.setPosition(STATS_X, STATS_Y + 2 * LINE_H);
      t.setFillColor(sf::Color(255, 165, 0)); window.draw(t); }

    { sf::Text t("Scenario: default", font, 13);
      t.setPosition(STATS_X, STATS_Y + 3 * LINE_H);
      t.setFillColor(sf::Color(180, 180, 180)); window.draw(t); }

    { sf::Text t("[RUNNING]", font, 13);
      t.setPosition(STATS_X, STATS_Y + 4 * LINE_H);
      t.setFillColor(sf::Color(60, 220, 60)); window.draw(t); }
}

void Renderer::draw(const World& world, const Simulation& sim, sf::RenderWindow& window) {
    if (++since_rebuild >= RENDER_INTERVAL) {
        rebuildAgentVertices(world);
        since_rebuild = 0;
    }

    window.clear(sf::Color(26, 26, 46));
    drawZoneBackgrounds(world, window);
    drawZoneBorders(world, window);

    if (any_sprites_loaded) {
        // Group agents by tile so each texture is bound exactly once — minimum texture state changes.
        std::array<sf::VertexArray, 6> batches;
        for (int t = 0; t < 6; ++t)
            batches[t].setPrimitiveType(sf::Quads);

        for (size_t i = 0; i < world.agents.size(); ++i) {
            const Agent& a = world.agents[i];
            int tile = a.is_bot ? 5 : (int)a.state;
            tile = std::min(tile, 5);
            sf::Vertex* src = &vertex_array[i * 4];
            for (int v = 0; v < 4; ++v)
                batches[tile].append(src[v]);
        }

        // At most 6 draw calls regardless of agent count.
        for (int t = 0; t < 6; ++t) {
            if (batches[t].getVertexCount() == 0) continue;
            if (texture_loaded[t])
                window.draw(batches[t], &state_textures[t]);
            else
                window.draw(batches[t]);
        }
    } else {
        window.draw(vertex_array);
    }

    drawDashboard(sim, window);
    window.display();
}
