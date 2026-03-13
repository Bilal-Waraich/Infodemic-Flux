// DataLoader.cpp — CSV parser and agent spawner.
#include "DataLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

Platform DataLoader::parsePlatform(const std::string& p_str) {
    if (p_str == "Facebook") return Platform::FACEBOOK;
    if (p_str == "WhatsApp") return Platform::WHATSAPP;
    if (p_str == "TikTok") return Platform::TIKTOK;
    if (p_str == "Twitter" || p_str == "X") return Platform::TWITTER;
    if (p_str == "YouTube") return Platform::YOUTUBE;
    if (p_str == "Reddit") return Platform::REDDIT;
    if (p_str == "WeChat") return Platform::WECHAT;
    if (p_str == "Telegram") return Platform::TELEGRAM;
    return Platform::UNKNOWN;
}

float DataLoader::parseFloatOrDefault(const std::string& str, float default_val) {
    if (str.empty()) {
        std::cerr << "Warning: Missing float value. Using default " << default_val << ".\n";
        return default_val;
    }
    try {
        return std::stof(str);
    } catch (...) {
        std::cerr << "Warning: Unparseable float value '" << str << "'. Using default " << default_val << ".\n";
        return default_val;
    }
}

void DataLoader::spawnAgentsForZone(World& w, Zone& z, int count, std::mt19937& rng) {
    z.agent_start_idx = w.agents.size();
    z.agent_count = count;

    auto tnorm = [&](float mean, float sigma) {
        return std::clamp(std::normal_distribution<float>(mean, sigma)(rng), 0.0f, 1.0f);
    };

    std::lognormal_distribution<float> lognorm_dist(0.0f, 1.2f);

    for (int i = 0; i < count; ++i) {
        Agent a{};
        a.id = w.agents.size();
        a.zone_id = z.id;
        a.state = BeliefState::SUSCEPTIBLE;
        a.exposure_timer = 0;
        a.overload_timer = 0;
        a.prior_exposure_count = 0;
        a.is_bot = false;

        a.literacy_score      = tnorm(z.avg_literacy,            0.12f);
        a.press_freedom       = tnorm(z.avg_press_freedom,       0.10f);
        a.polarization        = tnorm(z.avg_polarization,        0.10f);
        a.institutional_trust = tnorm(z.avg_institutional_trust, 0.10f);
        // Seed from agent's own polarization so high-polarisation individuals are personally more biased.
        a.confirmation_bias   = tnorm(a.polarization,            0.15f);
        a.emotional_state     = tnorm(0.2f,                      0.10f);
        a.sharing_habit       = tnorm(0.22f,                     0.18f);

        // /10 pulls lognormal into [0,1]; fat tail produces the small influencer fraction.
        float raw = lognorm_dist(rng);
        a.social_influence = std::clamp(raw / 10.0f, 0.0f, 1.0f);

        a.correction_resistance = tnorm(1.0f - z.avg_literacy, 0.10f);

        a.platforms[0] = z.dominant_platforms[0];
        a.platform_count = 1;

        if (tnorm(z.platform_penetration[1], 0.05f) > 0.5f) {
            a.platforms[1] = z.dominant_platforms[1];
            a.platform_count = 2;
        }

        if (a.platform_count == 2 && tnorm(z.platform_penetration[2], 0.05f) > 0.5f) {
            a.platforms[2] = z.dominant_platforms[2];
            a.platform_count = 3;
        }

        w.agents.push_back(a);
    }
}

