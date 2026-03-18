// Renderer.cpp — country-mode visualiser: sprites, social graph edges,
//                influencer auras, virality flash, scenario panel, platform chart.
#include "Renderer.h"
#include <cmath>
#include <algorithm>
#include <random>
#include <iostream>
#include <sstream>
#include <cstdio>

// ── Agent names ────────────────────────────────────────────────────────────
static const char* AGENT_NAMES[30] = {
    "Aisha","Raj","Carlos","Mei","Fatima",
    "Liam","Nadia","Omar","Sofia","Chen",
    "Amara","Diego","Priya","Kenji","Zara",
    "Mikael","Nia","Hassan","Elena","Tariq",
    "Yuki","Amira","Lucas","Soo","Kwame",
    "Leila","Ivan","Tanya","Felix","Hana"
};
const char* Renderer::agentName(uint32_t id) { return AGENT_NAMES[id % 30]; }

// ── Platform colour / label ────────────────────────────────────────────────
sf::Color Renderer::platformColor(Platform p) {
    switch (p) {
        case Platform::FACEBOOK:  return {66,  103, 178};
        case Platform::WHATSAPP:  return {37,  211, 102};
        case Platform::TIKTOK:    return {255,  40,  80};
        case Platform::TWITTER:   return {29,  161, 242};
        case Platform::YOUTUBE:   return {220,   0,   0};
        case Platform::REDDIT:    return {255,  69,   0};
        case Platform::WECHAT:    return {9,   187,   7};
        case Platform::TELEGRAM:  return {0,   136, 204};
        default:                  return {140, 140, 140};
    }
}
const char* Renderer::platformLabel(Platform p) {
    switch (p) {
        case Platform::FACEBOOK:  return "FB";
        case Platform::WHATSAPP:  return "WA";
        case Platform::TIKTOK:    return "TK";
        case Platform::TWITTER:   return "TW";
        case Platform::YOUTUBE:   return "YT";
        case Platform::REDDIT:    return "RD";
        case Platform::WECHAT:    return "WC";
        case Platform::TELEGRAM:  return "TG";
        default:                  return "?";
    }
}

// ── State colour ──────────────────────────────────────────────────────────
sf::Color Renderer::stateColor(BeliefState s) {
    switch (s) {
        case BeliefState::SUSCEPTIBLE: return {80,  200, 130};
        case BeliefState::EXPOSED:     return {255, 200,  40};
        case BeliefState::INFECTED:    return {230,  40,  60};
        case BeliefState::RECOVERED:   return {80,  150, 220};
        case BeliefState::IMMUNE:      return {100, 220, 100};
        default:                       return {140, 140, 140};
    }
}

// ── Spritesheet rects (128×128 each) ──────────────────────────────────────
// bot=0,0  exposed=1,0  immune=2,0
// infected=0,1  recovered=1,1  susceptible=2,1
sf::IntRect Renderer::spriteRect(BeliefState s, bool is_bot) {
    if (is_bot) return {0,   0,   128, 128};
    switch (s) {
        case BeliefState::SUSCEPTIBLE: return {256, 128, 128, 128};
        case BeliefState::EXPOSED:     return {128,   0, 128, 128};
        case BeliefState::INFECTED:    return {  0, 128, 128, 128};
        case BeliefState::RECOVERED:   return {128, 128, 128, 128};
        case BeliefState::IMMUNE:      return {256,   0, 128, 128};
        default:                       return {256, 128, 128, 128};
    }
}

// ── Constructor ────────────────────────────────────────────────────────────
Renderer::Renderer() : move_rng(std::random_device{}()) {}

// ── Geo → panel coords ────────────────────────────────────────────────────
sf::Vector2f Renderer::geoToPanel(float lon, float lat) const {
    float lon_span = current_country.lon_max - current_country.lon_min;
    float lat_span = current_country.lat_max - current_country.lat_min;
    if (lon_span < 0.1f) lon_span = 0.1f;
    if (lat_span < 0.1f) lat_span = 0.1f;
    const float margin = 0.06f;
    float usable_w = SIM_W * (1.f - 2.f * margin);
    float usable_h = WIN_H * (1.f - 2.f * margin);
    float scale = std::min(usable_w / lon_span, usable_h / lat_span);
    float cx = SIM_W * 0.5f, cy = WIN_H * 0.5f;
    float lon_c = (current_country.lon_min + current_country.lon_max) * 0.5f;
    float lat_c = (current_country.lat_min + current_country.lat_max) * 0.5f;
    return {cx + (lon - lon_c) * scale, cy - (lat - lat_c) * scale};
}

void Renderer::buildPolygonScreen() {
    polygon_screen.clear();
    for (const auto& pt : current_country.polygon)
        polygon_screen.push_back(geoToPanel(pt.first, pt.second));
}

