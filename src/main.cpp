// src/main.cpp
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>


#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJGroundLayer.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace geode::prelude;

#ifdef _WIN32
    #include <Windows.h>
#endif

static constexpr int   GRID_W   = 48;
static constexpr int   GRID_H   = 24;

static constexpr float VIEW_BACK_X  = 220.f;
static constexpr float VIEW_FRONT_X = 380.f;
static constexpr float VIEW_HALF_Y  = 170.f;

static constexpr int   WRITE_EVERY_N_FRAMES = 3;
static constexpr char  FILLED_CHAR = '#';
static constexpr char  EMPTY_CHAR  = '.';



static float getObjectRadius(GameObject const* obj) {
    return std::max(obj->m_scaleX, obj->m_scaleY) * obj->m_objectRadius;
}

static inline float dot(cocos2d::CCPoint a, cocos2d::CCPoint b) {
    return a.x * b.x + a.y * b.y;
}

static inline cocos2d::CCPoint sub(cocos2d::CCPoint a, cocos2d::CCPoint b) {
    return {a.x - b.x, a.y - b.y};
}

static bool circleIntersectsRect(cocos2d::CCPoint c, float r, cocos2d::CCRect const& rect) {
    float cx = std::clamp(c.x, rect.getMinX(), rect.getMaxX());
    float cy = std::clamp(c.y, rect.getMinY(), rect.getMaxY());
    float dx = c.x - cx;
    float dy = c.y - cy;
    return dx * dx + dy * dy < r * r;
}

static void projectPoints(cocos2d::CCPoint const* pts, int n, cocos2d::CCPoint axis, float& outMin, float& outMax) {
    float p0 = dot(pts[0], axis);
    outMin = outMax = p0;
    for (int i = 1; i < n; ++i) {
        float p = dot(pts[i], axis);
        outMin = std::min(outMin, p);
        outMax = std::max(outMax, p);
    }
}

static bool overlap1D(float aMin, float aMax, float bMin, float bMax) {
    return (aMax > bMin) && (bMax > aMin);
}

static bool convexPolyIntersectsRect(cocos2d::CCPoint const* poly, int n, cocos2d::CCRect const& rect) {
    std::array<cocos2d::CCPoint, 4> aabb = {
        cocos2d::CCPoint{rect.getMinX(), rect.getMinY()},
        cocos2d::CCPoint{rect.getMinX(), rect.getMaxY()},
        cocos2d::CCPoint{rect.getMaxX(), rect.getMaxY()},
        cocos2d::CCPoint{rect.getMaxX(), rect.getMinY()},
    };

    for (int i = 0; i < n; ++i) {
        auto a = poly[i];
        auto b = poly[(i + 1) % n];
        auto e = sub(b, a);
        cocos2d::CCPoint axis(-e.y, e.x);

        float pMin, pMax, rMin, rMax;
        projectPoints(poly, n, axis, pMin, pMax);
        projectPoints(aabb.data(), 4, axis, rMin, rMax);
        if (!overlap1D(pMin, pMax, rMin, rMax)) return false;
    }

    {
        cocos2d::CCPoint axis(1.f, 0.f);
        float pMin, pMax, rMin, rMax;
        projectPoints(poly, n, axis, pMin, pMax);
        projectPoints(aabb.data(), 4, axis, rMin, rMax);
        if (!overlap1D(pMin, pMax, rMin, rMax)) return false;
    }
    {
        cocos2d::CCPoint axis(0.f, 1.f);
        float pMin, pMax, rMin, rMax;
        projectPoints(poly, n, axis, pMin, pMax);
        projectPoints(aabb.data(), 4, axis, rMin, rMax);
        if (!overlap1D(pMin, pMax, rMin, rMax)) return false;
    }

    return true;
}

static cocos2d::CCRect getStableObjectRect(GameObject* obj) {
    bool wasDirty = obj->m_isObjectRectDirty;
    bool wasBoxOffset = obj->m_boxOffsetCalculated;
    cocos2d::CCRect rect = obj->getObjectRect();
    obj->m_isObjectRectDirty = wasDirty;
    obj->m_boxOffsetCalculated = wasBoxOffset;
    return rect;
}

