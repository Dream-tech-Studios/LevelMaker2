// View2D.cpp : the top-down plan pane. Hold the left button and drag to trace
// a hall; the drag paints floor cells and the walls fall out of the cell
// boundaries. Right button erases, middle button (or space) pans, wheel zooms.
//
#include "framework.h"
#include "LevelApp.h"

#include <cmath>
#include <cstdio>

namespace {

const wchar_t* kClassName = L"LevelMakerView2D";

const float kMinZoom = 1.5f;
const float kMaxZoom = 48.0f;

struct View2DState
{
    float camX;          // view centre, in cell coordinates
    float camY;
    float zoom;          // pixels per cell

    bool  painting;
    bool  erasing;
    bool  panning;
    int   lastCellX;
    int   lastCellY;

    POINT panAnchor;
    float panCamX;
    float panCamY;

    int   mouseCellX;
    int   mouseCellY;
    bool  hasMouse;

    HDC     memDC;
    HBITMAP memBmp;
    int     memW;
    int     memH;

    View2DState()
        : camX(0.0f), camY(0.0f), zoom(9.0f)
        , painting(false), erasing(false), panning(false)
        , lastCellX(0), lastCellY(0)
        , panCamX(0.0f), panCamY(0.0f)
        , mouseCellX(0), mouseCellY(0), hasMouse(false)
        , memDC(NULL), memBmp(NULL), memW(0), memH(0)
    {
        panAnchor.x = panAnchor.y = 0;
    }
};

View2DState g_v;

inline float Clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void ScreenToCellF(const RECT& rc, int px, int py, float& cx, float& cy)
{
    const float halfW = (float)(rc.right - rc.left) * 0.5f;
    const float halfH = (float)(rc.bottom - rc.top) * 0.5f;
    cx = ((float)px - halfW) / g_v.zoom + g_v.camX;
    cy = ((float)py - halfH) / g_v.zoom + g_v.camY;
}

void CellToScreen(const RECT& rc, float cx, float cy, float& px, float& py)
{
    const float halfW = (float)(rc.right - rc.left) * 0.5f;
    const float halfH = (float)(rc.bottom - rc.top) * 0.5f;
    px = (cx - g_v.camX) * g_v.zoom + halfW;
    py = (cy - g_v.camY) * g_v.zoom + halfH;
}

void ScreenToCell(const RECT& rc, int px, int py, int& cx, int& cy)
{
    float fx = 0.0f, fy = 0.0f;
    ScreenToCellF(rc, px, py, fx, fy);
    cx = (int)floorf(fx);
    cy = (int)floorf(fy);
}

void ReleaseBackBuffer()
{
    if (g_v.memBmp)
    {
        DeleteObject(g_v.memBmp);
        g_v.memBmp = NULL;
    }
    if (g_v.memDC)
    {
        DeleteDC(g_v.memDC);
        g_v.memDC = NULL;
    }
    g_v.memW = g_v.memH = 0;
}

void EnsureBackBuffer(HDC hdc, int w, int h)
{
    if (g_v.memDC && g_v.memW == w && g_v.memH == h)
        return;
    ReleaseBackBuffer();
    g_v.memDC = CreateCompatibleDC(hdc);
    if (!g_v.memDC)
        return;
    g_v.memBmp = CreateCompatibleBitmap(hdc, w, h);
    if (!g_v.memBmp)
    {
        DeleteDC(g_v.memDC);
        g_v.memDC = NULL;
        return;
    }
    SelectObject(g_v.memDC, g_v.memBmp);
    g_v.memW = w;
    g_v.memH = h;
}

void DrawPlan(HDC hdc, const RECT& rc)
{
    const LevelGrid& grid = g_app.grid;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    HBRUSH bg = CreateSolidBrush(RGB(26, 28, 33));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // visible cell window
    float fx0 = 0.0f, fy0 = 0.0f, fx1 = 0.0f, fy1 = 0.0f;
    ScreenToCellF(rc, 0, 0, fx0, fy0);
    ScreenToCellF(rc, w, h, fx1, fy1);
    int cx0 = (int)floorf(fx0) - 1;
    int cy0 = (int)floorf(fy0) - 1;
    int cx1 = (int)ceilf(fx1) + 1;
    int cy1 = (int)ceilf(fy1) + 1;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > grid.Width()) cx1 = grid.Width();
    if (cy1 > grid.Height()) cy1 = grid.Height();

