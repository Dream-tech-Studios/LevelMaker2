// LevelDoc.h : the editor's document state, with no platform types in it.
//
// This replaces LevelApp.h's AppState. The Win32 build kept three HWNDs in
// that struct; here the views own themselves and the document is just data,
// so the same struct serves UIKit without changes.
//
#pragma once

#include "LevelGrid.h"

#include <string>

struct LevelDoc
{
    LevelGrid    grid;
    int          brush;         // stamp width in cells
    bool         straightMode;  // strokes are locked to one direction
    int          drawAngle;     // that direction, in 45 degree steps
    LmFacing     facing;        // which way generated surfaces look
    bool         showCeiling;   // ceilings in the preview and in exports
    bool         showWire;      // draw quad outlines in the 3D preview
    bool         cutaway;       // cull back faces, so you can see into halls
    bool         modified;
    std::string  filePath;      // empty for an unsaved level

    // --- editor preferences ----------------------------------------------
    // The Win32 build had no need for these: a mouse has a second button and
    // always reports a position. They live here, beside the other view
    // options, so one change notification updates every pane at once.
    bool         eraser;            // strokes clear floor instead of setting it
    bool         fingerDraws;       // one finger paints rather than panning
    bool         pressureSensitive; // Pencil force scales the stamp width

    LevelDoc()
        : brush(3)
        , straightMode(false)
        , drawAngle(0)
        , facing(LM_FACE_INWARD)
        , showCeiling(false)
        , showWire(true)
        , cutaway(false)
        , modified(false)
        , eraser(false)
        , fingerDraws(false)
        , pressureSensitive(false)
    {
    }
};

// One document per process, as on Windows.
LevelDoc& LmDoc();

// --- cached preview mesh --------------------------------------------------
// Meshing a large level is not free, and both the Metal view and the status
// line want the result, so it is built once and held until something that
// affects it changes. Anything that edits the grid, or that touches
// showCeiling or facing, must call LmInvalidatePreviewMesh().
const LmMesh& LmPreviewMesh();
void          LmInvalidatePreviewMesh();
// Bumped on every rebuild, so a renderer can tell whether its vertex buffer
// is still current without comparing contents.
unsigned      LmPreviewMeshGeneration();

// --- plan-view wall edges -------------------------------------------------
// Which side of a cell boundary is floor, accounting for chamfers: +1 / -1
// for a wall facing one way or the other, 0 for no wall. Lifted out of
// View2D.cpp so the UIKit plan view can draw exactly what the mesh builds.
// cut may be null when diagonal smoothing is off.
int LmPlanWallH(const LevelGrid& g, const unsigned char* cut, int gw, int x, int y);
int LmPlanWallV(const LevelGrid& g, const unsigned char* cut, int gw, int x, int y);
