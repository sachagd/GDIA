// src/main.cpp
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace geode::prelude;

#ifdef _WIN32
    #include <Windows.h>
#endif

// -------------------- config (tweak anytime) --------------------
static constexpr int   GRID_W   = 48;
static constexpr int   GRID_H   = 24;

static constexpr float VIEW_BACK_X  = 220.f;
static constexpr float VIEW_FRONT_X = 380.f;
static constexpr float VIEW_HALF_Y  = 170.f;

static constexpr int   WRITE_EVERY_N_FRAMES = 3;
static constexpr char  FILLED_CHAR = '#';
static constexpr char  EMPTY_CHAR  = '.';

// -------------------- geometry helpers --------------------
static float getObjectRadius(GameObject const* obj) {
    return std::max(obj->m_scaleX, obj->m_scaleY) * obj->m_objectRadius;
}

static inline float dot(cocos2d::CCPoint a, cocos2d::CCPoint b) {
    return a.x * b.x + a.y * b.y;
}

static inline cocos2d::CCPoint sub(cocos2d::CCPoint a, cocos2d::CCPoint b) {
    return {a.x - b.x, a.y - b.y};
}

static bool rectIntersectsRect(cocos2d::CCRect const& a, cocos2d::CCRect const& b) {
    return !(a.getMaxX() <= b.getMinX() || a.getMinX() >= b.getMaxX() ||
             a.getMaxY() <= b.getMinY() || a.getMinY() >= b.getMaxY());
}

static bool circleIntersectsRect(cocos2d::CCPoint c, float r, cocos2d::CCRect const& rect) {
    float cx = std::clamp(c.x, rect.getMinX(), rect.getMaxX());
    float cy = std::clamp(c.y, rect.getMinY(), rect.getMaxY());
    float dx = c.x - cx;
    float dy = c.y - cy;
    return dx * dx + dy * dy <= r * r;
}

static void projectPoly(std::array<cocos2d::CCPoint, 4> const& poly, cocos2d::CCPoint axis, float& outMin, float& outMax) {
    float p0 = dot(poly[0], axis);
    outMin = outMax = p0;
    for (int i = 1; i < 4; ++i) {
        float p = dot(poly[i], axis);
        outMin = std::min(outMin, p);
        outMax = std::max(outMax, p);
    }
}

static bool overlap1D(float aMin, float aMax, float bMin, float bMax) {
    return !(aMax < bMin || bMax < aMin);
}

static bool obbIntersectsRect(std::array<cocos2d::CCPoint, 4> const& obb, cocos2d::CCRect const& rect) {
    // Build rect polygon (AABB)
    std::array<cocos2d::CCPoint, 4> aabb = {
        cocos2d::CCPoint{rect.getMinX(), rect.getMinY()},
        cocos2d::CCPoint{rect.getMinX(), rect.getMaxY()},
        cocos2d::CCPoint{rect.getMaxX(), rect.getMaxY()},
        cocos2d::CCPoint{rect.getMaxX(), rect.getMinY()},
    };

    // SAT axes:
    // - 2 axes from OBB edges
    // - 2 axes from AABB (x and y)
    std::array<cocos2d::CCPoint, 4> axes;

    // OBB edge normals
    for (int i = 0; i < 2; ++i) {
        auto e = sub(obb[(i + 1) & 3], obb[i]);
        axes[i] = cocos2d::CCPoint(-e.y, e.x);
    }
    axes[2] = cocos2d::CCPoint(1.f, 0.f);
    axes[3] = cocos2d::CCPoint(0.f, 1.f);
    for (auto axis : axes) {
        // No need to normalize for SAT overlap test
        float oMin, oMax, rMin, rMax;
        projectPoly(obb, axis, oMin, oMax);
        projectPoly(aabb, axis, rMin, rMax);
        if (!overlap1D(oMin, oMax, rMin, rMax)) return false;
    }
    return true;
}

