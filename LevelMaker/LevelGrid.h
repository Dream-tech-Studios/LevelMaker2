// LevelGrid.h : the level document -- a 2D grid of floor cells, plus
// low-poly mesh generation (greedy-merged quads) and file I/O.
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LmVec3
{
    float x, y, z;
};

// Which way the generated surfaces look. A hall is interior space, so the
// walls face the floor by default; Outward flips everything, Both emits each
// quad twice so the mesh reads as solid from either side.
enum LmFacing
{
    LM_FACE_INWARD = 0,
    LM_FACE_OUTWARD,
    LM_FACE_BOTH
};

enum LmMaterial
{
    LM_MAT_FLOOR = 0,
    LM_MAT_WALL,
    LM_MAT_CEILING,
    LM_MAT_COUNT
};

// Diagonal smoothing: which corner of a cell is chamfered away. The chamfer
// is a full-cell diagonal, so the cell keeps exactly half of itself, and a
// run of them along a 45 degree staircase joins up into one straight edge.
// Compass directions are plan-view ones: north is -y, west is -x.
enum LmCut
{
    LM_CUT_NONE = 0,
    LM_CUT_NW,      // keeps the south-east half
    LM_CUT_NE,      // keeps the south-west half
    LM_CUT_SE,      // keeps the north-west half
    LM_CUT_SW       // keeps the north-east half
};

// One convex, planar face. The mesh is quads on purpose: a hall traced in
// this editor turns into a handful of big rectangles instead of a cell soup.
// Diagonal smoothing also emits triangles -- the half cells where a 45 degree
// run cuts a corner off -- so nv says how many of v[] are real. A triangle
// still fills v[3] (with a copy of v[2]) so naive four-vertex code works.
struct LmQuad
{
    LmVec3 v[4];
    LmVec3 n;
    int    mat;
    int    nv;      // 3 or 4

    LmQuad() : mat(LM_MAT_FLOOR), nv(4) {}
};

struct LmMesh
{
    std::vector<LmQuad> quads;
    int                 quadsByMat[LM_MAT_COUNT];

    LmMesh() { Clear(); }

    void Clear()
    {
        quads.clear();
        for (int i = 0; i < LM_MAT_COUNT; ++i)
            quadsByMat[i] = 0;
    }

    size_t TriangleCount() const
    {
        size_t t = 0;
        for (size_t i = 0; i < quads.size(); ++i)
            t += (quads[i].nv == 3) ? 1 : 2;
        return t;
    }
};

// Drawing directions for the plan view's straight-line mode. 0 degrees is
// east and angles step counter-clockwise on screen, so the y component is
// negative for "north" (plan-view y grows downward).
struct LmDir
{
    int dx;
    int dy;
};

// --- diagonal chamfers ---------------------------------------------------
// Shared by the mesh builder and the plan view, so the drawing and the
// geometry agree on what a chamfered cell looks like.

// Which of the cell's four edges the chamfer has eaten.
bool   LmCutEatsN(int cut);
bool   LmCutEatsE(int cut);
bool   LmCutEatsS(int cut);
bool   LmCutEatsW(int cut);
// Step from one cell of a chamfer run to the next one along the same line.
void   LmCutStep(int cut, int& sx, int& sy);
// The chamfer edge of cell (x, y) in grid units, wound so a wall built from
// it faces the floor that is left.
void   LmCutEdge(int cut, int x, int y, float& ax, float& ay, float& bx, float& by);
// That wall's inward normal, in world space.
LmVec3 LmCutNormal(int cut);
// The three corners the cell keeps, wound for an upward-facing floor.
void   LmCutTriangle(int cut, int x, int y, float px[3], float py[3]);

LmDir LmDirForAngle(int degrees);
// nearest of the eight directions to a drag, or fallbackDegrees if it is zero
int   LmSnapAngle(int dx, int dy, int fallbackDegrees);
// how many whole steps along dir a drag of (dx, dy) reaches; never negative
int   LmStepsAlong(int dx, int dy, LmDir dir);

class LevelGrid
{
public:
    static const int kDefaultSize = 192;

    LevelGrid();

    void Reset(int w, int h);
    void Clear();                       // wipes cells, keeps size + undo history

    int  Width() const { return m_w; }
    int  Height() const { return m_h; }
    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < m_w && y < m_h; }
    bool IsFloor(int x, int y) const
    {
        return InBounds(x, y) && m_cells[(size_t)y * m_w + x] != 0;
    }
    int  FloorCount() const;
    bool FloorBounds(int& x0, int& y0, int& x1, int& y1) const;  // inclusive; false when empty

    // --- painting -------------------------------------------------------
    // brush is the stamp width in cells. Returns true when cells changed.
    bool PaintCell(int cx, int cy, int brush, bool floorValue);
    bool PaintLine(int x0, int y0, int x1, int y1, int brush, bool floorValue);

    // --- undo / redo ----------------------------------------------------
    void BeginStroke();                 // snapshot before a drag
    void RevertStroke();                // back to the snapshot, stroke still open
    bool StrokeActive() const { return m_strokeActive; }
    void EndStroke();                   // commit the snapshot if anything moved
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !m_undo.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }

    // --- level scale ----------------------------------------------------
    float CellSize() const { return m_cellSize; }
    float WallHeight() const { return m_wallHeight; }
    void  SetCellSize(float s) { m_cellSize = s; }
    void  SetWallHeight(float h) { m_wallHeight = h; }

    // --- diagonal smoothing ---------------------------------------------
    // With this on, 45 degree staircases are chamfered into straight edges
    // instead of coming out as a flight of steps.
    bool SmoothDiagonals() const { return m_smoothDiagonals; }
    void SetSmoothDiagonals(bool on) { m_smoothDiagonals = on; }

    // One LmCut per cell, row major, w*h entries -- or empty when smoothing
    // is off. The views use it to draw what the mesh will be.
    void BuildCutMap(std::vector<uint8_t>& cuts) const;

    void BuildMesh(LmMesh& out, bool includeCeiling, LmFacing facing) const;

    bool Save(const wchar_t* path) const;
    bool Load(const wchar_t* path);
    bool ExportObj(const wchar_t* path, bool includeCeiling, LmFacing facing) const;
    bool ExportFbxAscii(const wchar_t* path, bool includeCeiling, LmFacing facing) const;

private:
    void PushUndo(const std::vector<uint8_t>& state);
    int  CutCandidate(int x, int y) const;      // LM_CUT_*, ignoring runs
    int  WallSideH(const uint8_t* cut, int x, int y) const;
    int  WallSideV(const uint8_t* cut, int x, int y) const;

    int                  m_w;
    int                  m_h;
    std::vector<uint8_t> m_cells;
    float                m_cellSize;
    float                m_wallHeight;
    bool                 m_smoothDiagonals;

    std::vector<std::vector<uint8_t> > m_undo;
    std::vector<std::vector<uint8_t> > m_redo;
    std::vector<uint8_t>               m_strokeSnapshot;
    bool                               m_strokeActive;
};
