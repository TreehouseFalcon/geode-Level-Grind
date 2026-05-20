#include "LGLevelBrowserLayer.hpp"
#include "Geode/cocos/CCDirector.h"
#include "Geode/cocos/cocoa/CCObject.h"
#include "Geode/cocos/label_nodes/CCLabelBMFont.h"
#include "Geode/cocos/menu_nodes/CCMenu.h"
#include "Geode/cocos/sprite_nodes/CCSprite.h"
#include "Geode/ui/General.hpp"
#include "Geode/ui/ProgressBar.hpp"
#include "Geode/ui/ScrollLayer.hpp"
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJListLayer.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/SetIDPopup.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/utils/web.hpp>
#include <algorithm>
#include <cstring>
#include <optional>
#include <random>
#include <unordered_map>

#include <cue/RepeatingBackground.hpp>

using namespace geode::prelude;

static constexpr CCSize LIST_SIZE{356.f, 220.f};
static constexpr int PER_PAGE = 10;

namespace {
struct LGSearchParams {
    std::vector<int> difficulties;
    std::vector<int> lengths;
    std::vector<std::string> grindTypes;
    std::vector<int> demonDifficulties;
    std::vector<int> versions;
    bool newerFirst = false;
    bool recentlyAdded = false;
};

std::unordered_map<GJSearchObject*, LGSearchParams> s_pendingLGSearches;

std::string joinLevelIDs(std::vector<int> const& ids, int start, int end) {
    std::string levelIDs;
    for (int i = start; i < end; ++i) {
        if (!levelIDs.empty()) levelIDs += ",";
        levelIDs += numToString(ids[i]);
    }
    return levelIDs;
}
}

LevelBrowserLayer* LGLevelBrowserLayer::createCompatible(
    std::vector<int> difficulties,
    std::vector<int> lengths,
    std::vector<std::string> grindTypes,
    std::vector<int> demonDifficulties,
    std::vector<int> versions,
    bool newerFirst,
    bool recentlyAdded
) {
    auto object = GJSearchObject::create(SearchType::Type19, "0");
    if (!object) return nullptr;

    s_pendingLGSearches[object] = LGSearchParams {
        std::move(difficulties),
        std::move(lengths),
        std::move(grindTypes),
        std::move(demonDifficulties),
        std::move(versions),
        newerFirst,
        recentlyAdded
    };

    auto layer = LevelBrowserLayer::create(object);
    if (!layer) {
        s_pendingLGSearches.erase(object);
    }
    return layer;
}

class $modify(LGLevelBrowserLayerCompat, LevelBrowserLayer) {
    struct Fields {
        bool m_isLevelGrindBrowser = false;
        bool m_fetchingIDs = false;
        bool m_waitingForGDPage = false;
        LGSearchParams m_params;
        geode::async::TaskHolder<web::WebResponse> m_searchTask;
        std::vector<int> m_allLevelIDs;
        int m_totalLevels = 0;
        int m_totalPages = 1;
        int m_page = 0;
        int m_completedLevels = 0;
        bool m_shouldShowProgressBar = false;
        float m_completionPercent = 0.f;
        CCLabelBMFont* m_completionInfoLabel = nullptr;
        ProgressBar* m_progressBar = nullptr;
        CCMenuItemSpriteExtra* m_randomPageButton = nullptr;
    };

    bool init(GJSearchObject* object) {
        auto pending = s_pendingLGSearches.find(object);
        auto params = pending != s_pendingLGSearches.end()
            ? std::optional<LGSearchParams>(std::move(pending->second))
            : std::nullopt;
        if (pending != s_pendingLGSearches.end()) {
            s_pendingLGSearches.erase(pending);
        }

        if (!LevelBrowserLayer::init(object)) return false;

        if (params) {
            m_fields->m_isLevelGrindBrowser = true;
            m_fields->m_params = std::move(*params);
            m_fields->m_page = 0;
            m_fields->m_totalPages = 1;

            this->setupLevelGrindUI();
            this->updateLevelGrindPageUI();
            this->fetchLevelGrindIDs();
        }

        return true;
    }

    void onExit() {
        if (m_fields->m_isLevelGrindBrowser) {
            m_fields->m_searchTask.cancel();
        }
        LevelBrowserLayer::onExit();
    }

