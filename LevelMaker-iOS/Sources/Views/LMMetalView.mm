// LMMetalView.mm : Metal port of the OpenGL 1.1 preview in View3D.cpp.
//
#import "LMMetalView.h"
#import "LMDocument.h"

#include "LevelDoc.h"
#include "LevelGrid.h"

#include <simd/simd.h>
#include <cmath>
#include <vector>

namespace {

const float kPi = 3.14159265358979f;

struct LMVertex
{
    float position[3];
    float color[3];
};

struct LMUniforms
{
    simd_float4x4 mvp;
};

inline float Clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}
inline float Deg2Rad(float d)
{
    return d * kPi / 180.0f;
}

// View3D.cpp's ShadeColor, unchanged.
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

inline void PushVertex(std::vector<LMVertex>& out, const LmVec3& p, const float c[3])
{
    LMVertex v;
    v.position[0] = p.x; v.position[1] = p.y; v.position[2] = p.z;
    v.color[0] = c[0];   v.color[1] = c[1];   v.color[2] = c[2];
    out.push_back(v);
}

inline void PushLine(std::vector<LMVertex>& out,
                     float x0, float y0, float z0,
                     float x1, float y1, float z1,
                     const float c[3])
{
    LmVec3 a; a.x = x0; a.y = y0; a.z = z0;
    LmVec3 b; b.x = x1; b.y = y1; b.z = z1;
    PushVertex(out, a, c);
    PushVertex(out, b, c);
}

// --- matrices -------------------------------------------------------------
// Metal's clip space puts depth in [0, 1], unlike the [-1, 1] that glFrustum
// produced, so this is a Metal projection rather than a transcription.

simd_float4x4 Perspective(float fovyRad, float aspect, float zn, float zf)
{
    const float ys = 1.0f / tanf(fovyRad * 0.5f);
    const float xs = ys / aspect;
    const float zs = zf / (zn - zf);
    return simd_matrix(
        (simd_float4){ xs, 0,  0,       0 },
        (simd_float4){ 0,  ys, 0,       0 },
        (simd_float4){ 0,  0,  zs,     -1 },
        (simd_float4){ 0,  0,  zn * zs, 0 });
}

simd_float4x4 Translation(float x, float y, float z)
{
    return simd_matrix(
        (simd_float4){ 1, 0, 0, 0 },
        (simd_float4){ 0, 1, 0, 0 },
        (simd_float4){ 0, 0, 1, 0 },
        (simd_float4){ x, y, z, 1 });
}

simd_float4x4 RotationX(float rad)
{
    const float c = cosf(rad), s = sinf(rad);
    return simd_matrix(
        (simd_float4){ 1, 0, 0, 0 },
        (simd_float4){ 0, c, s, 0 },
        (simd_float4){ 0,-s, c, 0 },
        (simd_float4){ 0, 0, 0, 1 });
}

simd_float4x4 RotationY(float rad)
{
    const float c = cosf(rad), s = sinf(rad);
    return simd_matrix(
        (simd_float4){ c, 0,-s, 0 },
        (simd_float4){ 0, 1, 0, 0 },
        (simd_float4){ s, 0, c, 0 },
        (simd_float4){ 0, 0, 0, 1 });
}

} // namespace

@interface LMMetalView () <MTKViewDelegate>
@end

@implementation LMMetalView
{
    id<MTLCommandQueue>         _queue;
    id<MTLRenderPipelineState>  _pipeline;
    id<MTLDepthStencilState>    _depthState;

    id<MTLBuffer>               _triBuffer;
    id<MTLBuffer>               _lineBuffer;    // floor grid + quad outlines
    NSUInteger                  _triCount;
    NSUInteger                  _gridLineCount; // leading portion of _lineBuffer
    NSUInteger                  _wireLineCount; // the rest

    unsigned                    _meshGeneration;
    BOOL                        _buffersValid;

    // camera, matching View3DState
    float _yaw, _pitch, _dist;
    float _tx, _ty, _tz;
    BOOL  _framed;

    // gesture anchors
    float   _orbitYaw, _orbitPitch;
    float   _panTx, _panTy, _panTz;
    float   _pinchDist;
}

#pragma mark - setup

