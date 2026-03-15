// EventSystem.cpp — scenario event loading and dispatch.
//
// JSON parser uses manual string scanning (extractString/extractFloat) rather than a
// library. Scenario files are flat one-level objects; nesting is not supported.
// Concatenating all lines before scanning avoids issues with multi-line JSON objects.
#include "EventSystem.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

void EventSystem::schedule(SimEvent e) {
    event_queue.push(std::move(e));
}

void EventSystem::processAll(Simulation& sim, uint32_t current_tick) {
    while (!event_queue.empty() &&
           event_queue.top().trigger_tick <= current_tick)
    {
        SimEvent e = event_queue.top();
        event_queue.pop();

        switch (e.type) {

        case EventType::INJECT_MISINFO: {
            uint32_t agent_id = e.float_params.count("agent_id")
                ? (uint32_t)e.float_params.at("agent_id") : 0;
            float ev = e.float_params.count("emotional_valence")
                ? e.float_params.at("emotional_valence") : 0.7f;
            float pl = e.float_params.count("plausibility")
                ? e.float_params.at("plausibility") : 0.6f;
            bool vi = e.float_params.count("via_influencer")
                ? e.float_params.at("via_influencer") > 0.5f : false;
            sim.injectMisinformation(agent_id, ev, pl, vi);
            break;
        }

        case EventType::INJECT_FACTCHECK: {
            Platform p = e.string_params.count("platform")
                ? stringToPlatform(e.string_params.at("platform"))
                : Platform::TWITTER;
            float eff = e.float_params.count("effectiveness")
                ? e.float_params.at("effectiveness") : 0.5f;
            sim.injectFactCheck(eff, p);
            break;
        }

        case EventType::CRISIS_TRIGGER: {
            float intensity = e.float_params.count("intensity")
                ? e.float_params.at("intensity") : 0.6f;
            int duration = e.float_params.count("duration_ticks")
                ? (int)e.float_params.at("duration_ticks") : 100;
            sim.triggerCrisisEvent(intensity, duration);
            break;
        }

        case EventType::INTERNET_SHUTDOWN: {
            if (!e.string_params.count("zone")) break;
            const std::string& zone_name = e.string_params.at("zone");
            for (Zone& z : sim.world.zones) {
                if (z.name == zone_name) {
                    z.internet_penetration = 0.0f;
                    std::cout << "Tick " << current_tick
                              << ": Internet shutdown in " << zone_name << "\n";
                    break;
                }
            }
            break;
        }

        case EventType::BOT_ACTIVATION: {
            // Bots are identified by zone_id % zones.size() matching botnet_id —
            // that's how injectBotnet() distributes them across zones.
            int botnet_id = e.float_params.count("botnet_id")
                ? (int)e.float_params.at("botnet_id") : 0;
            int activated = 0;
            for (size_t i = 0; i < sim.world.agents.size(); ++i) {
                Agent& a = sim.world.agents[i];
                if (a.is_bot && (a.zone_id % sim.world.zones.size()) == (size_t)botnet_id) {
                    a.state = BeliefState::INFECTED;
                    sim.current_states[i] = BeliefState::INFECTED;
                    activated++;
                }
            }
            std::cout << "Tick " << current_tick
                      << ": Botnet " << botnet_id << " activated ("
                      << activated << " bots)\n";
            break;
        }

        case EventType::INFLUENCER_SHARE: {
            if (!e.string_params.count("zone")) break;
            const std::string& zone_name = e.string_params.at("zone");
            int top_n = e.float_params.count("top_n")
                ? (int)e.float_params.at("top_n") : 3;
            for (const Zone& z : sim.world.zones) {
                if (z.name != zone_name) continue;
                std::vector<uint32_t> zone_agents;
                for (uint32_t i = z.agent_start_idx;
                     i < z.agent_start_idx + z.agent_count; ++i)
                    zone_agents.push_back(i);
                std::sort(zone_agents.begin(), zone_agents.end(),
                    [&](uint32_t a, uint32_t b) {
                        return sim.world.agents[a].social_influence
                             > sim.world.agents[b].social_influence;
                    });
                int infect_n = std::min(top_n, (int)zone_agents.size());
                for (int i = 0; i < infect_n; ++i) {
                    uint32_t idx = zone_agents[i];
                    sim.world.agents[idx].state = BeliefState::INFECTED;
                    sim.current_states[idx]     = BeliefState::INFECTED;
                }
                std::cout << "Tick " << current_tick
                          << ": " << infect_n << " influencers seeded in "
                          << zone_name << "\n";
                break;
            }
            break;
        }

        }
    }
}