// -------------------- Eclipse-like section iteration --------------------
template <class F>
static void forEachSectionObject(GJBaseGameLayer const* game, F&& callback) {
    if (game->m_sections.empty()) return;

    int n_cols = static_cast<int>(game->m_sections.size());
    int col0 = std::max(0, game->m_leftSectionIndex);
    int col1 = std::min(game->m_rightSectionIndex, n_cols - 1);

    for (int col = col0; col <= col1; ++col) {
        auto* section_col = game->m_sections[col];
        if (!section_col) continue;

        auto* section_counts_col = (col >= 0 && col < static_cast<int>(game->m_sectionSizes.size())) 
            ? game->m_sectionSizes[col] : nullptr;
        if (!section_counts_col) continue;

        int n_rows_in_col = static_cast<int>(section_col->size());
        int row0 = std::max(0, game->m_bottomSectionIndex);
        int row1 = std::min(game->m_topSectionIndex, n_rows_in_col - 1);

        for (int row = row0; row <= row1; ++row) {
            auto* section_cell = section_col->at(row);
            if (!section_cell) continue;

            if (row < 0 || row >= static_cast<int>(section_counts_col->size())) continue;
            int n_objs_in_cell = section_counts_col->at(row);

            int cell_vec_size = static_cast<int>(section_cell->size());
            int kMax = std::min(n_objs_in_cell, cell_vec_size);

            for (int k = 0; k < kMax; ++k) {
                auto* obj = section_cell->at(k);
                if (!obj) continue;
                callback(obj);
            }
        }
    }
}

// -------------------- grid --------------------
struct OccGrid {
    int W = 0, H = 0;
    float minX = 0, maxX = 0, minY = 0, maxY = 0;
    float cellW = 0, cellH = 0;
    std::vector<uint8_t> occ;

    void init(int w, int h, float mnX, float mxX, float mnY, float mxY) {
        W = w; H = h;
        minX = mnX; maxX = mxX;
        minY = mnY; maxY = mxY;
        cellW = (maxX - minX) / float(W);
        cellH = (maxY - minY) / float(H);
        occ.assign(size_t(W * H), 0);
    }

    inline int idx(int x, int y) const { return y * W + x; }

    inline int xToCell(float x) const {
        int cx = int((x - minX) / cellW);
        return std::clamp(cx, 0, W - 1);
    }
    inline int yToCell(float y) const {
        int cy = int((y - minY) / cellH);
        return std::clamp(cy, 0, H - 1);
    }

    inline cocos2d::CCRect cellRect(int cx, int cy) const {
        float x0 = minX + cx * cellW;
        float y0 = minY + cy * cellH;
        return cocos2d::CCRect(x0, y0, cellW, cellH);
    }

    template <class IntersectsCellFn>
    void rasterBBox(float bbMinX, float bbMaxX, float bbMinY, float bbMaxY, IntersectsCellFn&& intersectsCell) {
        if (bbMaxX < minX || bbMinX > maxX || bbMaxY < minY || bbMinY > maxY) return;

        int x0 = xToCell(bbMinX);
        int x1 = xToCell(bbMaxX);
        int y0 = yToCell(bbMinY);
        int y1 = yToCell(bbMaxY);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (intersectsCell(cellRect(x, y)))
                    occ[idx(x, y)] = 1;
            }
        }
    }
};

// -------------------- file output --------------------
static void writeTextAtomic(std::string const& text) {
    auto dir = Mod::get()->getSaveDir();
    std::filesystem::create_directories(dir);

    auto path = dir / "matrix.txt";
    auto tmp  = dir / "matrix.txt.tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), (std::streamsize)text.size());
        out.flush();
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(text.data(), (std::streamsize)text.size());
    }
}

static std::string gridToAscii(OccGrid const& g, float px, float py) {
    std::string s;
    s.reserve((size_t)g.W * (size_t)g.H + (size_t)g.H + 256);

    s += "W=" + std::to_string(g.W) + " H=" + std::to_string(g.H) + "\n";
    s += "player=(" + std::to_string(px) + "," + std::to_string(py) + ")\n";
    s += "x=[" + std::to_string(g.minX) + "," + std::to_string(g.maxX) + "] ";
    s += "y=[" + std::to_string(g.minY) + "," + std::to_string(g.maxY) + "]\n\n";

    for (int y = g.H - 1; y >= 0; --y) {
        for (int x = 0; x < g.W; ++x) {
            s.push_back(g.occ[g.idx(x, y)] ? FILLED_CHAR : EMPTY_CHAR);
        }
        s.push_back('\n');
    }
    return s;
}