struct HitPoly {
    std::array<cocos2d::CCPoint, 4> pts{};
    int n = 0;
    float minX = 0.f, maxX = 0.f, minY = 0.f, maxY = 0.f;
};

static HitPoly makeAabbPoly(cocos2d::CCRect const& r) {
    HitPoly hp;
    hp.pts = {
        cocos2d::CCPoint{r.getMinX(), r.getMinY()},
        cocos2d::CCPoint{r.getMinX(), r.getMaxY()},
        cocos2d::CCPoint{r.getMaxX(), r.getMaxY()},
        cocos2d::CCPoint{r.getMaxX(), r.getMinY()},
    };
    hp.n = 4;
    hp.minX = r.getMinX(); hp.maxX = r.getMaxX();
    hp.minY = r.getMinY(); hp.maxY = r.getMaxY();
    return hp;
}

static void computeBBox(HitPoly& hp) {
    hp.minX = hp.maxX = hp.pts[0].x;
    hp.minY = hp.maxY = hp.pts[0].y;
    for (int i = 1; i < hp.n; ++i) {
        hp.minX = std::min(hp.minX, hp.pts[i].x);
        hp.maxX = std::max(hp.maxX, hp.pts[i].x);
        hp.minY = std::min(hp.minY, hp.pts[i].y);
        hp.maxY = std::max(hp.maxY, hp.pts[i].y);
    }
}

static void getConvexHitPoly(GameObject* obj, HitPoly& out) {
    if (obj->m_objectType == GameObjectType::Slope) {
        auto r = getStableObjectRect(obj);

        std::array<cocos2d::CCPoint, 3> tri = {
            cocos2d::CCPoint{r.getMinX(), r.getMinY()},
            cocos2d::CCPoint{r.getMinX(), r.getMaxY()},
            cocos2d::CCPoint{r.getMaxX(), r.getMinY()},
        };

        cocos2d::CCPoint topRight{r.getMaxX(), r.getMaxY()};
        switch (obj->m_slopeDirection) {
            case 0: case 7: tri[1] = topRight; break;
            case 1: case 5: tri[0] = topRight; break;
            case 3: case 6: tri[2] = topRight; break;
            default: break;
        }

        out.pts[0] = tri[0];
        out.pts[1] = tri[1];
        out.pts[2] = tri[2];
        out.n = 3;
        computeBBox(out);
        return;
    }

    if (auto* ob = obj->m_orientedBox) {
        out.n = 4;
        std::copy_n(std::begin(ob->m_corners), 4, out.pts.begin());
        computeBBox(out);
        return;
    }

    out = makeAabbPoly(getStableObjectRect(obj));
}

template <class F>
static void forEachSectionObject(GJBaseGameLayer const* game, F&& callback) {
    int count = game->m_sections.empty() ? -1 : static_cast<int>(game->m_sections.size());

    for (int i = game->m_leftSectionIndex; i <= game->m_rightSectionIndex && i < count; ++i) {
        auto leftSection = game->m_sections[i];
        if (!leftSection) continue;

        auto leftSectionSize = static_cast<int>(leftSection->size());
        for (int j = game->m_bottomSectionIndex; j <= game->m_topSectionIndex && j < leftSectionSize; ++j) {
            auto section = leftSection->at(j);
            if (!section) continue;

            auto sectionSize = game->m_sectionSizes[i]->at(j);
            for (int k = 0; k < sectionSize; ++k) {
                auto obj = section->at(k);
                if (!obj) continue;

                callback(obj);
            }
        }
    }
}

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

static void collectGroundLayers(cocos2d::CCNode* node, std::vector<GJGroundLayer*>& out, int depth) {
    if (!node || depth < 0) return;

    if (auto* g = typeinfo_cast<GJGroundLayer*>(node)) {
        out.push_back(g);
    }

    auto* children = node->getChildren();
    if (!children) return;

    for (auto* ch : CCArrayExt<cocos2d::CCNode*>(children)) {
        collectGroundLayers(ch, out, depth - 1);
    }
}

static float nodeYInSpace(cocos2d::CCNode* node, cocos2d::CCNode* space) {
    auto w = node->convertToWorldSpace({0.f, 0.f});
    auto p = space->convertToNodeSpace(w);
    return p.y;
}