    void onInfo(CCObject* sender) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::onInfo(sender);
            return;
        }

        FLAlertLayer::create(
            "Levels for grinding",
            "Here you can find <cp>levels</c> for <cy>grinding</c>!\n"
            "You can also <cr>configure</c> what levels to search for in the main <cj>Level Grind</c> layer.\n"
            "Thanks for using <cj>Level Grind</c> mod!",
            "OK"
        )->show();
    }

    void onRefresh(CCObject* sender) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::onRefresh(sender);
            return;
        }
        if (m_fields->m_fetchingIDs || m_fields->m_waitingForGDPage) return;
        this->fetchLevelGrindIDs();
    }

    void onNextPage(CCObject* sender) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::onNextPage(sender);
            return;
        }
        if (m_fields->m_fetchingIDs || m_fields->m_waitingForGDPage) return;
        if (m_fields->m_page + 1 >= m_fields->m_totalPages) return;

        m_fields->m_page++;
        this->loadLevelGrindPage();
    }

    void onPrevPage(CCObject* sender) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::onPrevPage(sender);
            return;
        }
        if (m_fields->m_fetchingIDs || m_fields->m_waitingForGDPage) return;
        if (m_fields->m_page <= 0) return;

        m_fields->m_page--;
        this->loadLevelGrindPage();
    }

    void onGoToLastPage(CCObject* sender) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::onGoToLastPage(sender);
            return;
        }
        if (m_fields->m_fetchingIDs || m_fields->m_waitingForGDPage) return;
        if (m_fields->m_totalPages <= 1) return;

        m_fields->m_page = m_fields->m_totalPages - 1;
        this->loadLevelGrindPage();
    }

    void onGoToPage(CCObject* sender) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::onGoToPage(sender);
            return;
        }

        auto current = std::clamp(m_fields->m_page + 1, 1, std::max(1, m_fields->m_totalPages));
        auto popup = SetIDPopup::create(
            current, 1, std::max(1, m_fields->m_totalPages), "Go to Page", "Go",
            false, current, 60.f, true, true
        );
        if (popup) {
            popup->m_delegate = this;
            popup->show();
        }
    }

    void setIDPopupClosed(SetIDPopup* popup, int value) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::setIDPopupClosed(popup, value);
            return;
        }
        if (!popup || popup->m_cancelled) return;

        value = std::clamp(value, 1, std::max(1, m_fields->m_totalPages));
        auto newPage = value - 1;
        if (newPage == m_fields->m_page) return;

        m_fields->m_page = newPage;
        this->loadLevelGrindPage();
    }

    void setupPageInfo(gd::string info, char const* key) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::setupPageInfo(info, key);
            return;
        }
        if (!m_fields->m_waitingForGDPage || !this->isCurrentLevelGrindKey(key)) return;
        this->updateLevelGrindPageUI();
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int type) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::loadLevelsFinished(levels, key, type);
            return;
        }
        if (!m_fields->m_waitingForGDPage || !this->isCurrentLevelGrindKey(key)) return;

        m_fields->m_waitingForGDPage = false;
        LevelBrowserLayer::loadLevelsFinished(levels, key, type);
        this->updateLevelGrindPageUI();
    }

    void loadLevelsFailed(char const* key, int type) {
        if (!m_fields->m_isLevelGrindBrowser) {
            LevelBrowserLayer::loadLevelsFailed(key, type);
            return;
        }
        if (!m_fields->m_waitingForGDPage || !this->isCurrentLevelGrindKey(key)) return;

        m_fields->m_waitingForGDPage = false;
        LevelBrowserLayer::loadLevelsFailed(key, type);
        this->updateLevelGrindPageUI();
    }

    void onRandomLevelGrindPage(CCObject*) {
        if (!m_fields->m_isLevelGrindBrowser) return;
        if (m_fields->m_fetchingIDs || m_fields->m_waitingForGDPage) return;
        if (m_fields->m_totalPages <= 1) return;

        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, m_fields->m_totalPages - 1);

        auto newPage = dist(rng);
        if (newPage == m_fields->m_page) {
            newPage = (newPage + 1) % m_fields->m_totalPages;
        }

        m_fields->m_page = newPage;
        this->loadLevelGrindPage();
    }

    void setupLevelGrindUI() {
        auto winSize = CCDirector::sharedDirector()->getWinSize();

        bool hideCompletionInfo = false;
        if (auto mod = Mod::get()) {
            hideCompletionInfo = mod->getSavedValue<bool>("hide-completion-info");
        }

        m_fields->m_completionInfoLabel = CCLabelBMFont::create("Completed 0 from 0", "goldFont.fnt");
        if (m_fields->m_completionInfoLabel) {
            m_fields->m_completionInfoLabel->setPosition({ winSize.width / 2.f, winSize.height - 5.f });
            m_fields->m_completionInfoLabel->setAnchorPoint({ 0.5f, 1.f });
            m_fields->m_completionInfoLabel->setScale(0.45f);
            m_fields->m_completionInfoLabel->setVisible(!hideCompletionInfo);
            this->addChild(m_fields->m_completionInfoLabel, 10);
        }

        m_fields->m_progressBar = ProgressBar::create(ProgressBarStyle::Slider);
        if (m_fields->m_progressBar) {
            m_fields->m_progressBar->setScale(0.8f);
            m_fields->m_progressBar->setRotation(-90.f);

            CCPoint barPos = { 55.f, winSize.height / 2.f };
            if (m_list) {
                constexpr float gapFromList = 20.f;
                float listLeftX = m_list->getPositionX() + 5.f;
                float barHalfThickness = m_fields->m_progressBar->getScaledContentSize().height * 0.5f;
                barPos = ccp(listLeftX - gapFromList - barHalfThickness, winSize.height / 2.f);
            }
            m_fields->m_progressBar->setPosition(barPos);
            m_fields->m_progressBar->setVisible(false);
            this->addChild(m_fields->m_progressBar, 10);
        }

        auto pageMenu = CCMenu::create();
        pageMenu->setID("lg-level-browser-page-menu");
        pageMenu->setPosition({ winSize.width - 35.f, winSize.height - 82.f });

        auto randomPageSpr = CCSprite::create("GJ_button_01.png");
        if (randomPageSpr) {
            randomPageSpr->setScale(0.7f);
            if (auto randomIcon = CCSprite::create("random_btn_2.png"_spr)) {
                auto size = randomPageSpr->getContentSize();
                randomIcon->setPosition({ size.width / 2.f, size.height / 2.f });
                randomIcon->setAnchorPoint({ 0.5f, 0.5f });
                randomIcon->setScale(0.05f);
                randomPageSpr->addChild(randomIcon, 1);
            }

            m_fields->m_randomPageButton = CCMenuItemSpriteExtra::create(
                randomPageSpr, this, menu_selector(LGLevelBrowserLayerCompat::onRandomLevelGrindPage)
            );
            if (m_fields->m_randomPageButton) {
                m_fields->m_randomPageButton->setID("random-page-btn");
                m_fields->m_randomPageButton->setVisible(false);
                pageMenu->addChild(m_fields->m_randomPageButton);
            }
        }

        this->addChild(pageMenu, 10);
    }

    bool isCurrentLevelGrindKey(char const* key) {
        if (!key || !m_searchObject) return true;
        return std::strcmp(key, m_searchObject->getKey()) == 0;
    }

    void recalculateLevelGrindCompletionProgress() {
        m_fields->m_completedLevels = 0;
        m_fields->m_completionPercent = 0.f;

        auto gsm = GameStatsManager::sharedState();
        if (gsm) {
            for (auto levelID : m_fields->m_allLevelIDs) {
                if (gsm->hasCompletedOnlineLevel(levelID)) {
                    m_fields->m_completedLevels++;
                }
            }
        }

        if (m_fields->m_totalLevels > 0) {
            auto percent = static_cast<float>(m_fields->m_completedLevels) /
                static_cast<float>(m_fields->m_totalLevels);
            m_fields->m_completionPercent = std::clamp(percent * 100.f, 0.f, 100.f);
        }

        if (m_fields->m_progressBar) {
            m_fields->m_progressBar->updateProgress(m_fields->m_completionPercent);
        }
        if (m_fields->m_completionInfoLabel) {
            m_fields->m_completionInfoLabel->setString(
                fmt::format(
                    "Completed {} from {}",
                    m_fields->m_completedLevels,
                    m_fields->m_totalLevels
                ).c_str()
            );
        }
    }

    void updateLevelGrindPageUI() {
        auto totalPages = std::max(1, m_fields->m_totalPages);
        auto first = m_fields->m_totalLevels > 0 ? m_fields->m_page * PER_PAGE + 1 : 0;
        auto last = m_fields->m_totalLevels > 0
            ? std::min((m_fields->m_page + 1) * PER_PAGE, m_fields->m_totalLevels)
            : 0;

        m_itemCount = m_fields->m_totalLevels;
        m_lastPage = totalPages;
        m_pageStartIdx = first;
        m_pageEndIdx = last;

        if (m_countText) {
            m_countText->setString(fmt::format("{} to {} of {}", first, last, m_fields->m_totalLevels).c_str());
        }
        if (m_pageText) {
            m_pageText->setString(numToString(m_fields->m_page + 1).c_str());
        }

        if (m_leftArrow) m_leftArrow->setVisible(m_fields->m_page > 0);
        if (m_rightArrow) m_rightArrow->setVisible(m_fields->m_page + 1 < totalPages);
        if (m_lastBtn) m_lastBtn->setVisible(totalPages > 1 && m_fields->m_page + 1 < totalPages);
        if (m_pageBtn) m_pageBtn->setVisible(totalPages > 1);
        if (m_fields->m_randomPageButton) {
            m_fields->m_randomPageButton->setVisible(totalPages > 1);
        }

        bool hideBar = false;
        bool hideCompletionInfo = false;
        if (auto mod = Mod::get()) {
            hideBar = mod->getSavedValue<bool>("hide-bar");
            hideCompletionInfo = mod->getSavedValue<bool>("hide-completion-info");
        }

        if (m_fields->m_progressBar) {
            m_fields->m_progressBar->setVisible(m_fields->m_shouldShowProgressBar && !hideBar);
        }
        if (m_fields->m_completionInfoLabel) {
            m_fields->m_completionInfoLabel->setVisible(!hideCompletionInfo);
        }
    }

    void finishLevelGrindEmpty(char const* message) {
        if (message) {
            Notification::create(message, NotificationIcon::Info)->show();
        }

        m_fields->m_fetchingIDs = false;
        m_fields->m_waitingForGDPage = false;
        m_fields->m_allLevelIDs.clear();
        m_fields->m_totalLevels = 0;
        m_fields->m_totalPages = 1;
        m_fields->m_page = 0;
        m_fields->m_shouldShowProgressBar = false;
        this->recalculateLevelGrindCompletionProgress();

        auto empty = CCArray::create();
        LevelBrowserLayer::loadLevelsFinished(empty, m_searchObject ? m_searchObject->getKey() : "", 0);
        this->updateLevelGrindPageUI();
    }

    void loadLevelGrindPage() {
        if (m_fields->m_allLevelIDs.empty()) {
            this->finishLevelGrindEmpty(nullptr);
            return;
        }

        auto startIdx = m_fields->m_page * PER_PAGE;
        auto endIdx = std::min(startIdx + PER_PAGE, static_cast<int>(m_fields->m_allLevelIDs.size()));
        if (startIdx >= static_cast<int>(m_fields->m_allLevelIDs.size())) {
            m_fields->m_page = std::max(0, m_fields->m_totalPages - 1);
            startIdx = m_fields->m_page * PER_PAGE;
            endIdx = std::min(startIdx + PER_PAGE, static_cast<int>(m_fields->m_allLevelIDs.size()));
        }

        auto levelIDs = joinLevelIDs(m_fields->m_allLevelIDs, startIdx, endIdx);
        auto object = GJSearchObject::create(SearchType::Type19, levelIDs);
        if (!object) {
            m_fields->m_waitingForGDPage = false;
            Notification::create("Failed to create level search", NotificationIcon::Error)->show();
            return;
        }

        m_fields->m_waitingForGDPage = true;
        this->updateLevelGrindPageUI();
        LevelBrowserLayer::loadPage(object);
    }

    void fetchLevelGrindIDs() {
        if (m_fields->m_fetchingIDs) return;

        m_fields->m_searchTask.cancel();
        m_fields->m_fetchingIDs = true;
        m_fields->m_waitingForGDPage = false;

        matjson::Value body;
        auto const& params = m_fields->m_params;
        if (!params.difficulties.empty()) body["difficulties"] = params.difficulties;
        if (!params.lengths.empty()) body["lengths"] = params.lengths;
        if (!params.demonDifficulties.empty()) body["demonDifficulties"] = params.demonDifficulties;
        if (!params.grindTypes.empty()) body["grindTypes"] = params.grindTypes;
        if (!params.versions.empty()) body["versions"] = params.versions;
        body["newerFirst"] = params.newerFirst;
        body["recentlyAdded"] = params.recentlyAdded;

        auto req = web::WebRequest();
        req.bodyJSON(body);

        WeakRef<LGLevelBrowserLayerCompat> weakSelf = this;
        m_fields->m_searchTask.spawn(
            req.post("https://api.delivel.tech/get_levels"),
            [weakSelf](web::WebResponse const& res) {
                auto self = weakSelf.lock();
                if (!self || !self->getParent() || !self->isRunning()) return;

                self->m_fields->m_fetchingIDs = false;

                if (!res.ok()) {
                    Notification::create("Failed to fetch levels", NotificationIcon::Error)->show();
                    self->finishLevelGrindEmpty(nullptr);
                    return;
                }

                auto jsonRes = res.json();
                if (!jsonRes) {
                    Notification::create("Invalid response from server", NotificationIcon::Error)->show();
                    self->finishLevelGrindEmpty(nullptr);
                    return;
                }

                auto json = jsonRes.unwrap();
                int totalCount = 0;
                if (json.contains("count")) {
                    if (auto count = json["count"].as<int>()) {
                        totalCount = count.unwrap();
                    }
                }

                if (totalCount == 0) {
                    self->finishLevelGrindEmpty("No levels found");
                    return;
                }

                std::vector<int> allIDs;
                if (json.contains("ids")) {
                    if (auto arrRes = json["ids"].asArray()) {
                        for (auto id : arrRes.unwrap()) {
                            if (auto idVal = id.asInt()) {
                                allIDs.push_back(idVal.unwrap());
                            }
                        }
                    }
                }

                if (allIDs.empty()) {
                    self->finishLevelGrindEmpty("No levels found");
                    return;
                }

                std::vector<int> filteredIDs;
                bool onlyUncompleted = false;
                bool onlyCompleted = false;

                if (auto mod = Mod::get()) {
                    onlyUncompleted = mod->getSavedValue<bool>("only-uncompleted");
                    onlyCompleted = mod->getSavedValue<bool>("only-completed");
                }

                if (onlyUncompleted || onlyCompleted) {
                    if (auto gsm = GameStatsManager::sharedState()) {
                        for (auto id : allIDs) {
                            auto isCompleted = gsm->hasCompletedOnlineLevel(id);
                            if ((onlyUncompleted && !isCompleted) || (onlyCompleted && isCompleted)) {
                                filteredIDs.push_back(id);
                            }
                        }
                    } else {
                        filteredIDs = allIDs;
                    }
                } else {
                    filteredIDs = allIDs;
                }

                if (filteredIDs.empty()) {
                    self->finishLevelGrindEmpty(
                        onlyUncompleted ? "No uncompleted levels found" :
                        onlyCompleted ? "No completed levels found" :
                        "No levels found"
                    );
                    return;
                }

                self->m_fields->m_allLevelIDs = std::move(filteredIDs);
                self->m_fields->m_totalLevels = static_cast<int>(self->m_fields->m_allLevelIDs.size());
                self->m_fields->m_totalPages = std::max(
                    1,
                    (self->m_fields->m_totalLevels + PER_PAGE - 1) / PER_PAGE
                );
                self->m_fields->m_shouldShowProgressBar = !onlyUncompleted && !onlyCompleted;
                if (self->m_fields->m_page >= self->m_fields->m_totalPages) {
                    self->m_fields->m_page = std::max(0, self->m_fields->m_totalPages - 1);
                }

                self->recalculateLevelGrindCompletionProgress();
                self->loadLevelGrindPage();
            }
        );
    }
};

