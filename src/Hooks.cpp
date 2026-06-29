#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "StatsManager.hpp"
#include "HudNode.hpp"
#include <algorithm>

using namespace geode::prelude;

// Override virtuals by redeclaring the EXACT signature (no $override macro in
// current Geode) and calling PlayLayer::method(...). A hook that compiles but
// never fires almost always means a signature mismatch.
class $modify(PCPlayLayer, PlayLayer) {
    struct Fields {
        HudNode*    m_hud = nullptr;    // weak; owned by the scene graph
        float       m_startPct = -1.f;  // <0 until sampled on the first gameplay frame
        float       m_maxPct = 0.f;     // furthest % reached this run
        int         m_lastLivePct = -1; // last whole-% at which we pushed a live HUD update
        bool        m_runOpen = false;
        std::string m_levelKey;
        bool        m_classic = true;   // !m_isPlatformer
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto f = m_fields.self();
        f->m_classic = !this->m_isPlatformer;
        f->m_levelKey = StatsManager::get()->levelKeyFor(level);
        StatsManager::get()->beginLevel(f->m_levelKey, level);

        if (f->m_classic && Mod::get()->getSettingValue<bool>("enable-hud")) {
            auto hud = HudNode::create(f->m_levelKey);
            if (hud) {
                hud->setID("practice-coach-hud"_spr);
                hud->setZOrder(1000);  // setZOrder, NOT setLocalZOrder (doesn't exist in this fork)
                this->addChild(hud);   // child of PlayLayer == screen-fixed overlay
                hud->setVisible(this->m_isPracticeMode);
                f->m_hud = hud;
            }
        }
        this->pcBeginRun();
        return true;
    }

    // Open a run. start% and max% are sampled on the first gameplay frame (below).
    void pcBeginRun() {
        auto f = m_fields.self();
        f->m_startPct = -1.f;
        f->m_maxPct = 0.f;
        f->m_lastLivePct = -1;
        f->m_runOpen = true;
        StatsManager::get()->clearLive();
    }

    // Robust current %: getCurrentPercent() can read 0 when another mod (e.g. MegaHack)
    // hooks the percent path. Fall back to player-X / level-length, which is exactly
    // what the game derives the percentage from.
    float pcPercent() {
        // getCurrentPercent() is the accurate source; the player-X / level-length
        // estimate under-reads (GD offsets its percentage), so only use it as a
        // fallback when getCurrentPercent() reads <= 0.
        float p = this->getCurrentPercent();
        if (p <= 0.f && this->m_player1 && this->m_levelLength > 0.f)
            p = this->m_player1->getPositionX() / this->m_levelLength * 100.f;
        return std::clamp(p, 0.f, 100.f);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        auto f = m_fields.self();
        if (f->m_runOpen && f->m_classic) {
            float p = this->pcPercent();
            if (f->m_startPct < 0.f) f->m_startPct = p;  // first frame = the run's true start
            if (f->m_hud) f->m_hud->setLiveProgress(p);  // live position cursor, every frame
            if (p > f->m_maxPct) {
                f->m_maxPct = p;
                // Live HUD update on each new whole-% high-water mark: preview the
                // sections you've SURVIVED so far this run, so the numbers move while
                // you're pushing a new best -- before you ever die.
                int ip = static_cast<int>(p);
                if (f->m_hud && ip > f->m_lastLivePct) {
                    f->m_lastLivePct = ip;
                    StatsManager::get()->setLive(f->m_startPct, f->m_maxPct);
                    f->m_hud->refresh();
                }
            }
        }
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto f = m_fields.self();
        // Death location = furthest % reached this run (frame-tracked in postUpdate),
        // robust even if getCurrentPercent() is reset/zeroed by the time we're called.
        float deathPct = std::max(f->m_maxPct, this->pcPercent());
        // Null-safe anti-cheat check: with no anti-cheat spike, m_anticheatSpike is null
        // and a null `object` would spuriously match it -> every death looked illegit.
        bool isAnticheat = (this->m_anticheatSpike != nullptr) && (object == this->m_anticheatSpike);
        bool legit = !this->m_disablePlayerHitbox && !this->m_isTestMode && !isAnticheat;
        PlayLayer::destroyPlayer(player, object);

        if (f->m_runOpen && f->m_classic) {
            log::debug("[pc] death isAC={} start={:.1f}% -> {:.1f}% legit={}",
                       isAnticheat, f->m_startPct, deathPct, legit);
            // CRITICAL: only a LEGIT death actually resets the player. GD's anti-cheat
            // spike fires a NON-FATAL destroyPlayer (and noclip/test-mode deaths aren't
            // real either) -- if we closed the run on those, tracking would freeze while
            // you keep playing. So close + record ONLY on a real death; otherwise leave
            // the run open so progress keeps tracking. (m_startPct guard skips a death
            // before the first gameplay frame -- a degenerate spawn death at ~0%.)
            if (legit) {
                if (f->m_startPct >= 0.f) {
                    float dp = std::clamp(deathPct, 0.f, 100.f);
                    StatsManager::get()->recordAttempt(f->m_levelKey, f->m_startPct, dp,
                                                       false, this->m_isPracticeMode);
                }
                f->m_runOpen = false;
                StatsManager::get()->clearLive();  // drop the live preview
                if (f->m_hud) f->m_hud->refresh();  // show the real recorded state
            }
        }
    }

    void levelComplete() {
        auto f = m_fields.self();
        if (f->m_runOpen && f->m_classic) {
            float startPct = f->m_startPct < 0.f ? 0.f : f->m_startPct;
            StatsManager::get()->recordAttempt(f->m_levelKey, startPct, 100.f, true,
                                               this->m_isPracticeMode);
        }
        f->m_runOpen = false;
        StatsManager::get()->clearLive();
        if (f->m_hud) f->m_hud->refresh();
        PlayLayer::levelComplete();
    }

    void resetLevel() {
        auto f = m_fields.self();
        // Capture furthest % BEFORE the base reset zeroes the player position.
        float curMax = f->m_maxPct;
        if (f->m_classic) {
            float p = this->pcPercent();
            if (p > curMax) curMax = p;
        }
        bool wasOpen = f->m_runOpen && f->m_classic;
        float start = f->m_startPct < 0.f ? 0.f : f->m_startPct;

        PlayLayer::resetLevel();

        // A still-open run at reset means a MANUAL restart (no death/complete closed
        // it). If it made real progress, record it as an ABANDONED run: it SURVIVED
        // the sections it reached (raising their reach, lowering their failSm) with no
        // death. This is what makes practicing/passing a section update the coach.
        if (wasOpen && curMax > start + 0.5f) {
            StatsManager::get()->recordAbandon(f->m_levelKey, start, curMax,
                                               this->m_isPracticeMode);
        }
        StatsManager::get()->clearLive();
        f->m_runOpen = false;
        this->pcBeginRun();

        if (f->m_hud) {
            f->m_hud->setVisible(this->m_isPracticeMode && f->m_classic);
            f->m_hud->refresh();  // reset the HUD to the recorded state for the new run
        }
    }

    void togglePracticeMode(bool practice) {
        PlayLayer::togglePracticeMode(practice);
        auto f = m_fields.self();
        if (f->m_hud) f->m_hud->setVisible(practice && f->m_classic);
    }

    void onExit() {
        StatsManager::get()->clearLive();
        StatsManager::get()->flush();  // debounced save flushed on leaving the level
        PlayLayer::onExit();
    }
};
