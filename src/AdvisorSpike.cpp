// Cross-level skill-progression advisor -- Phase 1 MVP (brain + a menu button/popup).
// Fetches the AREDL ladder, finds your true clears via GameStatsManager
// (hasCompletedOnlineLevel -- reliable, not dependent on which levels are loaded),
// estimates a skill tier, recommends, and surfaces it in a "Coach" button on the
// main menu.
#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include "Advisor.hpp"
#include <thread>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

static long jInt(matjson::Value const& v, const char* k) {
    if (!v.contains(k)) return 0; auto r = v[k].asInt(); return r ? static_cast<long>(r.unwrap()) : 0;
}
static double jDbl(matjson::Value const& v, const char* k) {
    if (!v.contains(k)) return 0.0; auto r = v[k].asDouble(); return r ? r.unwrap() : 0.0;
}
static std::string jStr(matjson::Value const& v, const char* k) {
    if (!v.contains(k)) return ""; auto r = v[k].asString(); return r ? r.unwrap() : std::string();
}
static bool jBool(matjson::Value const& v, const char* k) {
    if (!v.contains(k)) return false; auto r = v[k].asBool(); return r ? r.unwrap() : false;
}

Advisor* Advisor::get() {
    static Advisor inst;
    return &inst;
}

void Advisor::compute(std::function<void()> onDone) {
    if (m_busy) return;
    m_busy = true;
    std::thread([this, onDone] {
        auto ladder = std::make_shared<std::vector<LadderEntry>>();
        auto res = web::WebRequest().getSync("https://api.aredl.net/v2/api/aredl/levels");
        if (res.ok()) {
            auto val = res.json();
            if (val) {
                auto arrRes = val.unwrap().asArray();
                if (arrRes) {
                    std::vector<matjson::Value> elems = arrRes.unwrap();
                    for (matjson::Value const& e : elems) {
                        LadderEntry le;
                        le.levelID = static_cast<int>(jInt(e, "level_id"));
                        if (le.levelID <= 0) continue;
                        le.position = static_cast<int>(jInt(e, "position"));
                        le.points = jDbl(e, "points");
                        le.tier = jDbl(e, "gddl_tier");
                        le.name = jStr(e, "name");
                        le.legacy = jBool(e, "legacy");
                        ladder->push_back(le);
                    }
                }
            }
        } else {
            log::warn("[advisor] AREDL fetch failed (HTTP {})", res.code());
        }

        Loader::get()->queueInMainThread([this, onDone, ladder] {
            auto gsm = GameStatsManager::sharedState();

            // Tier bridge: the in-run consistency model looks up a level's GDDL tier
            // here (this map persists on the singleton, read on the main thread).
            for (LadderEntry const& e : *ladder)
                if (e.tier > 0) this->m_tierById[e.levelID] = e.tier;
            this->m_ladderReady = !ladder->empty();

            // RELIABLE clears: ask GameStatsManager about every ladder demon directly
            // (independent of which GJGameLevels happen to be loaded in GLM).
            std::map<int, bool> cleared;
            std::vector<double> clearTiers;
            int bestPos = 1 << 30;
            std::string bestName;
            for (LadderEntry const& e : *ladder) {
                if (gsm && gsm->hasCompletedOnlineLevel(e.levelID)) {
                    cleared[e.levelID] = true;
                    if (e.tier > 0) clearTiers.push_back(e.tier);
                    if (e.position > 0 && e.position < bestPos) {
                        bestPos = e.position;
                        bestName = e.name;
                    }
                }
            }

            // Progress for "closest to a clear": scan the user's DOWNLOADED levels
            // (loaded from the local save at startup -- this is where your normal %
            // lives), plus any online/stored levels currently cached. Reliable, and
            // doesn't depend on what's been browsed this session.
            std::map<int, LadderEntry const*> byId;
            for (LadderEntry const& e : *ladder) byId[e.levelID] = &e;
            std::vector<ProgressLevel> inProgress;
            std::map<int, bool> seen;
            auto glm = GameLevelManager::sharedState();
            auto scanDict = [&](cocos2d::CCDictionary* d) {
                if (!d) return;
                for (auto [key, lvl] : d->asExt<gd::string, GJGameLevel*>()) {
                    if (!lvl) continue;
                    int id = static_cast<int>(lvl->m_levelID.value());
                    if (cleared.count(id) || seen.count(id)) continue;
                    auto it = byId.find(id);
                    if (it == byId.end()) continue;   // not on the ladder
                    seen[id] = true;
                    int npct = static_cast<int>(lvl->m_normalPercent.value());
                    if (npct <= 0) continue;
                    ProgressLevel pl;
                    pl.entry = *it->second;
                    pl.normalPct = npct;
                    pl.practicePct = lvl->m_practicePercent;
                    inProgress.push_back(pl);
                }
            };
            if (glm) {
                scanDict(glm->m_downloadedLevels);
                scanDict(glm->m_onlineLevels);
                scanDict(glm->m_storedLevels);
            }

            AdvisorResult r;
            r.clears = static_cast<int>(clearTiers.size());
            r.hardestPosition = (bestPos == (1 << 30)) ? 0 : bestPos;
            r.hardestName = bestName;

            // --- Skill FLOOR from true clears: 85th percentile of cleared tiers. ---
            double clearSkill = 0.0;
            if (!clearTiers.empty()) {
                std::sort(clearTiers.begin(), clearTiers.end());
                size_t idx = static_cast<size_t>(std::floor(0.85 * (clearTiers.size() - 1)));
                clearSkill = clearTiers[idx];
            }

            // --- Progress on UNCLEARED demons can only RAISE the estimate (never ---
            // lower it: not having pushed a level isn't evidence of weakness). Credit
            // a best NORMAL-mode run of X% on a tier-T demon as T * (X/100)^0.4 -- a
            // concave curve, so deep progress on a HARD demon (whose front is brutal)
            // counts for a lot. 53% on tier 34 -> ~26.4, comfortably above a tier-24
            // clear, matching the felt difficulty. Easy demons at high % credit BELOW
            // the clear floor, so they can't inflate skill.
            double progressSkill = 0.0;
            ProgressLevel const* topProg = nullptr;
            for (ProgressLevel const& p : inProgress) {
                if (p.entry.tier <= 0 || p.normalPct <= 0) continue;
                double credited = p.entry.tier * std::pow(p.normalPct / 100.0, 0.4);
                if (credited > progressSkill) { progressSkill = credited; topProg = &p; }
            }

            r.clearSkill = clearSkill;
            r.skillTier  = std::max(clearSkill, progressSkill);
            if (topProg && progressSkill > clearSkill + 0.05)
                r.skillBasis = fmt::format("{}% on {}", topProg->normalPct, topProg->entry.name);
            else
                r.skillBasis = fmt::format("{} clears", r.clears);

            std::sort(inProgress.begin(), inProgress.end(),
                      [](ProgressLevel const& a, ProgressLevel const& b) { return a.normalPct > b.normalPct; });
            r.closestToClear.assign(inProgress.begin(),
                                    inProgress.begin() + std::min<size_t>(inProgress.size(), 8));

            // Distribution dump for calibration: the strongest partial-progress runs
            // by credited ability (what can push skill above the clear floor).
            {
                std::vector<ProgressLevel> byCredit = inProgress;
                std::sort(byCredit.begin(), byCredit.end(),
                          [](ProgressLevel const& a, ProgressLevel const& b) {
                              return a.entry.tier * std::pow(a.normalPct / 100.0, 0.4) >
                                     b.entry.tier * std::pow(b.normalPct / 100.0, 0.4);
                          });
                std::string dump;
                for (size_t i = 0; i < byCredit.size() && i < 10; ++i) {
                    ProgressLevel const& p = byCredit[i];
                    dump += fmt::format("\n    {:<22} tier {:.1f}  {}%  -> credited {:.1f}",
                                        p.entry.name, p.entry.tier, p.normalPct,
                                        p.entry.tier * std::pow(p.normalPct / 100.0, 0.4));
                }
                log::info("[advisor] clearSkill {:.1f} | progressSkill {:.1f} ({}) | "
                          "top partial runs:{}",
                          clearSkill, progressSkill, r.skillBasis, dump);
            }

            double S = r.skillTier;
            for (LadderEntry const& e : *ladder) {
                if (e.legacy || e.tier <= 0 || e.name.empty() || cleared.count(e.levelID)) continue;
                if (e.tier > S - 0.5 && e.tier <= S + 2.0) r.tryThese.push_back(e);
            }
            std::sort(r.tryThese.begin(), r.tryThese.end(),
                      [](LadderEntry const& a, LadderEntry const& b) { return a.tier < b.tier; });
            if (r.tryThese.size() > 8) r.tryThese.resize(8);
            r.ready = true;
            m_result = r;

            log::info("[advisor] CLEARED {} demons; hardest '{}' #{}; skill tier {:.1f}; "
                      "closest-to-clear {}; tryThese {}",
                      r.clears, r.hardestName, r.hardestPosition, r.skillTier,
                      r.closestToClear.size(), r.tryThese.size());

            m_busy = false;
            if (onDone) onDone();
        });
    }).detach();
}

