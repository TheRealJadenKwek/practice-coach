#pragma once

#include <Geode/Geode.hpp>
#include "Persistence.hpp"
#include "HotspotMap.hpp"
#include "Recommender.hpp"
#include <string>
#include <vector>

using namespace geode::prelude;

// A single 0%-start ("cold") run, distilled from the attempt ring. The consistency
// model is built ONLY from these, so backwards/checkpoint practice can't inflate it.
struct FullRun {
    float reach = 0.f;       // furthest % reached (100 if completed)
    bool  completed = false;
    bool  isDeath = false;   // true death here (not a completion or a manual-restart abandon)
};

// Singleton facade over the saved stats. Owns the in-memory store, a HotspotMap
// for the currently-active level, and orchestrates persistence (debounced save).
class StatsManager {
public:
    static StatsManager* get();

    std::string levelKeyFor(GJGameLevel* level);
    void beginLevel(std::string const& key, GJGameLevel* level);
    void recordAttempt(std::string const& key, float startPct, float deathPct,
                       bool completed, bool isPractice);
    void recordAbandon(std::string const& key, float startPct, float maxPct,
                       bool isPractice);

    std::vector<ComputedSection> sectionsFor(std::string const& key);
    std::vector<ComputedSection> leastConsistent(std::string const& key, int n);
    Recommendation recommendFor(std::string const& key);
    float fullClearOdds(std::string const& key);  // est. 0-100% clear chance, <0 if no data
    std::vector<FullRun> fullRuns(std::string const& key);  // 0%-start runs from the ring
    // How far you CURRENTLY reach from a cold start, over recent runs -- the adaptive
    // "growing edge" that drives goal-setting (difficulty-aware corroboration inside).
    float reliableReach(std::string const& key);
    LevelStats const* statsFor(std::string const& key);

    // Live preview: while a run is in progress, sectionsFor()/recommendFor()/
    // fullClearOdds() include a provisional "survived up to maxPct" attempt so the
    // HUD updates in real time as you progress, before the run ends. Ephemeral --
    // never touches the stored stats (that happens on death/complete/abandon).
    void setLive(float startPct, float maxPct) { m_liveActive = true; m_liveStart = startPct; m_liveMax = maxPct; }
    void clearLive() { m_liveActive = false; }

    void flush();  // write to saved.json if dirty

private:
    StatsStore& store();
    void ensureActive(std::string const& key);
    int minReachTrust() const;
    double recencyLambda() const;

    StatsStore  m_store;
    bool        m_loaded = false;
    bool        m_dirty = false;
    std::string m_activeKey;
    HotspotMap  m_map;
    bool        m_mapBuilt = false;
    bool        m_liveActive = false;   // a run is in progress -> show live preview
    float       m_liveStart = 0.f;
    float       m_liveMax = 0.f;
};
