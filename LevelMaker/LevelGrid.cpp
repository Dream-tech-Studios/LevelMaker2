// LevelGrid.cpp : grid editing, greedy quad meshing, save/load, OBJ export.
//
#include "framework.h"
#include "LevelGrid.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <unordered_map>

namespace {

const size_t kMaxUndo = 64;

const char kMagic[4] = { 'L', 'V', 'L', 'M' };
// v2 appends one byte after the cells: the diagonal smoothing flag. v1 files
// still load, and smoothing comes up on, the way it does for a new level.
const uint32_t kVersion = 2;

} // namespace

// --------------------------------------------------------- draw directions

namespace {

const LmDir kDirs[8] = {
    { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 },
    { -1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }
};

int AngleIndex(int degrees)
{
    int i = (degrees / 45) % 8;
    if (i < 0)
        i += 8;
    return i;
}

} // namespace

LmDir LmDirForAngle(int degrees)
{
    return kDirs[AngleIndex(degrees)];
}

int LmSnapAngle(int dx, int dy, int fallbackDegrees)
{
    if (dx == 0 && dy == 0)
        return AngleIndex(fallbackDegrees) * 45;
    float deg = atan2f(-(float)dy, (float)dx) * 180.0f / 3.14159265f;
    if (deg < 0.0f)
        deg += 360.0f;
    return (((int)floorf(deg / 45.0f + 0.5f)) % 8) * 45;
}

int LmStepsAlong(int dx, int dy, LmDir dir)
{
    const int lenSq = dir.dx * dir.dx + dir.dy * dir.dy;
    if (lenSq == 0)
        return 0;
    const int steps = (int)floorf((float)(dx * dir.dx + dy * dir.dy) / (float)lenSq + 0.5f);
    return (steps < 0) ? 0 : steps;
}

LevelGrid::LevelGrid()
    : m_w(0)
    , m_h(0)
    , m_cellSize(1.0f)
    , m_wallHeight(3.0f)
    , m_smoothDiagonals(true)
    , m_strokeActive(false)
{
    Reset(kDefaultSize, kDefaultSize);
}

void LevelGrid::Reset(int w, int h)
{
    if (w < 8) w = 8;
    if (h < 8) h = 8;
    if (w > 1024) w = 1024;
    if (h > 1024) h = 1024;
    m_w = w;
    m_h = h;
    m_cells.assign((size_t)w * h, 0);
    m_undo.clear();
    m_redo.clear();
    m_strokeSnapshot.clear();
    m_strokeActive = false;
}

void LevelGrid::Clear()
{
    std::vector<uint8_t> blank((size_t)m_w * m_h, 0);
    if (blank != m_cells)
    {
        PushUndo(m_cells);
        m_cells.swap(blank);
    }
}

int LevelGrid::FloorCount() const
{
    int n = 0;
    for (size_t i = 0; i < m_cells.size(); ++i)
        if (m_cells[i])
            ++n;
    return n;
}

bool LevelGrid::FloorBounds(int& x0, int& y0, int& x1, int& y1) const
{
    bool any = false;
    x0 = m_w; y0 = m_h; x1 = -1; y1 = -1;
    for (int y = 0; y < m_h; ++y)
    {
        for (int x = 0; x < m_w; ++x)
        {
            if (!m_cells[(size_t)y * m_w + x])
                continue;
            any = true;
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    }
    return any;
}

// ---------------------------------------------------------------- painting

bool LevelGrid::PaintCell(int cx, int cy, int brush, bool floorValue)
{
    if (brush < 1) brush = 1;
    const int lo = -(brush - 1) / 2;
    const int hi = brush / 2;
    const uint8_t nv = floorValue ? (uint8_t)1 : (uint8_t)0;

    bool changed = false;
    for (int dy = lo; dy <= hi; ++dy)
    {
        for (int dx = lo; dx <= hi; ++dx)
        {
            const int x = cx + dx;
            const int y = cy + dy;
            if (!InBounds(x, y))
                continue;
            uint8_t& c = m_cells[(size_t)y * m_w + x];
            if (c != nv)
            {
                c = nv;
                changed = true;
            }
        }
    }
    return changed;
}

bool LevelGrid::PaintLine(int x0, int y0, int x1, int y1, int brush, bool floorValue)
{
    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int steps = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
    if (steps == 0)
        return PaintCell(x0, y0, brush, floorValue);

    bool changed = false;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float)i / (float)steps;
        const int x = x0 + (int)floorf((float)dx * t + 0.5f);
        const int y = y0 + (int)floorf((float)dy * t + 0.5f);
        if (PaintCell(x, y, brush, floorValue))
            changed = true;
    }
    return changed;
}

// ------------------------------------------------------------- undo / redo