// -------------------- PlayLayer hook --------------------
class $modify(AIGridToFile, PlayLayer) {
    struct Fields {
        bool prevK = false;
        bool enabled = false;
        int frameCounter = 0;
        OccGrid grid;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        #ifndef _WIN32
            return;
        #else
        // toggle with K
        bool kDown = (GetAsyncKeyState('K') & 0x8000) != 0;
        bool kPressed = kDown && !m_fields->prevK;
        m_fields->prevK = kDown;

        if (kPressed) {
            m_fields->enabled = !m_fields->enabled;
            m_fields->frameCounter = 0;

            std::string status = std::string("enabled=") + (m_fields->enabled ? "true\n" : "false\n");
            status += "saveDir=" + Mod::get()->getSaveDir().string() + "\n";
            writeTextAtomic(status);
        }

        if (!m_fields->enabled) return;

        auto* player = m_player1;
        if (!player) return;

        float px = player->getPositionX();
        float py = player->getPositionY();

        m_fields->grid.init(
            GRID_W, GRID_H,
            px - VIEW_BACK_X,  px + VIEW_FRONT_X,
            py - VIEW_HALF_Y,  py + VIEW_HALF_Y
        );

        forEachSectionObject(this, [&](GameObject* obj) {
            if (!obj->m_isActivated || obj->m_isGroupDisabled) return;
            if (obj->m_objectType == GameObjectType::Decoration) return;
            if (obj == m_player1 || obj == m_player2) return;

            // 1) Circle: exact circle-rect intersection
            float r = getObjectRadius(obj);
            if (r > 0.f) {
                cocos2d::CCPoint c = obj->getPosition();
                m_fields->grid.rasterBBox(
                    c.x - r, c.x + r, c.y - r, c.y + r,
                    [&](cocos2d::CCRect const& cell) {
                        return circleIntersectsRect(c, r, cell);
                    }
                );
                return;
            }

            // 2) Oriented box: SAT OBB vs cell AABB
            if (auto* ob = obj->m_orientedBox) {
                std::array<cocos2d::CCPoint, 4> q = {
                    ob->m_corners[0], ob->m_corners[1], ob->m_corners[2], ob->m_corners[3]
                };

                float mnX = q[0].x, mxX = q[0].x, mnY = q[0].y, mxY = q[0].y;
                for (int i = 1; i < 4; ++i) {
                    mnX = std::min(mnX, q[i].x); mxX = std::max(mxX, q[i].x);
                    mnY = std::min(mnY, q[i].y); mxY = std::max(mxY, q[i].y);
                }

                m_fields->grid.rasterBBox(
                    mnX, mxX, mnY, mxY,
                    [&](cocos2d::CCRect const& cell) {
                        return obbIntersectsRect(q, cell);
                    }
                );
                return;
            }

            // 3) Axis-aligned rect: exact rect-rect intersection (fixes thin bars)
            bool wasDirty = obj->m_isObjectRectDirty;
            bool wasBoxOffset = obj->m_boxOffsetCalculated;
            cocos2d::CCRect rect = obj->getObjectRect();
            obj->m_isObjectRectDirty = wasDirty;
            obj->m_boxOffsetCalculated = wasBoxOffset;

            m_fields->grid.rasterBBox(
                rect.getMinX(), rect.getMaxX(), rect.getMinY(), rect.getMaxY(),
                [&](cocos2d::CCRect const& cell) {
                    return rectIntersectsRect(rect, cell);
                }
            );
        });

        if ((m_fields->frameCounter++ % WRITE_EVERY_N_FRAMES) == 0) {
            writeTextAtomic(gridToAscii(m_fields->grid, px, py));
        }
#endif
    }
};
