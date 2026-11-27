// LMPlanView.mm : Core Graphics port of View2D.cpp's DrawPlan plus the
// touch/Pencil equivalents of its mouse handling.
//
#import "LMPlanView.h"
#import "LMDocument.h"

#include "LevelDoc.h"
#include "LevelGrid.h"

#include <cmath>
#include <vector>

namespace {

const CGFloat kMinZoom = 1.5f;
const CGFloat kMaxZoom = 48.0f;

// The Win32 view's palette, straight across, so the two builds look the same.
struct Rgb { CGFloat r, g, b; };
const Rgb kBg        = { 26/255.0,  28/255.0,  33/255.0  };
const Rgb kInside    = { 33/255.0,  36/255.0,  42/255.0  };
const Rgb kBorder    = { 70/255.0,  75/255.0,  86/255.0  };
const Rgb kGridFine  = { 42/255.0,  46/255.0,  54/255.0  };
const Rgb kGridMajor = { 56/255.0,  61/255.0,  72/255.0  };
const Rgb kFloor     = { 118/255.0, 124/255.0, 134/255.0 };
const Rgb kWall      = { 228/255.0, 232/255.0, 240/255.0 };
const Rgb kCursor    = { 255/255.0, 196/255.0, 92/255.0  };

inline void SetFill(CGContextRef c, const Rgb& v, CGFloat a = 1.0)
{
    CGContextSetRGBFillColor(c, v.r, v.g, v.b, a);
}
inline void SetStroke(CGContextRef c, const Rgb& v, CGFloat a = 1.0)
{
    CGContextSetRGBStrokeColor(c, v.r, v.g, v.b, a);
}
inline CGFloat Clampf(CGFloat v, CGFloat lo, CGFloat hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

} // namespace

@interface LMPlanView () <UIGestureRecognizerDelegate, UIPencilInteractionDelegate>
@end

@implementation LMPlanView
{
    // --- camera, in cell coordinates -------------------------------------
    CGFloat _camX;
    CGFloat _camY;
    CGFloat _zoom;

    // --- stroke state, mirroring View2DState -----------------------------
    BOOL    _painting;
    BOOL    _straightStroke;
    BOOL    _snapDrag;
    BOOL    _strokeFloor;
    int     _anchorX, _anchorY;
    int     _lastCellX, _lastCellY;
    CGPoint _lastPoint;         // view coords, so A/D style angle changes redraw

    // the one touch that owns the current stroke
    UITouch *__weak _strokeTouch;

    // --- brush indicator -------------------------------------------------
    int     _cursorCellX, _cursorCellY;
    BOOL    _hasCursor;

    // Stamp width for the stroke in progress. Equal to the document's brush
    // unless Pencil pressure is scaling it.
    int     _effectiveBrush;

    // --- navigation ------------------------------------------------------
    CGFloat _panCamX, _panCamY;
    CGFloat _pinchStartZoom;
}

#pragma mark - setup

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (!self)
        return nil;

    _camX = _camY = 0.0;
    _zoom = 9.0;                       // the Win32 default
    _cursorCellX = _cursorCellY = 0;
    _hasCursor = NO;

    self.backgroundColor = [UIColor colorWithRed:kBg.r green:kBg.g blue:kBg.b alpha:1.0];
    self.opaque = YES;
    self.multipleTouchEnabled = YES;
    self.contentMode = UIViewContentModeRedraw;

    // Two-finger pan always navigates; one finger joins in unless the finger
    // is being used to draw.
    UIPanGestureRecognizer *pan =
        [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(onPan:)];
    pan.minimumNumberOfTouches = 1;
    pan.maximumNumberOfTouches = 2;
    pan.delegate = self;
    [self addGestureRecognizer:pan];

    UIPinchGestureRecognizer *pinch =
        [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(onPinch:)];
    pinch.delegate = self;
    [self addGestureRecognizer:pinch];