Platform EventSystem::stringToPlatform(const std::string& s) const {
    if (s == "Facebook")  return Platform::FACEBOOK;
    if (s == "WhatsApp")  return Platform::WHATSAPP;
    if (s == "TikTok")    return Platform::TIKTOK;
    if (s == "Twitter")   return Platform::TWITTER;
    if (s == "YouTube")   return Platform::YOUTUBE;
    if (s == "Reddit")    return Platform::REDDIT;
    if (s == "WeChat")    return Platform::WECHAT;
    if (s == "Telegram")  return Platform::TELEGRAM;
    return Platform::UNKNOWN;
}

bool EventSystem::hasKey(const std::string& line, const std::string& key) const {
    return line.find("\"" + key + "\"") != std::string::npos;
}

std::string EventSystem::extractString(const std::string& line, const std::string& key) const {
    size_t key_pos = line.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";
    size_t colon = line.find(':', key_pos);
    if (colon == std::string::npos) return "";
    size_t q1 = line.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return line.substr(q1 + 1, q2 - q1 - 1);
}

float EventSystem::extractFloat(const std::string& line, const std::string& key) const {
    size_t key_pos = line.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return 0.0f;
    size_t colon = line.find(':', key_pos);
    if (colon == std::string::npos) return 0.0f;
    size_t val_start = colon + 1;
    while (val_start < line.size() &&
           (line[val_start] == ' ' || line[val_start] == '\t'))
        val_start++;
    try   { return std::stof(line.substr(val_start)); }
    catch (...) { return 0.0f; }
}

static EventType parseEventType(const std::string& s) {
    if (s == "INJECT_MISINFO")    return EventType::INJECT_MISINFO;
    if (s == "INJECT_FACTCHECK")  return EventType::INJECT_FACTCHECK;
    if (s == "CRISIS_TRIGGER")    return EventType::CRISIS_TRIGGER;
    if (s == "INTERNET_SHUTDOWN") return EventType::INTERNET_SHUTDOWN;
    if (s == "BOT_ACTIVATION")    return EventType::BOT_ACTIVATION;
    if (s == "INFLUENCER_SHARE")  return EventType::INFLUENCER_SHARE;
    throw std::runtime_error("Unknown event type: " + s);
}

void EventSystem::loadFromJSON(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open scenario: " + path);

    std::string full, line;
    while (std::getline(file, line)) full += line + " ";

    size_t arr_start = full.find("\"events\"");
    if (arr_start == std::string::npos)
        throw std::runtime_error("No 'events' key in " + path);

    size_t pos = full.find('[', arr_start);
    int loaded = 0;

    while (pos != std::string::npos) {
        size_t obj_start = full.find('{', pos + 1);
        if (obj_start == std::string::npos) break;
        size_t obj_end = full.find('}', obj_start);
        if (obj_end == std::string::npos) break;

        std::string obj = full.substr(obj_start, obj_end - obj_start + 1);

        if (!hasKey(obj, "tick") || !hasKey(obj, "type")) {
            pos = obj_end;
            continue;
        }

        SimEvent e;
        e.trigger_tick = (uint32_t)extractFloat(obj, "tick");

        std::string type_str = extractString(obj, "type");
        try { e.type = parseEventType(type_str); }
        catch (...) { pos = obj_end; continue; }

        for (const char* key : {"agent_id","emotional_valence","plausibility",
                                 "via_influencer","effectiveness","intensity",
                                 "duration_ticks","botnet_id","top_n"})
            if (hasKey(obj, key))
                e.float_params[key] = extractFloat(obj, key);

        for (const char* key : {"platform","zone"})
            if (hasKey(obj, key))
                e.string_params[key] = extractString(obj, key);

        event_queue.push(e);
        loaded++;
        pos = obj_end;
    }

    std::cout << "Loaded scenario: " << path << " (" << loaded << " events)\n";
}
