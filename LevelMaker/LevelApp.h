// LevelApp.h : state shared between the main frame and the two view panes.
//
// There is exactly one document and one of each view, so the state lives in a
// single global rather than being threaded through window user data.
//
#pragma once

#include "framework.h"
#include "LevelGrid.h"

#include <string>

struct AppState
{
    LevelGrid    grid;
    int          brush;        // stamp width in cells
    bool         straightMode; // strokes are locked to one direction
    int          drawAngle;    // that direction, in 45 degree steps
    LmFacing     facing;       // which way generated surfaces look
    bool         showCeiling;  // ceilings in the preview and in exports
    bool         showWire;     // draw quad outlines in the 3D preview
    bool         cutaway;      // cull back faces, so you can see into the halls
    bool         modified;
    std::wstring filePath;

    HWND hMain;
    HWND hView2D;
    HWND hView3D;

    AppState()
        : brush(3)
        , straightMode(false)
        , drawAngle(0)
        , facing(LM_FACE_INWARD)
        , showCeiling(false)
        , showWire(true)
        , cutaway(false)   // off by default: the blockout reads as solid
        , modified(false)
        , hMain(NULL)
        , hView2D(NULL)
        , hView3D(NULL)
    {
    }
};

extern AppState g_app;

// plan view (GDI)
bool View2D_Register(HINSTANCE hInstance);
HWND View2D_Create(HWND parent, HINSTANCE hInstance);
void View2D_FitToLevel(HWND hwnd);
void View2D_AngleChanged(HWND hwnd);

// 3D preview (OpenGL)
bool View3D_Register(HINSTANCE hInstance);
HWND View3D_Create(HWND parent, HINSTANCE hInstance);
void View3D_ResetCamera(HWND hwnd);
void View3D_InvalidateMesh(HWND hwnd);

// called by the views after they change the grid
void App_GridChanged();
void App_StatusChanged();