    // Pencil hover, where the hardware has it: keeps the brush indicator
    // tracking before the tip touches down.
    UIHoverGestureRecognizer *hover =
        [[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(onHover:)];
    [self addGestureRecognizer:hover];

    // Pencil 2 / Pencil Pro double-tap toggles the eraser.
    UIPencilInteraction *pencil = [[UIPencilInteraction alloc] init];
    pencil.delegate = self;
    [self addInteraction:pencil];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                            selector:@selector(reloadFromDocument)
                                                name:LMDocumentChangedNotification
                                              object:nil];
    return self;
}

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (CGFloat)zoom { return _zoom; }

- (void)reloadFromDocument
{
    [self setNeedsDisplay];
}

#pragma mark - transforms

- (void)screenToCellF:(CGPoint)p outX:(CGFloat *)cx outY:(CGFloat *)cy
{
    const CGFloat halfW = self.bounds.size.width  * 0.5;
    const CGFloat halfH = self.bounds.size.height * 0.5;
    *cx = (p.x - halfW) / _zoom + _camX;
    *cy = (p.y - halfH) / _zoom + _camY;
}

- (CGPoint)cellToScreen:(CGFloat)cx y:(CGFloat)cy
{
    const CGFloat halfW = self.bounds.size.width  * 0.5;
    const CGFloat halfH = self.bounds.size.height * 0.5;
    return CGPointMake((cx - _camX) * _zoom + halfW,
                       (cy - _camY) * _zoom + halfH);
}

- (void)screenToCell:(CGPoint)p outX:(int *)cx outY:(int *)cy
{
    CGFloat fx = 0, fy = 0;
    [self screenToCellF:p outX:&fx outY:&fy];
    *cx = (int)floor(fx);
    *cy = (int)floor(fy);
}

- (void)fitToLevel
{
    const LevelGrid& grid = LmDoc().grid;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!grid.FloorBounds(x0, y0, x1, y1))
    {
        x0 = 0; y0 = 0; x1 = grid.Width() - 1; y1 = grid.Height() - 1;
    }

    const CGFloat spanX = (CGFloat)(x1 - x0 + 1);
    const CGFloat spanY = (CGFloat)(y1 - y0 + 1);
    _camX = (CGFloat)(x0 + x1 + 1) * 0.5;
    _camY = (CGFloat)(y0 + y1 + 1) * 0.5;

    const CGSize size = self.bounds.size;
    if (size.width > 1 && size.height > 1 && spanX > 0 && spanY > 0)
    {
        // 6 points of margin on the tighter axis
        const CGFloat zx = (size.width  - 12.0) / spanX;
        const CGFloat zy = (size.height - 12.0) / spanY;
        _zoom = Clampf(MIN(zx, zy), kMinZoom, kMaxZoom);
    }
    [self setNeedsDisplay];
}

#pragma mark - drawing