LGLevelBrowserLayer* LGLevelBrowserLayer::create(
    std::vector<int> difficulties, 
    std::vector<int> lengths, 
    std::vector<std::string> grindTypes, 
    std::vector<int> demonDifficulties, 
    std::vector<int> versions,
    bool newerFirst,
    bool recentlyAdded
) {
    auto ret = new LGLevelBrowserLayer;

    ret->m_difficulties = difficulties;
    ret->m_lengths = lengths;
    ret->m_grindTypes = grindTypes;
    ret->m_demonDifficulties = demonDifficulties;
    ret->m_versions = versions;
    ret->m_isNewerFirst = newerFirst;
    ret->m_isRecentlyAdded = recentlyAdded;

    if (ret && ret->init(nullptr)) {
        ret->autorelease();
        return ret;
    }
    
    delete ret;
    return nullptr;
}

bool LGLevelBrowserLayer::init(GJSearchObject* object) {
    if (!CCLayer::init()) return false;

    
    m_searchObject = nullptr;
    m_listLayer = nullptr;
    m_scrollLayer = nullptr;
    m_circle = nullptr;
    m_levelsLabel = nullptr;
    m_completionInfoLabel = nullptr;
    m_pageButtonLabel = nullptr;
    m_pageButton = nullptr;
    m_randomPageButton = nullptr;
    m_refreshBtn = nullptr;
    m_prevButton = nullptr;
    m_nextButton = nullptr;
    m_progressBar = nullptr;
    m_completedLevels = 0;
    m_shouldShowProgressBar = false;
    m_completionPercent = 0.f;
    
    m_searchObject = object;

    if (Mod::get()->getSavedValue<bool>("disable-custom-background")) {
		auto bg = createLayerBG();
		bg->setColor({ 0, 102, 255 });
        addChild(bg, -1);
	} else {
		auto customBg = cue::RepeatingBackground::create("game_bg_01_001.png", 1.0f, cue::RepeatMode::X);
        customBg->setColor(Mod::get()->getSavedValue<cocos2d::ccColor3B>("rgbBackground"));
        customBg->setSpeed(Mod::get()->getSavedValue<float>("background-speed"));
		addChild(customBg, -1);
	}

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    const char* title = "Grinding Levels";

    auto uiMenu = CCMenu::create();
    uiMenu->setPosition({ 0, 0 });

    if (!Mod::get()->getSavedValue<bool>("disable-star-particles")) {
        auto grindParticles = CCParticleSnow::create();
        auto texture = CCTextureCache::sharedTextureCache()->addImage("GJ_bigStar_noShadow.png"_spr, true);
        grindParticles->m_fStartSpin = 0.f;
        grindParticles->m_fEndSpin = 180.f;
        grindParticles->m_fStartSize = 6.f;
        grindParticles->m_fEndSize = 3.f;
        grindParticles->setTexture(texture);

        this->addChild(grindParticles);
    }

    addBackButton(this, BackButtonStyle::Green);

    auto cornerLeft = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    auto cornerRight = CCSprite::createWithSpriteFrameName("GJ_sideArt_001.png");
    cornerRight->setFlipX(true);

    this->addChild(cornerLeft);
    this->addChild(cornerRight);

    cornerLeft->setPosition({ 35.5, 35.5 });
    cornerRight->setPosition({ winSize.width - 35.5f, 35.5f });

    auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");

    auto infoButton = CCMenuItemSpriteExtra::create(
        infoSpr,
        this,
        menu_selector(LGLevelBrowserLayer::onInfoButton)
    );
    infoButton->setPosition({ 25, 25 });
    uiMenu->addChild(infoButton);

    m_listLayer = GJListLayer::create(
        nullptr, title, {191, 114, 62, 255}, LIST_SIZE.width, LIST_SIZE.height, 0
    );

    m_listLayer->setPosition(
        { winSize / 2 - m_listLayer->getScaledContentSize() / 2 - 5 }
    );

    auto scrollLayer = ScrollLayer::create({
        m_listLayer->getContentSize().width, m_listLayer->getContentSize().height
    });
    scrollLayer->setPosition({ 0, 0 });
    m_listLayer->addChild(scrollLayer);

    m_scrollLayer = scrollLayer;

    auto contentLayer = scrollLayer->m_contentLayer;
    if (contentLayer) {
        auto layout = ColumnLayout::create();
        contentLayer->setLayout(layout);
        layout->setGap(0.f);
        layout->setAutoGrowAxis(220.f);
        layout->setAxisReverse(true);
        layout->setAxisAlignment(AxisAlignment::End);
    }

    this->addChild(m_listLayer);
    this->addChild(uiMenu, 10);

    m_levelsLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_levelsLabel->setPosition({ winSize.width - 5, winSize.height - 5 });
    m_levelsLabel->setAnchorPoint({ 1.f, 1.f });
    m_levelsLabel->setScale(0.45f);
    this->addChild(m_levelsLabel, 10);

    bool hideCompletionInfo = false;
    if (auto mod = Mod::get()) {
        hideCompletionInfo = mod->getSavedValue<bool>("hide-completion-info");
    }

    m_completionInfoLabel = CCLabelBMFont::create("Completed 0 from 0", "goldFont.fnt");
    if (m_completionInfoLabel) {
        m_completionInfoLabel->setPosition({ winSize.width / 2.f, winSize.height - 5.f });
        m_completionInfoLabel->setAnchorPoint({ 0.5f, 1.f });
        m_completionInfoLabel->setScale(0.45f);
        m_completionInfoLabel->setVisible(!hideCompletionInfo);
        this->addChild(m_completionInfoLabel, 10);
    }

    auto pageBtnSpr = CCSprite::create("GJ_button_02.png");
    pageBtnSpr->setScale(0.7f);
    if (pageBtnSpr) {
        m_pageButton = CCMenuItemSpriteExtra::create(
            pageBtnSpr, this, menu_selector(LGLevelBrowserLayer::onPageButton)
        );
        if (m_pageButton) {
            auto pageMenu = CCMenu::create();
            pageMenu->setID("lg-page-menu");
            pageMenu->setContentSize({ 28, 110 });
            pageMenu->setPosition({winSize.width - 7, winSize.height - 80});
            pageMenu->setAnchorPoint({1.f, 0.5f});
            pageMenu->setLayout(ColumnLayout::create()
                    ->setGap(5.f)
                    ->setAutoScale(true)
                    ->setGrowCrossAxis(true)
                    ->setCrossAxisOverflow(true)
                    ->setAxisReverse(true)
                    ->setAxisAlignment(AxisAlignment::End));
            pageMenu->addChild(m_pageButton);

            auto randomPageSpr = CCSprite::create("GJ_button_01.png");
            if (randomPageSpr) {
                randomPageSpr->setScale(0.7f);

                auto randomIcon = CCSprite::create("random_btn_2.png"_spr);
                if (randomIcon) {
                    auto size = randomPageSpr->getContentSize();
                    randomIcon->setPosition({ size.width / 2.f, size.height / 2.f });
                    randomIcon->setAnchorPoint({ 0.5f, 0.5f });
                    randomIcon->setScale(0.05f);
                    randomPageSpr->addChild(randomIcon, 1);
                }

                m_randomPageButton = CCMenuItemSpriteExtra::create(
                    randomPageSpr, this, menu_selector(LGLevelBrowserLayer::onRandomPage)
                );

                if (m_randomPageButton) {
                    m_randomPageButton->setID("random-page-btn");
                    m_randomPageButton->setVisible(false);
                    pageMenu->addChild(m_randomPageButton);
                }
            }

            this->addChild(pageMenu, 10);
            pageMenu->updateLayout();
            if (m_randomPageButton) {
                m_randomPageButton->setPosition({ 14.f, 64.f });
            }

            m_pageButtonLabel = CCLabelBMFont::create(numToString(m_page + 1).c_str(), "bigFont.fnt");

            if (m_pageButtonLabel) {
                auto size = m_pageButton->getContentSize();
                m_pageButtonLabel->setPosition({size.width / 2.f, size.height / 2.f});
                m_pageButtonLabel->setAnchorPoint({0.5f, 0.5f});
                m_pageButtonLabel->setID("lg-page-label");
                m_pageButtonLabel->setScale(0.6f);
                m_pageButton->addChild(m_pageButtonLabel, 1);
            }
            this->updatePageButton();
        }
    }

    auto refreshSpr = CCSprite::createWithSpriteFrameName("GJ_replayBtn_001.png");
    refreshSpr->setScale(0.75f);
    m_refreshBtn = CCMenuItemSpriteExtra::create(
        refreshSpr, this, menu_selector(LGLevelBrowserLayer::onRefresh));
    m_refreshBtn->setPosition({winSize.width - 35, 35});
    m_refreshBtn->setID("refresh-btn");

    uiMenu->addChild(m_refreshBtn);

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");

    m_prevButton = CCMenuItemSpriteExtra::create(
        prevSpr, 
        this, 
        menu_selector(LGLevelBrowserLayer::onPrevPage)
    );

    m_prevButton->setPosition({20, winSize.height / 2});
    uiMenu->addChild(m_prevButton);

    if (m_prevButton) {
        m_prevButton->setVisible(false);
    }

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    nextSpr->setFlipX(true);
    m_nextButton = CCMenuItemSpriteExtra::create(
        nextSpr, 
        this, 
        menu_selector(LGLevelBrowserLayer::onNextPage)
    );
    m_nextButton->setPosition({winSize.width - 20, winSize.height / 2});
    uiMenu->addChild(m_nextButton);

    if (m_nextButton) m_nextButton->setVisible(false);

    m_progressBar = ProgressBar::create(ProgressBarStyle::Slider);

    if (m_progressBar) {
        m_progressBar->setScale(0.8f);

        m_progressBar->setRotation(-90.f);

        if (m_progressBar) {

            constexpr float gapFromList = 20.f;
            float listLeftX = m_listLayer->getPositionX() + 5.f;
            float listCenterY = winSize.height / 4.f - 10.f;
            float barHalfThickness = m_progressBar->getScaledContentSize().height * 0.5f;

            CCPoint barPos = {
                listLeftX - gapFromList - barHalfThickness,
                listCenterY
            };

            m_progressBar->setPosition(barPos);

            addChild(m_progressBar);

            m_progressBar->setVisible(false);
        } else {
            log::error("failed to create progress timer");
        }
    } else {
        log::error("failed to create progress bar sprites");
    }

    m_circle = nullptr;

    auto glm = GameLevelManager::get();
    if (glm) {
        glm->m_levelManagerDelegate = this;
    }

    this->scheduleUpdate();
    this->setKeypadEnabled(true);
    
    this->performFetchLevels();

    return true;
}

