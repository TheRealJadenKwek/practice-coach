#include "HudNode.hpp"
#include "StatsManager.hpp"
#include "Consistency.hpp"

using namespace geode::prelude;

bool HudNode::init(std::string const& levelKey) {
    if (!CCNode::init()) return false;
    m_levelKey = levelKey;
    auto ws = CCDirector::get()->getWinSize();

    // GD built-in fonts are passed WITHOUT _spr (that suffix is only for
    // mod-shipped resources and would 404 here).
    m_recoLabel = CCLabelBMFont::create("", "goldFont.fnt");
    if (!m_recoLabel) return false;
    m_recoLabel->setAnchorPoint({0.f, 1.f});
    m_recoLabel->setScale(0.5f);
    m_recoLabel->setPosition({10.f, ws.height - 10.f});
    this->addChild(m_recoLabel);

    m_listLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (!m_listLabel) return false;
    m_listLabel->setAnchorPoint({0.f, 1.f});
    m_listLabel->setScale(0.5f);
    m_listLabel->setPosition({10.f, ws.height - 34.f});
    this->addChild(m_listLabel);

    // Consistency gauge (cold-run readiness + wall/choke), just above the heat strip.
    m_oddsLabel = CCLabelBMFont::create("", "goldFont.fnt");
    if (!m_oddsLabel) return false;
    m_oddsLabel->setAnchorPoint({0.f, 0.f});
    m_oddsLabel->setScale(0.42f);
    m_oddsLabel->setPosition({10.f, 26.f});
    this->addChild(m_oddsLabel);

    // The coaching verdict (keep grinding / not ready / go for the clear), above it.
    m_verdictLabel = CCLabelBMFont::create("", "goldFont.fnt");
    if (!m_verdictLabel) return false;
    m_verdictLabel->setAnchorPoint({0.f, 0.f});
    m_verdictLabel->setPosition({10.f, 40.f});
    this->addChild(m_verdictLabel);

    m_draw = CCDrawNode::create();  // one node for the whole mini-map strip
    if (!m_draw) return false;
    this->addChild(m_draw);

    // Live position cursor over the heat strip (moves every frame during a run).
    m_cursor = cocos2d::CCLayerColor::create({120, 220, 255, 210}, 2.f, 14.f);
    if (m_cursor) {
        m_cursor->setPosition({0.f, 11.f});
        m_cursor->setVisible(false);
        this->addChild(m_cursor, 5);
    }

    this->refresh();
    return true;
}

HudNode* HudNode::create(std::string const& levelKey) {
    auto n = new HudNode();
    if (n && n->init(levelKey)) {
        n->autorelease();
        return n;
    }
    CC_SAFE_DELETE(n);
    return nullptr;
}

void HudNode::refresh() {
    auto* sm = StatsManager::get();
    auto ws = CCDirector::get()->getWinSize();

    auto reco = sm->recommendFor(m_levelKey);
    m_recoLabel->setString(reco.headline.c_str());

    std::string body;
    int shown = 0;
    for (auto const& s : sm->leastConsistent(m_levelKey, 3)) {
        if (s.deaths == 0) continue;
        body += fmt::format("{}. {:.0f}-{:.0f}%   fail {:.0f}%   (n{})\n",
                            shown + 1, s.lo, s.hi, s.failSm * 100.f, s.reach);
        ++shown;
    }
    if (shown == 0) body = "(no hotspots yet - keep playing)";
    m_listLabel->setString(body.c_str());

    // Honest cold-run consistency + the coaching verdict (full-run-based, so
    // backwards/checkpoint practice can't inflate it).
    auto cons = computeConsistency(m_levelKey);
    if (Mod::get()->getSettingValue<bool>("show-clear-odds")) {
        m_oddsLabel->setVisible(true);
        if (!cons.ready) {
            m_oddsLabel->setString("Cold-proven: (need more full runs)");
        } else if (cons.grinding) {
            // Above-your-level project: cold reach is meaningless; show best progress.
            std::string base = fmt::format("Progress: {:.0f}%", cons.solidThrough);
            if (!cons.classText.empty()) base += "   " + cons.classText;
            m_oddsLabel->setString(base.c_str());
        } else {
            // Lead with the frontier (it MOVES as you consolidate). Show clear-odds only
            // once it's meaningful (>=5%), so it doesn't sit pinned at "<1%".
            std::string base = fmt::format("Cold-proven to {:.0f}%", cons.solidThrough);
            float pct = cons.cL * 100.f;
            if (pct >= 5.f) base += fmt::format("   clear ~{:.0f}%", pct);
            if (!cons.classText.empty()) base += "   " + cons.classText;
            m_oddsLabel->setString(base.c_str());
        }
    } else {
        m_oddsLabel->setVisible(false);
    }
    m_verdictLabel->setString(cons.verdict.c_str());
    m_verdictLabel->limitLabelWidth(ws.width - 20.f, 0.42f, 0.22f);  // fit the sentence

    m_draw->clear();
    if (Mod::get()->getSettingValue<bool>("show-mini-map")) {
        const float y0 = 14.f, h = 8.f, x0 = 10.f, w = ws.width - 20.f;
        // dark baseline track
        m_draw->drawRect({x0, y0}, {x0 + w, y0 + h}, {0.1f, 0.1f, 0.1f, 0.55f}, 0.f, {0.f, 0.f, 0.f, 0.f});
        for (auto const& s : sm->sectionsFor(m_levelKey)) {
            float consistency = 1.f - s.failSm;            // 1 = green, 0 = red
            ccColor4F col = {1.f - consistency, consistency, 0.15f, 0.9f};
            float lx = x0 + (s.lo / 100.f) * w;
            float rx = x0 + (s.hi / 100.f) * w;
            m_draw->drawRect({lx, y0}, {rx, y0 + h}, col, 0.f, {0.f, 0.f, 0.f, 0.f});
        }
    }
}

void HudNode::setLiveProgress(float pct) {
    if (!m_cursor) return;
    if (pct < 0.f || !Mod::get()->getSettingValue<bool>("show-mini-map")) {
        m_cursor->setVisible(false);
        return;
    }
    float cp = pct > 100.f ? 100.f : pct;
    auto ws = CCDirector::get()->getWinSize();
    const float x0 = 10.f, w = ws.width - 20.f;
    m_cursor->setPositionX(x0 + cp / 100.f * w - 1.f);
    m_cursor->setVisible(true);
}