- (void)drawRect:(CGRect)dirty
{
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    const LevelDoc& doc = LmDoc();
    const LevelGrid& grid = doc.grid;
    const CGSize size = self.bounds.size;

    SetFill(ctx, kBg);
    CGContextFillRect(ctx, self.bounds);

    // visible cell window
    CGFloat fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0;
    [self screenToCellF:CGPointZero outX:&fx0 outY:&fy0];
    [self screenToCellF:CGPointMake(size.width, size.height) outX:&fx1 outY:&fy1];
    int cx0 = (int)floor(fx0) - 1, cy0 = (int)floor(fy0) - 1;
    int cx1 = (int)ceil(fx1)  + 1, cy1 = (int)ceil(fy1)  + 1;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > grid.Width())  cx1 = grid.Width();
    if (cy1 > grid.Height()) cy1 = grid.Height();

    // the same chamfer map the mesh builder uses, so the plan view shows the
    // diagonal the 3D preview will actually build
    std::vector<uint8_t> cuts;
    grid.BuildCutMap(cuts);
    const int gw = grid.Width();
    const unsigned char *cut = cuts.empty() ? NULL : &cuts[0];

    // --- level extents ---------------------------------------------------
    {
        const CGPoint a = [self cellToScreen:0 y:0];
        const CGPoint b = [self cellToScreen:(CGFloat)grid.Width() y:(CGFloat)grid.Height()];
        const CGRect r = CGRectMake(a.x, a.y, b.x - a.x, b.y - a.y);
        SetFill(ctx, kInside);
        CGContextFillRect(ctx, r);
        SetStroke(ctx, kBorder);
        CGContextSetLineWidth(ctx, 1.0);
        CGContextStrokeRect(ctx, r);
    }

    // --- grid lines ------------------------------------------------------
    if (_zoom >= 2.0)
    {
        const BOOL drawFine = (_zoom >= 7.0);
        const CGPoint o = [self cellToScreen:0 y:0];
        const CGPoint e = [self cellToScreen:(CGFloat)grid.Width() y:(CGFloat)grid.Height()];

        CGContextSetLineWidth(ctx, 1.0);
        for (int pass = 0; pass < 2; ++pass)
        {
            const BOOL major = (pass == 1);
            if (!major && !drawFine)
                continue;
            SetStroke(ctx, major ? kGridMajor : kGridFine);
            CGContextBeginPath(ctx);
            for (int x = cx0; x <= cx1; ++x)
            {
                if (((x % 8) == 0) != (major ? YES : NO))
                    continue;
                const CGFloat sx = [self cellToScreen:(CGFloat)x y:0].x;
                CGContextMoveToPoint(ctx, sx, o.y);
                CGContextAddLineToPoint(ctx, sx, e.y);
            }
            for (int y = cy0; y <= cy1; ++y)
            {
                if (((y % 8) == 0) != (major ? YES : NO))
                    continue;
                const CGFloat sy = [self cellToScreen:0 y:(CGFloat)y].y;
                CGContextMoveToPoint(ctx, o.x, sy);
                CGContextAddLineToPoint(ctx, e.x, sy);
            }
            CGContextStrokePath(ctx);
        }
    }

    // --- floor: merge each row into runs so a big hall is a few fills -----
    SetFill(ctx, kFloor);
    for (int y = cy0; y < cy1; ++y)
    {
        int x = cx0;
        while (x < cx1)
        {
            const bool skip = !grid.IsFloor(x, y) || (cut && cut[(size_t)y * gw + x]);
            if (skip) { ++x; continue; }
            const int start = x;
            while (x < cx1 && grid.IsFloor(x, y) && !(cut && cut[(size_t)y * gw + x]))
                ++x;
            const CGPoint a = [self cellToScreen:(CGFloat)start y:(CGFloat)y];
            const CGPoint b = [self cellToScreen:(CGFloat)x y:(CGFloat)(y + 1)];
            CGContextFillRect(ctx, CGRectMake(floor(a.x), floor(a.y),
                                              ceil(b.x) - floor(a.x),
                                              ceil(b.y) - floor(a.y)));
        }
    }

    // --- chamfered cells go down as half-cell triangles -------------------
    if (cut)
    {
        CGContextBeginPath(ctx);
        for (int y = cy0; y < cy1; ++y)
        {
            for (int x = cx0; x < cx1; ++x)
            {
                const int c = cut[(size_t)y * gw + x];
                if (!c)
                    continue;
                float px[3], py[3];
                LmCutTriangle(c, x, y, px, py);
                for (int k = 0; k < 3; ++k)
                {
                    const CGPoint p = [self cellToScreen:px[k] y:py[k]];
                    if (k == 0) CGContextMoveToPoint(ctx, p.x, p.y);
                    else        CGContextAddLineToPoint(ctx, p.x, p.y);
                }
                CGContextClosePath(ctx);
            }
        }
        CGContextFillPath(ctx);
    }

    // --- walls: every boundary with floor on exactly one side -------------
    if (_zoom >= 3.0)
    {
        SetStroke(ctx, kWall);
        CGContextSetLineWidth(ctx, (_zoom >= 12.0) ? 3.0 : 2.0);
        CGContextSetLineCap(ctx, kCGLineCapButt);
        CGContextBeginPath(ctx);

        // horizontal boundaries, merged along x
        for (int y = cy0; y <= cy1; ++y)
        {
            int x = cx0;
            while (x < cx1)
            {
                const int side = LmPlanWallH(grid, cut, gw, x, y);
                if (side == 0) { ++x; continue; }
                const int start = x;
                while (x < cx1 && LmPlanWallH(grid, cut, gw, x, y) == side)
                    ++x;
                const CGPoint a = [self cellToScreen:(CGFloat)start y:(CGFloat)y];
                const CGPoint b = [self cellToScreen:(CGFloat)x y:(CGFloat)y];
                CGContextMoveToPoint(ctx, a.x, a.y);
                CGContextAddLineToPoint(ctx, b.x, b.y);
            }
        }
        // vertical boundaries, merged along y
        for (int x = cx0; x <= cx1; ++x)
        {
            int y = cy0;
            while (y < cy1)
            {
                const int side = LmPlanWallV(grid, cut, gw, x, y);
                if (side == 0) { ++y; continue; }
                const int start = y;
                while (y < cy1 && LmPlanWallV(grid, cut, gw, x, y) == side)
                    ++y;
                const CGPoint a = [self cellToScreen:(CGFloat)x y:(CGFloat)start];
                const CGPoint b = [self cellToScreen:(CGFloat)x y:(CGFloat)y];
                CGContextMoveToPoint(ctx, a.x, a.y);
                CGContextAddLineToPoint(ctx, b.x, b.y);
            }
        }
        CGContextStrokePath(ctx);

        // the chamfer edges themselves
        if (cut)
        {
            CGContextBeginPath(ctx);
            for (int y = cy0; y < cy1; ++y)
            {
                for (int x = cx0; x < cx1; ++x)
                {
                    const int c = cut[(size_t)y * gw + x];
                    if (!c)
                        continue;
                    float ax = 0, ay = 0, bx = 0, by = 0;
                    LmCutEdge(c, x, y, ax, ay, bx, by);
                    const CGPoint a = [self cellToScreen:ax y:ay];
                    const CGPoint b = [self cellToScreen:bx y:by];
                    CGContextMoveToPoint(ctx, a.x, a.y);
                    CGContextAddLineToPoint(ctx, b.x, b.y);
                }
            }
            CGContextStrokePath(ctx);
        }
    }

    // --- brush indicator --------------------------------------------------
    if (_hasCursor)
    {
        // mid-stroke the indicator shows the width actually being stamped
        const int b = _painting ? _effectiveBrush : doc.brush;
        const int half = b / 2;
        const CGPoint a = [self cellToScreen:(CGFloat)(_cursorCellX - half)
                                          y:(CGFloat)(_cursorCellY - half)];
        const CGPoint e = [self cellToScreen:(CGFloat)(_cursorCellX - half + b)
                                          y:(CGFloat)(_cursorCellY - half + b)];
        SetStroke(ctx, kCursor, LmDoc().eraser ? 0.95 : 0.85);
        CGContextSetLineWidth(ctx, 1.5);
        CGContextSetLineDash(ctx, 0, NULL, 0);
        if (LmDoc().eraser)
        {
            const CGFloat dash[2] = { 4.0, 3.0 };
            CGContextSetLineDash(ctx, 0, dash, 2);
        }
        CGContextStrokeRect(ctx, CGRectMake(a.x, a.y, e.x - a.x, e.y - a.y));
        CGContextSetLineDash(ctx, 0, NULL, 0);
    }

    // --- straight-mode guide ---------------------------------------------
    if ((doc.straightMode || _straightStroke) && _hasCursor)
    {
        const LmDir d = LmDirForAngle(doc.drawAngle);
        const int ox = _straightStroke ? _anchorX : _cursorCellX;
        const int oy = _straightStroke ? _anchorY : _cursorCellY;
        const CGPoint a = [self cellToScreen:(CGFloat)ox + 0.5 y:(CGFloat)oy + 0.5];
        const CGPoint b = [self cellToScreen:(CGFloat)(ox + d.dx * 1000) + 0.5
                                          y:(CGFloat)(oy + d.dy * 1000) + 0.5];
        SetStroke(ctx, kCursor, 0.35);
        CGContextSetLineWidth(ctx, 1.0);
        CGContextBeginPath(ctx);
        CGContextMoveToPoint(ctx, a.x, a.y);
        CGContextAddLineToPoint(ctx, b.x, b.y);
        CGContextStrokePath(ctx);
    }
}

