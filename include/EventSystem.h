// EventSystem.h — scenario event scheduler and JSON loader.
//
// JSON parser uses manual string scanning rather than a library — scenario files are
// flat one-level objects; a full dependency for two value types isn't justified.
#pragma once
#include "Simulation.h"
#include <queue>
#include <vector>
#include <string>
#include <unordered_map>

enum class EventType {
    INJECT_MISINFO,
    INJECT_FACTCHECK,
    CRISIS_TRIGGER,
    INTERNET_SHUTDOWN,
    BOT_ACTIVATION,
    INFLUENCER_SHARE
};

// Parameters stored in flat string-keyed maps so the JSON loader can fill them
// without knowing each EventType's schema in advance.
struct LegacySimEvent {
    uint32_t    trigger_tick;
    EventType   type;
    std::unordered_map<std::string, float>       float_params;
    std::unordered_map<std::string, std::string> string_params;

    bool operator>(const LegacySimEvent& o) const {
        return trigger_tick > o.trigger_tick;
    }
};

class EventSystem {
public:
    void schedule(LegacySimEvent e);

    // Must be called before Simulation::tick() each step.
    void processAll(Simulation& sim, uint32_t current_tick);

    // Throws std::runtime_error if file cannot be opened or has no "events" key.
    void loadFromJSON(const std::string& path);

    bool empty() const { return event_queue.empty(); }

private:
    std::priority_queue<
        LegacySimEvent,
        std::vector<LegacySimEvent>,
        std::greater<LegacySimEvent>> event_queue;

    Platform    stringToPlatform(const std::string& s) const;

    // Extracts "key": "value" from a flat JSON object string. Fails on nested objects.
    std::string extractString(const std::string& line, const std::string& key) const;

    // Extracts "key": <number>. Returns 0.0f on failure.
    float       extractFloat (const std::string& line, const std::string& key) const;

    bool        hasKey       (const std::string& line, const std::string& key) const;
};