- (instancetype)initWithFrame:(CGRect)frame device:(id<MTLDevice>)device
{
    if (!device)
        device = MTLCreateSystemDefaultDevice();

    self = [super initWithFrame:frame device:device];
    if (!self)
        return nil;

    _yaw = -35.0f; _pitch = 34.0f; _dist = 160.0f;
    _tx = _ty = _tz = 0.0f;
    _framed = NO;
    _meshGeneration = 0;
    _buffersValid = NO;

    self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    self.clearColor = MTLClearColorMake(0.07, 0.08, 0.10, 1.0);
    self.enableSetNeedsDisplay = YES;       // redraw on demand, not at 60 Hz
    self.paused = YES;
    self.delegate = self;

    if (![self buildPipeline])
        return self;                        // the plan view still works

    [self addGestureRecognizers];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                            selector:@selector(documentChanged)
                                                name:LMDocumentChangedNotification
                                              object:nil];
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (BOOL)buildPipeline
{
    id<MTLDevice> device = self.device;
    if (!device)
        return NO;

    _queue = [device newCommandQueue];
    id<MTLLibrary> lib = [device newDefaultLibrary];
    if (!_queue || !lib)
        return NO;

    MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
    vd.attributes[0].format = MTLVertexFormatFloat3;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat3;
    vd.attributes[1].offset = sizeof(float) * 3;
    vd.attributes[1].bufferIndex = 0;
    vd.layouts[0].stride = sizeof(LMVertex);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [lib newFunctionWithName:@"lm_vertex"];
    pd.fragmentFunction = [lib newFunctionWithName:@"lm_fragment"];
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = self.colorPixelFormat;
    pd.depthAttachmentPixelFormat = self.depthStencilPixelFormat;

    NSError *err = nil;
    _pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!_pipeline)
    {
        NSLog(@"LevelMaker: 3D preview pipeline failed: %@", err);
        return NO;
    }

    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = MTLCompareFunctionLessEqual;   // was glDepthFunc
    dd.depthWriteEnabled = YES;
    _depthState = [device newDepthStencilStateWithDescriptor:dd];
    return YES;
}

- (void)addGestureRecognizers
{
    UIPanGestureRecognizer *pan =
        [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(onPan:)];
    pan.minimumNumberOfTouches = 1;
    pan.maximumNumberOfTouches = 2;
    [self addGestureRecognizer:pan];

    UIPinchGestureRecognizer *pinch =
        [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(onPinch:)];
    [self addGestureRecognizer:pinch];
}

#pragma mark - document

- (void)documentChanged
{
    if (_meshGeneration != LmPreviewMeshGeneration())
        _buffersValid = NO;
    [self setNeedsDisplay];
}

- (void)resetCameraForcingWholeGrid:(BOOL)force
{
    const LevelGrid& grid = LmDoc().grid;
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

    _tx = cx;
    _ty = grid.WallHeight() * 0.5f;
    _tz = cz;
    _dist = Clampf(span * 1.05f + 8.0f, 8.0f, 6000.0f);
    if (any)
        _framed = YES;
    [self setNeedsDisplay];
}

#pragma mark - buffers

- (void)rebuildBuffers
{
    const LevelDoc& doc = LmDoc();
    const LmMesh& mesh = LmPreviewMesh();
    _meshGeneration = LmPreviewMeshGeneration();

    if (!_framed)
        [self resetCameraForcingWholeGrid:NO];

    // --- triangles: fan each quad, baking the flat colour in --------------
    std::vector<LMVertex> tris;
    tris.reserve(mesh.TriangleCount() * 3);
    for (size_t i = 0; i < mesh.quads.size(); ++i)
    {
        const LmQuad& q = mesh.quads[i];
        float col[3];
        ShadeColor(q.n, q.mat, col);
        for (int k = 2; k < q.nv; ++k)
        {
            PushVertex(tris, q.v[0],     col);
            PushVertex(tris, q.v[k - 1], col);
            PushVertex(tris, q.v[k],     col);
        }
    }

    // --- lines: the floor grid first, then the quad outlines --------------
    std::vector<LMVertex> lines;
    {
        const LevelGrid& grid = doc.grid;
        const float cs = grid.CellSize();
        const float w = (float)grid.Width() * cs;
        const float h = (float)grid.Height() * cs;
        const float step = cs * 8.0f;
        const float gridCol[3] = { 0.20f, 0.22f, 0.26f };

        for (float x = 0.0f; x <= w + 0.001f; x += step)
            PushLine(lines, x, -0.02f, 0.0f, x, -0.02f, h, gridCol);
        for (float z = 0.0f; z <= h + 0.001f; z += step)
            PushLine(lines, 0.0f, -0.02f, z, w, -0.02f, z, gridCol);
    }
    _gridLineCount = lines.size();

    {
        const float wireCol[3] = { 0.10f, 0.11f, 0.13f };
        for (size_t i = 0; i < mesh.quads.size(); ++i)
        {
            const LmQuad& q = mesh.quads[i];
            for (int k = 0; k < q.nv; ++k)
            {
                const LmVec3& a = q.v[k];
                const LmVec3& b = q.v[(k + 1) % q.nv];
                PushVertex(lines, a, wireCol);
                PushVertex(lines, b, wireCol);
            }
        }
    }
    _wireLineCount = lines.size() - _gridLineCount;

    id<MTLDevice> device = self.device;
    _triCount = tris.size();
    _triBuffer = _triCount
        ? [device newBufferWithBytes:tris.data()
                              length:tris.size() * sizeof(LMVertex)
                             options:MTLResourceStorageModeShared]
        : nil;
    _lineBuffer = lines.size()
        ? [device newBufferWithBytes:lines.data()
                              length:lines.size() * sizeof(LMVertex)
                             options:MTLResourceStorageModeShared]
        : nil;

    _buffersValid = YES;
}