void LevelGrid::PushUndo(const std::vector<uint8_t>& state)
{
    m_undo.push_back(state);
    if (m_undo.size() > kMaxUndo)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void LevelGrid::BeginStroke()
{
    m_strokeSnapshot = m_cells;
    m_strokeActive = true;
}

void LevelGrid::RevertStroke()
{
    if (m_strokeActive && m_strokeSnapshot.size() == m_cells.size())
        m_cells = m_strokeSnapshot;
}

void LevelGrid::EndStroke()
{
    if (!m_strokeActive)
        return;
    m_strokeActive = false;
    if (m_strokeSnapshot.size() == m_cells.size() && m_strokeSnapshot != m_cells)
        PushUndo(m_strokeSnapshot);
    m_strokeSnapshot.clear();
}

bool LevelGrid::Undo()
{
    if (m_undo.empty())
        return false;
    m_redo.push_back(m_cells);
    m_cells = m_undo.back();
    m_undo.pop_back();
    return true;
}

bool LevelGrid::Redo()
{
    if (m_redo.empty())
        return false;
    m_undo.push_back(m_cells);
    m_cells = m_redo.back();
    m_redo.pop_back();
    return true;
}

// ------------------------------------------------------------ mesh builder

namespace {

void ReverseQuad(LmQuad& q)
{
    if (q.nv == 3)
    {
        const LmVec3 v1 = q.v[1];
        q.v[1] = q.v[2];
        q.v[2] = v1;
        q.v[3] = q.v[2];
    }
    else
    {
        const LmVec3 v0 = q.v[0];
        const LmVec3 v1 = q.v[1];
        q.v[0] = q.v[3];
        q.v[1] = q.v[2];
        q.v[2] = v1;
        q.v[3] = v0;
    }
    q.n.x = -q.n.x;
    q.n.y = -q.n.y;
    q.n.z = -q.n.z;
}

} // namespace

// --- diagonal smoothing helpers ------------------------------------------
//
// A chamfered cell keeps half of itself, so two of its four edges disappear
// and the hypotenuse takes their place. Which two depends on the corner cut:

bool LmCutEatsN(int c) { return c == LM_CUT_NW || c == LM_CUT_NE; }
bool LmCutEatsE(int c) { return c == LM_CUT_NE || c == LM_CUT_SE; }
bool LmCutEatsS(int c) { return c == LM_CUT_SE || c == LM_CUT_SW; }
bool LmCutEatsW(int c) { return c == LM_CUT_NW || c == LM_CUT_SW; }

// Step from one cell of a chamfer run to the next. NW and SE chamfers lie on
// the same "/" line; NE and SW lie on the "\" line.
void LmCutStep(int c, int& sx, int& sy)
{
    if (c == LM_CUT_NW || c == LM_CUT_SE) { sx = 1; sy = -1; }
    else                                  { sx = 1; sy = 1; }
}

// The chamfer edge of one cell, as grid-space endpoints ordered so that the
// wall quad built from them faces the floor that is left behind.
void LmCutEdge(int c, int x, int y, float& ax, float& ay, float& bx, float& by)
{
    switch (c)
    {
    case LM_CUT_NW:                 // south-west corner -> north-east corner
        ax = (float)x;       ay = (float)(y + 1);
        bx = (float)(x + 1); by = (float)y;
        break;
    case LM_CUT_SE:                 // the same line, walked the other way
        ax = (float)(x + 1); ay = (float)y;
        bx = (float)x;       by = (float)(y + 1);
        break;
    case LM_CUT_NE:                 // north-west corner -> south-east corner
        ax = (float)x;       ay = (float)y;
        bx = (float)(x + 1); by = (float)(y + 1);
        break;
    default:                        // LM_CUT_SW, the same line reversed
        ax = (float)(x + 1); ay = (float)(y + 1);
        bx = (float)x;       by = (float)y;
        break;
    }
}

// Inward normal of that chamfer: it points at the half of the cell that is
// still floor.
LmVec3 LmCutNormal(int c)
{
    const float r = 0.70710678f;
    LmVec3 n;
    n.y = 0.0f;
    switch (c)
    {
    case LM_CUT_NW: n.x =  r; n.z =  r; break;
    case LM_CUT_NE: n.x = -r; n.z =  r; break;
    case LM_CUT_SE: n.x = -r; n.z = -r; break;
    default:        n.x =  r; n.z = -r; break;   // LM_CUT_SW
    }
    return n;
}

// The three corners the cell keeps, wound so the floor faces up. (The
// ceiling copy reverses them.)
void LmCutTriangle(int c, int x, int y, float px[3], float py[3])
{
    const float x0 = (float)x,       x1 = (float)(x + 1);
    const float y0 = (float)y,       y1 = (float)(y + 1);
    switch (c)
    {
    case LM_CUT_NW:
        px[0] = x0; py[0] = y1;  px[1] = x1; py[1] = y1;  px[2] = x1; py[2] = y0;
        break;
    case LM_CUT_NE:
        px[0] = x0; py[0] = y0;  px[1] = x0; py[1] = y1;  px[2] = x1; py[2] = y1;
        break;
    case LM_CUT_SE:
        px[0] = x0; py[0] = y0;  px[1] = x0; py[1] = y1;  px[2] = x1; py[2] = y0;
        break;
    default:   // LM_CUT_SW
        px[0] = x0; py[0] = y0;  px[1] = x1; py[1] = y1;  px[2] = x1; py[2] = y0;
        break;
    }
}

// A cell is a chamfer candidate when two neighbours that meet at a corner are
// both empty and the opposite two are both floor -- that is exactly the step
// of a staircase. The cell diagonally past the cut corner has to be empty too,
// or the chamfer would open a gap at a pinch point.
int LevelGrid::CutCandidate(int x, int y) const
{
    if (!IsFloor(x, y))
        return LM_CUT_NONE;

    const bool n = IsFloor(x, y - 1);
    const bool s = IsFloor(x, y + 1);
    const bool w = IsFloor(x - 1, y);
    const bool e = IsFloor(x + 1, y);

    if (!n && !w && s && e && !IsFloor(x - 1, y - 1)) return LM_CUT_NW;
    if (!n && !e && s && w && !IsFloor(x + 1, y - 1)) return LM_CUT_NE;
    if (!s && !e && n && w && !IsFloor(x + 1, y + 1)) return LM_CUT_SE;
    if (!s && !w && n && e && !IsFloor(x - 1, y + 1)) return LM_CUT_SW;
    return LM_CUT_NONE;
}

void LevelGrid::BuildCutMap(std::vector<uint8_t>& cuts) const
{
    cuts.clear();
    if (!m_smoothDiagonals)
        return;                 // left empty: callers take that as "no cuts"
    cuts.assign((size_t)m_w * m_h, (uint8_t)LM_CUT_NONE);

    // Every outside corner of every room is a candidate, so a second pass
    // keeps only the ones that are part of a diagonal run. A lone corner --
    // the corner of a square room, the end cap of a hall -- stays square.
    std::vector<uint8_t> cand((size_t)m_w * m_h, (uint8_t)LM_CUT_NONE);
    for (int y = 0; y < m_h; ++y)
        for (int x = 0; x < m_w; ++x)
            cand[(size_t)y * m_w + x] = (uint8_t)CutCandidate(x, y);

    for (int y = 0; y < m_h; ++y)
    {
        for (int x = 0; x < m_w; ++x)
        {
            const size_t i = (size_t)y * m_w + x;
            const int c = cand[i];
            if (c == LM_CUT_NONE)
                continue;

            int sx = 0, sy = 0;
            LmCutStep(c, sx, sy);
            const bool ahead  = InBounds(x + sx, y + sy) &&
                                cand[(size_t)(y + sy) * m_w + (x + sx)] == c;
            const bool behind = InBounds(x - sx, y - sy) &&
                                cand[(size_t)(y - sy) * m_w + (x - sx)] == c;
            if (ahead || behind)
                cuts[i] = (uint8_t)c;
        }
    }
}


// Is there a wall on the row boundary above cell (x, y), and which side is
// the floor on? +1 for the cell below the line, -1 for the cell above it, 0
// for no wall -- either side matching, or a chamfer having eaten that edge.
int LevelGrid::WallSideH(const uint8_t* cut, int x, int y) const
{
    const bool above = IsFloor(x, y - 1);
    const bool below = IsFloor(x, y);
    if (above == below)
        return 0;
    if (below)
        return (cut && LmCutEatsN(cut[(size_t)y * m_w + x])) ? 0 : 1;
    return (cut && LmCutEatsS(cut[(size_t)(y - 1) * m_w + x])) ? 0 : -1;
}

// The same for the column boundary left of cell (x, y): +1 when the floor is
// the cell to the right, -1 when it is the cell to the left.
int LevelGrid::WallSideV(const uint8_t* cut, int x, int y) const
{
    const bool left  = IsFloor(x - 1, y);
    const bool right = IsFloor(x, y);
    if (left == right)
        return 0;
    if (right)
        return (cut && LmCutEatsW(cut[(size_t)y * m_w + x])) ? 0 : 1;
    return (cut && LmCutEatsE(cut[(size_t)y * m_w + (x - 1)])) ? 0 : -1;
}

void LevelGrid::BuildMesh(LmMesh& out, bool includeCeiling, LmFacing facing) const
{
    out.Clear();

    const float cs = m_cellSize;
    const float hh = m_wallHeight;

    // Diagonal smoothing: cells on a 45 degree staircase lose a corner, so
    // the steps join into one straight edge. The map is empty when the
    // setting is off, and every test below then behaves as it always did.
    std::vector<uint8_t> cuts;
    BuildCutMap(cuts);
    const uint8_t* cut = cuts.empty() ? NULL : &cuts[0];

    // Floors (and ceilings): greedy rectangles over the filled cells, so a
    // straight 40-cell hall is one quad instead of forty. Chamfered cells sit
    // the merge out and become single triangles afterwards.
    std::vector<uint8_t> used((size_t)m_w * m_h, 0);
    std::vector<uint8_t> absorbed;
    if (cut)
    {
        for (size_t i = 0; i < used.size(); ++i)
            if (cut[i] != LM_CUT_NONE)
                used[i] = 1;
        absorbed.assign((size_t)m_w * m_h, 0);
    }

    for (int y = 0; y < m_h; ++y)
    {
        int x = 0;
        while (x < m_w)
        {
            const size_t idx = (size_t)y * m_w + x;
            if (!m_cells[idx] || used[idx])
            {
                ++x;
                continue;
            }

            int w = 1;
            while (x + w < m_w && m_cells[idx + w] && !used[idx + w])
                ++w;

            int h = 1;
            for (;;)
            {
                const int ny = y + h;
                if (ny >= m_h)
                    break;
                const size_t rowBase = (size_t)ny * m_w + x;
                bool ok = true;
                for (int i = 0; i < w; ++i)
                {
                    if (!m_cells[rowBase + i] || used[rowBase + i])
                    {
                        ok = false;
                        break;
                    }
                }
                if (!ok)
                    break;
                ++h;
            }

            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i)
                    used[(size_t)(y + j) * m_w + x + i] = 1;

            // A one-cell-thick rectangle can swallow a chamfer sitting
            // against either end, turning two faces into one trapezoid: the
            // half cell shares that whole edge, so the union is still a
            // convex quad. Without this a diagonal hall would be a strip
            // plus a triangle at each end of every row or column.
            float blx = (float)x,       brx = (float)(x + w);
            float tlx = (float)x,       trx = (float)(x + w);
            float tlz = (float)y,       trz = (float)y;
            float blz = (float)(y + h), brz = (float)(y + h);

            bool grewSideways = false;
            if (cut && h == 1)
            {
                if (x > 0)
                {
                    const size_t li = (size_t)y * m_w + (x - 1);
                    const int lc = cut[li];
                    if (lc == LM_CUT_NW || lc == LM_CUT_SW)   // keeps its east edge
                    {
                        absorbed[li] = 1;
                        grewSideways = true;
                        if (lc == LM_CUT_NW) blx = (float)(x - 1);
                        else                 tlx = (float)(x - 1);
                    }
                }
                if (x + w < m_w)
                {
                    const size_t ri = (size_t)y * m_w + (x + w);
                    const int rc = cut[ri];
                    if (rc == LM_CUT_NE || rc == LM_CUT_SE)   // keeps its west edge
                    {
                        absorbed[ri] = 1;
                        grewSideways = true;
                        if (rc == LM_CUT_NE) brx = (float)(x + w + 1);
                        else                 trx = (float)(x + w + 1);
                    }
                }
            }
            // The greedy pass tends to cut a diagonal band into one-cell
            // columns, so the same trick has to work the other way up. Only
            // one axis at a time, or a single cell could pick up chamfers on
            // all four sides and stop being a quad.
            if (cut && w == 1 && !grewSideways)
            {
                if (y > 0)
                {
                    const size_t ai = (size_t)(y - 1) * m_w + x;
                    const int ac = cut[ai];
                    if (ac == LM_CUT_NW || ac == LM_CUT_NE)   // keeps its south edge
                    {
                        absorbed[ai] = 1;
                        if (ac == LM_CUT_NW) trz = (float)(y - 1);
                        else                 tlz = (float)(y - 1);
                    }
                }
                if (y + h < m_h)
                {
                    const size_t bi = (size_t)(y + h) * m_w + x;
                    const int bc = cut[bi];
                    if (bc == LM_CUT_SE || bc == LM_CUT_SW)   // keeps its north edge
                    {
                        absorbed[bi] = 1;
                        if (bc == LM_CUT_SE) blz = (float)(y + h + 1);
                        else                 brz = (float)(y + h + 1);
                    }
                }
            }

            LmQuad q;
            q.mat = LM_MAT_FLOOR;
            q.n.x = 0.0f; q.n.y = 1.0f; q.n.z = 0.0f;
            q.v[0].x = blx * cs; q.v[0].y = 0.0f; q.v[0].z = blz * cs;
            q.v[1].x = brx * cs; q.v[1].y = 0.0f; q.v[1].z = brz * cs;
            q.v[2].x = trx * cs; q.v[2].y = 0.0f; q.v[2].z = trz * cs;
            q.v[3].x = tlx * cs; q.v[3].y = 0.0f; q.v[3].z = tlz * cs;
            out.quads.push_back(q);

            if (includeCeiling)
            {
                LmQuad c;
                c.mat = LM_MAT_CEILING;
                c.n.x = 0.0f; c.n.y = -1.0f; c.n.z = 0.0f;
                c.v[0].x = tlx * cs; c.v[0].y = hh; c.v[0].z = tlz * cs;
                c.v[1].x = trx * cs; c.v[1].y = hh; c.v[1].z = trz * cs;
                c.v[2].x = brx * cs; c.v[2].y = hh; c.v[2].z = brz * cs;
                c.v[3].x = blx * cs; c.v[3].y = hh; c.v[3].z = blz * cs;
                out.quads.push_back(c);
            }

            x += w;
        }
    }

    // The half cells left over from the chamfers: one floor triangle each.
    if (cut)
    {
        for (int y = 0; y < m_h; ++y)
        {
            for (int x = 0; x < m_w; ++x)
            {
                const size_t i = (size_t)y * m_w + x;
                const int c = cut[i];
                if (c == LM_CUT_NONE || absorbed[i])
                    continue;

                float px[3], py[3];
                LmCutTriangle(c, x, y, px, py);

                LmQuad t;
                t.nv = 3;
                t.mat = LM_MAT_FLOOR;
                t.n.x = 0.0f; t.n.y = 1.0f; t.n.z = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    t.v[k].x = px[k] * cs;
                    t.v[k].y = 0.0f;
                    t.v[k].z = py[k] * cs;
                }
                t.v[3] = t.v[2];
                out.quads.push_back(t);

                if (includeCeiling)
                {
                    LmQuad u;
                    u.nv = 3;
                    u.mat = LM_MAT_CEILING;
                    u.n.x = 0.0f; u.n.y = -1.0f; u.n.z = 0.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        u.v[k].x = px[2 - k] * cs;
                        u.v[k].y = hh;
                        u.v[k].z = py[2 - k] * cs;
                    }
                    u.v[3] = u.v[2];
                    out.quads.push_back(u);
                }
            }
        }
    }

    // Walls that run along X: every boundary between grid row y-1 and row y
    // where exactly one side is floor and neither side's chamfer has eaten
    // it. Consecutive cells merge into one quad.
    for (int y = 0; y <= m_h; ++y)
    {
        int x = 0;
        while (x < m_w)
        {
            const int side = WallSideH(cut, x, y);
            if (side == 0)
            {
                ++x;
                continue;
            }
            const int start = x;
            while (x < m_w && WallSideH(cut, x, y) == side)
                ++x;

            const float x0 = (float)start * cs;
            const float x1 = (float)x * cs;
            const float z  = (float)y * cs;

            LmQuad q;
            q.mat = LM_MAT_WALL;
            if (side > 0)
            {
                // floor is on the +Z side, so the face looks back into it
                q.n.x = 0.0f; q.n.y = 0.0f; q.n.z = 1.0f;
                q.v[0].x = x0; q.v[0].y = 0.0f; q.v[0].z = z;
                q.v[1].x = x1; q.v[1].y = 0.0f; q.v[1].z = z;
                q.v[2].x = x1; q.v[2].y = hh;   q.v[2].z = z;
                q.v[3].x = x0; q.v[3].y = hh;   q.v[3].z = z;
            }
            else
            {
                // floor is on the -Z side
                q.n.x = 0.0f; q.n.y = 0.0f; q.n.z = -1.0f;
                q.v[0].x = x1; q.v[0].y = 0.0f; q.v[0].z = z;
                q.v[1].x = x0; q.v[1].y = 0.0f; q.v[1].z = z;
                q.v[2].x = x0; q.v[2].y = hh;   q.v[2].z = z;
                q.v[3].x = x1; q.v[3].y = hh;   q.v[3].z = z;
            }
            out.quads.push_back(q);
        }
    }

    // Walls that run along Z.
    for (int x = 0; x <= m_w; ++x)
    {
        int y = 0;
        while (y < m_h)
        {
            const int side = WallSideV(cut, x, y);
            if (side == 0)
            {
                ++y;
                continue;
            }
            const int start = y;
            while (y < m_h && WallSideV(cut, x, y) == side)
                ++y;

            const float z0 = (float)start * cs;
            const float z1 = (float)y * cs;
            const float xp = (float)x * cs;

            LmQuad q;
            q.mat = LM_MAT_WALL;
            if (side > 0)
            {
                // floor is on the +X side, so the face looks back into it
                q.n.x = 1.0f; q.n.y = 0.0f; q.n.z = 0.0f;
                q.v[0].x = xp; q.v[0].y = 0.0f; q.v[0].z = z1;
                q.v[1].x = xp; q.v[1].y = 0.0f; q.v[1].z = z0;
                q.v[2].x = xp; q.v[2].y = hh;   q.v[2].z = z0;
                q.v[3].x = xp; q.v[3].y = hh;   q.v[3].z = z1;
            }
            else
            {
                // floor is on the -X side
                q.n.x = -1.0f; q.n.y = 0.0f; q.n.z = 0.0f;
                q.v[0].x = xp; q.v[0].y = 0.0f; q.v[0].z = z0;
                q.v[1].x = xp; q.v[1].y = 0.0f; q.v[1].z = z1;
                q.v[2].x = xp; q.v[2].y = hh;   q.v[2].z = z1;
                q.v[3].x = xp; q.v[3].y = hh;   q.v[3].z = z0;
            }
            out.quads.push_back(q);
        }
    }

    // Diagonal walls: one quad per run of chamfers, so a 45 degree hall gets
    // a single flat wall down each side instead of a row of little steps.
    if (cut)
    {
        std::vector<uint8_t> walked((size_t)m_w * m_h, 0);
        for (int y = 0; y < m_h; ++y)
        {
            for (int x = 0; x < m_w; ++x)
            {
                const size_t i = (size_t)y * m_w + x;
                if (walked[i] || cut[i] == LM_CUT_NONE)
                    continue;

                const int c = cut[i];
                int sx = 0, sy = 0;
                LmCutStep(c, sx, sy);

                // back up to the first cell of the run, then walk it out
                int fx = x, fy = y;
                while (InBounds(fx - sx, fy - sy) &&
                       cut[(size_t)(fy - sy) * m_w + (fx - sx)] == c)
                {
                    fx -= sx;
                    fy -= sy;
                }
                int lx = fx, ly = fy;
                walked[(size_t)ly * m_w + lx] = 1;
                while (InBounds(lx + sx, ly + sy) &&
                       cut[(size_t)(ly + sy) * m_w + (lx + sx)] == c)
                {
                    lx += sx;
                    ly += sy;
                    walked[(size_t)ly * m_w + lx] = 1;
                }

                // One edge spanning the whole run. SE and SW chamfers are
                // wound against the step direction -- they face the other way
                // along the same line -- so their quad starts at the last
                // cell instead of the first.
                const bool reversed = (c == LM_CUT_SE || c == LM_CUT_SW);
                const int  sX = reversed ? lx : fx, sY = reversed ? ly : fy;
                const int  eX = reversed ? fx : lx, eY = reversed ? fy : ly;

                float ax, ay, bx, by, skipX, skipY;
                LmCutEdge(c, sX, sY, ax, ay, skipX, skipY);
                LmCutEdge(c, eX, eY, skipX, skipY, bx, by);

                LmQuad q;
                q.mat = LM_MAT_WALL;
                q.n = LmCutNormal(c);
                q.v[0].x = ax * cs; q.v[0].y = 0.0f; q.v[0].z = ay * cs;
                q.v[1].x = bx * cs; q.v[1].y = 0.0f; q.v[1].z = by * cs;
                q.v[2].x = bx * cs; q.v[2].y = hh;   q.v[2].z = by * cs;
                q.v[3].x = ax * cs; q.v[3].y = hh;   q.v[3].z = ay * cs;
                out.quads.push_back(q);
            }
        }
    }

    // Everything above is built facing inward, into the walkable space.
    if (facing == LM_FACE_OUTWARD)
    {
        for (size_t i = 0; i < out.quads.size(); ++i)
            ReverseQuad(out.quads[i]);
    }
    else if (facing == LM_FACE_BOTH)
    {
        const size_t n = out.quads.size();
        out.quads.reserve(n * 2);
        for (size_t i = 0; i < n; ++i)
        {
            LmQuad back = out.quads[i];
            ReverseQuad(back);
            out.quads.push_back(back);
        }
    }

    for (size_t i = 0; i < out.quads.size(); ++i)
    {
        const int m = out.quads[i].mat;
        if (m >= 0 && m < LM_MAT_COUNT)
            out.quadsByMat[m]++;
    }
}

