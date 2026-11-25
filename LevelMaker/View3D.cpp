// View3D.cpp : the 3D preview pane. Plain OpenGL 1.1 out of the Windows SDK --
// no extension loader, no shaders, nothing to install. Flat-shaded quads with
// optional outlines, which is what a low-poly blockout should look like.
//
#include "framework.h"
#include "LevelApp.h"

#include <GL/gl.h>
#include <cmath>

#pragma comment(lib, "opengl32.lib")

namespace {

const wchar_t* kClassName = L"LevelMakerView3D";

const float kPi = 3.14159265358979f;

struct View3DState
{
    HDC    dc;
    HGLRC  rc;

    float  yaw;        // degrees around +Y
    float  pitch;      // degrees above the floor
    float  dist;
    float  tx, ty, tz; // orbit target, world units

    bool   orbiting;
    bool   panning;
    POINT  last;

    LmMesh mesh;
    bool   meshValid;
    bool   framed;     // camera has been framed on real geometry at least once

    View3DState()
        : dc(NULL), rc(NULL)
        , yaw(-35.0f), pitch(34.0f), dist(160.0f)
        , tx(0.0f), ty(0.0f), tz(0.0f)
        , orbiting(false), panning(false)
        , meshValid(false), framed(false)
    {
        last.x = last.y = 0;
    }
};

View3DState g_v;

inline float Clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float Deg2Rad(float d)
{
    return d * kPi / 180.0f;
}

void FrameLevel(bool force)
{
    const LevelGrid& grid = g_app.grid;
    const float cs = grid.CellSize();

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const bool any = grid.FloorBounds(x0, y0, x1, y1);
    if (!any)
    {
        if (!force)
            return;
        x0 = 0; y0 = 0; x1 = grid.Width() - 1; y1 = grid.Height() - 1;
    }

    const float cx = (float)(x0 + x1 + 1) * 0.5f * cs;
    const float cz = (float)(y0 + y1 + 1) * 0.5f * cs;
    const float spanX = (float)(x1 - x0 + 1) * cs;
    const float spanZ = (float)(y1 - y0 + 1) * cs;
    const float span = (spanX > spanZ) ? spanX : spanZ;

    g_v.tx = cx;
    g_v.ty = grid.WallHeight() * 0.5f;
    g_v.tz = cz;
    g_v.dist = Clampf(span * 1.05f + 8.0f, 8.0f, 6000.0f);
    if (any)
        g_v.framed = true;
}

bool MakeCurrent(HWND hwnd)
{
    if (!g_v.dc)
        g_v.dc = GetDC(hwnd);
    if (!g_v.dc || !g_v.rc)
        return false;
    return wglMakeCurrent(g_v.dc, g_v.rc) != FALSE;
}

void EnsureMesh()
{
    if (g_v.meshValid)
        return;
    g_app.grid.BuildMesh(g_v.mesh, g_app.showCeiling, g_app.facing);
    g_v.meshValid = true;
    if (!g_v.framed)
        FrameLevel(false);
}

void ShadeColor(const LmVec3& n, int mat, float out[3])
{
    static const float kBase[LM_MAT_COUNT][3] = {
        { 0.55f, 0.57f, 0.61f },   // floor
        { 0.82f, 0.79f, 0.73f },   // wall
        { 0.34f, 0.35f, 0.40f }    // ceiling
    };
    const float lx = 0.43f, ly = 0.78f, lz = 0.46f;   // already unit length
    float d = n.x * lx + n.y * ly + n.z * lz;
    if (d < 0.0f)
        d = -d * 0.35f;                               // soft bounce on back faces
    const float lit = 0.38f + 0.62f * d;
    const int m = (mat >= 0 && mat < LM_MAT_COUNT) ? mat : 0;
    for (int i = 0; i < 3; ++i)
        out[i] = kBase[m][i] * lit;
}

void DrawFloorGrid()
{
    const LevelGrid& grid = g_app.grid;
    const float cs = grid.CellSize();
    const float w = (float)grid.Width() * cs;
    const float h = (float)grid.Height() * cs;
    const float step = cs * 8.0f;

    glColor3f(0.20f, 0.22f, 0.26f);
    glBegin(GL_LINES);
    for (float x = 0.0f; x <= w + 0.001f; x += step)
    {
        glVertex3f(x, -0.02f, 0.0f);
        glVertex3f(x, -0.02f, h);
    }
    for (float z = 0.0f; z <= h + 0.001f; z += step)
    {
        glVertex3f(0.0f, -0.02f, z);
        glVertex3f(w, -0.02f, z);
    }
    glEnd();
}

void Render(HWND hwnd)
{
    if (!MakeCurrent(hwnd))
        return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    EnsureMesh();

    glViewport(0, 0, w, h);
    glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    // with both sides generated there is nothing to cut away
    if (g_app.cutaway && g_app.facing != LM_FACE_BOTH)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    else
    {
        glDisable(GL_CULL_FACE);
    }

    // projection: 60 degree vertical field of view
    const float aspect = (float)w / (float)h;
    const float znear = 0.5f;
    const float zfar = 20000.0f;
    const float top = znear * tanf(Deg2Rad(60.0f) * 0.5f);
    const float right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, znear, zfar);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -g_v.dist);
    glRotatef(g_v.pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(g_v.yaw, 0.0f, 1.0f, 0.0f);
    glTranslatef(-g_v.tx, -g_v.ty, -g_v.tz);

    DrawFloorGrid();

    // filled pass
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    glBegin(GL_QUADS);
    for (size_t i = 0; i < g_v.mesh.quads.size(); ++i)
    {
        const LmQuad& q = g_v.mesh.quads[i];
        float col[3];
        ShadeColor(q.n, q.mat, col);
        glColor3f(col[0], col[1], col[2]);
        for (int k = 0; k < 4; ++k)
            glVertex3f(q.v[k].x, q.v[k].y, q.v[k].z);
    }
    glEnd();
    glDisable(GL_POLYGON_OFFSET_FILL);

    // outlines
    if (g_app.showWire && !g_v.mesh.quads.empty())
    {
        glColor3f(0.10f, 0.11f, 0.13f);
        for (size_t i = 0; i < g_v.mesh.quads.size(); ++i)
        {
            const LmQuad& q = g_v.mesh.quads[i];
            glBegin(GL_LINE_LOOP);
            for (int k = 0; k < 4; ++k)
                glVertex3f(q.v[k].x, q.v[k].y, q.v[k].z);
            glEnd();
        }
    }

    SwapBuffers(g_v.dc);
}