    float sx = 0.0f, sy = 0.0f;

    // level extents
    {
        float ex = 0.0f, ey = 0.0f;
        CellToScreen(rc, 0.0f, 0.0f, sx, sy);
        CellToScreen(rc, (float)grid.Width(), (float)grid.Height(), ex, ey);
        HBRUSH inside = CreateSolidBrush(RGB(33, 36, 42));
        RECT r;
        r.left = (int)sx; r.top = (int)sy; r.right = (int)ex; r.bottom = (int)ey;
        FillRect(hdc, &r, inside);
        DeleteObject(inside);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 75, 86));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    // grid lines: fine lines when zoomed in, 8-cell blocks always
    if (g_v.zoom >= 2.0f)
    {
        HPEN fine = CreatePen(PS_SOLID, 1, RGB(42, 46, 54));
        HPEN major = CreatePen(PS_SOLID, 1, RGB(56, 61, 72));
        HGDIOBJ oldPen = SelectObject(hdc, major);
        const bool drawFine = (g_v.zoom >= 7.0f);

        for (int x = cx0; x <= cx1; ++x)
        {
            const bool isMajor = (x % 8) == 0;
            if (!isMajor && !drawFine)
                continue;
            CellToScreen(rc, (float)x, 0.0f, sx, sy);
            SelectObject(hdc, isMajor ? major : fine);
            float top = 0.0f, bottom = 0.0f, ignore = 0.0f;
            CellToScreen(rc, 0.0f, 0.0f, ignore, top);
            CellToScreen(rc, 0.0f, (float)grid.Height(), ignore, bottom);
            MoveToEx(hdc, (int)sx, (int)top, NULL);
            LineTo(hdc, (int)sx, (int)bottom);
        }
        for (int y = cy0; y <= cy1; ++y)
        {
            const bool isMajor = (y % 8) == 0;
            if (!isMajor && !drawFine)
                continue;
            CellToScreen(rc, 0.0f, (float)y, sx, sy);
            SelectObject(hdc, isMajor ? major : fine);
            float left = 0.0f, right = 0.0f, ignore = 0.0f;
            CellToScreen(rc, 0.0f, 0.0f, left, ignore);
            CellToScreen(rc, (float)grid.Width(), 0.0f, right, ignore);
            MoveToEx(hdc, (int)left, (int)sy, NULL);
            LineTo(hdc, (int)right, (int)sy);
        }
        SelectObject(hdc, oldPen);
        DeleteObject(fine);
        DeleteObject(major);
    }

    // floor: fill runs of cells per row so big halls cost a handful of calls
    HBRUSH floorBrush = CreateSolidBrush(RGB(118, 124, 134));
    for (int y = cy0; y < cy1; ++y)
    {
        int x = cx0;
        while (x < cx1)
        {
            if (!grid.IsFloor(x, y))
            {
                ++x;
                continue;
            }
            const int start = x;
            while (x < cx1 && grid.IsFloor(x, y))
                ++x;
            float px0 = 0.0f, py0 = 0.0f, px1 = 0.0f, py1 = 0.0f;
            CellToScreen(rc, (float)start, (float)y, px0, py0);
            CellToScreen(rc, (float)x, (float)(y + 1), px1, py1);
            RECT r;
            r.left = (int)floorf(px0);
            r.top = (int)floorf(py0);
            r.right = (int)ceilf(px1);
            r.bottom = (int)ceilf(py1);
            FillRect(hdc, &r, floorBrush);
        }
    }
    DeleteObject(floorBrush);

    // walls: every cell boundary where exactly one side is floor
    if (g_v.zoom >= 3.0f)
    {
        const int thickness = (g_v.zoom >= 12.0f) ? 3 : 2;
        HPEN wallPen = CreatePen(PS_SOLID, thickness, RGB(228, 232, 240));
        HGDIOBJ oldPen = SelectObject(hdc, wallPen);

        for (int y = cy0; y <= cy1; ++y)
        {
            int x = cx0;
            while (x <= cx1)
            {
                const bool a = grid.IsFloor(x, y - 1);
                const bool b = grid.IsFloor(x, y);
                if (a == b || x >= cx1)
                {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < cx1 && grid.IsFloor(x, y - 1) == a && grid.IsFloor(x, y) == b)
                    ++x;
                float px0 = 0.0f, py0 = 0.0f, px1 = 0.0f, py1 = 0.0f;
                CellToScreen(rc, (float)start, (float)y, px0, py0);
                CellToScreen(rc, (float)x, (float)y, px1, py1);
                MoveToEx(hdc, (int)px0, (int)py0, NULL);
                LineTo(hdc, (int)px1, (int)py1);
            }
        }
        for (int x = cx0; x <= cx1; ++x)
        {
            int y = cy0;
            while (y <= cy1)
            {
                const bool a = grid.IsFloor(x - 1, y);
                const bool b = grid.IsFloor(x, y);
                if (a == b || y >= cy1)
                {
                    ++y;
                    continue;
                }
                const int start = y;
                while (y < cy1 && grid.IsFloor(x - 1, y) == a && grid.IsFloor(x, y) == b)
                    ++y;
                float px0 = 0.0f, py0 = 0.0f, px1 = 0.0f, py1 = 0.0f;
                CellToScreen(rc, (float)x, (float)start, px0, py0);
                CellToScreen(rc, (float)x, (float)y, px1, py1);
                MoveToEx(hdc, (int)px0, (int)py0, NULL);
                LineTo(hdc, (int)px1, (int)py1);
            }
        }
        SelectObject(hdc, oldPen);
        DeleteObject(wallPen);
    }

    // brush footprint under the cursor
    if (g_v.hasMouse)
    {
        const int brush = g_app.brush;
        const int lo = -(brush - 1) / 2;
        const int hi = brush / 2;
        float px0 = 0.0f, py0 = 0.0f, px1 = 0.0f, py1 = 0.0f;
        CellToScreen(rc, (float)(g_v.mouseCellX + lo), (float)(g_v.mouseCellY + lo), px0, py0);
        CellToScreen(rc, (float)(g_v.mouseCellX + hi + 1), (float)(g_v.mouseCellY + hi + 1), px1, py1);

        HPEN pen = CreatePen(PS_SOLID, 1, g_v.erasing ? RGB(240, 120, 110) : RGB(250, 206, 92));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, (int)px0, (int)py0, (int)px1, (int)py1);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    // HUD
    {
        wchar_t text[160];
        swprintf(text, 160,
                 L"PLAN  \x2022  brush %d cells  \x2022  cell %d, %d  \x2022  %.0f px/cell",
                 g_app.brush, g_v.mouseCellX, g_v.mouseCellY, g_v.zoom);
        HGDIOBJ oldFont = SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(150, 158, 172));
        RECT tr;
        tr.left = 10; tr.top = 8; tr.right = w - 8; tr.bottom = 30;
        DrawTextW(hdc, text, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }
}

