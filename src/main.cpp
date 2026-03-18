// main.cpp — country-mode simulation with individual agents, social graph, and scenarios.
//
// argv[1] = agent count (default: 25, clamped 5–60)
#include <SFML/Graphics.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <ctime>
#include <algorithm>

#include "Agent.h"
#include "PlatformProfile.h"
#include "World.h"
#include "DataLoader.h"
#include "Simulation.h"
#include "Renderer.h"
#include "CountryBorders.h"
#include "NetworkBuilder.h"
#include "EventSystem.h"

static_assert(sizeof(Agent) == 64, "Agent size check");

// Build a single-zone world for one country.
// Loads full CSV to find zone parameters, then creates fresh single-zone world.
// Adds 2 stationary bot agents and builds a Watts-Strogatz-style social graph.
static World buildCountryWorld(const std::string& iso3,
                                int num_agents) {
    World full = DataLoader::buildWorld("data/processed/agents_config.csv", 1);

    World w;
    w.buildPlatformProfiles();

    Zone z;
    z.id   = 0;
    z.name = iso3;

    bool found = false;
    for (const auto& fz : full.zones) {
        if (fz.name == iso3) {
            z.avg_literacy            = fz.avg_literacy;
            z.avg_press_freedom       = fz.avg_press_freedom;
            z.avg_polarization        = fz.avg_polarization;
            z.internet_penetration    = fz.internet_penetration;
            z.avg_institutional_trust = fz.avg_institutional_trust;
            z.dominant_platforms[0]   = fz.dominant_platforms[0];
            z.dominant_platforms[1]   = fz.dominant_platforms[1];
            z.dominant_platforms[2]   = fz.dominant_platforms[2];
            z.platform_penetration[0] = fz.platform_penetration[0];
            z.platform_penetration[1] = fz.platform_penetration[1];
            z.platform_penetration[2] = fz.platform_penetration[2];
            z.region                  = fz.region;
            found = true;
            break;
        }
    }

    if (!found) {
        z.avg_literacy            = 0.75f;
        z.avg_press_freedom       = 0.55f;
        z.avg_polarization        = 0.45f;
        z.internet_penetration    = 0.70f;
        z.avg_institutional_trust = 0.45f;
        z.dominant_platforms[0]   = Platform::FACEBOOK;
        z.dominant_platforms[1]   = Platform::WHATSAPP;
        z.dominant_platforms[2]   = Platform::YOUTUBE;
        z.platform_penetration[0] = 0.70f;
        z.platform_penetration[1] = 0.55f;
        z.platform_penetration[2] = 0.50f;
        z.region                  = "Unknown";
        std::cout << "Note: " << iso3 << " not in CSV — using defaults\n";
    }

    z.infected_count  = 0;
    z.recovered_count = 0;
    z.spread_velocity = 0.f;

    // Reserve extra capacity for bots
    const int NUM_BOTS = 2;
    w.reserveCapacity(num_agents + NUM_BOTS + 1, 1);

    std::mt19937 rng(42);
    DataLoader::spawnAgentsForZone(w, z, num_agents, rng);
    w.zones.push_back(z);

    // Build social graph first (BA preferential attachment on regular agents)
    w.adjacency_list.assign(w.agents.size(), {});
    NetworkBuilder::build(w, 4);

    // Inject botnet via NetworkBuilder: creates a fully-connected bot clique
    // wired into the top influencer nodes — much richer topology than manual injection.
    NetworkBuilder::injectBotnet(w, 1, NUM_BOTS);

    std::cout << "Country " << iso3 << ": " << num_agents << " agents + "
              << NUM_BOTS << " bots  "
              << "literacy=" << z.avg_literacy
              << " polarization=" << z.avg_polarization << "\n";
    return w;
}

