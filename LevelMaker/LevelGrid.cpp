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
const uint32_t kVersion = 1;

} // namespace

LevelGrid::LevelGrid()
    : m_w(0)
    , m_h(0)
    , m_cellSize(1.0f)
    , m_wallHeight(3.0f)
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

void LevelGrid::BuildMesh(LmMesh& out, bool includeCeiling) const
{
    out.Clear();

    const float cs = m_cellSize;
    const float hh = m_wallHeight;

    // Floors (and ceilings): greedy rectangles over the filled cells, so a
    // straight 40-cell hall is one quad instead of forty.
    std::vector<uint8_t> used((size_t)m_w * m_h, 0);
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

            const float x0 = (float)x * cs;
            const float x1 = (float)(x + w) * cs;
            const float z0 = (float)y * cs;
            const float z1 = (float)(y + h) * cs;

            LmQuad q;
            q.mat = LM_MAT_FLOOR;
            q.n.x = 0.0f; q.n.y = 1.0f; q.n.z = 0.0f;
            q.v[0].x = x0; q.v[0].y = 0.0f; q.v[0].z = z1;
            q.v[1].x = x1; q.v[1].y = 0.0f; q.v[1].z = z1;
            q.v[2].x = x1; q.v[2].y = 0.0f; q.v[2].z = z0;
            q.v[3].x = x0; q.v[3].y = 0.0f; q.v[3].z = z0;
            out.quads.push_back(q);

            if (includeCeiling)
            {
                LmQuad c;
                c.mat = LM_MAT_CEILING;
                c.n.x = 0.0f; c.n.y = -1.0f; c.n.z = 0.0f;
                c.v[0].x = x0; c.v[0].y = hh; c.v[0].z = z0;
                c.v[1].x = x1; c.v[1].y = hh; c.v[1].z = z0;
                c.v[2].x = x1; c.v[2].y = hh; c.v[2].z = z1;
                c.v[3].x = x0; c.v[3].y = hh; c.v[3].z = z1;
                out.quads.push_back(c);
            }

            x += w;
        }
    }

    // Walls that run along X: every boundary between grid row y-1 and row y
    // where exactly one side is floor. Consecutive cells merge into one quad.
    for (int y = 0; y <= m_h; ++y)
    {
        int x = 0;
        while (x < m_w)
        {
            const bool a = IsFloor(x, y - 1);
            const bool b = IsFloor(x, y);
            if (a == b)
            {
                ++x;
                continue;
            }
            const int start = x;
            while (x < m_w && IsFloor(x, y - 1) == a && IsFloor(x, y) == b)
                ++x;

            const float x0 = (float)start * cs;
            const float x1 = (float)x * cs;
            const float z  = (float)y * cs;

            LmQuad q;
            q.mat = LM_MAT_WALL;
            if (b)
            {
                // floor on the +Z side, so the visible face looks toward -Z
                q.n.x = 0.0f; q.n.y = 0.0f; q.n.z = -1.0f;
                q.v[0].x = x1; q.v[0].y = 0.0f; q.v[0].z = z;
                q.v[1].x = x0; q.v[1].y = 0.0f; q.v[1].z = z;
                q.v[2].x = x0; q.v[2].y = hh;   q.v[2].z = z;
                q.v[3].x = x1; q.v[3].y = hh;   q.v[3].z = z;
            }
            else
            {
                q.n.x = 0.0f; q.n.y = 0.0f; q.n.z = 1.0f;
                q.v[0].x = x0; q.v[0].y = 0.0f; q.v[0].z = z;
                q.v[1].x = x1; q.v[1].y = 0.0f; q.v[1].z = z;
                q.v[2].x = x1; q.v[2].y = hh;   q.v[2].z = z;
                q.v[3].x = x0; q.v[3].y = hh;   q.v[3].z = z;
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
            const bool a = IsFloor(x - 1, y);
            const bool b = IsFloor(x, y);
            if (a == b)
            {
                ++y;
                continue;
            }
            const int start = y;
            while (y < m_h && IsFloor(x - 1, y) == a && IsFloor(x, y) == b)
                ++y;

            const float z0 = (float)start * cs;
            const float z1 = (float)y * cs;
            const float xp = (float)x * cs;

            LmQuad q;
            q.mat = LM_MAT_WALL;
            if (b)
            {
                // floor on the +X side, face looks toward -X
                q.n.x = -1.0f; q.n.y = 0.0f; q.n.z = 0.0f;
                q.v[0].x = xp; q.v[0].y = 0.0f; q.v[0].z = z0;
                q.v[1].x = xp; q.v[1].y = 0.0f; q.v[1].z = z1;
                q.v[2].x = xp; q.v[2].y = hh;   q.v[2].z = z1;
                q.v[3].x = xp; q.v[3].y = hh;   q.v[3].z = z0;
            }
            else
            {
                q.n.x = 1.0f; q.n.y = 0.0f; q.n.z = 0.0f;
                q.v[0].x = xp; q.v[0].y = 0.0f; q.v[0].z = z1;
                q.v[1].x = xp; q.v[1].y = 0.0f; q.v[1].z = z0;
                q.v[2].x = xp; q.v[2].y = hh;   q.v[2].z = z0;
                q.v[3].x = xp; q.v[3].y = hh;   q.v[3].z = z1;
            }
            out.quads.push_back(q);
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

    if (version != kVersion)
        return false;
    if (w < 8 || h < 8 || w > 1024 || h > 1024)
        return false;

    const size_t count = (size_t)w * (size_t)h;
    if (blob.size() < kHeader + count)
        return false;

    std::vector<uint8_t> cells(count, 0);
    for (size_t i = 0; i < count; ++i)
        cells[i] = blob[kHeader + i] ? (uint8_t)1 : (uint8_t)0;

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

bool LevelGrid::ExportObj(const wchar_t* path, bool includeCeiling) const
{
    LmMesh mesh;
    BuildMesh(mesh, includeCeiling);

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
    AppendFmt(obj, "# %u quads, %u triangles, cell %.3f, wall height %.3f\r\n",
              (unsigned)mesh.quads.size(), (unsigned)mesh.TriangleCount(),
              m_cellSize, m_wallHeight);
    obj += "mtllib " + mtlName + "\r\n";
    obj += "o level\r\n";

    for (size_t i = 0; i < verts.size(); ++i)
        AppendFmt(obj, "v %.4f %.4f %.4f\r\n", verts[i].x, verts[i].y, verts[i].z);
    for (size_t i = 0; i < norms.size(); ++i)
        AppendFmt(obj, "vn %.1f %.1f %.1f\r\n", norms[i].x, norms[i].y, norms[i].z);

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