bool Renderer::insidePolygon(sf::Vector2f p) const {
    if (polygon_screen.size() < 3) return true;
    int n = (int)polygon_screen.size();
    bool inside = false;
    for (int i = 0, j = n-1; i < n; j = i++) {
        float xi = polygon_screen[i].x, yi = polygon_screen[i].y;
        float xj = polygon_screen[j].x, yj = polygon_screen[j].y;
        if (((yi > p.y) != (yj > p.y)) &&
            (p.x < (xj-xi)*(p.y-yi)/(yj-yi)+xi))
            inside = !inside;
    }
    return inside;
}

void Renderer::computeMapCrop() {
    if (!map_loaded) return;
    sf::Vector2u msz = map_texture.getSize();
    float lpad = (current_country.lon_max - current_country.lon_min) * 0.08f;
    float bpad = (current_country.lat_max - current_country.lat_min) * 0.08f;
    int u0 = (int)std::clamp((current_country.lon_min - lpad + 180.f) / 360.f * msz.x, 0.f, (float)msz.x-1);
    int u1 = (int)std::clamp((current_country.lon_max + lpad + 180.f) / 360.f * msz.x, 0.f, (float)msz.x);
    int v0 = (int)std::clamp((90.f - current_country.lat_max - bpad) / 180.f * msz.y, 0.f, (float)msz.y-1);
    int v1 = (int)std::clamp((90.f - current_country.lat_min + bpad) / 180.f * msz.y, 0.f, (float)msz.y);
    if (u1 <= u0) { u0=0; u1=(int)msz.x; }
    if (v1 <= v0) { v0=0; v1=(int)msz.y; }
    map_crop = sf::IntRect(u0, v0, u1-u0, v1-v0);
}

// ── Random point inside polygon ───────────────────────────────────────────
sf::Vector2f Renderer::randomInPolygon(std::mt19937& rng) {
    float xmin=1e9f, xmax=-1e9f, ymin=1e9f, ymax=-1e9f;
    for (auto& p : polygon_screen) {
        xmin=std::min(xmin,p.x); xmax=std::max(xmax,p.x);
        ymin=std::min(ymin,p.y); ymax=std::max(ymax,p.y);
    }
    xmin=std::max(xmin,20.f); xmax=std::min(xmax,(float)SIM_W-20.f);
    ymin=std::max(ymin,20.f); ymax=std::min(ymax,(float)WIN_H-20.f);
    if (xmin >= xmax) xmax = xmin + 40.f;
    if (ymin >= ymax) ymax = ymin + 40.f;
    std::uniform_real_distribution<float> rx(xmin, xmax), ry(ymin, ymax);
    sf::Vector2f p;
    int tries = 0;
    do { p = {rx(rng), ry(rng)}; tries++; }
    while (!insidePolygon(p) && tries < 300);
    return p;
}

void Renderer::initAgentPositions(const World& world) {
    size_t N = world.agents.size();
    agent_pos.resize(N);
    agent_target.resize(N);
    target_cooldown.resize(N, 0);
    std::mt19937 rng(42);
    for (size_t i = 0; i < N; ++i) {
        agent_pos[i]    = randomInPolygon(rng);
        agent_target[i] = agent_pos[i];  // bots are stationary
    }
}

// ── init / switchCountry / resetForScenario ───────────────────────────────
void Renderer::init(const World& world, sf::RenderWindow& /*window*/,
                    const CountryDef& country) {
    map_loaded = map_texture.loadFromFile("assets/map/world_dark.jpg");
    if (map_loaded) map_texture.setSmooth(true);

    sprites_loaded = sprite_sheet.loadFromFile("assets/sprites/spritesheet.png");
    if (!sprites_loaded)
        std::cerr << "Warning: could not load assets/sprites/spritesheet.png\n";

    font_loaded = font.loadFromFile("assets/fonts/Roboto-Regular.ttf") ||
                  font.loadFromFile("/System/Library/Fonts/Helvetica.ttc");

    switchCountry(world, country);
}

void Renderer::switchCountry(const World& world, const CountryDef& country) {
    current_country = country;
    event_log.clear();
    sir_head = 0; sir_count = 0;
    pulses.clear();
    virality_flash_alpha = 0.f;
    buildPolygonScreen();
    computeMapCrop();
    initAgentPositions(world);
}

void Renderer::resetForScenario() {
    event_log.clear();
    sir_head = 0; sir_count = 0;
    pulses.clear();
    virality_flash_alpha = 0.f;
}

