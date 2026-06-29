#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>

// One level on the difficulty ladder (from AREDL).
struct LadderEntry {
    int         levelID = 0;
    int         position = 0;   // AREDL position (1 = hardest)
    double      points = 0;
    double      tier = 0;       // GDDL tier 1-39 (continuous difficulty)
    std::string name;
    bool        legacy = false;
};

// A ladder level the user has progress on but hasn't cleared.
struct ProgressLevel {
    LadderEntry entry;
    int normalPct = 0;     // best % in a real (normal) run -> how close to a clear
    int practicePct = 0;
};

struct AdvisorResult {
    bool        ready = false;
    double      skillTier = 0;          // estimated skill on the GDDL 1-39 scale
    double      clearSkill = 0;         // skill from clears alone (the floor)
    std::string skillBasis;             // what set the skill, e.g. "53% on Zodiac" or "17 clears"
    int         clears = 0;             // # of ladder demons you've actually completed
    int         hardestPosition = 0;    // best (lowest) AREDL position you've cleared
    std::string hardestName;
    std::vector<ProgressLevel> closestToClear;  // in-progress demons, nearest-to-clear first
    std::vector<LadderEntry>   tryThese;        // untouched demons near your skill
};

// Cross-level skill advisor. Fetches the AREDL ladder, joins it to the user's GD
// completion (via GameStatsManager) + progress, estimates skill, recommends.
class Advisor {
public:
    static Advisor* get();
    void compute(std::function<void()> onDone);   // async; onDone runs on the main thread
    AdvisorResult const& result() const { return m_result; }
    bool busy() const { return m_busy; }

    // Tier bridge for the in-run consistency model: GDDL tier (1-39) for a level id,
    // or 0 if unknown (ladder not loaded yet / offline / level not on AREDL).
    bool   ladderReady() const { return m_ladderReady; }
    double tierFor(int levelID) const {
        auto it = m_tierById.find(levelID);
        return it == m_tierById.end() ? 0.0 : it->second;
    }
    static double tierForLevelID(int levelID) { return get()->tierFor(levelID); }
    double skill() const { return m_result.skillTier; }

private:
    AdvisorResult m_result;
    bool m_busy = false;
    bool m_ladderReady = false;
    std::map<int, double> m_tierById;   // levelID -> GDDL tier, filled from the AREDL ladder
};
