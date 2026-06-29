#include "StatsManager.hpp"
#include "Advisor.hpp"
#include <algorithm>
#include <chrono>
#include <functional>

using namespace geode::prelude;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

StatsManager* StatsManager::get() {
    static StatsManager inst;
    return &inst;
}

StatsStore& StatsManager::store() {
    if (!m_loaded) {
        m_store = Mod::get()->getSavedValue<StatsStore>("stats", StatsStore{});
        m_loaded = true;
    }
    return m_store;
}

int StatsManager::minReachTrust() const {
    return static_cast<int>(Mod::get()->getSettingValue<int64_t>("min-reach-trust"));
}

double StatsManager::recencyLambda() const {
    return Mod::get()->getSettingValue<double>("recency-lambda");
}

// Composite key: GD reuses small ids across buckets and editor levels can be 0.
std::string StatsManager::levelKeyFor(GJGameLevel* level) {
    int64_t id = static_cast<int64_t>(level->m_levelID.value());  // SeedValueRSV -> .value()
    switch (level->m_levelType) {
        case GJLevelType::Main:  return fmt::format("main:{}", id);
        case GJLevelType::Saved: return fmt::format("online:{}", id);
        default: {
            if (id > 0) return fmt::format("local:{}", id);
            // Editor/local with no stable id: hash name + script length (renames break this).
            std::string nm = level->m_levelName.c_str();
            uint64_t h = static_cast<uint64_t>(std::hash<std::string>{}(nm)) ^
                         static_cast<uint64_t>(level->m_levelString.size());
            return fmt::format("local:h{}", h);
        }
    }
}

void StatsManager::beginLevel(std::string const& key, GJGameLevel* level) {
    auto& st = store().levels[key];
    if (st.type.empty()) {  // freshly created entry
        st.numericId = static_cast<int64_t>(level->m_levelID.value());
        auto pos = key.find(':');
        st.type = (pos == std::string::npos) ? key : key.substr(0, pos);
        st.name = level->m_levelName.c_str();
    }
    m_activeKey = key;
    m_map.setRecency(recencyLambda());
    m_map.rebuildFrom(st.attempts);
    m_mapBuilt = true;
}

void StatsManager::recordAttempt(std::string const& key, float startPct, float deathPct,
                                 bool completed, bool isPractice) {
    auto& st = store().levels[key];
    st.totalAttempts++;
    if (completed) st.totalCompletions++;
    float reached = completed ? 100.0f : deathPct;
    st.bestPercent = std::max(st.bestPercent, reached);
    if (startPct <= 0.5f) st.fullRunAttempts++;
    st.lastPlayedMs = nowMs();

    Attempt a;
    a.index = st.nextAttemptIndex++;
    a.startPct = startPct;
    a.deathPct = completed ? 100.0f : deathPct;
    a.completed = completed;
    a.practice = isPractice;
    a.timeMs = st.lastPlayedMs;
    st.attempts.push_back(a);

    // Cap the rolling history (lifetime counters above already captured the totals).
    int cap = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-attempts"));
    if (static_cast<int>(st.attempts.size()) > cap) {
        st.attempts.erase(st.attempts.begin(),
                          st.attempts.begin() + (static_cast<int>(st.attempts.size()) - cap));
    }

    if (key == m_activeKey && m_mapBuilt) {
        m_map.setRecency(recencyLambda());
        m_map.record(startPct, a.deathPct, /*isDeath=*/!completed);
    }

    m_dirty = true;
    flush();  // persist immediately -- don't rely on onExit (a mid-level quit lost data)
}

// A survived/partial run (manual restart after passing some sections). Counts as
// reaching its furthest %, with NO death -- raises reach, lowers failSm.
void StatsManager::recordAbandon(std::string const& key, float startPct, float maxPct,
                                 bool isPractice) {
    auto& st = store().levels[key];
    st.totalAttempts++;
    float reached = std::clamp(maxPct, 0.0f, 100.0f);
    st.bestPercent = std::max(st.bestPercent, reached);
    if (startPct <= 0.5f) st.fullRunAttempts++;
    st.lastPlayedMs = nowMs();

    Attempt a;
    a.index = st.nextAttemptIndex++;
    a.startPct = startPct;
    a.deathPct = reached;     // furthest reached (abandoned=true => NOT a death)
    a.completed = false;
    a.abandoned = true;
    a.practice = isPractice;
    a.timeMs = st.lastPlayedMs;
    st.attempts.push_back(a);

    int cap = static_cast<int>(Mod::get()->getSettingValue<int64_t>("max-attempts"));
    if (static_cast<int>(st.attempts.size()) > cap) {
        st.attempts.erase(st.attempts.begin(),
                          st.attempts.begin() + (static_cast<int>(st.attempts.size()) - cap));
    }

    if (key == m_activeKey && m_mapBuilt) {
        m_map.setRecency(recencyLambda());
        m_map.record(startPct, reached, /*isDeath=*/false);
    }

    m_dirty = true;
    flush();  // persist immediately -- don't rely on onExit (a mid-level quit lost data)
}

void StatsManager::ensureActive(std::string const& key) {
    if (key != m_activeKey || !m_mapBuilt) {
        auto& st = store().levels[key];
        m_map.setRecency(recencyLambda());
        m_map.rebuildFrom(st.attempts);
        m_activeKey = key;
        m_mapBuilt = true;
    }
}

