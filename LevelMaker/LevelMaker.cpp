// LevelMaker.cpp : main frame -- menus, layout, file handling.
//
// Left pane: the plan view you trace halls in. Right pane: a live low-poly
// 3D preview of the same level. Nothing outside the Windows SDK is used.
//
#include "framework.h"
#include "LevelMaker.h"
#include "LevelApp.h"

#include <cstdio>
#include <string>

#define MAX_LOADSTRING 100

AppState g_app;

namespace {

const int kStripHeight = 30;
const int kSplitWidth  = 6;
const int kMinPaneWidth = 140;

HINSTANCE g_hInst = NULL;
WCHAR     g_title[MAX_LOADSTRING];
WCHAR     g_windowClass[MAX_LOADSTRING];

float g_splitFrac = 0.52f;
bool  g_draggingSplit = false;

struct Stats
{
    int    cells;
    size_t quads;
    size_t tris;
};
Stats g_stats = { 0, 0, 0 };

const float kWallHeights[] = { 2.0f, 2.5f, 3.0f, 4.0f, 6.0f };
const float kCellSizes[]   = { 0.5f, 1.0f, 2.0f, 4.0f };
const int   kWallHeightCount = (int)(sizeof(kWallHeights) / sizeof(kWallHeights[0]));
const int   kCellSizeCount   = (int)(sizeof(kCellSizes) / sizeof(kCellSizes[0]));

const wchar_t kLevelFilter[] = L"LevelMaker level (*.lvl)\0*.lvl\0All files (*.*)\0*.*\0";
const wchar_t kObjFilter[]   = L"Wavefront OBJ (*.obj)\0*.obj\0All files (*.*)\0*.*\0";
const wchar_t kFbxFilter[]   = L"ASCII FBX (*.fbx)\0*.fbx\0All files (*.*)\0*.*\0";

// ---------------------------------------------------------------- helpers

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

std::wstring SwapExtension(const std::wstring& path, const wchar_t* ext)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
        return path.substr(0, dot) + ext;
    return path + ext;
}

int SplitPosition(const RECT& client)
{
    const int w = client.right - client.left;
    int x = (int)((float)w * g_splitFrac);
    const int maxX = w - kMinPaneWidth - kSplitWidth;
    if (maxX < kMinPaneWidth)
        return w / 2;
    if (x < kMinPaneWidth) x = kMinPaneWidth;
    if (x > maxX) x = maxX;
    return x;
}

void StripRect(HWND hwnd, RECT& out)
{
    GetClientRect(hwnd, &out);
    out.top = out.bottom - kStripHeight;
    if (out.top < 0)
        out.top = 0;
}

void InvalidateStrip(HWND hwnd)
{
    RECT r;
    StripRect(hwnd, r);
    InvalidateRect(hwnd, &r, FALSE);
}