// ── Agent movement ────────────────────────────────────────────────────────
void Renderer::updateAgentMovement(const World& world, int steps) {
    const float SPEED_BASE = 0.8f;
    const float ARRIVE     = 8.f;
    for (int s = 0; s < steps; ++s) {
        for (size_t i = 0; i < agent_pos.size(); ++i) {
            // Bots are stationary
            if (i < world.agents.size() && world.agents[i].is_bot) continue;

            float speed = SPEED_BASE *
                (world.agents[i].state == BeliefState::INFECTED ? 1.5f : 1.0f);

            sf::Vector2f& pos = agent_pos[i];
            sf::Vector2f& tgt = agent_target[i];
            sf::Vector2f diff = tgt - pos;
            float dist = std::sqrt(diff.x*diff.x + diff.y*diff.y);

            if (dist < ARRIVE || target_cooldown[i] <= 0) {
                tgt = randomInPolygon(move_rng);
                std::uniform_int_distribution<int> cd(80, 200);
                target_cooldown[i] = cd(move_rng);
            } else {
                target_cooldown[i]--;
                sf::Vector2f next = pos + (diff / dist) * speed;
                if (insidePolygon(next)) pos = next;
                else target_cooldown[i] = 0;
            }
        }
    }
}

// ── Pulse rings ───────────────────────────────────────────────────────────
void Renderer::spawnPulse(uint32_t agent_id) {
    if (agent_id >= agent_pos.size() || pulses.size() >= 200) return;
    PulseRing p;
    p.pos        = agent_pos[agent_id] + sf::Vector2f(LEFT_W, 0.f);
    p.radius     = 4.f;
    p.max_radius = 32.f;
    p.age        = 0;
    pulses.push_back(p);
}

void Renderer::logEvent(const VisEvent& ev) {
    event_log.push_front(ev);
    if (event_log.size() > 14) event_log.pop_back();
}

void Renderer::triggerViralityFlash() {
    virality_flash_alpha = 120.f;
}

void Renderer::updateAndDrawPulses(sf::RenderWindow& window) {
    for (auto& p : pulses) p.age++;
    for (const auto& p : pulses) {
        if (p.age >= PulseRing::LIFETIME) continue;
        float t = (float)p.age / PulseRing::LIFETIME;
        float r = p.max_radius * t;
        uint8_t a = (uint8_t)(200 * (1.f - t));
        sf::CircleShape ring(r);
        ring.setOrigin(r, r);
        ring.setPosition(p.pos);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor({230, 40, 60, a});
        ring.setOutlineThickness(1.5f);
        window.draw(ring);
    }
    pulses.erase(std::remove_if(pulses.begin(), pulses.end(),
        [](const PulseRing& p){ return p.age >= PulseRing::LIFETIME; }),
        pulses.end());
}

// ── handleClick / handleScenarioClick ─────────────────────────────────────
int Renderer::handleClick(float mx, float my) const {
    for (int i = 0; i < (int)country_button_rects.size(); ++i)
        if (country_button_rects[i].contains(mx, my))
            return i + country_scroll;
    return -1;
}
int Renderer::handleScenarioClick(float mx, float my) const {
    for (int i = 0; i < (int)scenario_button_rects.size(); ++i)
        if (scenario_button_rects[i].contains(mx, my))
            return i;
    return -1;
}

