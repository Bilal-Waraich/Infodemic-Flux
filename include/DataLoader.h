// DataLoader.h — reads agents_config.csv and constructs a fully populated World.
#pragma once

#include <string>
#include <random>
#include "World.h"
#include "Zone.h"

class DataLoader {
public:
    // Returns an empty World on file-open failure.
    static World buildWorld(const std::string& csv_path, int agents_per_zone);
    static void spawnAgentsForZone(World& w, Zone& z, int count, std::mt19937& rng);

private:

    // Returns Platform::UNKNOWN for unrecognised names.
    static Platform parsePlatform(const std::string& p_str);

    // Returns default_val and logs a warning on empty or unparseable input.
    static float parseFloatOrDefault(const std::string& str, float default_val);
};
