#pragma once

#include "Persistence.hpp"
#include <cstdint>
#include <vector>

// A section the engine derived from where you actually die. `reach` counts the
// attempts whose [start, endpoint] span overlapped this section, so
// failSm = deaths/reach is always a real probability in [0,1].
struct ComputedSection {
    float    lo = 0.f, hi = 0.f;  // percent bounds
    uint32_t reach = 0;
    uint32_t deaths = 0;
    float    failSm = 0.f;        // beta-smoothed fail rate [0,1]
    float    reachRate = 0.f;     // reach / total runs
    float    ev = 0.f;            // reachRate * failSm  (expected attempts drained)
    bool     trusted = false;     // enough data to act on
};

// Two-layer segmentation: a fixed fine grid is the online sufficient statistic
// (O(1) per attempt); sections are merged out of it on read (O(B)). No manual
// checkpoints needed -- boundaries emerge from your death distribution.
//
// Two histograms are kept: m_deaths (DEATHS only, the fail numerator) and m_ends
// (the endpoint of EVERY attempt incl. completions + abandons, the reach
// denominator). Splitting them lets a SURVIVED prefix raise a later section's
// reach without adding a death -- so practicing/clearing a section drives its
// failSm down. Practice + normal deaths are intentionally combined (this is a
// practice coach); each attempt's start% is tracked, so a checkpoint run only
// counts toward the sections it actually reached.
class HotspotMap {
public:
    // Trust threshold shared with the Recommender so the two never desync.
    static constexpr uint32_t MIN_DEATHS_HOT = 5;

    void reset();
    void rebuildFrom(std::vector<Attempt> const& attempts);
    // isDeath INVERTS the old `reachedEnd` polarity: pass true for a real death,
    // false for a completion or an abandon (a survived/partial run). endPct is the
    // furthest % the run reached.
    void record(float startPct, float endPct, bool isDeath);   // online, O(1)
    std::vector<ComputedSection> computeSections(int minReachTrust) const;  // O(B)
    double totalRuns() const { return m_n; }
    // Recency: each new attempt first decays all bins by lambda (<1) so recent runs
    // dominate. 1.0 = off (all runs equal). Set before rebuildFrom / record.
    void setRecency(double lambda) { m_lambda = lambda < 0.0 ? 0.0 : (lambda > 1.0 ? 1.0 : lambda); }

private:
    static constexpr int B = 200;     // 0.5%-wide bins across [0,100]
    double m_startAt[B] = {};         // attempts that began in each bin
    double m_deaths[B]  = {};         // DEATHS only (fail numerator; completions/abandons excluded)
    double m_ends[B]    = {};         // endpoint of EVERY attempt (death/complete/abandon)
    double m_n = 0.0;                 // total (recency-weighted) attempts
    double m_lambda = 1.0;            // recency decay applied per attempt (1.0 = off)
    static int binOf(float pct);
};