// --------------------------------------------------------------- file i/o
//
// Plain Win32 file calls: no CRT file handles, no wide-path quirks between
// compilers, and no deprecation warnings under /sdl.

namespace {

bool WriteWholeFile(const wchar_t* path, const void* data, size_t size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    const char* p = (const char*)data;
    size_t left = size;
    while (left > 0)
    {
        const DWORD chunk = (left > 0x10000000u) ? 0x10000000u : (DWORD)left;
        DWORD written = 0;
        if (!WriteFile(h, p, chunk, &written, NULL) || written == 0)
        {
            ok = false;
            break;
        }
        p += written;
        left -= written;
    }
    CloseHandle(h);
    return ok;
}

bool ReadWholeFile(const wchar_t* path, std::vector<uint8_t>& out)
{
    out.clear();
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size;
    size.QuadPart = 0;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (LONGLONG)(64 * 1024 * 1024))
    {
        CloseHandle(h);
        return false;
    }

    out.resize((size_t)size.QuadPart);
    size_t done = 0;
    bool ok = true;
    while (done < out.size())
    {
        const size_t left = out.size() - done;
        const DWORD chunk = (left > 0x10000000u) ? 0x10000000u : (DWORD)left;
        DWORD got = 0;
        if (!ReadFile(h, &out[done], chunk, &got, NULL) || got == 0)
        {
            ok = false;
            break;
        }
        done += got;
    }
    CloseHandle(h);
    if (!ok)
        out.clear();
    return ok;
}

