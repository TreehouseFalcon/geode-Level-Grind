#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include "../../utils/globals.hpp"
#include "Geode/cocos/base_nodes/CCNode.h"
#include "Geode/cocos/label_nodes/CCLabelBMFont.h"
#include "Geode/cocos/sprite_nodes/CCSprite.h"
#include "Geode/ui/ProgressBar.hpp"
#include "ccTypes.h"

#include <UIBuilder.hpp>
#include <fmt/base.h>

using namespace geode::prelude;

namespace levelgrind {

class GrindPackCell : public CCNode {
public:
    static GrindPackCell* create(GrindPack packInfo) {
        auto ret = new GrindPackCell;
        if (ret && ret->init(packInfo)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    bool init(GrindPack packInfo) {
        if (!CCNode::init()) return false;

        this->setContentSize({ 356.f, 100.f });

        auto progressBar = Build(ProgressBar::create(ProgressBarStyle::Solid))
            .parent(this)
            .pos(
                this->getContentSize() / 2
            )
            .with([packInfo](ProgressBar* bar) {
                bar->setFillColor(packInfo.color);
            })
            .anchorPoint({ 0.5f, 0.5f })
            .scale(0.7f)
            .collect();

        size_t i = 0;
        
        if (GameStatsManager::get()->hasCompletedOnlineLevel(packInfo.levels.id1)) i++;
        if (GameStatsManager::get()->hasCompletedOnlineLevel(packInfo.levels.id2)) i++;
        if (GameStatsManager::get()->hasCompletedOnlineLevel(packInfo.levels.id3)) i++;

        int finalPercentage = 0;
        if (i == 1) finalPercentage = 33;
        else if (i == 2) finalPercentage = 66;
        else if (i == 3) finalPercentage = 100;

        progressBar->updateProgress(finalPercentage);

        std::string completed = fmt::format("{}/3", i);

        auto completedLabel = Build(CCLabelBMFont::create(completed.c_str(), "bigFont.fnt"))
            .parent(progressBar)
            .center()
            .collect();

        auto titleLabel = Build(CCLabelBMFont::create(packInfo.title.c_str(), "bigFont.fnt"))
            .color(packInfo.color)
            .parent(this)
            .pos(
                this->getContentWidth() / 2.f,
                (this->getContentHeight() / 2.f) + 32.f
            )
            .collect();

        auto difficultySprite = Build(CCSprite::createWithSpriteFrameName([packInfo] -> const char* {
            switch (packInfo.difficulty) {
                case levelgrind::CustomDifficultyEnum::Auto: return "diffIcon_auto_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::Easy: return "diffIcon_01_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::Normal: return "diffIcon_02_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::Hard: return "diffIcon_03_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::Harder: return "diffIcon_04_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::Insane: return "diffIcon_05_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::EasyDemon: return "diffIcon_07_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::MediumDemon: return "diffIcon_08_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::HardDemon: return "diffIcon_06_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::InsaneDemon: return "diffIcon_09_btn_001.png"; break;
                case levelgrind::CustomDifficultyEnum::ExtremeDemon: return "diffIcon_10_btn_001.png"; break;
                default: return "diffIcon_00_btn_001.png";
            }
        }()))
            .parent(this)
            .pos(
                this->getContentWidth() / 2.f - progressBar->getScaledContentWidth() / 1.5f,
                this->getContentHeight() / 2.f
            )
            .collect();
            
        auto viewBtn = Build(ButtonSprite::create("View", "bigFont.fnt", "GJ_button_01.png"))
            .parent(this)
            .pos(
                this->getContentWidth() / 2.f + progressBar->getScaledContentWidth() / 1.5f,
                this->getContentHeight() / 2.f
            )
            .collect();

        return true;
    }
};

}