#include "HotspotMap.hpp"
#include <algorithm>
#include <cmath>

int HotspotMap::binOf(float pct) {
    int b = static_cast<int>(std::floor(pct / 100.0f * B));
    if (b < 0) b = 0;
    if (b > B - 1) b = B - 1;
    return b;
}

void HotspotMap::reset() {
    for (int i = 0; i < B; ++i) { m_startAt[i] = 0.0; m_deaths[i] = 0.0; m_ends[i] = 0.0; }
    m_n = 0.0;
}

void HotspotMap::rebuildFrom(std::vector<Attempt> const& attempts) {
    reset();
    for (auto const& a : attempts) {
        float endPct = a.completed ? 100.0f : a.deathPct;   // furthest reached
        bool isDeath = !a.completed && !a.abandoned;         // only real deaths are fails
        record(a.startPct, endPct, isDeath);
    }
}

void HotspotMap::record(float startPct, float endPct, bool isDeath) {
    // Recency decay: fade all prior mass before adding this attempt at full weight,
    // so the map tracks your CURRENT skill (mastered sections fade out of the math).
    // Scaling all bins + m_n uniformly preserves reach >= deaths and failSm in [0,1].
    if (m_lambda < 1.0) {
        for (int i = 0; i < B; ++i) { m_startAt[i] *= m_lambda; m_deaths[i] *= m_lambda; m_ends[i] *= m_lambda; }
        m_n *= m_lambda;
    }
    float s = std::clamp(startPct, 0.0f, 100.0f);
    m_startAt[binOf(s)] += 1.0;
    m_n += 1.0;
    // Endpoint of EVERY attempt, clamped >= start (you can't end before you start).
    // This is the reach denominator -- survivals/completions raise reach, not fails.
    float e = std::max(s, std::clamp(endPct, 0.0f, 100.0f));
    m_ends[binOf(e)] += 1.0;
    if (isDeath) m_deaths[binOf(e)] += 1.0;  // fail numerator only
}

std::vector<ComputedSection> HotspotMap::computeSections(int minReachTrust) const {
    std::vector<ComputedSection> out;
    if (m_n <= 0.0) return out;

    constexpr int   MERGE_GAP = 2;       // bridge gaps of <= 2 empty bins
    constexpr int   MAX_SECTIONS = 8;
    constexpr float MIN_WIDTH = 1.5f;    // percent
    constexpr float ALPHA = 1.0f, BETA = 4.0f;  // Beta(1,4) display prior

    // Prefix/suffix sums let us compute each section's exact overlap count in O(1).
    // reach uses m_ends (where attempts ENDED), so a clean clear of an early prefix
    // raises a LATER (a>0) section's reach with no death. NOTE: for the start section
    // a==0, preEnd[0]==0, so m_ends is a no-op there -- the start's failSm only falls
    // because RECORDING the survival raises m_n (the reach denominator).
    std::vector<double> sufStart(B + 1, 0.0), preEnd(B + 1, 0.0);
    for (int i = B - 1; i >= 0; --i) sufStart[i] = sufStart[i + 1] + m_startAt[i];
    for (int i = 1; i <= B; ++i)     preEnd[i] = preEnd[i - 1] + m_ends[i - 1];

    // Sections are anchored on DEATHS (m_deaths); a survived-only region never
    // creates a section, it only lowers an existing death-anchored section's failSm.
    int i = 0;
    while (i < B) {
        if (m_deaths[i] <= 0.0) { ++i; continue; }
        int a = i, b = i, emptyRun = 0;
        for (int j = i + 1; j < B; ++j) {
            if (m_deaths[j] > 0.0) { b = j; emptyRun = 0; }
            else if (++emptyRun > MERGE_GAP) break;
        }

        double deaths = 0.0;
        for (int k = a; k <= b; ++k) deaths += m_deaths[k];
        // reach = attempts that started at/before b AND ended at/after a (overlap)
        double reach = m_n - sufStart[b + 1] - preEnd[a];
        if (reach < deaths) reach = deaths;  // belt-and-suspenders; holds structurally

        ComputedSection s;
        s.lo = static_cast<float>(a) / B * 100.0f;
        s.hi = static_cast<float>(b + 1) / B * 100.0f;
        if (s.hi - s.lo < MIN_WIDTH) {
            float c = 0.5f * (s.lo + s.hi);
            s.lo = std::max(0.0f, c - MIN_WIDTH * 0.5f);
            s.hi = std::min(100.0f, c + MIN_WIDTH * 0.5f);
        }
        s.reach     = static_cast<uint32_t>(std::llround(reach));
        s.deaths    = static_cast<uint32_t>(std::llround(deaths));
        s.failSm    = static_cast<float>((deaths + ALPHA) / (reach + ALPHA + BETA));
        s.reachRate = static_cast<float>(reach / m_n);
        s.ev        = s.reachRate * s.failSm;
        s.trusted   = (reach >= static_cast<double>(minReachTrust)) &&
                      (deaths >= static_cast<double>(MIN_DEATHS_HOT));
        out.push_back(s);

        i = b + 1;
    }

    // Keep the biggest EV drains, then present left-to-right.
    if (static_cast<int>(out.size()) > MAX_SECTIONS) {
        std::sort(out.begin(), out.end(),
                  [](ComputedSection const& x, ComputedSection const& y) { return x.ev > y.ev; });
        out.resize(MAX_SECTIONS);
    }
    std::sort(out.begin(), out.end(),
              [](ComputedSection const& x, ComputedSection const& y) { return x.lo < y.lo; });
    return out;
}
