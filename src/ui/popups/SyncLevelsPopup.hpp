#include <Geode/Geode.hpp>
#include "../BasePopup.hpp"

using namespace geode::prelude;

namespace levelgrind {

class SyncLevelsPopup : public BasePopup {
public:
    static SyncLevelsPopup* create();

private:
    bool init() override;

    TaskHolder<web::WebResponse> m_listener;

    ~SyncLevelsPopup() {
        m_listener.cancel();
    }
};

}