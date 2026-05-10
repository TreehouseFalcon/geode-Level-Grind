#include <Geode/Geode.hpp>
#include "../BasePopup.hpp"

using namespace geode::prelude;

namespace levelgrind {

class AddPackPopup : public BasePopup {
public:
    static AddPackPopup* create();

private:
    bool init() override;

    bool m_star = true;
    cocos2d::ccColor3B m_color;

    TaskHolder<web::WebResponse> m_listener;

    ~AddPackPopup() {
        m_listener.cancel();
    }
};

}