void LayoutChildren(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int h = rc.bottom - rc.top - kStripHeight;
    if (h < 1) h = 1;
    const int w = rc.right - rc.left;
    const int splitX = SplitPosition(rc);

    if (g_app.hView2D)
        MoveWindow(g_app.hView2D, 0, 0, splitX, h, TRUE);
    if (g_app.hView3D)
    {
        int right = w - splitX - kSplitWidth;
        if (right < 1) right = 1;
        MoveWindow(g_app.hView3D, splitX + kSplitWidth, 0, right, h, TRUE);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

void UpdateTitle()
{
    std::wstring t = L"LevelMaker  \x2014  ";
    t += g_app.filePath.empty() ? std::wstring(L"untitled.lvl") : FileNameOf(g_app.filePath);
    if (g_app.modified)
        t += L" *";
    if (g_app.hMain)
        SetWindowTextW(g_app.hMain, t.c_str());
}

void UpdateStats()
{
    LmMesh mesh;
    g_app.grid.BuildMesh(mesh, g_app.showCeiling, g_app.facing);
    g_stats.cells = g_app.grid.FloorCount();
    g_stats.quads = mesh.quads.size();
    g_stats.tris = mesh.TriangleCount();
}

void RefreshAll(bool rebuildMesh)
{
    UpdateStats();
    if (g_app.hView2D)
        InvalidateRect(g_app.hView2D, NULL, FALSE);
    if (g_app.hView3D)
    {
        if (rebuildMesh)
            View3D_InvalidateMesh(g_app.hView3D);
        else
            InvalidateRect(g_app.hView3D, NULL, FALSE);
    }
    if (g_app.hMain)
        InvalidateStrip(g_app.hMain);
    UpdateTitle();
}

void SetBrush(int n)
{
    if (n < 1) n = 1;
    if (n > 24) n = 24;
    if (n == g_app.brush)
        return;
    g_app.brush = n;
    if (g_app.hView2D)
        InvalidateRect(g_app.hView2D, NULL, FALSE);
    if (g_app.hMain)
        InvalidateStrip(g_app.hMain);
}

void SetAngle(int degrees)
{
    int a = degrees % 360;
    if (a < 0)
        a += 360;
    g_app.drawAngle = a;
    if (g_app.hView2D)
        View2D_AngleChanged(g_app.hView2D);
    if (g_app.hMain)
        InvalidateStrip(g_app.hMain);
}

void DrawStrip(HWND hwnd, HDC hdc)
{
    RECT rc;
    StripRect(hwnd, rc);

    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(50, 54, 62));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, rc.top, NULL);
    LineTo(hdc, rc.right, rc.top);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    HGDIOBJ oldFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);

    RECT tr = rc;
    tr.left += 10;
    tr.top += 7;
    SetTextColor(hdc, RGB(150, 158, 172));
    const wchar_t* help =
        g_app.straightMode
            ? L"Hold LEFT to lay a straight hall  \x2022  A / D turn 45\x00B0  \x2022  S back to freehand"
              L"  \x2022  RIGHT erases  \x2022  [ ] width"
            : L"Hold LEFT to trace a hall  \x2022  Shift = straight  \x2022  S locks an angle"
              L"  \x2022  RIGHT erases  \x2022  MIDDLE pans  \x2022  [ ] width";
    DrawTextW(hdc, help, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    static const wchar_t* kFacingName[] = { L"inward", L"outward", L"both sides" };
    wchar_t stats[200];
    swprintf(stats, 200,
             L"hall %d  \x2022  faces %s  \x2022  %s  \x2022  %d cells  \x2022  %u faces"
             L"  \x2022  %u tris",
             g_app.brush, kFacingName[(int)g_app.facing],
             g_app.grid.SmoothDiagonals() ? L"smooth 45\x00B0" : L"stepped 45\x00B0",
             g_stats.cells, (unsigned)g_stats.quads, (unsigned)g_stats.tris);
    RECT sr = rc;
    sr.right -= 12;
    sr.top += 7;
    SetTextColor(hdc, RGB(188, 196, 210));
    DrawTextW(hdc, stats, -1, &sr, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, oldFont);
}

void DrawSplitter(HWND hwnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int splitX = SplitPosition(rc);
    RECT band;
    band.left = splitX;
    band.right = splitX + kSplitWidth;
    band.top = 0;
    band.bottom = rc.bottom - kStripHeight;
    if (band.bottom < band.top)
        band.bottom = band.top;

    HBRUSH b = CreateSolidBrush(RGB(38, 41, 48));
    FillRect(hdc, &band, b);
    DeleteObject(b);
}

bool OverSplitter(HWND hwnd, int x, int y)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int splitX = SplitPosition(rc);
    return x >= splitX - 1 && x <= splitX + kSplitWidth + 1 && y < rc.bottom - kStripHeight;
}

// ------------------------------------------------------------- file dialogs

