// =============================================================================
//  world.h  -  The map model: grid, heightfield, doors, light map, decals
//
//  World holds no Qt state and draws nothing, so the same class can be used by
//  the renderer, the game rules, bots and (later) a network host. The map size
//  is set when a layout is loaded; nothing assumes 40x40 any more.
// =============================================================================
#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "game.h"
#include "assets.h"

class World {
public:
    // -- Grid ----------------------------------------------------------------------
    int         m_mapW = 0, m_mapH = 0;
    Grid<int>     m_map;          // CellType
    Grid<float>   m_plat;         // ledge / pit offsets
    Grid<uint8_t> m_acid;
    Grid<float>   m_cellBase;     // ground level a wall stands on
    Grid<float>   m_doorT;        // 0 closed .. 1 fully sunk into the floor
    Grid<uint8_t> m_doorMoving;
    std::vector<float> m_vh;      // rolling-terrain heights at cell corners
    std::vector<float> m_vhBase;  // heights as loaded (craters can only dent so far below this)
    int         m_theme = 0;      // sun direction / strength used by the light map

    // Copy a parsed layout in and build the rolling terrain (seeded fBm)
    void loadLayout(const ParsedMap &pm, float terrainAmp, int seed, int theme);

    // -- Terrain queries -----------------------------------------------------------
    bool  inside(int cx, int cy) const { return cx >= 0 && cy >= 0 && cx < m_mapW && cy < m_mapH; }
    float smoothH(float x, float y) const;        // bilinear rolling terrain
    float floorH(float x, float y) const;         // terrain + ledge/pit of that cell
    float cellTop(int cx, int cy) const;          // top of a solid cell
    bool  solidCell(int cx, int cy) const;
    float groundAt(float x, float y) const;       // standing height (wall tops for solid cells)

    // -- Movement & visibility -----------------------------------------------------
    bool posValid(float x, float y, float r = 0.28f) const;
    // (ox,oy) = current position: a move that doesn't worsen an existing overlap with
    // a ledge is allowed, so an actor can never get wedged against a corner
    bool canStep(float x, float y, float fromZ, float maxUp, float r = 0.28f, float ox = -1.f, float oy = -1.f) const;
    bool hasLOS(float x0, float y0, float z0, float x1, float y1, float z1) const;
    // First point where the segment enters terrain or a wall; false if it is clear
    bool traceSegment(float x0, float y0, float z0, float x1, float y1, float z1,
                      float &hx, float &hy, float &hz, float step = 0.05f) const;

    // -- Reachability (cells reachable by normal play, not through secrets) ----------
    std::vector<uint8_t> m_reach;
    void computeReachable(float sx, float sy);
    bool reachable(int cx, int cy) const { return inside(cx, cy) && !m_reach.empty() && m_reach[size_t(cy) * m_mapW + cx]; }

    // -- Light map (12 texels per tile): sun visibility + final ground light ---------
    static const int LM = 12;
    std::vector<uint8_t> m_sunVis, m_light;
    void  buildLightMap(int x0 = 0, int y0 = 0, int x1 = -1, int y1 = -1);   // -1 = to the map edge
    float sunVisAt(float x, float y) const;

    // -- Decals ----------------------------------------------------------------------
    // Bullet holes: one small decal texture per wall face that has been hit
    static const int WD_U = 96, WD_V = 256;                  // 96 texels per world unit (~1 cm)
    std::unordered_map<int, std::vector<uint8_t>> m_wallDecals;
    void stampWallDecal(int cx, int cy, int face, float u, float v, bool pellet);
    // Floor decals (32 texels per tile): blood + scorch marks
    static const int DM = 32;
    std::vector<uint8_t> m_blood, m_scorch;
    void addDecal(std::vector<uint8_t> &d, float x, float y, float radius, int amount);
    void clearDecals();
};
