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

// One convex, axis-aligned quad. The mesh is quads on purpose: a hall traced
// in this editor turns into a handful of big rectangles instead of a cell soup.
struct LmQuad
{
    LmVec3 v[4];
    LmVec3 n;
    int    mat;
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

    size_t TriangleCount() const { return quads.size() * 2; }
};

// Drawing directions for the plan view's straight-line mode. 0 degrees is
// east and angles step counter-clockwise on screen, so the y component is
// negative for "north" (plan-view y grows downward).
struct LmDir
{
    int dx;
    int dy;
};

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

    void BuildMesh(LmMesh& out, bool includeCeiling, LmFacing facing) const;

    bool Save(const wchar_t* path) const;
    bool Load(const wchar_t* path);
    bool ExportObj(const wchar_t* path, bool includeCeiling, LmFacing facing) const;
    bool ExportFbxAscii(const wchar_t* path, bool includeCeiling, LmFacing facing) const;

private:
    void PushUndo(const std::vector<uint8_t>& state);

    int                  m_w;
    int                  m_h;
    std::vector<uint8_t> m_cells;
    float                m_cellSize;
    float                m_wallHeight;

    std::vector<std::vector<uint8_t> > m_undo;
    std::vector<std::vector<uint8_t> > m_redo;
    std::vector<uint8_t>               m_strokeSnapshot;
    bool                               m_strokeActive;
};