void PaintAt(HWND hwnd, int px, int py, bool first, bool floorValue)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int cx = 0, cy = 0;
    ScreenToCell(rc, px, py, cx, cy);

    bool changed;
    if (first)
        changed = g_app.grid.PaintCell(cx, cy, g_app.brush, floorValue);
    else
        changed = g_app.grid.PaintLine(g_v.lastCellX, g_v.lastCellY, cx, cy, g_app.brush, floorValue);

    g_v.lastCellX = cx;
    g_v.lastCellY = cy;

    if (changed)
        App_GridChanged();
    else
        InvalidateRect(hwnd, NULL, FALSE);
}

void EndStroke(HWND hwnd)
{
    if (!g_v.painting && !g_v.erasing && !g_v.panning)
        return;
    if (g_v.painting || g_v.erasing)
        g_app.grid.EndStroke();
    g_v.painting = false;
    g_v.erasing = false;
    g_v.panning = false;
    if (GetCapture() == hwnd)
        ReleaseCapture();
    App_StatusChanged();
    InvalidateRect(hwnd, NULL, FALSE);
}

LRESULT CALLBACK View2DProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;   // the back buffer covers everything

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        if (w > 0 && h > 0)
        {
            EnsureBackBuffer(hdc, w, h);
            if (g_v.memDC)
            {
                DrawPlan(g_v.memDC, rc);
                BitBlt(hdc, 0, 0, w, h, g_v.memDC, 0, 0, SRCCOPY);
            }
            else
            {
                DrawPlan(hdc, rc);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        if (g_v.painting || g_v.erasing || g_v.panning)
            return 0;
        const bool floorValue = (msg == WM_LBUTTONDOWN);
        SetCapture(hwnd);
        g_app.grid.BeginStroke();
        g_v.painting = floorValue;
        g_v.erasing = !floorValue;
        PaintAt(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true, floorValue);
        return 0;
    }

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        EndStroke(hwnd);
        return 0;

    case WM_MBUTTONDOWN:
    {
        if (g_v.painting || g_v.erasing)
            return 0;
        SetCapture(hwnd);
        g_v.panning = true;
        g_v.panAnchor.x = GET_X_LPARAM(lParam);
        g_v.panAnchor.y = GET_Y_LPARAM(lParam);
        g_v.panCamX = g_v.camX;
        g_v.panCamY = g_v.camY;
        return 0;
    }

    case WM_CAPTURECHANGED:
        if ((HWND)lParam != hwnd)
        {
            if (g_v.painting || g_v.erasing)
                g_app.grid.EndStroke();
            g_v.painting = g_v.erasing = g_v.panning = false;
        }
        return 0;

    case WM_MOUSEMOVE:
    {
        const int mx = GET_X_LPARAM(lParam);
        const int my = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        ScreenToCell(rc, mx, my, g_v.mouseCellX, g_v.mouseCellY);
        g_v.hasMouse = true;

        if (g_v.panning)
        {
            g_v.camX = g_v.panCamX - (float)(mx - g_v.panAnchor.x) / g_v.zoom;
            g_v.camY = g_v.panCamY - (float)(my - g_v.panAnchor.y) / g_v.zoom;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (g_v.painting || g_v.erasing)
        {
            PaintAt(hwnd, mx, my, false, g_v.painting);
        }
        else
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        App_StatusChanged();
        return 0;
    }

    case WM_MOUSELEAVE:
        g_v.hasMouse = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);

        float beforeX = 0.0f, beforeY = 0.0f;
        ScreenToCellF(rc, pt.x, pt.y, beforeX, beforeY);

        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const float factor = powf(1.0015f, (float)delta);
        g_v.zoom = Clampf(g_v.zoom * factor, kMinZoom, kMaxZoom);

        float afterX = 0.0f, afterY = 0.0f;
        ScreenToCellF(rc, pt.x, pt.y, afterX, afterY);
        g_v.camX += beforeX - afterX;
        g_v.camY += beforeY - afterY;

        ScreenToCell(rc, pt.x, pt.y, g_v.mouseCellX, g_v.mouseCellY);
        InvalidateRect(hwnd, NULL, FALSE);
        App_StatusChanged();
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_CROSS));
        return TRUE;

    case WM_DESTROY:
        ReleaseBackBuffer();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool View2D_Register(HINSTANCE hInstance)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = View2DProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