void AppendFmt(std::string& s, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0)
        s.append(buf, (size_t)((n < (int)sizeof(buf)) ? n : (int)sizeof(buf) - 1));
}

uint64_t VertKey(const LmVec3& v)
{
    // coordinates are always >= 0 here and well under 20k, so 21 bits of
    // centimetre-quantised value per axis is plenty for welding duplicates
    const uint64_t xi = (uint64_t)(int64_t)(v.x * 100.0f + 0.5f) & 0x1FFFFF;
    const uint64_t yi = (uint64_t)(int64_t)(v.y * 100.0f + 0.5f) & 0x1FFFFF;
    const uint64_t zi = (uint64_t)(int64_t)(v.z * 100.0f + 0.5f) & 0x1FFFFF;
    return (xi << 42) | (yi << 21) | zi;
}

const char* MatName(int mat)
{
    switch (mat)
    {
    case LM_MAT_FLOOR:   return "floor";
    case LM_MAT_WALL:    return "wall";
    case LM_MAT_CEILING: return "ceiling";
    default:             return "default";
    }
}

std::wstring ReplaceExtension(const std::wstring& path, const wchar_t* ext)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
        return path.substr(0, dot) + ext;
    return path + ext;
}

std::string NarrowAscii(const std::wstring& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        out.push_back((s[i] > 0 && s[i] < 127) ? (char)s[i] : '_');
    return out;
}