// ── drawLeftPanel ─────────────────────────────────────────────────────────
void Renderer::drawLeftPanel(const std::vector<CountryDef>& defs,
                              sf::RenderWindow& window) {
    sf::RectangleShape bg({(float)LEFT_W, (float)WIN_H});
    bg.setFillColor({14, 14, 30});
    window.draw(bg);

    const float BTN_H  = 33.f;
    const float BTN_Y0 = 28.f;

    // ── Country list ──
    if (font_loaded) {
        sf::Text title("COUNTRIES", font, 11);
        title.setPosition(10.f, 8.f);
        title.setFillColor({100, 100, 160});
        window.draw(title);
    }

    country_button_rects.clear();
    int max_vis = std::min(VISIBLE_COUNTRIES, (int)defs.size() - country_scroll);
    for (int i = 0; i < max_vis; ++i) {
        int idx = i + country_scroll;
        bool act = (idx == selected_country);
        float by = BTN_Y0 + i * (BTN_H + 2.f);

        sf::RectangleShape btn({(float)LEFT_W - 12.f, BTN_H});
        btn.setPosition(6.f, by);
        btn.setFillColor(act ? sf::Color{40,80,160} : sf::Color{22,22,46});
        btn.setOutlineColor(act ? sf::Color{80,140,255} : sf::Color{36,36,66});
        btn.setOutlineThickness(1.f);
        window.draw(btn);

        if (font_loaded) {
            sf::Text lbl(defs[idx].display_name, font, 12);
            lbl.setPosition(14.f, by + 9.f);
            lbl.setFillColor(act ? sf::Color::White : sf::Color{160,160,200});
            window.draw(lbl);
        }
        country_button_rects.push_back({6.f, by, (float)LEFT_W-12.f, BTN_H});
    }

    // ── Scenario section ──
    float sy = BTN_Y0 + VISIBLE_COUNTRIES * (BTN_H + 2.f) + 10.f;

    // Divider
    sf::RectangleShape div({(float)LEFT_W - 16.f, 1.f});
    div.setPosition(8.f, sy);
    div.setFillColor({40, 40, 80});
    window.draw(div);
    sy += 6.f;

    if (font_loaded) {
        sf::Text slabel("SCENARIOS", font, 11);
        slabel.setPosition(10.f, sy);
        slabel.setFillColor({100, 100, 160});
        window.draw(slabel);
    }
    sy += 18.f;

    static const char* SCENARIO_NAMES[3] = {
        "Controlled Burn",
        "Silent Wildfire",
        "Perfect Storm"
    };
    static const sf::Color SCENARIO_COLORS[3] = {
        {40, 160, 80},    // green — slow, controlled
        {160, 100, 20},   // amber — hidden, smouldering
        {160, 30,  30}    // red — maximum severity
    };

    scenario_button_rects.clear();
    const float SB_H = 30.f;
    for (int i = 0; i < 3; ++i) {
        bool act = (selected_scenario == i + 1);
        sf::Color base = SCENARIO_COLORS[i];
        sf::Color fill = act ? base : sf::Color{22, 22, 46};
        sf::Color outline = act ? sf::Color{(uint8_t)std::min(255,base.r+60),
                                                   (uint8_t)std::min(255,base.g+60),
                                                   (uint8_t)std::min(255,base.b+60), 255}
                                : sf::Color{36, 36, 66};

        sf::RectangleShape btn({(float)LEFT_W - 12.f, SB_H});
        btn.setPosition(6.f, sy);
        btn.setFillColor(fill);
        btn.setOutlineColor(outline);
        btn.setOutlineThickness(1.f);
        window.draw(btn);

        if (font_loaded) {
            sf::Text lbl(SCENARIO_NAMES[i], font, 11);
            lbl.setPosition(12.f, sy + 8.f);
            lbl.setFillColor(act ? sf::Color::White : sf::Color{160, 160, 200});
            window.draw(lbl);
        }
        scenario_button_rects.push_back({6.f, sy, (float)LEFT_W-12.f, SB_H});
        sy += SB_H + 3.f;
    }

    // ── Controls hint ──
    if (font_loaded) {
        sf::Text hint("Space  pause\nF  fact-check\nC  crisis\nB  botnet", font, 10);
        hint.setPosition(8.f, WIN_H - 70.f);
        hint.setFillColor({70, 70, 110});
        window.draw(hint);
    }
}

