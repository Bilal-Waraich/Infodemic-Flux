// Renderer.h — country-mode SFML visualiser.
// Layout: [200px country+scenario list] [720px sim panel] [360px story panel]
#pragma once
#include <SFML/Graphics.hpp>
#include <array>
#include <vector>
#include <string>
#include <deque>
#include "World.h"
#include "Simulation.h"
#include "CountryBorders.h"

// ── Layout ─────────────────────────────────────────────────────────────────
static constexpr int LEFT_W      = 200;
static constexpr int SIM_W       = 720;
static constexpr int RIGHT_W     = 360;
static constexpr int WIN_W       = 1280;   // LEFT_W + SIM_W + RIGHT_W
static constexpr int WIN_H       = 720;
static constexpr int SIR_HISTORY = 300;

// ── Visual event log entry ─────────────────────────────────────────────────
struct VisEvent {
    std::string msg;
    uint32_t    tick;
    bool        is_infection;   // true=red, false=recovery/factcheck/info
};

// ── Infection pulse ring ───────────────────────────────────────────────────
struct PulseRing {
    sf::Vector2f pos;
    float        radius, max_radius;
    int          age;
    static constexpr int LIFETIME = 18;
};

class Renderer {
public:
    Renderer();
    void init(const World& world, sf::RenderWindow& window,
              const CountryDef& country);

    // Recompute positions/polygon when switching country
    void switchCountry(const World& world, const CountryDef& country);

    // Clear visual state when a scenario is loaded (same country, new sim state)
    void resetForScenario();

    void draw(const World& world, const Simulation& sim,
              sf::RenderWindow& window);

    void updateAgentMovement(const World& world, int ticks_per_frame);
    void spawnPulse(uint32_t agent_id);
    void logEvent(const VisEvent& ev);

    // Called by main when sim.virality_flash_this_tick is true
    void triggerViralityFlash();

    // Agent screen positions (sim-panel local, origin = (LEFT_W, 0))
    std::vector<sf::Vector2f> agent_pos;
    std::vector<sf::Vector2f> agent_target;

    int  selected_country  = 0;
    int  selected_scenario = 0;   // 0=none, 1-3 = scenario index
    bool paused            = false;

    static const char* agentName(uint32_t id);

    // Country click: returns country index (0..N-1) or -1
    int handleClick(float mx, float my) const;
    // Scenario click: returns 0-2 or -1
    int handleScenarioClick(float mx, float my) const;

private:
    // ── Map & polygon ──────────────────────────────────────────────────
    sf::Texture  map_texture;
    bool         map_loaded = false;

    CountryDef   current_country;
    std::vector<sf::Vector2f> polygon_screen;

    sf::Vector2f geoToPanel(float lon, float lat) const;
    void buildPolygonScreen();
    bool insidePolygon(sf::Vector2f p) const;

    sf::IntRect  map_crop;
    void computeMapCrop();

    // ── Agent sprites ─────────────────────────────────────────────────
    sf::Texture  sprite_sheet;
    bool         sprites_loaded = false;
    static sf::IntRect spriteRect(BeliefState s, bool is_bot);

    // ── Agent positions ────────────────────────────────────────────────
    void initAgentPositions(const World& world);
    sf::Vector2f randomInPolygon(std::mt19937& rng);

    // ── Platform / state colours ──────────────────────────────────────
    static sf::Color platformColor(Platform p);
    static sf::Color stateColor(BeliefState s);
    static const char* platformLabel(Platform p);

    // ── Movement ──────────────────────────────────────────────────────
    std::mt19937       move_rng;
    std::vector<int>   target_cooldown;

    // ── Pulse rings ───────────────────────────────────────────────────
    std::vector<PulseRing> pulses;
    void updateAndDrawPulses(sf::RenderWindow& window);

    // ── Virality flash ────────────────────────────────────────────────
    float virality_flash_alpha = 0.f;   // fades from 120 → 0 over ~20 frames

    // ── SIR ring-buffer ───────────────────────────────────────────────
    struct SIRSample { int s, e, i, r; };
    SIRSample sir_buf[SIR_HISTORY] = {};
    int sir_head = 0, sir_count = 0;

    // ── Story log ─────────────────────────────────────────────────────
    std::deque<VisEvent> event_log;   // newest at front, max 14

    // ── Draw sub-functions ────────────────────────────────────────────
    void drawLeftPanel(const std::vector<CountryDef>& defs,
                       sf::RenderWindow& window);
    void drawSimPanel(const World& world, const Simulation& sim,
                      sf::RenderWindow& window);
    void drawRightPanel(const World& world, const Simulation& sim,
                        sf::RenderWindow& window);

    // ── Font ──────────────────────────────────────────────────────────
    sf::Font font;
    bool     font_loaded = false;

    // ── Left panel scroll & buttons ───────────────────────────────────
    int  country_scroll = 0;
    static constexpr int VISIBLE_COUNTRIES = 12;
    std::vector<sf::FloatRect> country_button_rects;
    std::vector<sf::FloatRect> scenario_button_rects;
};