std::wstring FileNameOnly(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

template <typename T>
void AppendRaw(std::string& s, const T& value)
{
    s.append((const char*)&value, sizeof(T));
}

} // namespace

bool LevelGrid::Save(const wchar_t* path) const
{
    std::string blob;
    blob.reserve(m_cells.size() + 32);
    blob.append(kMagic, 4);
    AppendRaw(blob, kVersion);
    AppendRaw(blob, (uint32_t)m_w);
    AppendRaw(blob, (uint32_t)m_h);
    AppendRaw(blob, m_cellSize);
    AppendRaw(blob, m_wallHeight);
    if (!m_cells.empty())
        blob.append((const char*)&m_cells[0], m_cells.size());
    blob.push_back(m_smoothDiagonals ? (char)1 : (char)0);
    return WriteWholeFile(path, blob.data(), blob.size());
}

bool LevelGrid::Load(const wchar_t* path)
{
    std::vector<uint8_t> blob;
    if (!ReadWholeFile(path, blob))
        return false;

    const size_t kHeader = 4 + 4 + 4 + 4 + 4 + 4;
    if (blob.size() < kHeader)
        return false;
    if (memcmp(&blob[0], kMagic, 4) != 0)
        return false;

    uint32_t version = 0, w = 0, h = 0;
    float cellSize = 2.0f, wallHeight = 3.0f;
    memcpy(&version, &blob[4], 4);
    memcpy(&w, &blob[8], 4);
    memcpy(&h, &blob[12], 4);
    memcpy(&cellSize, &blob[16], 4);
    memcpy(&wallHeight, &blob[20], 4);

    if (version != 1 && version != kVersion)
        return false;
    if (w < 8 || h < 8 || w > 1024 || h > 1024)
        return false;

    const size_t count = (size_t)w * (size_t)h;
    if (blob.size() < kHeader + count)
        return false;

    std::vector<uint8_t> cells(count, 0);
    for (size_t i = 0; i < count; ++i)
        cells[i] = blob[kHeader + i] ? (uint8_t)1 : (uint8_t)0;

    // the flag byte is only there from v2 on
    m_smoothDiagonals = (version < 2) ? true
                      : (blob.size() > kHeader + count && blob[kHeader + count] != 0);

    m_w = (int)w;
    m_h = (int)h;
    m_cells.swap(cells);
    if (cellSize > 0.01f && cellSize < 1000.0f)
        m_cellSize = cellSize;
    if (wallHeight > 0.01f && wallHeight < 1000.0f)
        m_wallHeight = wallHeight;
    m_undo.clear();
    m_redo.clear();
    m_strokeSnapshot.clear();
    m_strokeActive = false;
    return true;
}