#pragma mark - stroke plumbing

// A pencil always draws. A finger draws only when the user has asked it to.
- (BOOL)touchPaints:(UITouch *)t
{
    if (t.type == UITouchTypePencil)
        return YES;
    return (t.type == UITouchTypeDirect) && LmDoc().fingerDraws;
}

- (void)beginStrokeAt:(CGPoint)p withTouch:(UITouch *)t
{
    LevelDoc& doc = LmDoc();
    _strokeTouch   = t;
    _painting      = YES;
    _strokeFloor   = !LmDoc().eraser;
    _straightStroke = doc.straightMode;
    _snapDrag      = NO;
    _lastPoint     = p;

    doc.grid.BeginStroke();
    [self screenToCell:p outX:&_anchorX outY:&_anchorY];
    _cursorCellX = _anchorX;
    _cursorCellY = _anchorY;
    _hasCursor = YES;

    [self applyPressureFrom:t];
    [self paintAt:p first:YES];
}

// Pencil pressure drives the stamp width, which is the unit `brush` was
// always in. Fingers report no usable force, so they keep the plain setting.
- (void)applyPressureFrom:(UITouch *)t
{
    const int base = LmDoc().brush;
    _effectiveBrush = base;

    if (!LmDoc().pressureSensitive)
        return;
    if (t.type != UITouchTypePencil || t.maximumPossibleForce <= 0.0)
        return;

    // A normal resting press sits near the middle of the range, so map force
    // to 0.4x...1.6x of the chosen brush and round to whole cells.
    const CGFloat unit = Clampf(t.force / t.maximumPossibleForce, 0.0, 1.0);
    const int scaled = (int)lround((CGFloat)base * (0.4 + 1.2 * unit));
    _effectiveBrush = (int)Clampf(scaled, 1, 16);
}

