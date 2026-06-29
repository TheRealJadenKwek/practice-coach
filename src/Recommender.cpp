#include "Recommender.hpp"
#include "Advisor.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

// Beta posterior mean of the pass-rate. BETA=2 here (mildly pessimistic) so that
// barely-tested sections aren't waved through as "solid".
static float passRateBeta(uint32_t reach, uint32_t deaths, float A, float Bp) {
    float dd = static_cast<float>(std::min(deaths, reach));
    float passed = static_cast<float>(reach) - dd;
    return (passed + A) / (static_cast<float>(reach) + A + Bp);
}

Recommendation Recommender::recommend(LevelStats const& stats,
                                      std::vector<ComputedSection> const& sections,
                                      float reliableReach) {
    Recommendation r;
    const int   COLD  = static_cast<int>(Mod::get()->getSettingValue<int64_t>("cold-start-runs"));
    const float FLOOR = static_cast<float>(Mod::get()->getSettingValue<double>("target-pass-floor"));
    const float A = 1.0f, Bp = 2.0f, RUNUP = 5.0f;

    // Cold start: not enough full runs to know where the real drain is.
    if (static_cast<int>(stats.fullRunAttempts) < COLD) {
        r.headline = fmt::format(
            "Do full runs to map your walls ({}/{}). One back-to-front pass helps.",
            stats.fullRunAttempts, COLD);
        return r;
    }

    std::vector<ComputedSection> cand;
    for (auto const& s : sections) if (s.reach > 0) cand.push_back(s);
    if (cand.empty()) {
        r.headline = "No death data yet - play a few runs to map your walls.";
        return r;
    }

    // Reliably chain to the end (cold) -> done drilling, go for it.
    if (reliableReach >= 95.0f) {
        r.headline = "You reliably reach the end - go for the full clear!";
        return r;
    }

    // Strategy depends on your PLAYSTYLE on this level, detected from the data: if your
    // cold reach is stuck near the start while you've practiced far past it, you're
    // grinding a project section-by-section (backwards) -- the "growing edge" (cold
    // reach) is useless, so target where you lose the MOST across ALL attempts. If your
    // cold reach is healthy, you're chaining forward -> follow the growing edge. The
    // tier guard keeps a level far BELOW your level always in chain-forward mode.
    double tier  = (stats.type == "online") ? Advisor::tierForLevelID(static_cast<int>(stats.numericId)) : 0.0;
    double skill = Advisor::get()->skill();
    bool   grind = (reliableReach < 25.0f && (stats.bestPercent - reliableReach) > 40.0f)
                   && !(tier > 0.0 && tier < skill - 5.0);

    if (grind) {
        // Worst wall = highest fail-rate among TRUSTED sections (where you actually die
        // most, practice included); fall back to most-deaths if nothing's trusted yet.
        int best = -1;
        float bestFail = -1.0f;
        for (size_t i = 0; i < cand.size(); ++i) {
            if (!cand[i].trusted) continue;
            float fail = 1.0f - passRateBeta(cand[i].reach, cand[i].deaths, A, Bp);
            if (fail > bestFail) { bestFail = fail; best = static_cast<int>(i); }
        }
        if (best < 0) {
            uint32_t most = 0;
            for (size_t i = 0; i < cand.size(); ++i)
                if (cand[i].deaths >= most) { most = cand[i].deaths; best = static_cast<int>(i); }
        }
        ComputedSection const& d = cand[best];
        float fail = 1.0f - passRateBeta(d.reach, d.deaths, A, Bp);
        r.hasDrill = true;
        r.drillLo = d.lo;
        r.drillHi = d.hi;
        r.suggestedStartPct = std::max(0.0f, d.lo - RUNUP);
        r.headline = fmt::format(
            "DRILL your worst wall: {:.0f}-{:.0f}%  (fail {:.0f}%) - chip at it backwards.",
            d.lo, d.hi, fail * 100.0f);
        return r;
    }

    // GROWING EDGE (at/below your level): the goal is your IMMEDIATE next wall.
    std::vector<ComputedSection> edge;
    for (auto const& s : cand) if (s.hi > reliableReach + 1.0f) edge.push_back(s);
    if (edge.empty()) {
        r.headline = fmt::format("You reliably reach {:.0f}% - go for the full clear!", reliableReach);
        return r;
    }
    cand = edge;

    bool allSolid = true;
    for (auto const& s : cand)
        if (passRateBeta(s.reach, s.deaths, A, Bp) < FLOOR) { allSolid = false; break; }
    if (allSolid) {
        r.headline = "All tracked sections are solid - go for the full clear!";
        return r;
    }

    // Full-run reach: weight by how often you reach a section in FULL ("fun") runs,
    // not in checkpoint practice (which would inflate late sections' reach).
    int fullRuns = 0;
    for (auto const& a : stats.attempts) if (a.startPct <= 0.5f) ++fullRuns;
    auto reachProbFull = [&](float lo) -> float {
        if (fullRuns == 0) return -1.0f;
        int reached = 0;
        for (auto const& a : stats.attempts) {
            if (a.startPct > 0.5f) continue;
            if (a.completed || a.deathPct >= lo) ++reached;
        }
        return static_cast<float>(reached) / static_cast<float>(fullRuns);
    };

    // First TRUSTED edge wall (cand sorted by lo); fall back to the very first.
    int best = 0;
    for (size_t i = 0; i < cand.size(); ++i)
        if (cand[i].trusted) { best = static_cast<int>(i); break; }

    ComputedSection const& d = cand[best];
    float fail = 1.0f - passRateBeta(d.reach, d.deaths, A, Bp);
    float drp = reachProbFull(d.lo);
    if (drp < 0.0f) drp = d.reachRate;
    if (d.lo <= 1.0f) drp = std::min(drp, 0.85f);
    r.hasDrill = true;
    r.drillLo = d.lo;
    r.drillHi = d.hi;
    r.suggestedStartPct = std::max(0.0f, d.lo - RUNUP);
    r.headline = fmt::format(
        "NEXT RUN: from {:.0f}% -> clear past {:.0f}%  (reach {:.0f}% of full runs | fail {:.0f}%)",
        r.suggestedStartPct, d.hi, drp * 100.0f, fail * 100.0f);
    return r;
}