#pragma mark - MTKViewDelegate

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size
{
    [self setNeedsDisplay];
}

- (void)drawInMTKView:(MTKView *)view
{
    if (!_pipeline)
        return;
    if (!_buffersValid)
        [self rebuildBuffers];

    MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!rp || !drawable)
        return;

    const LevelDoc& doc = LmDoc();
    const CGSize size = view.drawableSize;
    const float aspect = (size.height > 0) ? (float)(size.width / size.height) : 1.0f;

    // projection: the same 60 degree vertical field of view and clip range
    const simd_float4x4 proj = Perspective(Deg2Rad(60.0f), aspect, 0.5f, 20000.0f);
    // view: translate(0,0,-dist) * rotX(pitch) * rotY(yaw) * translate(-target)
    const simd_float4x4 v =
        simd_mul(Translation(0.0f, 0.0f, -_dist),
            simd_mul(RotationX(Deg2Rad(_pitch)),
                simd_mul(RotationY(Deg2Rad(_yaw)),
                         Translation(-_tx, -_ty, -_tz))));

    LMUniforms u;
    u.mvp = simd_mul(proj, v);

    id<MTLCommandBuffer> cb = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    [enc setRenderPipelineState:_pipeline];
    [enc setDepthStencilState:_depthState];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];   // was glFrontFace
    [enc setVertexBytes:&u length:sizeof(u) atIndex:1];

    // with both sides generated there is nothing to cut away
    const BOOL cull = doc.cutaway && doc.facing != LM_FACE_BOTH;
    [enc setCullMode:cull ? MTLCullModeBack : MTLCullModeNone];

    // the floor grid, flat under everything
    if (_lineBuffer && _gridLineCount)
    {
        [enc setVertexBuffer:_lineBuffer offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeLine vertexStart:0 vertexCount:_gridLineCount];
    }

    // filled pass, pushed back so the outlines land on top. This is the
    // Metal equivalent of the glPolygonOffset the GL path used.
    if (_triBuffer && _triCount)
    {
        [enc setDepthBias:0.0 slopeScale:1.0 clamp:0.0];
        [enc setVertexBuffer:_triBuffer offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:_triCount];
        [enc setDepthBias:0.0 slopeScale:0.0 clamp:0.0];
    }

    // outlines
    if (doc.showWire && _lineBuffer && _wireLineCount)
    {
        [enc setCullMode:MTLCullModeNone];
        [enc setVertexBuffer:_lineBuffer offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeLine
                vertexStart:_gridLineCount
                vertexCount:_wireLineCount];
    }

    [enc endEncoding];
    [cb presentDrawable:drawable];
    [cb commit];
}

#pragma mark - camera gestures

- (void)onPan:(UIPanGestureRecognizer *)g
{
    if (g.state == UIGestureRecognizerStateBegan)
    {
        _orbitYaw = _yaw; _orbitPitch = _pitch;
        _panTx = _tx; _panTy = _ty; _panTz = _tz;
        return;
    }
    if (g.state != UIGestureRecognizerStateChanged)
        return;

    const CGPoint d = [g translationInView:self];

    if (g.numberOfTouches >= 2)
    {
        // pan the orbit target in the camera's own screen plane
        const float scale = _dist * 0.0016f;
        const float yawRad = Deg2Rad(_yaw);
        const float rightX =  cosf(yawRad), rightZ = -sinf(yawRad);
        _tx = _panTx - (float)d.x * scale * rightX;
        _tz = _panTz - (float)d.x * scale * rightZ;
        _ty = _panTy + (float)d.y * scale;
    }
    else
    {
        _yaw   = _orbitYaw   + (float)d.x * 0.4f;
        _pitch = Clampf(_orbitPitch + (float)d.y * 0.4f, -89.0f, 89.0f);
    }
    [self setNeedsDisplay];
}

- (void)onPinch:(UIPinchGestureRecognizer *)g
{
    if (g.state == UIGestureRecognizerStateBegan)
    {
        _pinchDist = _dist;
        return;
    }
    if (g.state != UIGestureRecognizerStateChanged)
        return;
    const float s = (g.scale > 0.01) ? (float)g.scale : 0.01f;
    _dist = Clampf(_pinchDist / s, 2.0f, 12000.0f);
    [self setNeedsDisplay];
}

@end