bool LevelGrid::ExportObj(const wchar_t* path, bool includeCeiling, LmFacing facing) const
{
    LmMesh mesh;
    BuildMesh(mesh, includeCeiling, facing);

    const std::wstring wpath(path);
    const std::wstring mtlPath = ReplaceExtension(wpath, L".mtl");
    const std::string  mtlName = NarrowAscii(FileNameOnly(mtlPath));

    // weld vertices, gather the handful of distinct face normals
    std::unordered_map<uint64_t, int> vmap;
    std::vector<LmVec3> verts;
    std::vector<LmVec3> norms;
    std::vector<int> faceV(mesh.quads.size() * 4, 0);
    std::vector<int> faceN(mesh.quads.size(), 0);

    for (size_t q = 0; q < mesh.quads.size(); ++q)
    {
        const LmQuad& quad = mesh.quads[q];
        for (int i = 0; i < 4; ++i)
        {
            const uint64_t key = VertKey(quad.v[i]);
            std::unordered_map<uint64_t, int>::iterator it = vmap.find(key);
            int index;
            if (it == vmap.end())
            {
                verts.push_back(quad.v[i]);
                index = (int)verts.size();      // OBJ indices are 1-based
                vmap.insert(std::make_pair(key, index));
            }
            else
            {
                index = it->second;
            }
            faceV[q * 4 + i] = index;
        }

        int ni = -1;
        for (size_t n = 0; n < norms.size(); ++n)
        {
            if (norms[n].x == quad.n.x && norms[n].y == quad.n.y && norms[n].z == quad.n.z)
            {
                ni = (int)n + 1;
                break;
            }
        }
        if (ni < 0)
        {
            norms.push_back(quad.n);
            ni = (int)norms.size();
        }
        faceN[q] = ni;
    }

    std::string obj;
    obj.reserve(mesh.quads.size() * 48 + verts.size() * 32 + 256);
    obj += "# LevelMaker OBJ export\r\n";
    AppendFmt(obj, "# %u faces, %u triangles, cell %.3f, wall height %.3f\r\n",
              (unsigned)mesh.quads.size(), (unsigned)mesh.TriangleCount(),
              m_cellSize, m_wallHeight);
    obj += "mtllib " + mtlName + "\r\n";
    obj += "o level\r\n";

    for (size_t i = 0; i < verts.size(); ++i)
        AppendFmt(obj, "v %.4f %.4f %.4f\r\n", verts[i].x, verts[i].y, verts[i].z);
    for (size_t i = 0; i < norms.size(); ++i)
        AppendFmt(obj, "vn %.4f %.4f %.4f\r\n", norms[i].x, norms[i].y, norms[i].z);

    for (int mat = 0; mat < LM_MAT_COUNT; ++mat)
    {
        if (mesh.quadsByMat[mat] == 0)
            continue;
        AppendFmt(obj, "g %s\r\nusemtl %s\r\n", MatName(mat), MatName(mat));
        for (size_t q = 0; q < mesh.quads.size(); ++q)
        {
            if (mesh.quads[q].mat != mat)
                continue;
            const int n = faceN[q];
            if (mesh.quads[q].nv == 3)
                AppendFmt(obj, "f %d//%d %d//%d %d//%d\r\n",
                          faceV[q * 4 + 0], n, faceV[q * 4 + 1], n,
                          faceV[q * 4 + 2], n);
            else
                AppendFmt(obj, "f %d//%d %d//%d %d//%d %d//%d\r\n",
                          faceV[q * 4 + 0], n, faceV[q * 4 + 1], n,
                          faceV[q * 4 + 2], n, faceV[q * 4 + 3], n);
        }
    }

    if (!WriteWholeFile(path, obj.data(), obj.size()))
        return false;

    // companion material file; a failure here is not worth failing the export
    std::string mtl;
    mtl += "# LevelMaker materials\r\n";
    mtl += "newmtl floor\r\nKd 0.55 0.57 0.60\r\nKa 0.10 0.10 0.10\r\nKs 0 0 0\r\n\r\n";
    mtl += "newmtl wall\r\nKd 0.80 0.78 0.72\r\nKa 0.10 0.10 0.10\r\nKs 0 0 0\r\n\r\n";
    mtl += "newmtl ceiling\r\nKd 0.35 0.36 0.40\r\nKa 0.10 0.10 0.10\r\nKs 0 0 0\r\n";
    WriteWholeFile(mtlPath.c_str(), mtl.data(), mtl.size());
    return true;
}