static cocos2d::CCRect nodeBBoxToSpace(cocos2d::CCNode* node, cocos2d::CCNode* space) {
    auto* parent = node->getParent();
    if (!parent) return cocos2d::CCRect(0, 0, 0, 0);

    auto bb = node->boundingBox();

    cocos2d::CCPoint p0 = parent->convertToWorldSpace({bb.getMinX(), bb.getMinY()});
    cocos2d::CCPoint p1 = parent->convertToWorldSpace({bb.getMaxX(), bb.getMaxY()});

    cocos2d::CCPoint s0 = space->convertToNodeSpace(p0);
    cocos2d::CCPoint s1 = space->convertToNodeSpace(p1);

    float minX = std::min(s0.x, s1.x);
    float maxX = std::max(s0.x, s1.x);
    float minY = std::min(s0.y, s1.y);
    float maxY = std::max(s0.y, s1.y);

    return cocos2d::CCRect(minX, minY, maxX - minX, maxY - minY);
}

static void fillBelowY(OccGrid& g, float y) {
    int yMax = g.yToCell(y);
    for (int cy = 0; cy <= yMax; ++cy) {
        for (int cx = 0; cx < g.W; ++cx) {
            g.occ[g.idx(cx, cy)] = 1;
        }
    }
}

static void fillAboveY(OccGrid& g, float y) {
    int yMin = g.yToCell(y);
    for (int cy = yMin; cy < g.H; ++cy) {
        for (int cx = 0; cx < g.W; ++cx) {
            g.occ[g.idx(cx, cy)] = 1;
        }
    }
}


class $modify(AIGridToFile, PlayLayer) {
    struct Fields {
        bool prevK = false;
        bool enabled = false;
        int frameCounter = 0;
        GJGroundLayer* bottomGround = nullptr;
        GJGroundLayer* topGround = nullptr;
        bool groundsCached = false;
        OccGrid grid;
    };

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

#ifndef _WIN32
        return;
#else
        bool kDown = (GetAsyncKeyState('K') & 0x8000) != 0;
        bool kPressed = kDown && !m_fields->prevK;
        m_fields->prevK = kDown;

        if (kPressed) {
            m_fields->enabled = !m_fields->enabled;
            m_fields->frameCounter = 0;
            m_fields->groundsCached = false;
            m_fields->bottomGround = nullptr;
            m_fields->topGround = nullptr;


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
            if (obj->m_objectType == GameObjectType::CollisionObject) return;
            if (obj == m_player1 || obj == m_player2) return;

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

            HitPoly hp;
            getConvexHitPoly(obj, hp);

            m_fields->grid.rasterBBox(
                hp.minX, hp.maxX, hp.minY, hp.maxY,
                [&](cocos2d::CCRect const& cell) {
                    return convexPolyIntersectsRect(hp.pts.data(), hp.n, cell);
                }
            );
        });

        auto* space = player->getParent();
        if (space && !m_fields->groundsCached) {
            std::vector<GJGroundLayer*> grounds;
            grounds.reserve(8);
            collectGroundLayers(this, grounds, 6);

            grounds.erase(std::unique(grounds.begin(), grounds.end()), grounds.end());

            if (!grounds.empty()) {
                std::sort(grounds.begin(), grounds.end(), [&](auto* a, auto* b) {
                    return nodeYInSpace(a, space) < nodeYInSpace(b, space);
                });

                m_fields->bottomGround = grounds.front();
                m_fields->topGround = grounds.back();
                m_fields->groundsCached = true;
            }
        }

        if (space && m_fields->groundsCached && m_fields->bottomGround) {
            auto br = nodeBBoxToSpace(m_fields->bottomGround, space);
            fillBelowY(m_fields->grid, br.getMaxY());
        }
        if (space && m_fields->groundsCached && m_fields->topGround && m_fields->topGround != m_fields->bottomGround) {
            auto tr = nodeBBoxToSpace(m_fields->topGround, space);
            fillAboveY(m_fields->grid, tr.getMinY());
        }

        if ((m_fields->frameCounter++ % WRITE_EVERY_N_FRAMES) == 0) {
            writeTextAtomic(gridToAscii(m_fields->grid, px, py));
        }
#endif
    }
};