void LGLevelBrowserLayer::onInfoButton(CCObject* sender) {
    FLAlertLayer::create(
        "Levels for grinding",
        "Here you can find <cp>levels</c> for <cy>grinding</c>!\n" \
        "You can also <cr>configure</c> what levels to search for in the main <cj>Level Grind</c> layer.\n" \
        "Thanks for using <cj>Level Grind</c> mod!",
        "OK"
    )->show();
}

void LGLevelBrowserLayer::startLoading() {
    m_loading = true;
    
    if (m_circle) {
        m_circle->removeFromParent();
        m_circle = nullptr;
    }
    
    auto spinner = LoadingSpinner::create(64.f);
    if (spinner) {
        spinner->setID("lg-spinner");
        auto win = CCDirector::sharedDirector()->getWinSize();
        spinner->setPosition(win / 2);
        this->addChild(spinner, 1000);
        m_circle = spinner;
    } else {
        log::error("failed to create loading spinner");
    }
}

void LGLevelBrowserLayer::stopLoading() {
    m_loading = false;
    if (m_circle) {
        m_circle->removeFromParent();
        m_circle = nullptr;
    }
    
    if (m_scrollLayer) {
        m_scrollLayer->setVisible(true);
    }
    
    this->showUIElements();
}

void LGLevelBrowserLayer::hideUIElements() {
    if (m_nextButton) m_nextButton->setVisible(false);
    if (m_refreshBtn) m_refreshBtn->setVisible(false);
    if (m_pageButton) m_pageButton->setVisible(false);
    if (m_randomPageButton) m_randomPageButton->setVisible(false);
    if (m_levelsLabel) m_levelsLabel->setVisible(false);
    if (m_completionInfoLabel) m_completionInfoLabel->setVisible(false);
    if (m_progressBar) m_progressBar->setVisible(false);
}

