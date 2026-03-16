// Renderer.h — SFML visualiser. All agents drawn in ≤6 draw calls via VertexArray batching.
//
// Batching: agents of the same belief state share one sf::Texture and are grouped into one
// VertexArray. Six states → six draw calls max, regardless of agent count.
// Individual sf::Sprite draws would cost ~8 ms/frame in GPU state-change overhead alone.
#pragma once
#include <SFML/Graphics.hpp>
#include <array>
#include <vector>
#include "World.h"
#include "Simulation.h"

static constexpr int SIR_HISTORY    = 400; // ring buffer depth for dashboard SIR curves
static constexpr int RENDER_INTERVAL = 10; // ticks between vertex array rebuilds
static constexpr int DASH_X          = 880;

// Background fill colours per World Bank region (low alpha so agents remain visible).
// Order matches CLASS.xlsx region sort: EA&P, E&CA, LA&C, MENA, NA, SA, SSA.
static const sf::Color REGION_COLOURS[] = {
    sf::Color(30, 80,  120, 60),
    sf::Color(80, 50,  120, 60),
    sf::Color(40, 120,  80, 60),
    sf::Color(120, 80,  30, 60),
    sf::Color(60, 100, 140, 60),
    sf::Color(120, 100, 30, 60),
    sf::Color(120, 60,  30, 60),
};

class Renderer {
public:
    Renderer();

    // Must be called after sf::RenderWindow is created; caches window size.
    void init(const World& world, sf::RenderWindow& window);

    void draw(const World& world, const Simulation& sim, sf::RenderWindow& window);

private:
    void computeZoneRects(const World& world);
    std::vector<sf::FloatRect>  zone_rects;
    std::vector<sf::Vector2f>   agent_positions;

    bool loadSprites();
    std::array<sf::Texture, 6> state_textures;
    std::array<bool, 6>        texture_loaded = {};
    bool any_sprites_loaded = false;

    // Each agent is a 4-vertex Quad when sprites are loaded, or a single Point otherwise.
    void rebuildAgentVertices(const World& world);
    sf::VertexArray vertex_array;

    int since_rebuild = 0;

    void drawZoneBackgrounds(const World& world, sf::RenderWindow& window);
    void drawZoneBorders(const World& world, sf::RenderWindow& window);
    void drawDashboard(const Simulation& sim, sf::RenderWindow& window);

    struct SIRSample { int susceptible, exposed, infected, recovered; };
    SIRSample sir_buf[SIR_HISTORY] = {};
    int sir_head  = 0;
    int sir_count = 0;

    sf::Font font;
    bool font_loaded = false;
    unsigned int win_w = 1280, win_h = 720;
};