// Opens a GD level by its online ID. Tries the GLM cache first; otherwise runs a
// single-ID online search and opens LevelInfoLayer when the result arrives. Self-
// owning (retains across the async window) so it survives even if the popup that
// started it closes -- m_levelManagerDelegate is a single raw pointer, so a dangling
// delegate would crash. We save+restore the previous delegate rather than nulling it,
// so a concurrent vanilla/other-mod search isn't silently dropped.
class LevelByIdFetcher : public cocos2d::CCObject, public LevelManagerDelegate {
protected:
    int  m_targetID = 0;
    bool m_done = false;
    LevelManagerDelegate* m_prev = nullptr;

public:
    static LevelByIdFetcher* create(int onlineID) {
        auto self = new LevelByIdFetcher();
        self->m_targetID = onlineID;
        self->autorelease();
        return self;
    }

    void start() {
        auto glm = GameLevelManager::sharedState();
        m_prev = glm->m_levelManagerDelegate;
        glm->m_levelManagerDelegate = this;
        this->retain();  // stay alive across the async round-trip
        auto search = GJSearchObject::create(SearchType::Search, std::to_string(m_targetID));
        glm->getOnlineLevels(search);
    }

private:
    // Returns true only on the FIRST call. Defers our self-release to end of frame
    // (autorelease) so the object survives a synchronous double-dispatch (GD firing
    // both the 2-arg and 3-arg overloads for one request) without a use-after-free.
    bool finish() {
        if (m_done) return false;
        m_done = true;
        auto glm = GameLevelManager::sharedState();
        // Restore only if we're still the active delegate; if a newer search took over,
        // intentionally leave it in place (don't clobber the newer owner).
        if (glm->m_levelManagerDelegate == this)
            glm->m_levelManagerDelegate = m_prev;
        this->autorelease();  // balances start()'s retain; freed at end of frame
        return true;
    }

