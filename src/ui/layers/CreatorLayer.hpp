#include <Geode/Geode.hpp>
#include "../BaseLayer.hpp"

using namespace geode::prelude;

namespace levelgrind {

class CreatorLayer : public BaseLayer {
public:
    static CreatorLayer* create();

private:
    bool init() override;
    bool initFarMenus();
};

}