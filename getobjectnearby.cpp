#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <cmath>

using namespace geode::prelude;

#ifdef _WIN32
    #include <Windows.h>
#endif

static float getObjectRadius(GameObject const* obj) {
    return std::max(obj->m_scaleX, obj->m_scaleY) * obj->m_objectRadius;
}

template <class F>
static void forEachSectionObject(GJBaseGameLayer const* game, F&& callback) {
    if (game->m_sections.empty()) return;

    int sectionCols = static_cast<int>(game->m_sections.size());

    int i0 = std::max(0, game->m_leftSectionIndex);
    int i1 = std::min(game->m_rightSectionIndex, sectionCols - 1);

    for (int i = i0; i <= i1; ++i) {
        auto* col = game->m_sections[i];
        if (!col) continue;

        int colSize = static_cast<int>(col->size());

        int j0 = std::max(0, game->m_bottomSectionIndex);
        int j1 = std::min(game->m_topSectionIndex, colSize - 1);

        // sectionSizes safety
        if (i < 0 || i >= static_cast<int>(game->m_sectionSizes.size())) continue;
        auto* colSizes = game->m_sectionSizes[i];
        if (!colSizes) continue;

        for (int j = j0; j <= j1; ++j) {
            auto* cell = col->at(j);
            if (!cell) continue;

            if (j < 0 || j >= static_cast<int>(colSizes->size())) continue;
            int cellCount = colSizes->at(j);

            int actualSize = static_cast<int>(cell->size());
            int kMax = std::min(cellCount, actualSize);

            for (int k = 0; k < kMax; ++k) {
                auto* obj = cell->at(k);
                if (!obj) continue;
                callback(obj);
            }
        }
    }
}

class $modify(AIPlayLayerDump, PlayLayer) {
    struct Fields {
        bool prevK = false;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

#ifndef _WIN32
        return;
#else
        bool kDown = (GetAsyncKeyState('K') & 0x8000) != 0;
        bool kPressed = kDown && !m_fields->prevK;
        m_fields->prevK = kDown;
        if (!kPressed) return;

        auto* p = this->m_player1;
        if (!p) return;

        float px = p->getPositionX();
        float py = p->getPositionY();

        // Window around player (tweak anytime)
        float backX = 250.f;
        float frontX = 900.f;
        float halfY = 500.f;

        int printed = 0;
        int visited = 0;

        log::info("---- HITBOX DUMP (player x={}, y={}) ----", px, py);

        forEachSectionObject(this, [&](GameObject* obj) {
            ++visited;

            // match Eclipse basic “don’t draw junk” filter
            if (!obj->m_isActivated || obj->m_isGroupDisabled) return;
            if (obj->m_objectType == GameObjectType::Decoration) return;
            if (obj == m_player1 || obj == m_player2) return;

            float ox = obj->getPositionX();
            float oy = obj->getPositionY();
            float dx = ox - px;
            float dy = oy - py;

            if (dx < -backX || dx > frontX) return;
            if (std::abs(dy) > halfY) return;

            int oid = obj->m_objectID;
            int otype = static_cast<int>(obj->m_objectType);

            // 1) Circle hitbox (hazards etc.)
            float r = getObjectRadius(obj);
            if (r > 0.f) {
                log::info("obj id={} type={} dx={:.1f} dy={:.1f} HIT=CIRCLE cx={:.1f} cy={:.1f} r={:.2f}",
                          oid, otype, dx, dy, ox, oy, r);
                if (++printed >= 120) return;
                return;
            }

            // 2) Oriented box if available (rotated)
            if (auto* ob = obj->m_orientedBox) {
                auto const& c = ob->m_corners;
                log::info(
                    "obj id={} type={} dx={:.1f} dy={:.1f} HIT=OBB "
                    "p0=({:.1f},{:.1f}) p1=({:.1f},{:.1f}) p2=({:.1f},{:.1f}) p3=({:.1f},{:.1f})",
                    oid, otype, dx, dy,
                    c[0].x, c[0].y, c[1].x, c[1].y, c[2].x, c[2].y, c[3].x, c[3].y
                );
                if (++printed >= 120) return;
                return;
            }

            // 3) Axis-aligned object rect (preserve dirty flags like Eclipse)
            bool wasDirty = obj->m_isObjectRectDirty;
            bool wasBoxOffset = obj->m_boxOffsetCalculated;
            cocos2d::CCRect rect = obj->getObjectRect();
            obj->m_isObjectRectDirty = wasDirty;
            obj->m_boxOffsetCalculated = wasBoxOffset;

            log::info("obj id={} type={} dx={:.1f} dy={:.1f} HIT=AABB x={:.1f} y={:.1f} w={:.1f} h={:.1f}",
                      oid, otype, dx, dy,
                      rect.origin.x, rect.origin.y, rect.size.width, rect.size.height);

            if (++printed >= 120) return;
        });

        // also dump player hitboxes like Eclipse (outer + inner)
        {
            cocos2d::CCRect r1 = m_player1->getObjectRect();
            cocos2d::CCRect r2 = m_player1->getObjectRect(0.3f, 0.3f);
            log::info("PLAYER HIT=AABB outer x={:.1f} y={:.1f} w={:.1f} h={:.1f}",
                      r1.origin.x, r1.origin.y, r1.size.width, r1.size.height);
            log::info("PLAYER HIT=AABB inner x={:.1f} y={:.1f} w={:.1f} h={:.1f}",
                      r2.origin.x, r2.origin.y, r2.size.width, r2.size.height);
        }

        log::info("---- visited {} | printed {} ----", visited, printed);
#endif
    }
};