    // Strict ID match only -- no first-result fallback (a key collision could otherwise
    // open the wrong level). m_levelID is a SeedValueRSV; read via .value().
    GJGameLevel* matchById(cocos2d::CCArray* levels) {
        if (!levels) return nullptr;
        for (unsigned i = 0; i < levels->count(); ++i) {
            auto lvl = static_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if (lvl && lvl->m_levelID.value() == m_targetID) return lvl;
        }
        return nullptr;
    }

    static void openInfo(GJGameLevel* level) {
        CCDirector::sharedDirector()->pushScene(
            CCTransitionFade::create(0.5f, LevelInfoLayer::scene(level, false)));
    }

public:
    void loadLevelsFinished(cocos2d::CCArray* levels, char const*) override {
        GJGameLevel* level = matchById(levels);
        if (!finish()) return;  // already finished -> never double-open / re-enter
        if (level) openInfo(level);
        else Notification::create("Level not found", NotificationIcon::Error)->show();
    }
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int) override {
        this->loadLevelsFinished(levels, key);
    }
    void loadLevelsFailed(char const*) override {
        if (!finish()) return;
        Notification::create("Could not load level", NotificationIcon::Error)->show();
    }
    void loadLevelsFailed(char const* key, int) override { this->loadLevelsFailed(key); }
    void setupPageInfo(gd::string, char const*) override {}
};

// Open a level by online ID: cache-first, else async fetch. Main thread only.
static void pcOpenLevelById(int onlineID) {
    if (onlineID <= 0) return;
    auto glm = GameLevelManager::sharedState();
    if (auto lvl = glm->getSavedLevel(onlineID)) {
        CCDirector::sharedDirector()->pushScene(
            CCTransitionFade::create(0.5f, LevelInfoLayer::scene(lvl, false)));
        return;
    }
    LevelByIdFetcher::create(onlineID)->start();
}