// ── drawSimPanel ──────────────────────────────────────────────────────────
void Renderer::drawSimPanel(const World& world, const Simulation& sim,
                             sf::RenderWindow& window) {
    sf::Vector2f origin((float)LEFT_W, 0.f);

    // Plain green political-map background
    sf::RectangleShape bg({(float)SIM_W, (float)WIN_H});
    bg.setPosition(origin);
    bg.setFillColor({28, 58, 38});
    window.draw(bg);

    // Country polygon fill
    if (polygon_screen.size() >= 3) {
        sf::ConvexShape fill;
        fill.setPointCount(polygon_screen.size());
        for (size_t i = 0; i < polygon_screen.size(); ++i)
            fill.setPoint(i, polygon_screen[i] + origin);
        fill.setFillColor({72, 140, 80});
        fill.setOutlineColor(sf::Color::Transparent);
        window.draw(fill);

        // Border
        sf::VertexArray border(sf::LineStrip, polygon_screen.size() + 1);
        for (size_t i = 0; i <= polygon_screen.size(); ++i) {
            border[i].position = polygon_screen[i % polygon_screen.size()] + origin;
            border[i].color    = {200, 240, 160, 220};
        }
        window.draw(border);
    }

    size_t N = world.agents.size();

    // ── Social graph edges ─────────────────────────────────────────────────
    // Draw all edges as faint lines; highlight edges adjacent to infected agents.
    for (size_t i = 0; i < N && i < agent_pos.size(); ++i) {
        for (uint32_t j : world.adjacency_list[i]) {
            if (j <= i || j >= N || j >= agent_pos.size()) continue;

            sf::Vector2f pi = agent_pos[i] + origin;
            sf::Vector2f pj = agent_pos[j] + origin;

            BeliefState si = world.agents[i].state;
            BeliefState sj = world.agents[j].state;

            bool active_edge = (si == BeliefState::INFECTED &&
                                (sj == BeliefState::SUSCEPTIBLE || sj == BeliefState::EXPOSED))
                             || (sj == BeliefState::INFECTED &&
                                (si == BeliefState::SUSCEPTIBLE || si == BeliefState::EXPOSED));

            if (active_edge) {
                // Colored by the shared platform, brighter
                Platform tx = world.agents[i].platforms[0];
                sf::Color col = platformColor(tx);
                col.a = 90;
                sf::Vertex line[2] = {{pi, col}, {pj, col}};
                window.draw(line, 2, sf::Lines);
            } else {
                // Very faint structural edge
                sf::Color col{180, 200, 180, 14};
                sf::Vertex line[2] = {{pi, col}, {pj, col}};
                window.draw(line, 2, sf::Lines);
            }
        }
    }

    // ── Exposure connection lines (proximity) ──────────────────────────────
    for (size_t i = 0; i < N && i < agent_pos.size(); ++i) {
        if (world.agents[i].state != BeliefState::EXPOSED) continue;
        sf::Vector2f pi = agent_pos[i] + origin;
        for (size_t j = 0; j < N && j < agent_pos.size(); ++j) {
            if (world.agents[j].state != BeliefState::INFECTED) continue;
            sf::Vector2f pj = agent_pos[j] + origin;
            float dx=pi.x-pj.x, dy=pi.y-pj.y;
            if (dx*dx+dy*dy < 70.f*70.f) {
                sf::Vertex line[2] = {{pi,{255,200,40,55}},{pj,{230,40,60,55}}};
                window.draw(line, 2, sf::Lines);
            }
        }
    }

    // ── Agents ────────────────────────────────────────────────────────────
    const float SPRITE_SIZE   = 128.f;
    const float DISPLAY_SIZE  = 28.f;
    const float SCALE         = DISPLAY_SIZE / SPRITE_SIZE;

    for (size_t i = 0; i < N && i < agent_pos.size(); ++i) {
        const Agent& a = world.agents[i];
        sf::Vector2f sp = agent_pos[i] + origin;

        // ── Influencer aura (pulsing golden ring for high-influence agents) ──
        if (a.social_influence > 0.75f && !a.is_bot) {
            float pulse = std::sin((float)sim.current_tick * 0.12f + i) * 3.f;
            float aura_r = DISPLAY_SIZE * 0.7f + pulse;
            sf::CircleShape aura(aura_r);
            aura.setOrigin(aura_r, aura_r);
            aura.setPosition(sp);
            aura.setFillColor(sf::Color::Transparent);
            uint8_t aa = (uint8_t)(80 + 40 * std::sin((float)sim.current_tick * 0.08f));
            aura.setOutlineColor({255, 200, 50, aa});
            aura.setOutlineThickness(2.f);
            window.draw(aura);
        }

        // ── Bot static indicator (glowing red ring) ──
        if (a.is_bot && a.state == BeliefState::INFECTED) {
            float br = DISPLAY_SIZE * 0.7f;
            sf::CircleShape bring(br);
            bring.setOrigin(br, br);
            bring.setPosition(sp);
            bring.setFillColor(sf::Color::Transparent);
            bring.setOutlineColor({255, 0, 200, 160});
            bring.setOutlineThickness(2.f);
            window.draw(bring);
        }

        // ── Sprite ──
        if (sprites_loaded) {
            sf::Sprite spr(sprite_sheet, spriteRect(a.state, a.is_bot));
            spr.setScale(SCALE, SCALE);
            spr.setOrigin(SPRITE_SIZE * 0.5f, SPRITE_SIZE * 0.5f);
            spr.setPosition(sp);
            window.draw(spr);
        } else {
            const float R = 10.f;
            sf::CircleShape body(R);
            body.setOrigin(R, R);
            body.setPosition(sp);
            body.setFillColor(a.is_bot ? sf::Color{200,60,200} : stateColor(a.state));
            body.setOutlineColor({255,255,255,60});
            body.setOutlineThickness(1.2f);
            window.draw(body);
        }

        // ── Platform badge ──
        if (a.platform_count > 0) {
            const float BW=14.f, BH=7.f;
            sf::RectangleShape badge({BW, BH});
            badge.setOrigin(BW*0.5f, 0);
            badge.setPosition(sp.x, sp.y + DISPLAY_SIZE*0.5f + 2.f);
            badge.setFillColor(platformColor(a.platforms[0]));
            window.draw(badge);
        }

        // ── Name label ──
        if (font_loaded) {
            sf::Text name(agentName(a.id), font, 9);
            sf::FloatRect nb = name.getLocalBounds();
            name.setOrigin(nb.width*0.5f, 0);
            name.setPosition(sp.x, sp.y + DISPLAY_SIZE*0.5f + 11.f);
            name.setFillColor({230, 255, 210, 210});
            window.draw(name);
        }
    }

    updateAndDrawPulses(window);

    // ── Virality flash overlay ─────────────────────────────────────────────
    if (virality_flash_alpha > 0.f) {
        sf::RectangleShape flash({(float)SIM_W, (float)WIN_H});
        flash.setPosition(origin);
        flash.setFillColor({255, 40, 80, (uint8_t)virality_flash_alpha});
        window.draw(flash);
        virality_flash_alpha = std::max(0.f, virality_flash_alpha - 6.f);

        if (font_loaded) {
            sf::Text vt("VIRAL EVENT", font, 18);
            sf::FloatRect vb = vt.getLocalBounds();
            vt.setOrigin(vb.width*0.5f, vb.height*0.5f);
            vt.setPosition(origin.x + SIM_W*0.5f, WIN_H*0.35f);
            vt.setFillColor({255, 255, 255, (uint8_t)std::min(255.f, virality_flash_alpha * 2.f)});
            window.draw(vt);
        }
    }

    // ── HUD ───────────────────────────────────────────────────────────────
    if (font_loaded) {
        // Country name + scenario name
        std::string hud_title = current_country.display_name;
        if (sim.active_scenario.type != ScenarioType::NONE)
            hud_title += std::string("  [") + sim.active_scenario.name + "]";

        sf::Text ctitle(hud_title, font, 14);
        ctitle.setPosition((float)LEFT_W + 8.f, 6.f);
        ctitle.setFillColor({200, 240, 200});
        window.draw(ctitle);

        // Crisis indicator
        if (sim.crisis_remaining_ticks > 0) {
            char cbuf[40];
            std::snprintf(cbuf, sizeof(cbuf), "CRISIS  %d ticks", sim.crisis_remaining_ticks);
            sf::Text ct(cbuf, font, 11);
            ct.setPosition((float)LEFT_W + 8.f, 26.f);
            ct.setFillColor({255, 160, 40});
            window.draw(ct);
        }

        // LIVE/PAUSED
        sf::Text st(paused ? "PAUSED" : "LIVE", font, 11);
        st.setPosition((float)LEFT_W + (float)SIM_W - 50.f, 8.f);
        st.setFillColor(paused ? sf::Color{200,160,40} : sf::Color{60,220,100});
        window.draw(st);

        // Tick counter
        char tbuf[32];
        std::snprintf(tbuf, sizeof(tbuf), "t=%u", sim.current_tick);
        sf::Text tt(tbuf, font, 10);
        tt.setPosition((float)LEFT_W + (float)SIM_W - 50.f, 24.f);
        tt.setFillColor({100, 140, 100});
        window.draw(tt);

        // Upcoming scenario event warning
        if (sim.active_scenario.type != ScenarioType::NONE) {
            for (const auto& ev : sim.active_scenario.events) {
                int countdown = ev.tick - (int)sim.current_tick;
                if (countdown > 0 && countdown <= 30) {
                    const char* etype = (ev.type == ScenarioEvent::Type::CRISIS)   ? "CRISIS" :
                                        (ev.type == ScenarioEvent::Type::BOTNET)   ? "BOTNET" :
                                                                                      "FACT-CHECK";
                    char wbuf[60];
                    std::snprintf(wbuf, sizeof(wbuf), "⚠ %s in %d ticks", etype, countdown);
                    sf::Text wt(wbuf, font, 11);
                    wt.setPosition((float)LEFT_W + 8.f, WIN_H - 20.f);
                    wt.setFillColor({255, 220, 60});
                    window.draw(wt);
                    break;
                }
            }
        }
    }
}

