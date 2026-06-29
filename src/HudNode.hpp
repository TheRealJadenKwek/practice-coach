#pragma once

#include <Geode/Geode.hpp>
#include <string>

using namespace geode::prelude;

// A screen-fixed overlay (child of PlayLayer, so the gameplay camera doesn't
// move it). One refresh() rebuilds the gold recommendation line, the top-3
// least-consistent list, and the heat-strip mini-map.
class HudNode : public cocos2d::CCNode {
public:
    static HudNode* create(std::string const& levelKey);
    bool init(std::string const& levelKey);
    void refresh();
    void setLiveProgress(float pct);  // live cursor on the heat strip (pass -1 to hide)

private:
    std::string m_levelKey;
    cocos2d::CCLabelBMFont* m_recoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_listLabel = nullptr;
    cocos2d::CCLabelBMFont* m_oddsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_verdictLabel = nullptr;  // keep-grinding / not-ready / go-for-clear
    cocos2d::CCDrawNode*    m_draw = nullptr;
    cocos2d::CCLayerColor*  m_cursor = nullptr;
};
