#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

#ifdef _WIN32
    #include <Windows.h>
#endif

class $modify(MyPlayLayer, PlayLayer) {
    struct Fields {
        bool prevForcedJumpHeld = false;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

#ifndef _WIN32
        return;
#else
        // Hold J to force Jump held
        bool wantHeld = (GetAsyncKeyState('J') & 0x8000) != 0;

        auto* p = this->m_player1;
        if (!p) return;

        if (wantHeld != m_fields->prevForcedJumpHeld) {
            if (wantHeld) {
                p->pushButton(PlayerButton::Jump);
            } else {
                p->releaseButton(PlayerButton::Jump);
            }
            m_fields->prevForcedJumpHeld = wantHeld;
        }
#endif
    }
};