bool SetupGL(HWND hwnd)
{
    g_v.dc = GetDC(hwnd);
    if (!g_v.dc)
        return false;

    PIXELFORMATDESCRIPTOR pfd;
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int pf = ChoosePixelFormat(g_v.dc, &pfd);
    if (pf == 0 || !SetPixelFormat(g_v.dc, pf, &pfd))
        return false;

    g_v.rc = wglCreateContext(g_v.dc);
    if (!g_v.rc)
        return false;
    if (!wglMakeCurrent(g_v.dc, g_v.rc))
        return false;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_FLAT);
    glFrontFace(GL_CCW);
    glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
    return true;
}

void ShutdownGL(HWND hwnd)
{
    wglMakeCurrent(NULL, NULL);
    if (g_v.rc)
    {
        wglDeleteContext(g_v.rc);
        g_v.rc = NULL;
    }
    if (g_v.dc)
    {
        ReleaseDC(hwnd, g_v.dc);
        g_v.dc = NULL;
    }
}

LRESULT CALLBACK View3DProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        if (!SetupGL(hwnd))
        {
            MessageBoxW(hwnd,
                        L"Could not create an OpenGL context for the 3D preview.\n"
                        L"The plan view still works, and OBJ export is unaffected.",
                        L"LevelMaker", MB_OK | MB_ICONWARNING);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        Render(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        SetCapture(hwnd);
        g_v.orbiting = (msg == WM_LBUTTONDOWN);
        g_v.panning = (msg != WM_LBUTTONDOWN);
        g_v.last.x = GET_X_LPARAM(lParam);
        g_v.last.y = GET_Y_LPARAM(lParam);
        return 0;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        g_v.orbiting = false;
        g_v.panning = false;
        if (GetCapture() == hwnd)
            ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        if ((HWND)lParam != hwnd)
        {
            g_v.orbiting = false;
            g_v.panning = false;
        }
        return 0;

    case WM_MOUSEMOVE:
    {
        const int mx = GET_X_LPARAM(lParam);
        const int my = GET_Y_LPARAM(lParam);
        const int dx = mx - g_v.last.x;
        const int dy = my - g_v.last.y;
        g_v.last.x = mx;
        g_v.last.y = my;

        if (g_v.orbiting)
        {
            g_v.yaw += (float)dx * 0.4f;
            g_v.pitch = Clampf(g_v.pitch + (float)dy * 0.3f, -89.0f, 89.0f);
            if (g_v.yaw > 360.0f) g_v.yaw -= 360.0f;
            if (g_v.yaw < -360.0f) g_v.yaw += 360.0f;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        else if (g_v.panning)
        {
            const float a = Deg2Rad(g_v.yaw);
            const float p = Deg2Rad(g_v.pitch);
            const float rx = cosf(a), rz = sinf(a);                     // camera right
            const float ux = sinf(p) * sinf(a);                         // camera up
            const float uy = cosf(p);
            const float uz = -sinf(p) * cosf(a);
            const float k = g_v.dist * 0.0022f;
            // drag the level with the cursor: slide against camera right, with up
            g_v.tx += (-(float)dx * rx + (float)dy * ux) * k;
            g_v.ty += ((float)dy * uy) * k;
            g_v.tz += (-(float)dx * rz + (float)dy * uz) * k;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_v.dist = Clampf(g_v.dist * powf(0.9993f, (float)delta), 2.0f, 8000.0f);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        return TRUE;

    case WM_DESTROY:
        ShutdownGL(hwnd);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool View3D_Register(HINSTANCE hInstance)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = View3DProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

HWND View3D_Create(HWND parent, HINSTANCE hInstance)
{
    return CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           0, 0, 10, 10, parent, NULL, hInstance, NULL);
}

void View3D_ResetCamera(HWND hwnd)
{
    g_v.yaw = -35.0f;
    g_v.pitch = 34.0f;
    FrameLevel(true);
    InvalidateRect(hwnd, NULL, FALSE);
}

void View3D_InvalidateMesh(HWND hwnd)
{
    g_v.meshValid = false;
    InvalidateRect(hwnd, NULL, FALSE);
}
