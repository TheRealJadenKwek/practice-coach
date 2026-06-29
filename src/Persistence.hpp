#pragma once

#include <Geode/Geode.hpp>
#include <matjson/std.hpp>  // Serialize<> for std::vector / std::map (optional matjson header)
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace geode::prelude;

// One recorded attempt on a level.
struct Attempt {
    uint32_t index = 0;       // monotonic per-level attempt number
    float    startPct = 0.f;  // where this run began (checkpoint / startpos / 0)
    float    deathPct = 0.f;  // where it ended (100 if completed)
    bool     completed = false;
    bool     practice = false;
    bool     abandoned = false;  // survived/partial run; deathPct holds furthest %, not a death
    int64_t  timeMs = 0;      // unix ms
};

// Durable per-level stats. `attempts` is a capped ring (most recent N); the
// lifetime counters are NOT rolled back when the ring caps, so long-term totals
// stay accurate even though storage is bounded.
struct LevelStats {
    int64_t     numericId = 0;
    std::string type;                  // "main" | "online" | "local"
    std::string name;
    uint32_t    nextAttemptIndex = 1;
    uint32_t    totalAttempts = 0;
    uint32_t    totalCompletions = 0;
    uint32_t    fullRunAttempts = 0;   // runs that began at ~0%
    float       bestPercent = 0.f;
    int64_t     lastPlayedMs = 0;
    std::vector<Attempt> attempts;
};

struct StatsStore {
    int schemaVersion = 1;
    std::map<std::string, LevelStats> levels;
};

// Tolerant readers: missing/!wrong-typed keys fall back to a default instead of
// erroring, so a partial/old save never wipes the whole store (forward-compat).
namespace pc {
    inline int64_t jInt(matjson::Value const& j, const char* k, int64_t d) {
        if (j.contains(k)) { auto r = j[k].asInt(); if (r) return r.unwrap(); }
        return d;
    }
    inline double jDouble(matjson::Value const& j, const char* k, double d) {
        if (j.contains(k)) { auto r = j[k].asDouble(); if (r) return r.unwrap(); }
        return d;
    }
    inline bool jBool(matjson::Value const& j, const char* k, bool d) {
        if (j.contains(k)) { auto r = j[k].asBool(); if (r) return r.unwrap(); }
        return d;
    }
    inline std::string jStr(matjson::Value const& j, const char* k, std::string d) {
        if (j.contains(k)) { auto r = j[k].asString(); if (r) return r.unwrap(); }
        return d;
    }
}

// matjson v4 serialize names: fromJson -> Result<T>, toJson -> Value.
template <>
struct matjson::Serialize<Attempt> {
    static geode::Result<Attempt> fromJson(matjson::Value const& j) {
        Attempt a;
        a.index     = static_cast<uint32_t>(pc::jInt(j, "i", 0));
        a.startPct  = static_cast<float>(pc::jDouble(j, "s", 0.0));
        a.deathPct  = static_cast<float>(pc::jDouble(j, "d", 0.0));
        a.completed = pc::jBool(j, "c", false);
        a.practice  = pc::jBool(j, "p", false);
        a.abandoned = pc::jBool(j, "ab", false);  // old saves load as not-abandoned
        a.timeMs    = pc::jInt(j, "t", 0);
        return geode::Ok(a);
    }
    static matjson::Value toJson(Attempt const& a) {
        return matjson::makeObject({
            {"i", static_cast<int64_t>(a.index)},
            {"s", a.startPct},
            {"d", a.deathPct},
            {"c", a.completed},
            {"p", a.practice},
            {"ab", a.abandoned},
            {"t", a.timeMs},
        });
    }
};

template <>
struct matjson::Serialize<LevelStats> {
    static geode::Result<LevelStats> fromJson(matjson::Value const& j) {
        LevelStats ls;
        ls.numericId        = pc::jInt(j, "id", 0);
        ls.type             = pc::jStr(j, "type", "");
        ls.name             = pc::jStr(j, "name", "");
        ls.nextAttemptIndex = static_cast<uint32_t>(pc::jInt(j, "nai", 1));
        ls.totalAttempts    = static_cast<uint32_t>(pc::jInt(j, "tot", 0));
        ls.totalCompletions = static_cast<uint32_t>(pc::jInt(j, "comp", 0));
        ls.fullRunAttempts  = static_cast<uint32_t>(pc::jInt(j, "full", 0));
        ls.bestPercent      = static_cast<float>(pc::jDouble(j, "best", 0.0));
        ls.lastPlayedMs     = pc::jInt(j, "lp", 0);
        if (j.contains("att")) {
            auto a = j["att"].as<std::vector<Attempt>>();
            if (a) ls.attempts = a.unwrap();
        }
        return geode::Ok(ls);
    }
    static matjson::Value toJson(LevelStats const& ls) {
        auto o = matjson::makeObject({
            {"id",   ls.numericId},
            {"type", ls.type},
            {"name", ls.name},
            {"nai",  static_cast<int64_t>(ls.nextAttemptIndex)},
            {"tot",  static_cast<int64_t>(ls.totalAttempts)},
            {"comp", static_cast<int64_t>(ls.totalCompletions)},
            {"full", static_cast<int64_t>(ls.fullRunAttempts)},
            {"best", ls.bestPercent},
            {"lp",   ls.lastPlayedMs},
        });
        o["att"] = ls.attempts;  // vector<Attempt> -> Value via Serialize<Attempt>
        return o;
    }
};

template <>
struct matjson::Serialize<StatsStore> {
    static geode::Result<StatsStore> fromJson(matjson::Value const& j) {
        StatsStore s;
        s.schemaVersion = static_cast<int>(pc::jInt(j, "v", 1));
        if (j.contains("levels")) {
            auto m = j["levels"].as<std::map<std::string, LevelStats>>();
            if (m) s.levels = m.unwrap();
        }
        return geode::Ok(s);
    }
    static matjson::Value toJson(StatsStore const& s) {
        auto o = matjson::makeObject({{"v", static_cast<int64_t>(s.schemaVersion)}});
        o["levels"] = s.levels;  // map<string,LevelStats> -> Value
        return o;
    }
};