- (void)paintAt:(CGPoint)p first:(BOOL)first
{
    LevelDoc& doc = LmDoc();
    _lastPoint = p;

    if (_straightStroke)
    {
        [self paintStraight];
        return;
    }

    int cx = 0, cy = 0;
    [self screenToCell:p outX:&cx outY:&cy];

    bool changed;
    if (first)
        changed = doc.grid.PaintCell(cx, cy, _effectiveBrush, _strokeFloor);
    else
        changed = doc.grid.PaintLine(_lastCellX, _lastCellY, cx, cy,
                                     _effectiveBrush, _strokeFloor);

    _lastCellX = cx;
    _lastCellY = cy;
    _cursorCellX = cx;
    _cursorCellY = cy;

    if (changed)
        [[LMDocument shared] noteGridChanged];
    else
        [self setNeedsDisplay];
}

- (void)paintStraight
{
    LevelDoc& doc = LmDoc();
    int cx = 0, cy = 0;
    [self screenToCell:_lastPoint outX:&cx outY:&cy];

    const int dx = cx - _anchorX;
    const int dy = cy - _anchorY;
    if (_snapDrag)
        doc.drawAngle = LmSnapAngle(dx, dy, doc.drawAngle);

    const LmDir d = LmDirForAngle(doc.drawAngle);
    const int steps = LmStepsAlong(dx, dy, d);

    doc.grid.RevertStroke();
    doc.grid.PaintLine(_anchorX, _anchorY,
                       _anchorX + d.dx * steps, _anchorY + d.dy * steps,
                       _effectiveBrush, _strokeFloor);

    _lastCellX = _anchorX + d.dx * steps;
    _lastCellY = _anchorY + d.dy * steps;
    _cursorCellX = _lastCellX;
    _cursorCellY = _lastCellY;
    [[LMDocument shared] noteGridChanged];
}

- (void)endStroke
{
    if (!_painting)
        return;
    LmDoc().grid.EndStroke();
    _painting = NO;
    _straightStroke = NO;
    _snapDrag = NO;
    _strokeTouch = nil;
    [[LMDocument shared] noteGridChanged];
}

