#include <Geode/Geode.hpp>
#include "../BaseLayer.hpp"
#include "Geode/utils/async.hpp"
#include "Geode/utils/web.hpp"

using namespace geode::prelude;

namespace levelgrind {

class CreatorLayer : public BaseLayer {
public:
    static CreatorLayer* create();

private:
    bool init() override;
    bool initFarMenus();
    bool initMd();

    TaskHolder<web::WebResponse> m_listener;

    ~CreatorLayer() {
        m_listener.cancel();
    }
};

}