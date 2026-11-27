// LevelDoc.cpp : the single document instance and the plan-view wall tests.
//
#include "LevelDoc.h"

LevelDoc& LmDoc()
{
    static LevelDoc doc;
    return doc;
}

namespace {

LmMesh   g_previewMesh;
bool     g_previewValid = false;
unsigned g_previewGen   = 0;

} // namespace

const LmMesh& LmPreviewMesh()
{
    if (!g_previewValid)
    {
        const LevelDoc& doc = LmDoc();
        doc.grid.BuildMesh(g_previewMesh, doc.showCeiling, doc.facing);
        g_previewValid = true;
        ++g_previewGen;
    }
    return g_previewMesh;
}

void LmInvalidatePreviewMesh()
{
    g_previewValid = false;
}

unsigned LmPreviewMeshGeneration()
{
    return g_previewGen;
}

// Both of these are ports of the file-static helpers in the Win32 View2D.cpp,
// unchanged apart from taking unsigned char instead of uint8_t so the header
// stays free of <cstdint> for the Objective-C++ side.

int LmPlanWallH(const LevelGrid& g, const unsigned char* cut, int gw, int x, int y)
{
    const bool above = g.IsFloor(x, y - 1);
    const bool below = g.IsFloor(x, y);
    if (above == below)
        return 0;
    if (!cut)
        return below ? 1 : -1;
    if (below)
        return LmCutEatsN(cut[(size_t)y * gw + x]) ? 0 : 1;
    return LmCutEatsS(cut[(size_t)(y - 1) * gw + x]) ? 0 : -1;
}

int LmPlanWallV(const LevelGrid& g, const unsigned char* cut, int gw, int x, int y)
{
    const bool left  = g.IsFloor(x - 1, y);
    const bool right = g.IsFloor(x, y);
    if (left == right)
        return 0;
    if (!cut)
        return right ? 1 : -1;
    if (right)
        return LmCutEatsW(cut[(size_t)y * gw + x]) ? 0 : 1;
    return LmCutEatsE(cut[(size_t)y * gw + (x - 1)]) ? 0 : -1;
}