bool PromptForPath(HWND owner, bool save, const wchar_t* filter, const wchar_t* defExt,
                   const wchar_t* title, std::wstring& path)
{
    wchar_t buf[MAX_PATH];
    buf[0] = 0;
    if (!path.empty())
        lstrcpynW(buf, path.c_str(), MAX_PATH);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.Flags |= save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST;

    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok)
        return false;
    path = buf;
    return true;
}

bool DoSaveAs(HWND hwnd)
{
    std::wstring path = g_app.filePath;
    if (!PromptForPath(hwnd, true, kLevelFilter, L"lvl", L"Save Level", path))
        return false;
    if (!g_app.grid.Save(path.c_str()))
    {
        MessageBoxW(hwnd, L"Could not write that file.", L"LevelMaker", MB_OK | MB_ICONERROR);
        return false;
    }
    g_app.filePath = path;
    g_app.modified = false;
    UpdateTitle();
    return true;
}

bool DoSave(HWND hwnd)
{
    if (g_app.filePath.empty())
        return DoSaveAs(hwnd);
    if (!g_app.grid.Save(g_app.filePath.c_str()))
    {
        MessageBoxW(hwnd, L"Could not write that file.", L"LevelMaker", MB_OK | MB_ICONERROR);
        return false;
    }
    g_app.modified = false;
    UpdateTitle();
    return true;
}

bool ConfirmDiscard(HWND hwnd)
{
    if (!g_app.modified)
        return true;
    const int r = MessageBoxW(hwnd, L"Save changes to the current level?", L"LevelMaker",
                              MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL)
        return false;
    if (r == IDYES)
        return DoSave(hwnd);
    return true;
}

void DoNew(HWND hwnd)
{
    if (!ConfirmDiscard(hwnd))
        return;
    g_app.grid.Reset(LevelGrid::kDefaultSize, LevelGrid::kDefaultSize);
    g_app.filePath.clear();
    g_app.modified = false;
    if (g_app.hView2D)
        View2D_FitToLevel(g_app.hView2D);
    if (g_app.hView3D)
        View3D_ResetCamera(g_app.hView3D);
    RefreshAll(true);
}

void DoOpen(HWND hwnd)
{
    if (!ConfirmDiscard(hwnd))
        return;
    std::wstring path;
    if (!PromptForPath(hwnd, false, kLevelFilter, L"lvl", L"Open Level", path))
        return;
    if (!g_app.grid.Load(path.c_str()))
    {
        MessageBoxW(hwnd, L"That file is not a LevelMaker level, or it could not be read.",
                    L"LevelMaker", MB_OK | MB_ICONERROR);
        return;
    }
    g_app.filePath = path;
    g_app.modified = false;
    if (g_app.hView2D)
        View2D_FitToLevel(g_app.hView2D);
    if (g_app.hView3D)
        View3D_ResetCamera(g_app.hView3D);
    RefreshAll(true);
}

