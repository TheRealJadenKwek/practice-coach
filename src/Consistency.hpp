#pragma once
#include <string>

// Per-level "are you actually ready for a cold clear" read. Computed ONLY from
// 0%-start runs, so checkpoint/backwards practice can't inflate it -- difficulty
// dependence falls out empirically (on an extreme your cold runs die early, so the
// number is honestly low without any difficulty fudge factor).
struct ConsistencyResult {
    bool        ready = false;     // false -> still mapping; show only `verdict`
    bool        grinding = false;  // true on an above-your-level project (show progress, not cold-proven)
    float       cL = 0.f;         // cold-run clear readiness in [0,1]
    float       solidThrough = 0.f;  // cold reach (easy) OR best progress % (grind) -- see `grinding`
    std::string classText;        // "" if not ready/unknown, else "WALL" / "CHOKE @ 70-78%" / "worst @ ..."
    std::string verdict;          // the one-line coaching call
};

// Builds the read from StatsManager cold-run data joined with the Advisor's tier/skill.
ConsistencyResult computeConsistency(std::string const& levelKey);
