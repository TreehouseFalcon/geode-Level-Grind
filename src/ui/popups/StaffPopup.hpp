#include <Geode/Geode.hpp>
#include "../BasePopup.hpp"
#include "../../utils/globals.hpp"

using namespace geode::prelude;

namespace levelgrind {

class StaffPopup : public BasePopup {
public:
    static StaffPopup* create();

private:
    bool init() override;

    GrindPosition pos;
};

}