void LGLevelBrowserLayer::showUIElements() {
    bool hideBar = false;
    bool hideCompletionInfo = false;
    if (auto mod = Mod::get()) {
        hideBar = mod->getSavedValue<bool>("hide-bar");
        hideCompletionInfo = mod->getSavedValue<bool>("hide-completion-info");
    }

    if (m_refreshBtn) m_refreshBtn->setVisible(true);
    if (m_levelsLabel) m_levelsLabel->setVisible(true);
    if (m_completionInfoLabel) m_completionInfoLabel->setVisible(!hideCompletionInfo);
    if (m_progressBar) m_progressBar->setVisible(m_shouldShowProgressBar && !hideBar);
    
    if (m_prevButton) m_prevButton->setVisible(m_page > 0);
    if (m_nextButton) m_nextButton->setVisible(m_page + 1 < m_totalPages);
    if (m_pageButton) m_pageButton->setVisible(m_totalPages > 1);
    if (m_randomPageButton) m_randomPageButton->setVisible(m_totalPages > 1);
}

void LGLevelBrowserLayer::recalculateCompletionProgress() {
    m_completedLevels = 0;
    m_completionPercent = 0.f;

    if (m_allLevelIDs.empty()) {
        if (m_progressBar) {
            m_progressBar->updateProgress(m_completionPercent);
        }
        if (m_completionInfoLabel) {
            m_completionInfoLabel->setString(
                fmt::format("Completed {} from {}", m_completedLevels, m_totalLevels).c_str()
            );
        }
        return;
    }

    auto gsm = GameStatsManager::sharedState();
    if (gsm) {
        for (auto levelID : m_allLevelIDs) {
            if (gsm->hasCompletedOnlineLevel(levelID)) {
                m_completedLevels++;
            }
        }
    }

    if (m_totalLevels > 0) {
        float percent = static_cast<float>(m_completedLevels) / static_cast<float>(m_totalLevels);
        m_completionPercent = std::clamp(percent * 100.f, 0.f, 100.f);
    }

    if (m_progressBar) {
        m_progressBar->updateProgress(m_completionPercent);
    }
    if (m_completionInfoLabel) {
        m_completionInfoLabel->setString(
            fmt::format("Completed {} from {}", m_completedLevels, m_totalLevels).c_str()
        );
    }
}

