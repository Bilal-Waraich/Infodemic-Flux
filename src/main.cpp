// main.cpp — simulation orchestrator.
//
// argv[1] = scenario JSON path  (default: scenario_a.json)
// argv[2] = agents per zone     (default: 300)
// argv[3] = max ticks           (-1 = unlimited; used for headless analysis runs)
#include <SFML/Graphics.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <cstdlib>

#include "Agent.h"
#include "PlatformProfile.h"
#include "World.h"
#include "DataLoader.h"
#include "NetworkBuilder.h"
#include "Simulation.h"
#include "EventSystem.h"
#include "Renderer.h"

// Manual JSON helpers — same approach as EventSystem; no library dependency for two lookups.
static bool jsonBool(const std::string& json, const std::string& key, bool default_val = false) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return default_val;
    size_t val_start = colon + 1;
    while (val_start < json.size() &&
           (json[val_start] == ' ' || json[val_start] == '\t' || json[val_start] == '\n'))
        val_start++;
    if (json.substr(val_start, 4) == "true")  return true;
    if (json.substr(val_start, 5) == "false") return false;
    return default_val;
}

static std::string jsonString(const std::string& json, const std::string& key,
                               const std::string& default_val = "") {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return default_val;
    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) return default_val;
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return default_val;
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return default_val;
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// Returns the "name" field from the scenario JSON for use in the log filename.
// Falls back to the filename stem if the field is absent.
static std::string scenarioName(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        size_t slash = path.rfind('/');
        std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
        size_t dot = base.rfind('.');
        return (dot == std::string::npos) ? base : base.substr(0, dot);
    }
    std::string content, line;
    while (std::getline(f, line)) content += line + " ";
    std::string name = jsonString(content, "name");
    if (!name.empty()) return name;
    size_t slash = path.rfind('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.rfind('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

static bool scenarioInjectBots(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string content, line;
    while (std::getline(f, line)) content += line + " ";
    return jsonBool(content, "inject_bots", false);
}

static std::string topPlatformName(const World& world, const Simulation& sim) {
    int best_idx = 0;
    uint64_t best_count = 0;
    for (int p = 0; p <= static_cast<int>(Platform::UNKNOWN); ++p) {
        if (world.transmissions_by_platform[p] > best_count) {
            best_count = world.transmissions_by_platform[p];
            best_idx = p;
        }
    }
    return sim.platformName(static_cast<Platform>(best_idx));
}

static float topPlatformPct(const World& world) {
    uint64_t total = world.totalTransmissions();
    if (total == 0) return 0.0f;
    uint64_t best = 0;
    for (int p = 0; p <= static_cast<int>(Platform::UNKNOWN); ++p)
        if (world.transmissions_by_platform[p] > best)
            best = world.transmissions_by_platform[p];
    return 100.0f * static_cast<float>(best) / static_cast<float>(total);
}

int main(int argc, char* argv[]) {
    static_assert(sizeof(Agent) == 64, "Agent size check");

    std::string scenario_path = (argc > 1) ? argv[1] : "scenarios/scenario_a.json";
    int agents_per_zone       = (argc > 2) ? std::atoi(argv[2]) : 300;
    int max_ticks             = (argc > 3) ? std::atoi(argv[3]) : -1;
    if (agents_per_zone < 1) agents_per_zone = 1;

    std::cout << "Loading world data...\n";
    World world = DataLoader::buildWorld("data/processed/agents_config.csv", agents_per_zone);

    // m=5, p_close=0.048 → avg_degree≈16, clustering≈0.10
    std::cout << "Building network (m=6)...\n";
    NetworkBuilder::build(world, 5);
    NetworkBuilder::addInterZoneEdges(world, 80);

    if (scenarioInjectBots(scenario_path)) {
        std::cout << "Injecting botnet...\n";
        NetworkBuilder::injectBotnet(world, 2, 20);
    }

    Simulation sim(world);

    EventSystem events;
    events.loadFromJSON(scenario_path);

    std::filesystem::create_directories("logs");
    std::string sname = scenarioName(scenario_path);
    std::time_t ts = std::time(nullptr);
    std::string log_path = "logs/run_" + sname + "_" + std::to_string(static_cast<long>(ts)) + ".csv";
    std::ofstream log_file(log_path);
    if (!log_file.is_open())
        std::cerr << "Warning: could not open log file: " << log_path << "\n";
    else
        std::cout << "Logging to: " << log_path << "\n";

    // Window must be created before renderer.init() — init calls window.getSize().
    sf::RenderWindow window(sf::VideoMode(1280, 720), "Digital Contagion");
    window.setFramerateLimit(60);

    Renderer renderer;
    renderer.init(world, window);

    int  ticks_per_frame = 5;
    bool paused          = false;

    int      peak_infected    = 0;
    uint32_t peak_tick        = 0;
    int      first_tick_10pct = -1;
    int      total_agents     = static_cast<int>(world.agents.size());

    std::filesystem::create_directories("docs/figures"); // for S-key screenshots

    while (window.isOpen()) {
        sf::Event e;
        while (window.pollEvent(e)) {
            if (e.type == sf::Event::Closed)
                window.close();

            if (e.type == sf::Event::KeyPressed) {
                switch (e.key.code) {

                case sf::Keyboard::Escape:
                    window.close();
                    break;

                case sf::Keyboard::Space:
                    paused = !paused;
                    std::cout << (paused ? "Paused\n" : "Resumed\n");
                    break;

                case sf::Keyboard::F: {
                    SimEvent fe;
                    fe.trigger_tick = sim.current_tick;
                    fe.type = EventType::INJECT_FACTCHECK;
                    fe.float_params["effectiveness"] = 0.6f;
                    fe.string_params["platform"] = "Twitter";
                    events.schedule(fe);
                    std::cout << "Scheduled INJECT_FACTCHECK at tick " << sim.current_tick << "\n";
                    break;
                }

                case sf::Keyboard::C: {
                    SimEvent ce;
                    ce.trigger_tick = sim.current_tick;
                    ce.type = EventType::CRISIS_TRIGGER;
                    ce.float_params["intensity"] = 0.6f;
                    ce.float_params["duration_ticks"] = 80.0f;
                    events.schedule(ce);
                    std::cout << "Scheduled CRISIS_TRIGGER at tick " << sim.current_tick << "\n";
                    break;
                }

                case sf::Keyboard::Add:
                case sf::Keyboard::Equal:
                    ticks_per_frame = std::min(50, ticks_per_frame + 5);
                    std::cout << "Speed: " << ticks_per_frame << " ticks/frame\n";
                    break;

                case sf::Keyboard::Subtract:
                case sf::Keyboard::Hyphen:
                    ticks_per_frame = std::max(1, ticks_per_frame - 5);
                    std::cout << "Speed: " << ticks_per_frame << " ticks/frame\n";
                    break;

                case sf::Keyboard::S: {
                    sf::Texture tex;
                    tex.create(window.getSize().x, window.getSize().y);
                    tex.update(window);
                    sf::Image img = tex.copyToImage();
                    std::string shot_path = "docs/figures/screenshot_" +
                        std::to_string(sim.current_tick) + ".png";
                    if (img.saveToFile(shot_path))
                        std::cout << "Screenshot saved: " << shot_path << "\n";
                    else
                        std::cerr << "Failed to save screenshot\n";
                    break;
                }

                case sf::Keyboard::Num1:
                    events.loadFromJSON("scenarios/scenario_a.json"); break;
                case sf::Keyboard::Num2:
                    events.loadFromJSON("scenarios/scenario_b.json"); break;
                case sf::Keyboard::Num3:
                    events.loadFromJSON("scenarios/scenario_c.json"); break;

                default: break;
                }
            }
        }

        if (!paused) {
            for (int t = 0; t < ticks_per_frame; t++) {
                events.processAll(sim, sim.current_tick);
                sim.tick();
                sim.logStats(log_file);

                int cur_infected = world.countByState(BeliefState::INFECTED);
                if (cur_infected > peak_infected) {
                    peak_infected = cur_infected;
                    peak_tick     = sim.current_tick;
                }
                if (first_tick_10pct < 0 && total_agents > 0) {
                    float pct = static_cast<float>(cur_infected) / static_cast<float>(total_agents);
                    if (pct >= 0.10f)
                        first_tick_10pct = static_cast<int>(sim.current_tick);
                }
                if (max_ticks > 0 && (int)sim.current_tick >= max_ticks) {
                    window.close();
                    break;
                }
            }
        }

        renderer.draw(world, sim, window);
    }

    log_file.flush();
    log_file.close();

    float peak_pct = (total_agents > 0)
        ? 100.0f * static_cast<float>(peak_infected) / static_cast<float>(total_agents)
        : 0.0f;

    std::string fastest_zone = "N/A";
    std::string slowest_zone = "N/A";
    if (!world.zones.empty()) {
        uint32_t max_inf = 0, min_inf = UINT32_MAX;
        for (const auto& z : world.zones) {
            if (z.infected_count > max_inf) { max_inf = z.infected_count; fastest_zone = z.name; }
            if (z.infected_count < min_inf) { min_inf = z.infected_count; slowest_zone = z.name; }
        }
    }

    std::string top_platform     = topPlatformName(world, sim);
    float       top_platform_pct = topPlatformPct(world);

    std::cout << "\n=== SIMULATION COMPLETE ===\n";
    std::cout << "Peak infected:   " << peak_infected
              << " (" << peak_pct << "%) at tick " << peak_tick << "\n";
    std::cout << "Time to 10%:     "
              << (first_tick_10pct >= 0 ? std::to_string(first_tick_10pct) : "never")
              << " ticks\n";
    std::cout << "Top platform:    " << top_platform
              << " (" << top_platform_pct << "% of transmissions)\n";
    std::cout << "Fastest zone:    " << fastest_zone << "\n";
    std::cout << "Slowest zone:    " << slowest_zone << "\n";

    return 0;
}
