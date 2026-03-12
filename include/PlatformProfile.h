// PlatformProfile.h — per-platform spread constants used throughout the simulation.
#pragma once

#include <cstdint>

// uint8_t underlies Platform so it doubles as a direct index into
// World::transmissions_by_platform[9].
enum class Platform : uint8_t {
    FACEBOOK=0, WHATSAPP=1, TIKTOK=2, TWITTER=3,
    YOUTUBE=4, REDDIT=5, WECHAT=6, TELEGRAM=7, UNKNOWN=8
};

struct PlatformProfile {
    Platform type;

    // avg ticks to propagate one hop; TikTok=1 (FYP bypasses social graph), YouTube=5 (passive)
    float spread_speed;

    // WhatsApp=1.8 because messages arrive from known contacts, raising perceived credibility.
    float trust_multiplier;

    // 0=fully encrypted (WhatsApp, WeChat); 1=fully public (Twitter).
    // Key parameter behind the correction blind-spot finding.
    float visibility;

    // TikTok=2.2 (FYP); Reddit=0.7 (community votes partially suppress misinformation).
    float algorithm_amp;

    // Fraction of infected agents reachable by a fact-check on this platform.
    // Twitter=0.9 (public feed labels) vs WhatsApp=0.05 (E2E encryption, no feed surface).
    // 18x asymmetry is the model's core structural finding.
    float correction_reach;

    // YouTube=500 (indexed for years); Twitter=8 (feed moves fast).
    float content_longevity;

    // TikTok=2.0: watch-time optimisation rewards fear/anger content.
    float engagement_reward;

    int avg_connections;

    // Called once at startup by World::buildPlatformProfiles().
    static PlatformProfile defaults(Platform p) {
        switch (p) {
            case Platform::FACEBOOK:
                return {Platform::FACEBOOK, 3.0f, 1.2f, 0.9f, 1.5f, 0.6f, 200.0f, 1.4f, 150};
            case Platform::WHATSAPP:
                // Zero visibility = encrypted; correction_reach=0.05 is the structural blind spot.
                return {Platform::WHATSAPP, 2.0f, 1.8f, 0.0f, 1.0f, 0.05f, 100.0f, 1.0f, 50};
            case Platform::TIKTOK:
                return {Platform::TIKTOK, 1.0f, 0.9f, 0.95f, 2.2f, 0.4f, 20.0f, 2.0f, 500};
            case Platform::TWITTER:
                return {Platform::TWITTER, 1.0f, 0.8f, 1.0f, 1.3f, 0.9f, 8.0f, 1.2f, 200};
            case Platform::YOUTUBE:
                return {Platform::YOUTUBE, 5.0f, 1.1f, 0.9f, 1.8f, 0.3f, 500.0f, 1.6f, 100};
            case Platform::REDDIT:
                return {Platform::REDDIT, 4.0f, 0.9f, 0.95f, 0.7f, 0.7f, 50.0f, 0.6f, 80};
            case Platform::WECHAT:
                return {Platform::WECHAT, 2.0f, 1.7f, 0.0f, 1.1f, 0.05f, 80.0f, 1.1f, 60};
            case Platform::TELEGRAM:
                return {Platform::TELEGRAM, 2.0f, 1.4f, 0.3f, 1.0f, 0.15f, 60.0f, 1.0f, 100};
            case Platform::UNKNOWN:
            default:
                return {Platform::UNKNOWN, 3.0f, 1.0f, 0.5f, 1.0f, 0.5f, 50.0f, 1.0f, 100};
        }
    }
};