// ------------------------------------------------------------- FBX export
//
// ASCII FBX 7.4. One mesh node, per-polygon-vertex normals and three
// materials. Written by hand -- the format is plain text, so no SDK.

bool LevelGrid::ExportFbxAscii(const wchar_t* path, bool includeCeiling, LmFacing facing) const
{
    LmMesh mesh;
    BuildMesh(mesh, includeCeiling, facing);
    if (mesh.quads.empty())
        return false;

    // weld vertices exactly as the OBJ path does
    std::unordered_map<uint64_t, int> vmap;
    std::vector<LmVec3> verts;
    std::vector<int> faceV(mesh.quads.size() * 4, 0);

    for (size_t q = 0; q < mesh.quads.size(); ++q)
    {
        for (int i = 0; i < mesh.quads[q].nv; ++i)
        {
            const uint64_t key = VertKey(mesh.quads[q].v[i]);
            std::unordered_map<uint64_t, int>::iterator it = vmap.find(key);
            int index;
            if (it == vmap.end())
            {
                verts.push_back(mesh.quads[q].v[i]);
                index = (int)verts.size() - 1;      // FBX indices are 0-based
                vmap.insert(std::make_pair(key, index));
            }
            else
            {
                index = it->second;
            }
            faceV[q * 4 + i] = index;
        }
    }

    const size_t polys = mesh.quads.size();
    size_t polyVerts = 0;               // triangles contribute 3, quads 4
    for (size_t q = 0; q < polys; ++q)
        polyVerts += (size_t)mesh.quads[q].nv;

    std::string f;
    f.reserve(polys * 160 + verts.size() * 40 + 4096);

    f += "; FBX 7.4.0 project file\r\n";
    f += "; Written by LevelMaker\r\n";
    f += "; ----------------------------------------------------\r\n\r\n";

    f += "FBXHeaderExtension:  {\r\n";
    f += "\tFBXHeaderVersion: 1003\r\n";
    f += "\tFBXVersion: 7400\r\n";
    f += "\tCreator: \"LevelMaker\"\r\n";
    f += "}\r\n";

    f += "GlobalSettings:  {\r\n";
    f += "\tVersion: 1000\r\n";
    f += "\tProperties70:  {\r\n";
    f += "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\r\n";
    f += "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\r\n";
    f += "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\r\n";
    f += "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\r\n";
    f += "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\r\n";
    f += "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\r\n";
    f += "\t\tP: \"OriginalUpAxis\", \"int\", \"Integer\", \"\",1\r\n";
    f += "\t\tP: \"OriginalUpAxisSign\", \"int\", \"Integer\", \"\",1\r\n";
    f += "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\r\n";
    f += "\t\tP: \"OriginalUnitScaleFactor\", \"double\", \"Number\", \"\",1\r\n";
    f += "\t}\r\n";
    f += "}\r\n\r\n";

    f += "Documents:  {\r\n";
    f += "\tCount: 1\r\n";
    f += "\tDocument: 1000000, \"\", \"Scene\" {\r\n";
    f += "\t\tProperties70:  {\r\n";
    f += "\t\t\tP: \"SourceObject\", \"object\", \"\", \"\"\r\n";
    f += "\t\t\tP: \"ActiveAnimStackName\", \"KString\", \"\", \"\", \"\"\r\n";
    f += "\t\t}\r\n";
    f += "\t\tRootNode: 0\r\n";
    f += "\t}\r\n";
    f += "}\r\n";
    f += "References:  {\r\n}\r\n";

    f += "Definitions:  {\r\n";
    f += "\tVersion: 100\r\n";
    f += "\tCount: 6\r\n";
    f += "\tObjectType: \"GlobalSettings\" {\r\n\t\tCount: 1\r\n\t}\r\n";
    f += "\tObjectType: \"Model\" {\r\n\t\tCount: 1\r\n\t}\r\n";
    f += "\tObjectType: \"Geometry\" {\r\n\t\tCount: 1\r\n\t}\r\n";
    f += "\tObjectType: \"Material\" {\r\n\t\tCount: 3\r\n\t}\r\n";
    f += "}\r\n\r\n";

    f += "Objects:  {\r\n";
    f += "\tGeometry: 2000000, \"Geometry::level\", \"Mesh\" {\r\n";

    AppendFmt(f, "\t\tVertices: *%u {\r\n\t\t\ta: ", (unsigned)(verts.size() * 3));
    for (size_t i = 0; i < verts.size(); ++i)
    {
        AppendFmt(f, "%s%.4f,%.4f,%.4f", (i == 0) ? "" : ",",
                  verts[i].x, verts[i].y, verts[i].z);
        if ((i % 8) == 7 && i + 1 < verts.size())
            f += "\r\n\t\t\t";
    }
    f += "\r\n\t\t}\r\n";

    AppendFmt(f, "\t\tPolygonVertexIndex: *%u {\r\n\t\t\ta: ", (unsigned)polyVerts);
    for (size_t q = 0; q < polys; ++q)
    {
        // FBX marks the last index of a polygon by storing ~index
        if (mesh.quads[q].nv == 3)
            AppendFmt(f, "%s%d,%d,%d", (q == 0) ? "" : ",",
                      faceV[q * 4 + 0], faceV[q * 4 + 1], -(faceV[q * 4 + 2] + 1));
        else
            AppendFmt(f, "%s%d,%d,%d,%d", (q == 0) ? "" : ",",
                      faceV[q * 4 + 0], faceV[q * 4 + 1], faceV[q * 4 + 2],
                      -(faceV[q * 4 + 3] + 1));
        if ((q % 8) == 7 && q + 1 < polys)
            f += "\r\n\t\t\t";
    }
    f += "\r\n\t\t}\r\n";

    f += "\t\tGeometryVersion: 124\r\n";

    f += "\t\tLayerElementNormal: 0 {\r\n";
    f += "\t\t\tVersion: 102\r\n";
    f += "\t\t\tName: \"\"\r\n";
    f += "\t\t\tMappingInformationType: \"ByPolygonVertex\"\r\n";
    f += "\t\t\tReferenceInformationType: \"Direct\"\r\n";
    AppendFmt(f, "\t\t\tNormals: *%u {\r\n\t\t\t\ta: ", (unsigned)(polyVerts * 3));
    for (size_t q = 0; q < polys; ++q)
    {
        const LmVec3& n = mesh.quads[q].n;
        // %.1f was fine while every normal was an axis; the diagonal walls
        // need the decimals
        for (int k = 0; k < mesh.quads[q].nv; ++k)
            AppendFmt(f, "%s%.4f,%.4f,%.4f", (q == 0 && k == 0) ? "" : ",", n.x, n.y, n.z);
        if ((q % 6) == 5 && q + 1 < polys)
            f += "\r\n\t\t\t\t";
    }
    f += "\r\n\t\t\t}\r\n";
    f += "\t\t}\r\n";

    f += "\t\tLayerElementMaterial: 0 {\r\n";
    f += "\t\t\tVersion: 101\r\n";
    f += "\t\t\tName: \"\"\r\n";
    f += "\t\t\tMappingInformationType: \"ByPolygon\"\r\n";
    f += "\t\t\tReferenceInformationType: \"IndexToDirect\"\r\n";
    AppendFmt(f, "\t\t\tMaterials: *%u {\r\n\t\t\t\ta: ", (unsigned)polys);
    for (size_t q = 0; q < polys; ++q)
    {
        AppendFmt(f, "%s%d", (q == 0) ? "" : ",", mesh.quads[q].mat);
        if ((q % 32) == 31 && q + 1 < polys)
            f += "\r\n\t\t\t\t";
    }
    f += "\r\n\t\t\t}\r\n";
    f += "\t\t}\r\n";

    f += "\t\tLayer: 0 {\r\n";
    f += "\t\t\tVersion: 100\r\n";
    f += "\t\t\tLayerElement:  {\r\n";
    f += "\t\t\t\tType: \"LayerElementNormal\"\r\n";
    f += "\t\t\t\tTypedIndex: 0\r\n";
    f += "\t\t\t}\r\n";
    f += "\t\t\tLayerElement:  {\r\n";
    f += "\t\t\t\tType: \"LayerElementMaterial\"\r\n";
    f += "\t\t\t\tTypedIndex: 0\r\n";
    f += "\t\t\t}\r\n";
    f += "\t\t}\r\n";
    f += "\t}\r\n";

    f += "\tModel: 3000000, \"Model::level\", \"Mesh\" {\r\n";
    f += "\t\tVersion: 232\r\n";
    f += "\t\tProperties70:  {\r\n";
    f += "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\r\n";
    f += "\t\t\tP: \"DefaultAttributeIndex\", \"int\", \"Integer\", \"\",0\r\n";
    f += "\t\t}\r\n";
    f += "\t\tShading: T\r\n";
    f += "\t\tCulling: \"CullingOff\"\r\n";
    f += "\t}\r\n";

    static const float kMatColor[LM_MAT_COUNT][3] = {
        { 0.55f, 0.57f, 0.61f },
        { 0.82f, 0.79f, 0.73f },
        { 0.34f, 0.35f, 0.40f }
    };
    for (int mat = 0; mat < LM_MAT_COUNT; ++mat)
    {
        AppendFmt(f, "\tMaterial: %d, \"Material::%s\", \"\" {\r\n", 4000001 + mat, MatName(mat));
        f += "\t\tVersion: 102\r\n";
        f += "\t\tShadingModel: \"phong\"\r\n";
        f += "\t\tMultiLayer: 0\r\n";
        f += "\t\tProperties70:  {\r\n";
        AppendFmt(f, "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",%.3f,%.3f,%.3f\r\n",
                  kMatColor[mat][0], kMatColor[mat][1], kMatColor[mat][2]);
        f += "\t\t\tP: \"SpecularFactor\", \"Number\", \"\", \"A\",0\r\n";
        f += "\t\t}\r\n";
        f += "\t}\r\n";
    }
    f += "}\r\n\r\n";

    f += "Connections:  {\r\n";
    f += "\tC: \"OO\",3000000,0\r\n";           // model under the scene root
    f += "\tC: \"OO\",2000000,3000000\r\n";     // geometry under the model
    for (int mat = 0; mat < LM_MAT_COUNT; ++mat)
        AppendFmt(f, "\tC: \"OO\",%d,3000000\r\n", 4000001 + mat);
    f += "}\r\n";

    return WriteWholeFile(path, f.data(), f.size());
}