void DoExportObj(HWND hwnd)
{
    if (g_app.grid.FloorCount() == 0)
    {
        MessageBoxW(hwnd, L"Nothing to export yet -- trace a hall in the plan view first.",
                    L"LevelMaker", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring path = g_app.filePath.empty() ? std::wstring(L"level.obj")
                                               : SwapExtension(g_app.filePath, L".obj");
    if (!PromptForPath(hwnd, true, kObjFilter, L"obj", L"Export OBJ", path))
        return;
    if (!g_app.grid.ExportObj(path.c_str(), g_app.showCeiling, g_app.facing))
    {
        MessageBoxW(hwnd, L"Could not write that OBJ file.", L"LevelMaker", MB_OK | MB_ICONERROR);
        return;
    }

    wchar_t msg[512];
    swprintf(msg, 512,
             L"Exported %u quads (%u triangles) to\n%s\n\nA matching .mtl file was written "
             L"alongside it.",
             (unsigned)g_stats.quads, (unsigned)g_stats.tris, FileNameOf(path).c_str());
    MessageBoxW(hwnd, msg, L"LevelMaker", MB_OK | MB_ICONINFORMATION);
}

void DoExportFbx(HWND hwnd)
{
    if (g_app.grid.FloorCount() == 0)
    {
        MessageBoxW(hwnd, L"Nothing to export yet -- trace a hall in the plan view first.",
                    L"LevelMaker", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring path = g_app.filePath.empty() ? std::wstring(L"level.fbx")
                                               : SwapExtension(g_app.filePath, L".fbx");
    if (!PromptForPath(hwnd, true, kFbxFilter, L"fbx", L"Export ASCII FBX", path))
        return;
    if (!g_app.grid.ExportFbxAscii(path.c_str(), g_app.showCeiling, g_app.facing))
    {
        MessageBoxW(hwnd, L"Could not write that FBX file.", L"LevelMaker", MB_OK | MB_ICONERROR);
        return;
    }

    wchar_t msg[512];
    swprintf(msg, 512,
             L"Exported %u quads (%u triangles) to\n%s\n\nASCII FBX 7.4, one mesh with "
             L"floor, wall and ceiling materials.",
             (unsigned)g_stats.quads, (unsigned)g_stats.tris, FileNameOf(path).c_str());
    MessageBoxW(hwnd, msg, L"LevelMaker", MB_OK | MB_ICONINFORMATION);
}

void ShowControls(HWND hwnd)
{
    MessageBoxW(hwnd,
        L"PLAN VIEW (left)\n"
        L"    Hold left button and drag  -  trace a hall\n"
        L"    Hold right button              -  erase\n"
        L"    Middle button drag            -  pan\n"
        L"    Mouse wheel                    -  zoom\n"
        L"    [ and ]                             -  narrower / wider hall\n"
        L"    1 - 9                                 -  set hall width directly\n"
        L"    F                                        -  fit the view to the level\n\n"
        L"STRAIGHT HALLS\n"
        L"    S                 -  lock drawing to one direction\n"
        L"    A / D            -  turn that direction 45 degrees left / right\n"
        L"    Shift + drag  -  one straight hall, angle snapped to the drag\n"
        L"    B                 -  smooth diagonals: a 45 degree hall gets flat\n"
        L"                            walls instead of a staircase of cells\n\n"
        L"3D PREVIEW (right)\n"
        L"    Left drag    -  orbit\n"
        L"    Right drag  -  pan\n"
        L"    Wheel         -  zoom\n"
        L"    R                 -  reset the camera\n"
        L"    C / W / X   -  ceilings / quad outlines / cutaway\n\n"
        L"Ctrl+Z undo, Ctrl+Y redo, Ctrl+S save.\n"
        L"Ctrl+E exports OBJ, Ctrl+F exports ASCII FBX.\n"
        L"Settings > Wall Faces picks inward, outward or both sides.",
        L"LevelMaker Controls", MB_OK | MB_ICONINFORMATION);
}

void SyncMenu(HWND hwnd)
{
    HMENU menu = GetMenu(hwnd);
    if (!menu)
        return;

    EnableMenuItem(menu, IDM_UNDO,
                   MF_BYCOMMAND | (g_app.grid.CanUndo() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, IDM_REDO,
                   MF_BYCOMMAND | (g_app.grid.CanRedo() ? MF_ENABLED : MF_GRAYED));

    CheckMenuItem(menu, IDM_CEILING,
                  MF_BYCOMMAND | (g_app.showCeiling ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_WIREFRAME,
                  MF_BYCOMMAND | (g_app.showWire ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_CUTAWAY,
                  MF_BYCOMMAND | (g_app.cutaway ? MF_CHECKED : MF_UNCHECKED));

    static const int kBrushPresets[] = { 1, 2, 3, 4, 6, 8 };
    for (int i = 0; i < (int)(sizeof(kBrushPresets) / sizeof(kBrushPresets[0])); ++i)
    {
        const int id = IDM_BRUSH_SIZE_BASE + kBrushPresets[i];
        CheckMenuItem(menu, id,
                      MF_BYCOMMAND | ((g_app.brush == kBrushPresets[i]) ? MF_CHECKED : MF_UNCHECKED));
    }

    CheckMenuItem(menu, IDM_STRAIGHT,
                  MF_BYCOMMAND | (g_app.straightMode ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDM_SMOOTHDIAG,
                  MF_BYCOMMAND | (g_app.grid.SmoothDiagonals() ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuRadioItem(menu, IDM_FACE_INWARD, IDM_FACE_BOTH,
                       IDM_FACE_INWARD + (int)g_app.facing, MF_BYCOMMAND);
    CheckMenuRadioItem(menu, IDM_ANGLE_BASE, IDM_ANGLE_BASE + 7,
                       IDM_ANGLE_BASE + (g_app.drawAngle / 45) % 8, MF_BYCOMMAND);

    for (int i = 0; i < kWallHeightCount; ++i)
    {
        const bool on = (g_app.grid.WallHeight() == kWallHeights[i]);
        CheckMenuItem(menu, IDM_WALLH_BASE + i, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    }
    for (int i = 0; i < kCellSizeCount; ++i)
    {
        const bool on = (g_app.grid.CellSize() == kCellSizes[i]);
        CheckMenuItem(menu, IDM_CELLSZ_BASE + i, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    }
}

bool HandleCommand(HWND hwnd, int id)
{
    if (id > IDM_BRUSH_SIZE_BASE && id <= IDM_BRUSH_SIZE_BASE + 24)
    {
        SetBrush(id - IDM_BRUSH_SIZE_BASE);
        return true;
    }
    if (id >= IDM_ANGLE_BASE && id < IDM_ANGLE_BASE + 8)
    {
        SetAngle((id - IDM_ANGLE_BASE) * 45);
        return true;
    }
    if (id >= IDM_WALLH_BASE && id < IDM_WALLH_BASE + kWallHeightCount)
    {
        g_app.grid.SetWallHeight(kWallHeights[id - IDM_WALLH_BASE]);
        g_app.modified = true;
        if (g_app.hView3D)
            View3D_ResetCamera(g_app.hView3D);
        RefreshAll(true);
        return true;
    }
    if (id >= IDM_CELLSZ_BASE && id < IDM_CELLSZ_BASE + kCellSizeCount)
    {
        g_app.grid.SetCellSize(kCellSizes[id - IDM_CELLSZ_BASE]);
        g_app.modified = true;
        if (g_app.hView3D)
            View3D_ResetCamera(g_app.hView3D);
        RefreshAll(true);
        return true;
    }

    switch (id)
    {
    case IDM_NEW:         DoNew(hwnd); return true;
    case IDM_OPEN:        DoOpen(hwnd); return true;
    case IDM_SAVE:        DoSave(hwnd); return true;
    case IDM_SAVEAS:      DoSaveAs(hwnd); return true;
    case IDM_EXPORT_OBJ:  DoExportObj(hwnd); return true;
    case IDM_EXPORT_FBX:  DoExportFbx(hwnd); return true;
    case IDM_EXIT:        PostMessageW(hwnd, WM_CLOSE, 0, 0); return true;

    case IDM_UNDO:
        if (g_app.grid.Undo())
        {
            g_app.modified = true;
            RefreshAll(true);
        }
        return true;

    case IDM_REDO:
        if (g_app.grid.Redo())
        {
            g_app.modified = true;
            RefreshAll(true);
        }
        return true;

    case IDM_CLEAR:
        if (g_app.grid.FloorCount() > 0 &&
            MessageBoxW(hwnd, L"Clear the whole level?", L"LevelMaker",
                        MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
        {
            g_app.grid.Clear();
            g_app.modified = true;
            RefreshAll(true);
        }
        return true;

    case IDM_FITVIEW:
        if (g_app.hView2D)
            View2D_FitToLevel(g_app.hView2D);
        return true;

    case IDM_RESETCAM:
        if (g_app.hView3D)
            View3D_ResetCamera(g_app.hView3D);
        return true;

    case IDM_CEILING:
        g_app.showCeiling = !g_app.showCeiling;
        RefreshAll(true);
        return true;

    case IDM_WIREFRAME:
        g_app.showWire = !g_app.showWire;
        RefreshAll(false);
        return true;

    case IDM_CUTAWAY:
        g_app.cutaway = !g_app.cutaway;
        RefreshAll(false);
        return true;

    case IDM_BRUSH_DEC:   SetBrush(g_app.brush - 1); return true;
    case IDM_BRUSH_INC:   SetBrush(g_app.brush + 1); return true;

    case IDM_STRAIGHT:
        g_app.straightMode = !g_app.straightMode;
        if (g_app.hView2D)
            InvalidateRect(g_app.hView2D, NULL, FALSE);
        InvalidateStrip(hwnd);
        return true;

    case IDM_SMOOTHDIAG:
        g_app.grid.SetSmoothDiagonals(!g_app.grid.SmoothDiagonals());
        g_app.modified = true;
        RefreshAll(true);
        return true;

    case IDM_ROT_LEFT:    SetAngle(g_app.drawAngle + 45); return true;
    case IDM_ROT_RIGHT:   SetAngle(g_app.drawAngle - 45); return true;

    case IDM_FACE_INWARD:
    case IDM_FACE_OUTWARD:
    case IDM_FACE_BOTH:
        g_app.facing = (LmFacing)(id - IDM_FACE_INWARD);
        RefreshAll(true);
        return true;

    case IDM_HELP_KEYS:   ShowControls(hwnd); return true;

    default:
        break;
    }
    return false;
}

void HandleKey(HWND hwnd, int vk)
{
    if (GetKeyState(VK_CONTROL) & 0x8000)
        return;                                  // Ctrl shortcuts come in as accelerators

    if (vk >= '1' && vk <= '9')
    {
        SetBrush(vk - '0');
        return;
    }

    switch (vk)
    {
    case VK_OEM_4: SetBrush(g_app.brush - 1); break;   // [
    case VK_OEM_6: SetBrush(g_app.brush + 1); break;   // ]
    case 'A':      HandleCommand(hwnd, IDM_ROT_LEFT); break;
    case 'D':      HandleCommand(hwnd, IDM_ROT_RIGHT); break;
    case 'S':      HandleCommand(hwnd, IDM_STRAIGHT); break;
    case 'F':      HandleCommand(hwnd, IDM_FITVIEW); break;
    case 'R':      HandleCommand(hwnd, IDM_RESETCAM); break;
    case 'C':      HandleCommand(hwnd, IDM_CEILING); break;
    case 'W':      HandleCommand(hwnd, IDM_WIREFRAME); break;
    case 'X':      HandleCommand(hwnd, IDM_CUTAWAY); break;
    case 'B':      HandleCommand(hwnd, IDM_SMOOTHDIAG); break;
    default:       break;
    }
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        g_app.hMain = hwnd;
        g_app.hView2D = View2D_Create(hwnd, g_hInst);
        g_app.hView3D = View3D_Create(hwnd, g_hInst);
        LayoutChildren(hwnd);
        if (g_app.hView2D)
            View2D_FitToLevel(g_app.hView2D);
        if (g_app.hView3D)
            View3D_ResetCamera(g_app.hView3D);
        UpdateStats();
        UpdateTitle();
        return 0;

    case WM_SIZE:
        LayoutChildren(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 700;
        mmi->ptMinTrackSize.y = 460;
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DrawSplitter(hwnd, hdc);
        DrawStrip(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_INITMENU:
        SyncMenu(hwnd);
        return 0;

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        if (id == IDM_ABOUT)
        {
            DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_ABOUTBOX), hwnd, About);
            return 0;
        }
        if (HandleCommand(hwnd, id))
            return 0;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    case WM_KEYDOWN:
        HandleKey(hwnd, (int)wParam);
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);          // screen coordinates

        HWND targets[2];
        targets[0] = g_app.hView2D;
        targets[1] = g_app.hView3D;
        for (int i = 0; i < 2; ++i)
        {
            if (!targets[i])
                continue;
            RECT r;
            GetWindowRect(targets[i], &r);
            if (PtInRect(&r, pt))
                return SendMessageW(targets[i], WM_MOUSEWHEEL, wParam, lParam);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (OverSplitter(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)))
        {
            SetCapture(hwnd);
            g_draggingSplit = true;
        }
        return 0;

    case WM_MOUSEMOVE:
        if (g_draggingSplit)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int w = rc.right - rc.left;
            if (w > 0)
            {
                float frac = (float)GET_X_LPARAM(lParam) / (float)w;
                if (frac < 0.12f) frac = 0.12f;
                if (frac > 0.88f) frac = 0.88f;
                g_splitFrac = frac;
                LayoutChildren(hwnd);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_draggingSplit)
        {
            g_draggingSplit = false;
            if (GetCapture() == hwnd)
                ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        g_draggingSplit = false;
        return 0;

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        if (g_draggingSplit || OverSplitter(hwnd, pt.x, pt.y))
        {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    case WM_CLOSE:
        if (!ConfirmDiscard(hwnd))
            return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;
    ZeroMemory(&wcex, sizeof(wcex));
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_LEVELMAKER));
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = CreateSolidBrush(RGB(18, 20, 24));
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_LEVELMAKER);
    wcex.lpszClassName = g_windowClass;
    wcex.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_SMALL));
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_hInst = hInstance;

    // The menu lives in the resources under a numeric id, so load it here and
    // hand it to the window directly rather than looking it up by name.
    HMENU hMenu = LoadMenuW(hInstance, MAKEINTRESOURCEW(IDC_LEVELMAKER));
    if (!hMenu)
    {
        MessageBoxW(NULL, L"The menu bar resource could not be loaded.", L"LevelMaker",
                    MB_OK | MB_ICONWARNING);
    }

    HWND hwnd = CreateWindowExW(0, g_windowClass, g_title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, 0, 1280, 800, NULL, hMenu, hInstance, NULL);
    if (!hwnd)
    {
        if (hMenu)
            DestroyMenu(hMenu);
        return FALSE;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetFocus(hwnd);
    return TRUE;
}

} // namespace

void App_GridChanged()
{
    g_app.modified = true;
    RefreshAll(true);
}

void App_StatusChanged()
{
    if (g_app.hMain)
        InvalidateStrip(g_app.hMain);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR    lpCmdLine,
                      _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    SetProcessDPIAware();

    LoadStringW(hInstance, IDS_APP_TITLE, g_title, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_LEVELMAKER, g_windowClass, MAX_LOADSTRING);

    if (!MyRegisterClass(hInstance))
        return FALSE;
    if (!View2D_Register(hInstance) || !View3D_Register(hInstance))
    {
        MessageBoxW(NULL, L"Could not register the view windows.", L"LevelMaker",
                    MB_OK | MB_ICONERROR);
        return FALSE;
    }

    if (!InitInstance(hInstance, nCmdShow))
        return FALSE;

    // a level passed on the command line opens straight away
    if (lpCmdLine && lpCmdLine[0])
    {
        std::wstring path(lpCmdLine);
        if (path.size() >= 2 && path[0] == L'"' && path[path.size() - 1] == L'"')
            path = path.substr(1, path.size() - 2);
        if (!path.empty() && g_app.grid.Load(path.c_str()))
        {
            g_app.filePath = path;
            g_app.modified = false;
            if (g_app.hView2D)
                View2D_FitToLevel(g_app.hView2D);
            if (g_app.hView3D)
                View3D_ResetCamera(g_app.hView3D);
            RefreshAll(true);
        }
    }

    HACCEL hAccelTable = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDC_LEVELMAKER));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!TranslateAcceleratorW(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return (int)msg.wParam;
}
