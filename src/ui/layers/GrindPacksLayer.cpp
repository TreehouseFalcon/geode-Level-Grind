#include "GrindPacksLayer.hpp"

#include "../../managers/APIClient.hpp"
#include "../components/GrindPackCell.hpp"
#include "Geode/ui/Notification.hpp"
#include "Geode/utils/web.hpp"
#include <UIBuilder.hpp>

static constexpr CCSize LIST_SIZE {356.f, 220.f};
static constexpr int PER_PAGE = 10;

namespace levelgrind {

GrindPacksLayer* GrindPacksLayer::create() {
    auto ret = new GrindPacksLayer;
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool GrindPacksLayer::init() {
    if (!BaseLayer::init()) return false;

    replaceBgToClassic();

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    addSideArt(this, SideArt::Bottom, false);

    auto uiMenu = Build(CCMenu::create())
        .pos({ 0, 0 })
        .collect();

    auto infoButton = Build(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"))
        .intoMenuItem([] {
            FLAlertLayer::create(
                "Grind Packs",
                "Each pack consists of 3 grindable levels. Find one for you!",
                "OK"
            )->show();
        })
        .pos({ 25, 25 })
        .parent(uiMenu)
        .collect();

    m_listNode = Build(cue::ListNode::create({LIST_SIZE.width, LIST_SIZE.height}, {191, 114, 62, 255}, cue::ListBorderStyle::Levels))
        .anchorPoint({ 0.5f, 0.5f })
        .pos({ winSize.width / 2 - 5.f, winSize.height / 2 - 5.f })
        .parent(this)
        .zOrder(5)
        .collect();

    m_scrollLayer = m_listNode->getScrollLayer();

    auto contentLayer = m_scrollLayer->m_contentLayer;

    auto s = Ref(this);

    m_listener.spawn(
        APIClient::getInstance().getGrindPacks(),
        [s](web::WebResponse r) {
            if (!s) return;
            auto parsed = APIClient::getInstance().getGrindPacksParse(r);

            if (!parsed.ok) {
                Notification::create("Failed! Try again later.", NotificationIcon::Error)->show();
                return;
            }

            for (const auto& val : parsed.packs) {
                auto cell = GrindPackCell::create(val);
                s->m_listNode->addCell(cell);
            }
        }
    );

    return true;
}

}