void LGLevelBrowserLayer::refreshLevels() {
    if (m_loading) return;
    
    m_allLevelIDs.clear(); 
    
    this->hideUIElements();
    
    if (m_scrollLayer) {
        m_scrollLayer->setVisible(false);
    }
    
    this->performFetchLevels();
}

void LGLevelBrowserLayer::onNextPage(CCObject* sender) {
    if (!this->getParent() || !this->isRunning()) return;
    if (m_loading) return;
    if (m_page + 1 < m_totalPages) {
        m_page++;
        
        this->hideUIElements();
        
        this->loadPageFromStoredIDs(); 
    }
}

void LGLevelBrowserLayer::onPrevPage(CCObject* sender) {
    if (!this->getParent() || !this->isRunning()) return;
    if (m_loading) return;
    if (m_page > 0) {
        m_page--;
        
        this->hideUIElements();
        
        this->loadPageFromStoredIDs(); 
    }
}

void LGLevelBrowserLayer::onRefresh(CCObject* sender) {
    if (!this->getParent() || !this->isRunning()) return;
    this->refreshLevels();
}

void LGLevelBrowserLayer::onRandomPage(CCObject* sender) {
    if (!this->getParent() || !this->isRunning()) return;
    if (m_loading) return;
    if (m_totalPages <= 1) return;

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, m_totalPages - 1);

    int newPage = dist(rng);
    if (m_totalPages > 1 && newPage == m_page) {
        newPage = (newPage + 1) % m_totalPages;
    }

    if (newPage == m_page) return;

    m_page = newPage;
    this->hideUIElements();
    this->loadPageFromStoredIDs();
}

void LGLevelBrowserLayer::loadPageFromStoredIDs() {
    if (m_allLevelIDs.empty() || m_loading) return;
    
    if (m_scrollLayer && m_scrollLayer->m_contentLayer) {
        m_scrollLayer->m_contentLayer->removeAllChildrenWithCleanup(true);
    }
    
    this->startLoading();
    
    int startIdx = m_page * PER_PAGE;
    int endIdx = std::min(startIdx + PER_PAGE, static_cast<int>(m_allLevelIDs.size()));
    
    if (startIdx >= static_cast<int>(m_allLevelIDs.size())) {
        
        stopLoading();
        return;
    }
    
    std::vector<int> pageIDs;
    for (int i = startIdx; i < endIdx; ++i) {
        pageIDs.push_back(m_allLevelIDs[i]);
    }
    
    
    if (pageIDs.size() > 99) {
        pageIDs.resize(99);
    }
    
    
    std::string levelIDs;
    for (size_t i = 0; i < pageIDs.size(); ++i) {
        if (i > 0) levelIDs += ",";
        levelIDs += numToString(pageIDs[i]);
    }
    
    
    m_searchObject = GJSearchObject::create(SearchType::Type19, levelIDs);
    
    auto glm = GameLevelManager::get();
    if (glm) {
        glm->m_levelManagerDelegate = this;
        glm->getOnlineLevels(m_searchObject);
    } else {
        stopLoading();
    }
}

void LGLevelBrowserLayer::updatePageButton() {
    if (!this->getParent() || !this->isRunning()) return;
    
    if (m_prevButton) {
        m_prevButton->setVisible(m_page > 0);
    }
    if (m_nextButton) {
        m_nextButton->setVisible(m_page + 1 < m_totalPages);
    }
    if (m_pageButtonLabel) {
        m_pageButtonLabel->setString(numToString(m_page + 1).c_str());
    }
    if (m_pageButton) {
        m_pageButton->setVisible(m_totalPages > 1);
    }
    if (m_randomPageButton) {
        m_randomPageButton->setVisible(m_totalPages > 1);
    }
}

