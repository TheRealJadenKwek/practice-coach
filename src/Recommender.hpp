#pragma once

#include "HotspotMap.hpp"
#include "Persistence.hpp"
#include <string>
#include <vector>

struct Recommendation {
    std::string headline;            // the gold line on the HUD
    bool  hasDrill = false;
    float drillLo = 0.f, drillHi = 0.f;
    float suggestedStartPct = 0.f;   // where to start the suggested run from
};

// "What run to go for next." Ranks sections by EV-lost (reachRate * failRate):
// the wall that drains the most expected attempts from a full clear, not just
// your single worst spot. Ties break toward the later section (back-to-front).
class Recommender {
public:
    // reliableReach = how far you currently chain from a cold start; the recommender
    // skips sections you already pass and targets the first wall PAST that edge.
    static Recommendation recommend(LevelStats const& stats,
                                    std::vector<ComputedSection> const& sections,
                                    float reliableReach);
};