std::vector<ComputedSection> StatsManager::sectionsFor(std::string const& key) {
    ensureActive(key);
    if (m_liveActive) {
        // Preview the in-progress run as a survived-so-far attempt on a COPY of the
        // map, so the HUD reflects live progress before the run ends. The real map is
        // untouched (the attempt is recorded for real on death/complete/abandon).
        HotspotMap preview = m_map;
        preview.setRecency(recencyLambda());
        preview.record(m_liveStart, m_liveMax, /*isDeath=*/false);
        return preview.computeSections(minReachTrust());
    }
    return m_map.computeSections(minReachTrust());
}

std::vector<ComputedSection> StatsManager::leastConsistent(std::string const& key, int n) {
    auto secs = sectionsFor(key);
    std::sort(secs.begin(), secs.end(),
              [](ComputedSection const& x, ComputedSection const& y) {
                  if (x.trusted != y.trusted) return x.trusted > y.trusted;
                  return x.failSm > y.failSm;
              });
    if (static_cast<int>(secs.size()) > n) secs.resize(n);
    return secs;
}

Recommendation StatsManager::recommendFor(std::string const& key) {
    auto& st = store().levels[key];
    auto secs = sectionsFor(key);
    return Recommender::recommend(st, secs, reliableReach(key));
}

// Estimated chance of clearing 0-100% in one run = product of per-section pass
// rates (non-hotspot regions are ~100% pass). With recency weighting the section
// rates track current skill, so this rises as you master the level. <0 = no data.
float StatsManager::fullClearOdds(std::string const& key) {
    auto secs = sectionsFor(key);
    if (secs.empty()) return -1.0f;
    float odds = 1.0f;
    for (auto const& s : secs) {
        float deaths = static_cast<float>(std::min(s.deaths, s.reach));
        float passes = static_cast<float>(s.reach) - deaths;
        float passRate = (passes + 1.0f) / (static_cast<float>(s.reach) + 2.0f);  // Laplace
        odds *= passRate;
    }
    return odds;
}

// The 0%-start ("cold") runs from the retained ring. The consistency model uses
// ONLY these so checkpoint/backwards practice (mid-level starts) can't overstate
// cold-run readiness -- the exact inflation the Recommender also guards against.
std::vector<FullRun> StatsManager::fullRuns(std::string const& key) {
    std::vector<FullRun> out;
    auto st = statsFor(key);
    if (!st) return out;
    for (Attempt const& a : st->attempts) {
        if (a.startPct > 0.5f) continue;   // cold runs only
        FullRun fr;
        fr.completed = a.completed;
        fr.isDeath = !a.completed && !a.abandoned;
        fr.reach = a.completed ? 100.0f : std::clamp(a.deathPct, 0.0f, 100.0f);
        out.push_back(fr);
    }
    // Fold the in-progress run (survived-so-far, not a death) so the consistency HUD
    // updates LIVE as you push deeper -- matching sectionsFor()'s live preview. Only
    // when it's a cold (0%-start) run on the active level.
    if (m_liveActive && key == m_activeKey && m_liveStart <= 0.5f) {
        FullRun fr;
        fr.completed = false;
        fr.isDeath = false;
        fr.reach = std::clamp(m_liveMax, 0.0f, 100.0f);
        out.push_back(fr);
    }
    return out;
}

// How far you CURRENTLY chain from a cold (0%-start) run, over recent attempts. This
// is the adaptive "growing edge" that goal-setting follows, so a clean deep run moves
// the target forward instead of the coach nagging about a start you already pass.
// Difficulty-aware corroboration: on an EASY level one deep run counts (partial runs
// compose); on an EXTREME it needs several (they don't) -- the user's own principle.
float StatsManager::reliableReach(std::string const& key) {
    auto st = statsFor(key);
    if (!st) return 0.0f;
    std::vector<float> reaches;  // recent cold-run reaches, newest first
    for (auto it = st->attempts.rbegin(); it != st->attempts.rend() && reaches.size() < 8; ++it) {
        if (it->startPct > 0.5f) continue;   // cold runs only (began at the start)
        reaches.push_back(it->completed ? 100.0f : it->deathPct);
    }
    if (reaches.empty()) return 0.0f;
    double tier = (st->type == "online") ? Advisor::tierForLevelID(static_cast<int>(st->numericId)) : 0.0;
    int N = (tier <= 0.0) ? 2 : (tier <= 15.0 ? 1 : (tier <= 25.0 ? 2 : 3));  // corroboration depth
    std::sort(reaches.begin(), reaches.end(), [](float a, float b) { return a > b; });
    int idx = std::min(static_cast<int>(reaches.size()) - 1, N - 1);
    return reaches[idx];   // the N-th best recent cold reach
}

LevelStats const* StatsManager::statsFor(std::string const& key) {
    auto& lv = store().levels;
    auto it = lv.find(key);
    return it == lv.end() ? nullptr : &it->second;
}

void StatsManager::flush() {
    if (!m_dirty) return;
    Mod::get()->setSavedValue<StatsStore>("stats", m_store);
    // Write saved.json to DISK now -- setSavedValue only updates Geode's in-memory
    // store, which is otherwise flushed to disk on clean shutdown. A crash or a
    // force-quit would lose the whole session; saveData() makes each attempt durable.
    auto res = Mod::get()->saveData();
    if (!res) log::warn("[pc] saveData failed: {}", res.unwrapErr());
    m_dirty = false;
}