void LGLevelBrowserLayer::onPageButton(CCObject* sender) {
    if (!this->getParent() || !this->isRunning()) return;
    
    int current = std::clamp(m_page + 1, 1, std::max(1, m_totalPages));
    int begin = 1;
    int end = std::max(1, m_totalPages);
    auto popup = SetIDPopup::create(current, begin, end, "Go to Page", "Go",
                                    false, current, 60.f, true, true);
    if (popup) {
        popup->m_delegate = this;
        popup->show();
    }
}

void LGLevelBrowserLayer::keyBackClicked() {
    CCDirector::sharedDirector()->popSceneWithTransition(
        0.5f, PopTransition::kPopTransitionFade);
}

void LGLevelBrowserLayer::loadLevelsFinished(CCArray* levels, char const* key, int p2) {
    if (!this->getParent() || !this->isRunning()) {
        return;
    }
    
    if (!levels) {
        stopLoading();
        return;
    }
    
    populateFromArray(levels);
    m_scrollLayer->setVisible(true);
    stopLoading();
}

void LGLevelBrowserLayer::loadLevelsFailed(char const* key, int p1) {
    if (!this->getParent() || !this->isRunning()) {
        return;
    }
    Notification::create("Failed to load levels from GD servers", NotificationIcon::Error)->show();
    stopLoading();
}

void LGLevelBrowserLayer::populateFromArray(CCArray* levels) {
    if (!this->getParent() || !this->isRunning()) {
        return;
    }
    
    if (!m_scrollLayer || !m_scrollLayer->m_contentLayer || !levels) return;
    
    auto contentLayer = m_scrollLayer->m_contentLayer;
    contentLayer->removeAllChildrenWithCleanup(true);
    
    const float cellH = 90.f;
    int index = 0;
    
    for (GJGameLevel* level : CCArrayExt<GJGameLevel*>(levels)) {
        if (!level) continue;
        
        auto cell = LevelCell::create(356.f, cellH);
        cell->loadFromLevel(level);
        cell->setContentSize({356.f, cellH});
        cell->setAnchorPoint({0.0f, 1.0f});
        cell->updateBGColor(index);
        contentLayer->addChild(cell);
        index++;
    }
    
    int returned = static_cast<int>(levels->count());
    int first = m_page * PER_PAGE + 1;
    int last = m_page * PER_PAGE + returned;
    if (returned == 0) {
        first = 0;
        last = 0;
    }
    int total = (m_totalLevels > 0) ? m_totalLevels : returned;
    
    if (m_levelsLabel) {
        m_levelsLabel->setString(
            fmt::format("{} to {} of {}", first, last, total).c_str()
        );
    }
    
    m_needsLayout = true;
    this->updatePageButton();
}

void LGLevelBrowserLayer::setIDPopupClosed(SetIDPopup* popup, int value) {
    if (!this->getParent() || !this->isRunning()) return;
    if (!popup || popup->m_cancelled) return;
    
    if (value < 1) value = 1;
    if (m_totalPages > 0 && value > m_totalPages) value = m_totalPages;
    
    int newPage = value - 1;
    if (newPage != m_page) {
        m_page = newPage;
        this->updatePageButton();
        this->loadPageFromStoredIDs(); 
    }
}

void LGLevelBrowserLayer::onEnter() {
    CCLayer::onEnter();
    this->setTouchEnabled(true);
    this->scheduleUpdate();
    this->recalculateCompletionProgress();
}

void LGLevelBrowserLayer::onExit() {
    auto glm = GameLevelManager::get();
    if (glm && glm->m_levelManagerDelegate == this) {
        glm->m_levelManagerDelegate = nullptr;
    }
    CCLayer::onExit();
}

void LGLevelBrowserLayer::update(float dt) {
    if (!this->getParent() || !this->isRunning()) return;
    
    if (m_needsLayout) {
        if (m_scrollLayer && m_scrollLayer->m_contentLayer) {
            auto contentLayer = m_scrollLayer->m_contentLayer;
            if (contentLayer->getChildren() && 
                contentLayer->getChildren()->count() > 0) {
                contentLayer->updateLayout();
                if (m_scrollLayer) {
                    m_scrollLayer->scrollToTop();
                }
            }
        }
        m_needsLayout = false;
    }
    
    if (m_pageButtonLabel) {
        m_pageButtonLabel->setString(numToString(m_page + 1).c_str());
    }
    if (m_pageButton) {
        m_pageButton->setVisible(m_totalPages > 1);
    }
    if (m_randomPageButton) {
        m_randomPageButton->setVisible(m_totalPages > 1);
    }
}