// A second finger means the user is navigating, not drawing: throw the
// half-finished stroke away rather than leaving a stray smear behind.
- (void)abandonStroke
{
    if (!_painting)
        return;
    LevelDoc& doc = LmDoc();
    doc.grid.RevertStroke();
    doc.grid.EndStroke();
    _painting = NO;
    _straightStroke = NO;
    _strokeTouch = nil;
    [[LMDocument shared] noteGridChanged];
}

#pragma mark - touches

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (_painting)
    {
        // a new touch arriving mid-stroke is a navigation gesture starting
        [self abandonStroke];
        return;
    }
    for (UITouch *t in touches)
    {
        if ([self touchPaints:t])
        {
            [self beginStrokeAt:[t locationInView:self] withTouch:t];
            return;
        }
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (!_painting)
        return;
    for (UITouch *t in touches)
    {
        if (t != _strokeTouch)
            continue;
        // coalesced touches keep a fast Pencil stroke from going polygonal
        NSArray<UITouch *> *seq = [event coalescedTouchesForTouch:t] ?: @[ t ];
        for (UITouch *ct in seq)
        {
            [self applyPressureFrom:ct];
            [self paintAt:[ct locationInView:self] first:NO];
        }
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches)
        if (t == _strokeTouch)
            [self endStroke];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    for (UITouch *t in touches)
        if (t == _strokeTouch)
            [self abandonStroke];
}

#pragma mark - navigation

- (void)onPan:(UIPanGestureRecognizer *)g
{
    if (g.state == UIGestureRecognizerStateBegan)
    {
        [self abandonStroke];
        _panCamX = _camX;
        _panCamY = _camY;
        return;
    }
    if (g.state != UIGestureRecognizerStateChanged)
        return;

    const CGPoint d = [g translationInView:self];
    _camX = _panCamX - d.x / _zoom;
    _camY = _panCamY - d.y / _zoom;
    [self setNeedsDisplay];
}

- (void)onPinch:(UIPinchGestureRecognizer *)g
{
    if (g.state == UIGestureRecognizerStateBegan)
    {
        [self abandonStroke];
        _pinchStartZoom = _zoom;
        return;
    }
    if (g.state != UIGestureRecognizerStateChanged)
        return;

    const CGPoint focus = [g locationInView:self];
    CGFloat beforeX = 0, beforeY = 0;
    [self screenToCellF:focus outX:&beforeX outY:&beforeY];

    _zoom = Clampf(_pinchStartZoom * g.scale, kMinZoom, kMaxZoom);

    CGFloat afterX = 0, afterY = 0;
    [self screenToCellF:focus outX:&afterX outY:&afterY];
    _camX += beforeX - afterX;
    _camY += beforeY - afterY;
    [self setNeedsDisplay];
}

- (void)onHover:(UIHoverGestureRecognizer *)g
{
    if (g.state == UIGestureRecognizerStateEnded ||
        g.state == UIGestureRecognizerStateCancelled)
    {
        if (!_painting)
            _hasCursor = NO;
        [self setNeedsDisplay];
        return;
    }
    [self screenToCell:[g locationInView:self] outX:&_cursorCellX outY:&_cursorCellY];
    _hasCursor = YES;
    [self setNeedsDisplay];
}

#pragma mark - UIGestureRecognizerDelegate

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)g
{
    // A one-finger pan must not steal the gesture when the finger is the
    // drawing tool; two fingers always navigate.
    if ([g isKindOfClass:[UIPanGestureRecognizer class]])
    {
        UIPanGestureRecognizer *pan = (UIPanGestureRecognizer *)g;
        if (LmDoc().fingerDraws && pan.numberOfTouches < 2)
            return NO;
    }
    return YES;
}

- (BOOL)                             gestureRecognizer:(UIGestureRecognizer *)a
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)b
{
    return YES;      // pinch and two-finger pan belong together
}

#pragma mark - UIPencilInteractionDelegate

- (void)pencilInteractionDidTap:(UIPencilInteraction *)interaction
{
    // Routed through the document so the toolbar toggle and the double-tap
    // are the same change; the notification brings the redraw with it.
    LMDocument *d = [LMDocument shared];
    d.eraser = !d.eraser;
}

@end