int main(int argc, char* argv[]) {
    int num_agents = (argc > 1) ? std::atoi(argv[1]) : 25;
    if (num_agents < 5)  num_agents = 5;
    if (num_agents > 60) num_agents = 60;

    auto defs = getCountryDefs();
    if (defs.empty()) { std::cerr << "No country definitions.\n"; return 1; }

    int selected = 0;
    std::cout << "Building world for " << defs[selected].iso3 << "...\n";
    World world = buildCountryWorld(defs[selected].iso3, num_agents);

    Simulation sim(world);

    // Register pre-allocated bot IDs with the simulation
    for (size_t i = 0; i < world.agents.size(); ++i)
        if (world.agents[i].is_bot)
            sim.bot_agent_ids.push_back((uint32_t)i);

    sim.injectMisinformation(0, 0.7f, 0.65f, false);

    sf::RenderWindow window(sf::VideoMode(WIN_W, WIN_H), "Infodemic Flux");
    window.setFramerateLimit(60);

    Renderer renderer;
    renderer.init(world, window, defs[selected]);
    renderer.selected_country  = selected;
    renderer.selected_scenario = 0;   // no scenario selected

    std::filesystem::create_directories("logs");
    std::time_t ts = std::time(nullptr);
    std::ofstream log_file("logs/run_" + std::to_string((long)ts) + ".csv");
    if (log_file.is_open()) sim.logStats(log_file);

    int  ticks_per_frame = 1;
    bool paused          = false;
    const float SPREAD_RADIUS = 55.f;

    // EventSystem: runs each tick to dispatch JSON-scripted events.
    // Load custom scenario from scenarios/custom.json if present.
    EventSystem event_system;
    {
        const std::string custom_path = "scenarios/custom.json";
        if (std::filesystem::exists(custom_path)) {
            try {
                event_system.loadFromJSON(custom_path);
                std::cout << "Custom scenario loaded from " << custom_path << "\n";
            } catch (const std::exception& ex) {
                std::cerr << "Warning: " << ex.what() << "\n";
            }
        } else {
            std::cout << "Tip: place a scenarios/custom.json to script your own events.\n";
        }
    }

    std::cout << "\nControls:\n"
              << "  Click country    switch country\n"
              << "  1/2/3            load scenario (Controlled Burn / Silent Wildfire / Perfect Storm)\n"
              << "  Space            pause/resume\n"
              << "  F                fact-check via dominant platform\n"
              << "  C                trigger crisis event\n"
              << "  B                activate botnet\n"
              << "  I                internet shutdown (zero penetration this zone)\n"
              << "  +/-              speed up/down\n"
              << "  Esc              quit\n\n";

    while (window.isOpen()) {
        sf::Event e;
        while (window.pollEvent(e)) {
            if (e.type == sf::Event::Closed) window.close();

            if (e.type == sf::Event::MouseButtonPressed &&
                e.mouseButton.button == sf::Mouse::Left) {

                float mx = (float)e.mouseButton.x;
                float my = (float)e.mouseButton.y;

                // Scenario buttons take priority
                int scen = renderer.handleScenarioClick(mx, my);
                if (scen >= 0) {
                    ScenarioType st = (ScenarioType)(scen + 1);
                    sim.loadScenario(st);
                    renderer.selected_scenario = scen + 1;
                    renderer.resetForScenario();
                    std::cout << "Scenario: " << sim.active_scenario.name << "\n";
                }

                int clicked = renderer.handleClick(mx, my);
                if (clicked >= 0 && clicked < (int)defs.size() &&
                    clicked != renderer.selected_country) {
                    selected = clicked;
                    renderer.selected_country  = selected;
                    renderer.selected_scenario = 0;
                    std::cout << "Switching to " << defs[selected].display_name << "\n";
                    world = buildCountryWorld(defs[selected].iso3, num_agents);
                    sim.~Simulation();
                    new (&sim) Simulation(world);
                    sim.bot_agent_ids.clear();
                    for (size_t i = 0; i < world.agents.size(); ++i)
                        if (world.agents[i].is_bot)
                            sim.bot_agent_ids.push_back((uint32_t)i);
                    sim.injectMisinformation(0, 0.7f, 0.65f, false);
                    renderer.switchCountry(world, defs[selected]);
                }
            }

            if (e.type == sf::Event::KeyPressed) {
                switch (e.key.code) {

                case sf::Keyboard::Escape:
                    window.close(); break;

                case sf::Keyboard::Space:
                    paused = !paused;
                    renderer.paused = paused;
                    std::cout << (paused ? "Paused\n" : "Resumed\n"); break;

                case sf::Keyboard::F: {
                    Platform fp = world.zones.empty()
                        ? Platform::TWITTER
                        : world.zones[0].dominant_platforms[0];
                    sim.injectFactCheck(0.6f, fp);
                    int deploy = sim.factcheck_deploy_count.count((int)fp)
                               ? sim.factcheck_deploy_count.at((int)fp) : 1;
                    float eff = 0.6f * std::pow(0.8f, (float)(deploy - 1));
                    char buf[80];
                    std::snprintf(buf, sizeof(buf), "Fact-check via %s (eff=%.0f%%)",
                        sim.platformName(fp).c_str(), eff * 100.f);
                    renderer.logEvent({buf, sim.current_tick, false});
                    break;
                }

                case sf::Keyboard::C:
                    sim.triggerCrisisEvent(0.65f, 80);
                    renderer.logEvent({"Crisis: fear spiked — agents more credulous",
                                       sim.current_tick, true});
                    break;

                case sf::Keyboard::B:
                    sim.activateBotnet();
                    renderer.logEvent({"Botnet activated — bots now spreading",
                                       sim.current_tick, true});
                    break;

                case sf::Keyboard::I: {
                    // Internet shutdown: zero penetration for this zone, schedule via EventSystem
                    LegacySimEvent iev;
                    iev.trigger_tick = sim.current_tick;
                    iev.type         = EventType::INTERNET_SHUTDOWN;
                    iev.string_params["zone"] = defs[selected].iso3;
                    event_system.schedule(std::move(iev));
                    event_system.processAll(sim, sim.current_tick);
                    renderer.logEvent({"Internet shutdown — spread suppressed",
                                       sim.current_tick, true});
                    break;
                }

                case sf::Keyboard::Add:
                case sf::Keyboard::Equal:
                    ticks_per_frame = std::min(10, ticks_per_frame + 1);
                    std::cout << "Speed: " << ticks_per_frame << "\n"; break;

                case sf::Keyboard::Subtract:
                case sf::Keyboard::Hyphen:
                    ticks_per_frame = std::max(1, ticks_per_frame - 1);
                    std::cout << "Speed: " << ticks_per_frame << "\n"; break;

                case sf::Keyboard::Num1:
                case sf::Keyboard::Numpad1: {
                    sim.loadScenario(ScenarioType::CONTROLLED_BURN);
                    renderer.selected_scenario = 1;
                    renderer.resetForScenario();
                    std::cout << "Scenario: " << sim.active_scenario.name << "\n"; break;
                }
                case sf::Keyboard::Num2:
                case sf::Keyboard::Numpad2: {
                    sim.loadScenario(ScenarioType::SILENT_WILDFIRE);
                    renderer.selected_scenario = 2;
                    renderer.resetForScenario();
                    std::cout << "Scenario: " << sim.active_scenario.name << "\n"; break;
                }
                case sf::Keyboard::Num3:
                case sf::Keyboard::Numpad3: {
                    sim.loadScenario(ScenarioType::PERFECT_STORM);
                    renderer.selected_scenario = 3;
                    renderer.resetForScenario();
                    std::cout << "Scenario: " << sim.active_scenario.name << "\n"; break;
                }

                default: break;
                }
            }
        }

        if (!paused) {
            renderer.updateAgentMovement(world, ticks_per_frame);

            for (int t = 0; t < ticks_per_frame; ++t) {
                event_system.processAll(sim, sim.current_tick);
                sim.tickProximity(renderer.agent_pos, SPREAD_RADIUS);
                sim.logStats(log_file);

                // Pulse rings for newly infected
                for (uint32_t idx : sim.newly_infected)
                    renderer.spawnPulse(idx);

                // Transmission events → story log
                for (const auto& tx : sim.transmissions_this_tick) {
                    char buf[80];
                    if (tx.src == UINT32_MAX) {
                        std::snprintf(buf, sizeof(buf), "TikTok FYP → %s",
                            renderer.agentName(tx.dst));
                    } else {
                        std::snprintf(buf, sizeof(buf), "%s → %s  via %s",
                            renderer.agentName(tx.src),
                            renderer.agentName(tx.dst),
                            sim.platformName(tx.plat).c_str());
                    }
                    renderer.logEvent({buf, sim.current_tick, true});
                }

                // Scenario auto-events → story log
                for (const auto& ev : sim.auto_events_this_tick)
                    renderer.logEvent({ev.msg, sim.current_tick, ev.is_alert});

                // Virality flash
                if (sim.virality_flash_this_tick)
                    renderer.triggerViralityFlash();
            }
        }

        renderer.draw(world, sim, window);
    }

    log_file.close();
    return 0;
}