void LGLevelBrowserLayer::performFetchLevels() {
    if (m_loading) return;
    
    this->startLoading();
    
    matjson::Value body;
    if (!m_difficulties.empty()) body["difficulties"] = m_difficulties;
    if (!m_lengths.empty()) body["lengths"] = m_lengths;
    if (!m_demonDifficulties.empty()) body["demonDifficulties"] = m_demonDifficulties;
    if (!m_grindTypes.empty()) body["grindTypes"] = m_grindTypes;
    if (!m_versions.empty()) body["versions"] = m_versions;
    if (m_isNewerFirst) {
        body["newerFirst"] = true;
    } else {
        body["newerFirst"] = false;
    }

    if (m_isRecentlyAdded) {
        body["recentlyAdded"] = true;
    } else {
        body["recentlyAdded"] = false;
    }
    
    auto req = web::WebRequest();
    req.bodyJSON(body);
    
    WeakRef<LGLevelBrowserLayer> weakSelf = this;
    
    m_searchTask.spawn(
        req.post("https://api.delivel.tech/get_levels"),
        [weakSelf](web::WebResponse const& res) {
            
            auto self = weakSelf.lock();
            if (!self) return;
            if (!self->getParent() || !self->isRunning()) return;
            
            if (!res.ok()) {
                Notification::create("Failed to fetch levels", NotificationIcon::Error)->show();
                self->stopLoading();
                return;
            }
            
            auto jsonRes = res.json();
            if (!jsonRes) {
                Notification::create("Invalid response from server", NotificationIcon::Error)->show();
                self->stopLoading();
                return;
            }
            
            auto json = jsonRes.unwrap();
            
            int totalCount = 0;
            if (json.contains("count")) {
                if (auto count = json["count"].as<int>(); count) {
                    totalCount = count.unwrap();
                }
            }
            
            if (totalCount == 0) {
                Notification::create("No levels found", NotificationIcon::Info)->show();
                self->stopLoading();
                
                
                if (self->m_scrollLayer && self->m_scrollLayer->m_contentLayer) {
                    self->m_scrollLayer->m_contentLayer->removeAllChildrenWithCleanup(true);
                }
                
                self->m_levelsLabel->setString("0 to 0 of 0");
                self->m_totalLevels = 0;
                self->m_totalPages = 1;
                self->m_shouldShowProgressBar = false;
                self->m_completedLevels = 0;
                self->m_completionPercent = 0.f;
                if (self->m_progressBar) {
                    self->m_progressBar->updateProgress(0.f);
                }
                if (self->m_completionInfoLabel) {
                    self->m_completionInfoLabel->setString("Completed 0 from 0");
                }
                self->updatePageButton();
                return;
            }
            
            std::vector<int> allIDs;
            if (json.contains("ids")) {
                auto arrRes = json["ids"].asArray();
                if (arrRes) {
                    for (auto id : arrRes.unwrap()) {
                        if (auto idVal = id.asInt(); idVal) {
                            allIDs.push_back(idVal.unwrap());
                        }
                    }
                }
            }
            
            if (allIDs.empty()) {
                Notification::create("No levels found", NotificationIcon::Info)->show();
                self->stopLoading();
                
                
                if (self->m_scrollLayer && self->m_scrollLayer->m_contentLayer) {
                    self->m_scrollLayer->m_contentLayer->removeAllChildrenWithCleanup(true);
                }
                
                self->m_levelsLabel->setString("0 to 0 of 0");
                self->m_totalLevels = 0;
                self->m_totalPages = 1;
                self->m_shouldShowProgressBar = false;
                self->m_completedLevels = 0;
                self->m_completionPercent = 0.f;
                if (self->m_progressBar) {
                    self->m_progressBar->updateProgress(0.f);
                }
                if (self->m_completionInfoLabel) {
                    self->m_completionInfoLabel->setString("Completed 0 from 0");
                }
                self->updatePageButton();
                return;
            }
            
            std::vector<int> filteredIDs;
            bool onlyUncompleted = false;
            bool onlyCompleted = false;
            
            if (auto mod = Mod::get()) {
                onlyUncompleted = mod->getSavedValue<bool>("only-uncompleted");
                onlyCompleted = mod->getSavedValue<bool>("only-completed");
            }
            
            if (onlyUncompleted) {
                auto gsm = GameStatsManager::sharedState();
                if (gsm) {
                    for (auto id : allIDs) {
                        auto isCompleted = gsm->hasCompletedOnlineLevel(id);
                        if (!isCompleted) {
                            filteredIDs.push_back(id);
                        }
                    }
                } else {
                    filteredIDs = allIDs;
                }
                
                if (filteredIDs.empty()) {
                    Notification::create("No uncompleted levels found", NotificationIcon::Info)->show();
                    self->stopLoading();
                    
                    if (self->m_scrollLayer && self->m_scrollLayer->m_contentLayer) {
                        self->m_scrollLayer->m_contentLayer->removeAllChildrenWithCleanup(true);
                    }
                    
                    self->m_levelsLabel->setString("0 to 0 of 0");
                    self->m_totalLevels = 0;
                    self->m_totalPages = 1;
                    self->m_shouldShowProgressBar = false;
                    self->m_completedLevels = 0;
                    self->m_completionPercent = 0.f;
                    if (self->m_progressBar) {
                        self->m_progressBar->updateProgress(0.f);
                    }
                    if (self->m_completionInfoLabel) {
                        self->m_completionInfoLabel->setString("Completed 0 from 0");
                    }
                    self->updatePageButton();
                    return;
                }
            } else {
                if (onlyCompleted) {
                    auto gsm = GameStatsManager::sharedState();
                    if (gsm) {
                        for (auto id : allIDs) {
                            auto isCompleted = gsm->hasCompletedOnlineLevel(id);
                            if (isCompleted) {
                                filteredIDs.push_back(id);
                            }
                        }
                    } else {
                        filteredIDs = allIDs;
                    }
                    
                    if (filteredIDs.empty()) {
                        Notification::create("No completed levels found", NotificationIcon::Info)->show();
                        self->stopLoading();
                        
                        if (self->m_scrollLayer && self->m_scrollLayer->m_contentLayer) {
                            self->m_scrollLayer->m_contentLayer->removeAllChildrenWithCleanup(true);
                        }
                        
                        self->m_levelsLabel->setString("0 to 0 of 0");
                        self->m_totalLevels = 0;
                        self->m_totalPages = 1;
                        self->m_shouldShowProgressBar = false;
                        self->m_completedLevels = 0;
                        self->m_completionPercent = 0.f;
                        if (self->m_progressBar) {
                            self->m_progressBar->updateProgress(0.f);
                        }
                        if (self->m_completionInfoLabel) {
                            self->m_completionInfoLabel->setString("Completed 0 from 0");
                        }
                        self->updatePageButton();
                        return;
                    }
                } else {
                    filteredIDs = allIDs;
                }
            }
            
            self->m_allLevelIDs = filteredIDs;
            self->m_totalLevels = static_cast<int>(filteredIDs.size());
            self->m_totalPages = (self->m_totalLevels + PER_PAGE - 1) / PER_PAGE;
            self->m_shouldShowProgressBar = !onlyUncompleted && !onlyCompleted;
            self->recalculateCompletionProgress();
            if (self->m_totalPages < 1) {
                self->m_totalPages = 1;
            }
            
            if (self->m_page >= self->m_totalPages) {
                self->m_page = std::max(0, self->m_totalPages - 1);
            }
            
            int startIdx = self->m_page * PER_PAGE;
            int endIdx = std::min(startIdx + PER_PAGE, static_cast<int>(filteredIDs.size()));
            
            std::vector<int> pageIDs;
            for (int i = startIdx; i < endIdx; ++i) {
                pageIDs.push_back(filteredIDs[i]);
            }
            
            if (pageIDs.size() > 99) {
                pageIDs.resize(99);
            }
            
            std::string levelIDs;
            for (size_t i = 0; i < pageIDs.size(); ++i) {
                if (i > 0) levelIDs += ",";
                levelIDs += numToString(pageIDs[i]);
            }
            
            self->m_searchObject = GJSearchObject::create(SearchType::Type19, levelIDs);
            
            auto glm = GameLevelManager::get();
            if (glm && self->getParent() && self->isRunning()) {
                glm->m_levelManagerDelegate = self;
                glm->getOnlineLevels(self->m_searchObject);
            } else {
                self->stopLoading();
            }
        }
    );
}