// ── drawRightPanel ────────────────────────────────────────────────────────
void Renderer::drawRightPanel(const World& world, const Simulation& sim,
                               sf::RenderWindow& window) {
    const float RX = (float)(LEFT_W + SIM_W);
    sf::RectangleShape bg({(float)RIGHT_W, (float)WIN_H});
    bg.setPosition(RX, 0);
    bg.setFillColor({12, 12, 28});
    window.draw(bg);

    if (!font_loaded) return;

    size_t total = world.agents.size();
    int ns = world.countByState(BeliefState::SUSCEPTIBLE);
    int ne = world.countByState(BeliefState::EXPOSED);
    int ni = world.countByState(BeliefState::INFECTED);
    int nr = world.countByState(BeliefState::RECOVERED);

    // ── SIR chart ──────────────────────────────────────────────────────────
    sir_buf[sir_head] = {ns, ne, ni, nr};
    sir_head = (sir_head + 1) % SIR_HISTORY;
    if (sir_count < SIR_HISTORY) ++sir_count;

    sf::Text chart_lbl("Belief Dynamics", font, 12);
    chart_lbl.setPosition(RX+8.f, 8.f);
    chart_lbl.setFillColor({140, 140, 200});
    window.draw(chart_lbl);

    const float CX0=RX+8.f, CX1=RX+RIGHT_W-8.f;
    const float CY0=26.f, CY1=145.f;
    const float CW=CX1-CX0, CH=CY1-CY0;
    float ymax = std::max(1.f, (float)total);

    sf::RectangleShape cbg({CW,CH});
    cbg.setPosition(CX0,CY0);
    cbg.setFillColor({18,18,40});
    window.draw(cbg);

    auto drawCurve = [&](auto getter, sf::Color col) {
        if (sir_count < 2) return;
        sf::VertexArray va(sf::LineStrip, sir_count);
        for (int i = 0; i < sir_count; ++i) {
            int bi = (sir_head - sir_count + i + SIR_HISTORY) % SIR_HISTORY;
            float v = (float)getter(sir_buf[bi]);
            va[i].position = {CX0 + (float)i/(sir_count-1)*CW, CY1 - v/ymax*CH};
            va[i].color = col;
        }
        window.draw(va);
    };
    drawCurve([](const SIRSample& s){return s.s;}, {80,  200, 130});
    drawCurve([](const SIRSample& s){return s.e;}, {255, 200,  40});
    drawCurve([](const SIRSample& s){return s.i;}, {230,  40,  60});
    drawCurve([](const SIRSample& s){return s.r;}, { 80, 150, 220});

    // Legend + % infected
    float lx = CX0; float ly = CY1 + 4.f;
    struct LE { sf::Color c; const char* l; };
    for (auto& le : std::vector<LE>{
            {{80,200,130},"S"}, {{255,200,40},"E"},
            {{230,40,60},"I"},  {{80,150,220},"R"}}) {
        sf::RectangleShape sq({8.f,8.f}); sq.setPosition(lx,ly); sq.setFillColor(le.c);
        window.draw(sq);
        sf::Text lt(le.l, font, 10); lt.setPosition(lx+10.f, ly-1.f);
        lt.setFillColor({180,180,200}); window.draw(lt);
        lx += 28.f;
    }
    float pct = total>0 ? 100.f*(float)ni/total : 0.f;
    char pbuf[32]; std::snprintf(pbuf, sizeof(pbuf), "%.0f%% infected", pct);
    sf::Text pt(pbuf, font, 11); pt.setPosition(lx+6.f, ly-1.f);
    pt.setFillColor({230,60,80}); window.draw(pt);

    // ── Platform transmission bar chart ────────────────────────────────────
    float bcy = CY1 + 22.f;
    sf::Text plabel("Platform Transmissions", font, 12);
    plabel.setPosition(RX+8.f, bcy);
    plabel.setFillColor({140, 140, 200});
    window.draw(plabel);
    bcy += 16.f;

    uint64_t max_tx = 1;
    for (int p = 0; p < (int)Platform::UNKNOWN; ++p)
        max_tx = std::max(max_tx, world.transmissions_by_platform[p]);

    const float BAR_H = 9.f;
    const float BAR_MAX_W = RIGHT_W - 60.f;
    static const Platform SHOW_PLATFORMS[] = {
        Platform::FACEBOOK, Platform::WHATSAPP, Platform::TIKTOK,
        Platform::TWITTER,  Platform::YOUTUBE,  Platform::TELEGRAM
    };
    for (Platform plat : SHOW_PLATFORMS) {
        uint64_t tx = world.transmissions_by_platform[(int)plat];
        float bw = (float)tx / max_tx * BAR_MAX_W;

        sf::RectangleShape bar({bw, BAR_H});
        bar.setPosition(RX + 36.f, bcy);
        bar.setFillColor(platformColor(plat));
        window.draw(bar);

        if (font_loaded) {
            sf::Text blbl(platformLabel(plat), font, 9);
            blbl.setPosition(RX + 8.f, bcy - 0.f);
            blbl.setFillColor({160, 160, 200});
            window.draw(blbl);

            char txbuf[16]; std::snprintf(txbuf, sizeof(txbuf), "%llu", (unsigned long long)tx);
            sf::Text txlbl(txbuf, font, 9);
            txlbl.setPosition(RX + 38.f + bw + 2.f, bcy);
            txlbl.setFillColor({120,120,160});
            window.draw(txlbl);
        }
        bcy += BAR_H + 3.f;
    }

    // Debunking count indicator
    if (!sim.factcheck_deploy_count.empty()) {
        bcy += 2.f;
        sf::Text dc("Fact-check deploys:", font, 10);
        dc.setPosition(RX+8.f, bcy);
        dc.setFillColor({100,160,100});
        window.draw(dc);
        bcy += 13.f;
        for (const auto& kv : sim.factcheck_deploy_count) {
            if (kv.second == 0) continue;
            Platform fp = (Platform)kv.first;
            float eff = 0.6f * std::pow(0.8f, (float)kv.second);
            char dbuf[48];
            std::snprintf(dbuf, sizeof(dbuf), "  %s x%d  eff=%.0f%%",
                platformLabel(fp), kv.second, eff*100.f);
            sf::Text dt(dbuf, font, 10);
            dt.setPosition(RX+8.f, bcy);
            dt.setFillColor({80,180,80});
            window.draw(dt);
            bcy += 12.f;
        }
    }

    // ── Agent roster ───────────────────────────────────────────────────────
    bcy += 4.f;
    sf::RectangleShape div2({(float)RIGHT_W-16.f, 1.f});
    div2.setPosition(RX+8.f, bcy);
    div2.setFillColor({40,40,80});
    window.draw(div2);
    bcy += 6.f;

    sf::Text roster_lbl("Agents", font, 12);
    roster_lbl.setPosition(RX+8.f, bcy);
    roster_lbl.setFillColor({140,140,200});
    window.draw(roster_lbl);
    bcy += 18.f;

    const float ROW_H = 18.f;
    size_t N = world.agents.size();
    for (size_t i = 0; i < N; ++i) {
        if (bcy + ROW_H > WIN_H - 145.f) break;
        const Agent& a = world.agents[i];

        sf::CircleShape dot(5.f);
        dot.setOrigin(5.f, 5.f);
        dot.setPosition(RX+14.f, bcy+ROW_H*0.5f);
        dot.setFillColor(a.is_bot ? sf::Color{220,60,220} : stateColor(a.state));
        window.draw(dot);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%-8s %s",
            agentName(a.id), platformLabel(a.platforms[0]));
        sf::Text row(buf, font, 10);
        row.setPosition(RX+24.f, bcy+2.f);
        row.setFillColor(a.is_bot ? sf::Color{220,60,220} : sf::Color{160,160,200});
        window.draw(row);

        // Influencer star marker
        if (a.social_influence > 0.75f && !a.is_bot) {
            sf::Text star("★", font, 10);
            star.setPosition(RX+RIGHT_W-80.f, bcy+1.f);
            star.setFillColor({255,200,50});
            window.draw(star);
        }

        const char* sstr = "CLEAR";
        sf::Color   scol = {80,200,130};
        switch(a.state) {
            case BeliefState::EXPOSED:   sstr="EXPOSED";  scol={255,200,40};  break;
            case BeliefState::INFECTED:  sstr="INFECTED"; scol={230,40,60};   break;
            case BeliefState::RECOVERED: sstr="IMMUNE";   scol={80,150,220};  break;
            default: break;
        }
        sf::Text sr(sstr, font, 10);
        sr.setPosition(RX+RIGHT_W-66.f, bcy+2.f);
        sr.setFillColor(scol);
        window.draw(sr);

        bcy += ROW_H;
    }

    // ── Event log ──────────────────────────────────────────────────────────
    float elog_top = WIN_H - 145.f;

    sf::RectangleShape div3({(float)RIGHT_W-16.f, 1.f});
    div3.setPosition(RX+8.f, elog_top-4.f);
    div3.setFillColor({40,40,80});
    window.draw(div3);

    sf::Text elog_lbl("Recent Events", font, 12);
    elog_lbl.setPosition(RX+8.f, elog_top);
    elog_lbl.setFillColor({140,140,200});
    window.draw(elog_lbl);

    sf::RectangleShape elogbg({(float)RIGHT_W-16.f, 122.f});
    elogbg.setPosition(RX+8.f, elog_top+16.f);
    elogbg.setFillColor({18,18,40});
    window.draw(elogbg);

    float ey = elog_top + 19.f;
    for (const auto& ev : event_log) {
        if (ey + 12.f > WIN_H - 4.f) break;
        sf::Text et(ev.msg, font, 10);
        et.setPosition(RX+12.f, ey);
        et.setFillColor(ev.is_infection ? sf::Color{230,80,80} : sf::Color{80,200,130});
        window.draw(et);
        ey += 10.f;
    }
}

// ── draw ─────────────────────────────────────────────────────────────────
void Renderer::draw(const World& world, const Simulation& sim,
                    sf::RenderWindow& window) {
    static std::vector<CountryDef> s_defs = getCountryDefs();
    window.clear({8, 10, 20});
    drawLeftPanel(s_defs, window);
    drawSimPanel(world, sim, window);
    drawRightPanel(world, sim, window);
    window.display();
}