HWND View2D_Create(HWND parent, HINSTANCE hInstance)
{
    return CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           0, 0, 10, 10, parent, NULL, hInstance, NULL);
}

void View2D_FitToLevel(HWND hwnd)
{
    const LevelGrid& grid = g_app.grid;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!grid.FloorBounds(x0, y0, x1, y1))
    {
        x0 = 0; y0 = 0; x1 = grid.Width() - 1; y1 = grid.Height() - 1;
    }
    else
    {
        x0 -= 3; y0 -= 3; x1 += 3; y1 += 3;
    }

    g_v.camX = (float)(x0 + x1 + 1) * 0.5f;
    g_v.camY = (float)(y0 + y1 + 1) * 0.5f;

    RECT rc;
    GetClientRect(hwnd, &rc);
    const float w = (float)(rc.right - rc.left);
    const float h = (float)(rc.bottom - rc.top);
    const float cellsW = (float)(x1 - x0 + 1);
    const float cellsH = (float)(y1 - y0 + 1);
    if (w > 10.0f && h > 10.0f && cellsW > 0.0f && cellsH > 0.0f)
    {
        const float zx = w / cellsW;
        const float zy = h / cellsH;
        g_v.zoom = Clampf((zx < zy) ? zx : zy, kMinZoom, kMaxZoom);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}
