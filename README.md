# Infodemic Flux

[![Build](https://github.com/Bilal-Waraich/Infodemic-Flux/actions/workflows/build.yml/badge.svg)](https://github.com/Bilal-Waraich/Infodemic-Flux/actions/workflows/build.yml)

> A C++ agent-based simulation of misinformation spread across real country populations.
> Select a country, watch 25 named agents move across its map, and observe how a single
> false belief propagates — or gets corrected — through their social networks.

![Infodemic Flux demo](docs/demo.gif)

## What It Simulates

A single misinformation packet is injected into one agent (patient zero) at the start.
From there it spreads person-to-person based on proximity, platform mechanics, emotional
state, literacy, and social influence. Each agent is an individual with a name, a primary
social media platform, and real demographic parameters drawn from their country's data.

### Belief States

Every agent is always in one of four states:

| State | Sprite | Meaning |
|---|---|---|
| **Susceptible** | Green figure | Has not encountered the misinformation |
| **Exposed** | Orange figure (question mark) | Has seen it but hasn't decided whether to believe it yet — resolves after ~8 ticks |
| **Infected** | Red figure | Believes and actively spreads the misinformation |
| **Immune** | Blue medic figure | Was infected but mentally rejected or recovered from the belief — resistant to re-infection |

Agents labelled **Immune** in the right panel were previously **Infected** and have
recovered. In SIR epidemic modelling this state is called *Recovered*; in the context of
misinformation, recovery means the person has processed and rejected the false belief, which
confers resistance — so *Immune* is the more accurate label.

### Why Do Most Agents End Up Immune?

This is the herd immunity threshold playing out correctly. Each tick, infected agents have a
small natural recovery chance (`1.5% × literacy × (1 − correction_resistance)`). As the
susceptible pool shrinks, transmission slows, recoveries accumulate, and the misinformation
runs out of new hosts. The simulation ends in a stable state where the remaining susceptible
agents are protected by the recovered majority — structurally the same mechanism as epidemic
herd immunity, applied to belief propagation.

### Crisis Events (C key)

Pressing C triggers a crisis — a simulated shock (pandemic news, political scandal, war).
This spikes every agent's `emotional_state` by ~29%, making them more fearful and less
analytical. High emotional state directly amplifies infection probability: agents stop
evaluating evidence and react emotionally. The spike decays back to baseline over 80 ticks.
This models a well-documented real-world effect: misinformation spreads dramatically faster
during periods of collective fear or uncertainty.

### Fact-Check Campaigns (F key)

Pressing F deploys a correction campaign via the **selected country's dominant platform** —
so in India this means YouTube, in Nigeria WhatsApp, in the USA Facebook. Each infected
agent's recovery chance is:

```
P(recover) = 0.6 × platform.correction_reach × agent.literacy × (1 − correction_resistance)
```

Platform architecture matters enormously here. WhatsApp corrections are 8× less effective
than Twitter because end-to-end encryption means there is no feed surface to attach a
correction label to — the platform is structurally invisible to external correction campaigns.
A fact-check deployed on WhatsApp reaches approximately 5% of infected users; the same
campaign on Twitter reaches ~90%.

## Architecture

- **Language:** C++17, ~2800 lines across 11 source files
- **Agent struct:** 64 bytes exactly, cache-line aligned (`static_assert` enforced)
- **Spread model:** Proximity-based O(N²) — agents infect others within 55px on screen.
  Replaces graph traversal: for 25 agents this is trivially fast and visually accurate
- **Rendering:** SFML 2.6 — pixel art sprite sheet (6 × 128px figures), plain green
  political-style map per country, SIR chart, agent roster, transmission event log
- **Country borders:** Extracted from Natural Earth 110m GeoJSON, simplified to ≤120 points,
  agents spawn inside the real border polygon via rejection sampling
- **Simulation:** Double-buffered belief state (race-free), platform-specific spread
  multipliers, YouTube entrenchment ratchet, exposure window, crisis decay

## Platform Mechanics

| Platform | Spread modifier | Correction reach | Why |
|---|---|---|---|
| WhatsApp | +50% | 5% | High intimacy trust; encrypted — corrections can't reach |
| Twitter | −40% | 90% | Adversarial public context raises scepticism; corrections broadcast widely |
| Reddit | −50% | high | Downvoting and community moderation suppress low-quality claims |
| TikTok | algorithmic | moderate | FYP bypasses social graph entirely; correction competes with 50× confirming content |
| YouTube | standard | high | Recommendation rabbit-hole hardens `correction_resistance` over time |
| Facebook | standard | high | Dominant transmission platform in most countries |

## Data Sources

Country parameters (literacy, press freedom, polarisation, internet penetration, platform
dominance) are derived from real datasets:

| Dataset | Source | Parameter |
|---|---|---|
| Adult Literacy Rate | World Bank (SE.ADT.LITR.ZS) | `literacy_score` |
| Press Freedom Index | RSF via World Bank Data360 | `press_freedom_score` |
| Political Polarisation | V-Dem Institute (v2x_polyarchy) | `polarization_score` |
| Internet Penetration | ITU ICT Statistics | `internet_penetration` |
| Platform Dominance | DataReportal / We Are Social | `dominant_platforms` |
| Country Classifications | World Bank CLASS.xlsx | `region` |

## How to Build

```bash
git clone [your-repo-url]
cd infodemic-flux

# Install dependencies (macOS)
brew install cmake sfml@2
pip3 install pandas numpy

# Generate country border header from GeoJSON
python3 scripts/extract_borders.py

# Build data pipeline (agents_config.csv)
python3 scripts/build_agents_config.py

# Build and run
cmake -B build && cmake --build build
./build/simulation 25    # 25 agents (5–60 supported)
```

## Controls

| Key | Action |
|---|---|
| Click country | Switch to that country |
| Space | Pause / resume |
| F | Deploy fact-check via country's dominant platform |
| C | Trigger crisis event — spikes emotional state for 80 ticks |
| + / − | Speed up / slow down simulation |
| Esc | Quit |

## Sociological Implications

The simulation's starkest finding is structural, not numerical. WhatsApp's `correction_reach`
of 0.05 versus Twitter's 0.9 is not a tuned parameter — it is a direct encoding of platform
architecture. There is no feed, no algorithmic surface, and no public post on an encrypted
messaging app to attach a correction label to. The 18× asymmetry between platforms makes
quantitative what researchers have long argued qualitatively: encrypted messaging represents
a structural blind spot for the entire correction ecosystem. Transparency obligations and
algorithmic labelling regimes — the two dominant regulatory tools — are precisely useless
for the channel through which a large share of misinformation now travels.

The literacy correlation is real but limited. Higher-literacy populations are infected later,
not immune. The infection probability function includes confirmation bias, emotional state,
and social influence as amplifiers that operate independently of literacy. A highly literate
person in a high-polarisation environment, exposed to a high-emotional-valence claim from a
trusted contact, can still be infected — literacy raises the threshold but does not eliminate
susceptibility. The crisis mechanic makes this explicit: press C and watch literacy become
nearly irrelevant as emotional state overwhelms analytical processing.

The correction window problem is visible in the event log. A fact-check deployed early
reaches an epistemic audience; one deployed after prolonged infection faces a motivated
audience, because `correction_resistance` ratchets upward monotonically during infection
(modelling belief entrenchment). Early detection infrastructure — particularly on encrypted
platforms where the signal is currently invisible — is likely a higher-value investment than
improved correction content.