// Compact popup: skill header + a scrollable list of recommendations.
class CoachPopup : public geode::Popup {
protected:
    bool initWith(AdvisorResult const& r) {
        if (!Popup::init(360.f, 280.f)) return false;
        this->setTitle("Practice Coach");

        auto* head = CCLabelBMFont::create(
            fmt::format("Your skill: GDDL tier {:.0f}", r.skillTier).c_str(), "bigFont.fnt");
        head->limitLabelWidth(m_size.width - 40.f, 0.6f, 0.2f);
        head->setPosition(m_size.width / 2.f, m_size.height - 34.f);
        m_mainLayer->addChild(head);

        // Sub-line: what drove the number (clears, or a standout partial run).
        auto* basis = CCLabelBMFont::create(
            fmt::format("from {}", r.skillBasis).c_str(), "goldFont.fnt");
        basis->limitLabelWidth(m_size.width - 40.f, 0.4f, 0.18f);
        basis->setPosition(m_size.width / 2.f, m_size.height - 52.f);
        m_mainLayer->addChild(basis);

        float sw = m_size.width - 36.f, sh = m_size.height - 86.f;
        auto scroll = ScrollLayer::create({sw, sh});
        scroll->setPosition({18.f, 14.f});
        m_mainLayer->addChild(scroll);

        // levelID 0 = non-tappable (headers/blanks/summary); >0 = tap to open the level.
        struct Row { std::string text; const char* font; int levelID; };
        std::vector<Row> rows;
        rows.push_back({fmt::format("{} demons cleared - hardest: {} #{}",
                                    r.clears, r.hardestName, r.hardestPosition), "chatFont.fnt", 0});
        rows.push_back({"", "chatFont.fnt", 0});
        rows.push_back({"Closest to a clear:  (tap to play)", "goldFont.fnt", 0});
        if (r.closestToClear.empty())
            rows.push_back({"  (open your levels once so progress loads)", "chatFont.fnt", 0});
        for (ProgressLevel const& p : r.closestToClear)
            rows.push_back({fmt::format("  {}%  {}  (#{})", p.normalPct, p.entry.name, p.entry.position),
                            "chatFont.fnt", p.entry.levelID});
        rows.push_back({"", "chatFont.fnt", 0});
        rows.push_back({"Try next (near your level):  (tap to play)", "goldFont.fnt", 0});
        for (LadderEntry const& e : r.tryThese)
            rows.push_back({fmt::format("  tier {:.0f}  {}  (#{})", e.tier, e.name, e.position),
                            "chatFont.fnt", e.levelID});

        const float lineH = 18.f;
        float totalH = std::max(sh, static_cast<float>(rows.size()) * lineH + 6.f);
        scroll->m_contentLayer->setContentSize({sw, totalH});

        // One menu spans the content layer and holds the tappable rows. Touch priority
        // below the scroll (more-negative wins the touch-began) so a tap opens a level;
        // stealing-touches lets a vertical drag past the cancel limit scroll instead.
        auto rowMenu = CCMenu::create();
        rowMenu->setContentSize({sw, totalH});
        rowMenu->setAnchorPoint({0.f, 0.f});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setTouchPriority(-500);
        scroll->m_contentLayer->addChild(rowMenu);

        float yy = totalH - lineH;
        for (Row const& row : rows) {
            if (!row.text.empty()) {
                auto* lbl = CCLabelBMFont::create(row.text.c_str(), row.font);
                lbl->setAnchorPoint({0.f, 0.5f});
                lbl->setScale(0.55f);
                if (row.levelID > 0) {
                    auto* item = CCMenuItemSpriteExtra::create(
                        lbl, this, menu_selector(CoachPopup::onRow));
                    item->setTag(row.levelID);                 // GD level ids fit in int
                    item->setAnchorPoint({0.f, 0.5f});
                    item->setPosition({4.f, yy});
                    rowMenu->addChild(item);
                } else {
                    lbl->setPosition({4.f, yy});
                    scroll->m_contentLayer->addChild(lbl);     // plain, non-tappable
                }
            }
            yy -= lineH;
        }
        scroll->setStealingTouches(true);
        scroll->setCancelTouchLimit(10.f);  // px of vertical drag before a tap is cancelled
        scroll->scrollToTop();
        return true;
    }

    // A recommendation row was tapped -> open that level (cache-first, else fetch).
    // Close the popup first so returning from the level lands on a clean menu.
    void onRow(CCObject* sender) {
        int levelID = static_cast<CCNode*>(sender)->getTag();
        if (levelID <= 0) return;
        this->onClose(nullptr);
        pcOpenLevelById(levelID);
    }

public:
    static CoachPopup* create(AdvisorResult const& r) {
        auto ret = new CoachPopup();
        if (ret->initWith(r)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// "Coach" button on the main menu -> popup with skill + recommendations.
class $modify(PCAdvisorMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        static bool s_ran = false;
        if (!s_ran) {
            s_ran = true;
            Advisor::get()->compute(nullptr);
        }

        auto spr = ButtonSprite::create("Coach");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PCAdvisorMenuLayer::onCoach));
        btn->setID("practice-coach-button"_spr);
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition(CCDirector::get()->getWinSize().width / 2.f, 28.f);
        this->addChild(menu, 100);
        return true;
    }

    void onCoach(CCObject*) {
        auto const& r = Advisor::get()->result();
        if (!r.ready) {
            FLAlertLayer::create("Practice Coach",
                "Still analyzing your levels - give it a few seconds and reopen.", "OK")->show();
            return;
        }
        CoachPopup::create(r)->show();
    }
};