World DataLoader::buildWorld(const std::string& csv_path, int agents_per_zone) {
    World w;
    std::vector<Zone> parsed_zones;

    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << csv_path << "\n";
        return w;
    }

    std::string line;
    std::getline(file, line); // skip header

    uint16_t current_id = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string cell;

        // RFC-4180 quoted-field parser: country names like "Yemen, Rep." contain commas.
        // A naive comma split would corrupt every numeric column that follows.
        bool in_quotes = false;
        std::string current_col;
        for (char c : line) {
            if (c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ',' && !in_quotes) {
                cols.push_back(current_col);
                current_col.clear();
            } else {
                current_col += c;
            }
        }
        cols.push_back(current_col);

        if (cols.size() < 14) {
            std::cerr << "Warning: Skipping malformed row: " << line << "\n";
            continue;
        }

        Zone z;
        z.id = current_id++;
        z.name = cols[0];
        if (!z.name.empty() && z.name.front() == '"') z.name = z.name.substr(1, z.name.length() - 2);

        // cols[1] is the ISO3 code — kept in CSV for Python use; not needed here.
        z.region = cols[2];
        if (!z.region.empty() && z.region.front() == '"') z.region = z.region.substr(1, z.region.length() - 2);

        z.avg_literacy            = parseFloatOrDefault(cols[3], 0.5f);
        z.avg_press_freedom       = parseFloatOrDefault(cols[4], 0.5f);
        z.avg_polarization        = parseFloatOrDefault(cols[5], 0.5f);
        z.internet_penetration    = parseFloatOrDefault(cols[6], 0.5f);
        z.avg_institutional_trust = parseFloatOrDefault(cols[7], 0.5f);

        z.dominant_platforms[0] = parsePlatform(cols[8]);
        z.dominant_platforms[1] = parsePlatform(cols[9]);
        z.dominant_platforms[2] = parsePlatform(cols[10]);

        z.platform_penetration[0] = parseFloatOrDefault(cols[11], 0.5f);
        z.platform_penetration[1] = parseFloatOrDefault(cols[12], 0.5f);
        z.platform_penetration[2] = parseFloatOrDefault(cols[13], 0.5f);

        z.infected_count = 0;
        z.recovered_count = 0;
        z.spread_velocity = 0.0f;

        parsed_zones.push_back(z);
    }

    w.reserveCapacity(parsed_zones.size() * agents_per_zone, parsed_zones.size());
    w.buildPlatformProfiles();

    std::mt19937 rng(42);

    for (Zone& z : parsed_zones) {
        spawnAgentsForZone(w, z, agents_per_zone, rng);
        w.zones.push_back(z);
    }

    w.adjacency_list.resize(w.agents.size());

    size_t agents_bytes = w.agents.capacity() * sizeof(Agent);
    float agents_mb = agents_bytes / (1024.0f * 1024.0f);

    std::cout << "Loaded " << w.zones.size() << " zones, spawned " << w.agents.size() << " agents\n";
    std::cout << "Agent array: " << agents_bytes << " bytes (" << std::fixed << std::setprecision(2) << agents_mb << " MB)\n";

    // Vulnerability = low literacy + low institutional trust + high polarisation.
    struct ZoneScore { std::string name; float score; };
    std::vector<ZoneScore> scores;
    for (const auto& z : w.zones) {
        float score = (1.0f - z.avg_literacy)*0.4f + (1.0f - z.avg_institutional_trust)*0.3f + z.avg_polarization*0.3f;
        scores.push_back({z.name, score});
    }

    std::sort(scores.begin(), scores.end(), [](const ZoneScore& a, const ZoneScore& b) {
        return a.score > b.score;
    });

    std::cout << "Top 3 most vulnerable zones:\n";
    for (size_t i = 0; i < std::min<size_t>(3, scores.size()); ++i)
        std::cout << "  " << i+1 << ". " << scores[i].name << " (" << std::fixed << std::setprecision(3) << scores[i].score << ")\n";

    std::cout << "Top 3 most resistant zones:\n";
    for (size_t i = 0; i < std::min<size_t>(3, scores.size()); ++i) {
        size_t idx = scores.size() - 1 - i;
        std::cout << "  " << i+1 << ". " << scores[idx].name << " (" << std::fixed << std::setprecision(3) << scores[idx].score << ")\n";
    }

    return w;
}
