#include <Geode/Geode.hpp>
#include "../BaseLayer.hpp"
#include "Geode/ui/ScrollLayer.hpp"
#include "Geode/utils/async.hpp"
#include "Geode/utils/web.hpp"
#include <cue/ListNode.hpp>

using namespace geode::prelude;

namespace levelgrind {

class GrindPacksLayer : public BaseLayer {
public:
    static GrindPacksLayer* create();

private:
    bool init() override;

    cue::ListNode* m_listNode = nullptr;
    ScrollLayer* m_scrollLayer = nullptr;

    TaskHolder<web::WebResponse> m_listener;

    ~GrindPacksLayer() {
        m_listener.cancel();
    }
};

}