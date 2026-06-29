#include "Consistency.hpp"
#include "StatsManager.hpp"
#include "Advisor.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

ConsistencyResult computeConsistency(std::string const& key) {
    ConsistencyResult out;
    auto* sm = StatsManager::get();
    auto st = sm->statsFor(key);

    const int COLD = static_cast<int>(Mod::get()->getSettingValue<int64_t>("cold-start-runs"));

    // GATE: judge readiness only once there are enough cold (0%-start) runs.
    int coldRuns = st ? static_cast<int>(st->fullRunAttempts) : 0;
    auto runs = sm->fullRuns(key);
    if (coldRuns < COLD) {
        out.ready = false;
        out.verdict = fmt::format("Do full runs to map your real walls ({}/{}).", coldRuns, COLD);
        return out;
    }

    auto sections = sm->sectionsFor(key);  // death-anchored bounds, sorted left-to-right by lo

    // No death-anchored sections = no failure evidence yet (only abandons, or cleared).
    if (sections.empty()) {
        out.ready = false;
        out.verdict = (st && st->totalCompletions > 0)
            ? "No walls left - you've cleared this. Nice."
            : "Do full runs to map your real walls (no deaths logged yet).";
        return out;
    }

    // c_L = COLD-RUN clear odds = product over sections of Laplace P(pass hi | reached lo),
    // counted from cold runs only -- an honest "could you clear it in one cold run".
    double odds = 1.0;
    for (auto const& s : sections) {
        int reachedLo = 0, passedHi = 0;
        for (auto const& r : runs) {
            if (r.reach >= s.lo) ++reachedLo;
            if (r.reach >= s.hi) ++passedHi;
        }
        odds *= (passedHi + 1.0) / (reachedLo + 2.0);  // Laplace add-one
    }
    out.cL = static_cast<float>(std::clamp(odds, 0.0, 1.0));

    // Cold-proven frontier = your adaptive recent reach (difficulty-aware): how far you
    // currently chain from the start. Moves forward the moment you do a deep run, so
    // goal-setting tracks your CURRENT form instead of an old death histogram.
    float reach = sm->reliableReach(key);
    out.solidThrough = reach;

    // SHAPE from cold-run DEATHS only, INTEGER counts off the ring (no decay -> no NaN).
    std::vector<int> secDeaths(sections.size(), 0);
    int total = 0;
    for (auto const& r : runs) {
        if (!r.isDeath) continue;
        for (size_t i = 0; i < sections.size(); ++i) {
            if (r.reach >= sections[i].lo && r.reach < sections[i].hi) { secDeaths[i]++; ++total; break; }
        }
    }
    enum Shape { Unknown, Choke, Wall, Mixed } shape = Unknown;
    if (total >= 5) {
        int mx = 0, nSig = 0;
        for (int d : secDeaths) mx = std::max(mx, d);
        double maxShare = static_cast<double>(mx) / total;
        for (int d : secDeaths)
            if (static_cast<double>(d) / total >= 0.15 && d >= 3) ++nSig;  // float share + abs floor
        if (maxShare >= 0.55 || nSig <= 1)      shape = Choke;
        else if (maxShare <= 0.40 && nSig >= 3) shape = Wall;
        else                                    shape = Mixed;
    }

    // Difficulty context. L<=0 => tier unknown. Only ONLINE ids map to AREDL tiers.
    int tierId = (st && st->type == "online") ? static_cast<int>(st->numericId) : 0;
    double L = Advisor::tierForLevelID(tierId);
    double S = Advisor::get()->skill();
    long Lr = std::lround(L);

    out.ready = true;

    // GRIND MODE (detected from playstyle): cold reach stuck near the start while you've
    // practiced far past it = a section-by-section project. Don't follow the cold edge
    // and never say "shelve it" -- show best progress and point at your worst wall. The
    // tier guard keeps levels far below your level always in chain-forward mode.
    bool grind = (reach < 25.0f && st && (st->bestPercent - reach) > 40.0f)
                 && !(L > 0.0 && L < S - 5.0);
    if (grind) {
        // Worst wall = highest fail-rate among trusted sections (fall back to most deaths).
        int w = -1; float bestFail = -1.0f;
        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i].trusted && sections[i].failSm > bestFail) {
                bestFail = sections[i].failSm; w = static_cast<int>(i);
            }
        }
        if (w < 0) { int md = -1; for (size_t i = 0; i < sections.size(); ++i)
            if (static_cast<int>(sections[i].deaths) > md) { md = static_cast<int>(sections[i].deaths); w = static_cast<int>(i); } }
        out.grinding = true;
        out.solidThrough = st ? st->bestPercent : reach;   // best progress (practice incl.)
        out.classText = fmt::format("worst @ {:.0f}-{:.0f}%", sections[w].lo, sections[w].hi);
        std::string ctx = (L >= S + 3.0) ? fmt::format(" (tier {} stretch)", Lr) : "";
        out.verdict = fmt::format("Grinding{} - drill {:.0f}-{:.0f}%, chip away backwards.",
                                  ctx, sections[w].lo, sections[w].hi);
        return out;
    }

    // GROWING EDGE (at/below your level): the first wall PAST your current reach.
    int edgeIdx = -1;
    for (size_t i = 0; i < sections.size(); ++i)
        if (sections[i].hi > reach + 1.0f) { edgeIdx = static_cast<int>(i); break; }
    auto edgeSpot = [&]() { return fmt::format("{:.0f}-{:.0f}%", sections[edgeIdx].lo, sections[edgeIdx].hi); };

    if (edgeIdx >= 0 && shape == Choke)      out.classText = "CHOKE @ " + edgeSpot();
    else if (shape == Wall)                  out.classText = "WALL";
    else if (edgeIdx >= 0 && shape == Mixed) out.classText = "MIXED @ " + edgeSpot();
    else                                     out.classText = "";

    if (reach >= 95.0f) {
        out.verdict = "You reliably reach the end - go for the full clear!";
    } else if (edgeIdx < 0) {
        out.verdict = fmt::format("You reliably reach {:.0f}% - go for the full clear!", reach);
    } else if (L > 0 && L <= S - 5.0 && reach >= 70.0f) {
        out.verdict = "Below your level & cruising - move on to something harder to level up.";
    } else {
        switch (shape) {
            case Wall:  out.verdict = fmt::format("Keep grinding - push past {} (deaths are spread; chain full runs).", edgeSpot()); break;
            case Choke: out.verdict = fmt::format("Keep grinding - drill your wall at {}.", edgeSpot()); break;
            default:    out.verdict = fmt::format("Keep grinding - push your reach past {}.", edgeSpot()); break;
        }
    }
    return out;
}
