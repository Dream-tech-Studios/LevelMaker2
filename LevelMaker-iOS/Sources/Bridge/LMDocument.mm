// LMDocument.mm : Objective-C++ wrapper over the C++ LevelDoc.
//
#import "LMDocument.h"

#include "LevelDoc.h"
#include "LevelGrid.h"

#include <string>

NSString * const LMDocumentChangedNotification = @"LMDocumentChangedNotification";

@implementation LMDocument

+ (LMDocument *)shared
{
    static LMDocument *s = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [[LMDocument alloc] init]; });
    return s;
}

#pragma mark - change plumbing

- (void)postChanged
{
    [[NSNotificationCenter defaultCenter] postNotificationName:LMDocumentChangedNotification
                                                        object:self];
}

- (void)noteGridChanged
{
    LmDoc().modified = true;
    LmInvalidatePreviewMesh();
    [self postChanged];
}

- (void)noteDisplayChanged
{
    LmInvalidatePreviewMesh();
    [self postChanged];
}

#pragma mark - brush and drawing

- (NSInteger)brush          { return LmDoc().brush; }
- (void)setBrush:(NSInteger)v
{
    if (v < 1)  v = 1;
    if (v > 16) v = 16;
    LmDoc().brush = (int)v;
    [self postChanged];
}

- (BOOL)straightMode        { return LmDoc().straightMode ? YES : NO; }
- (void)setStraightMode:(BOOL)v
{
    LmDoc().straightMode = v ? true : false;
    [self postChanged];
}

- (NSInteger)drawAngle      { return LmDoc().drawAngle; }
- (void)setDrawAngle:(NSInteger)v
{
    // keep it in [0, 360) on 45 degree steps
    NSInteger a = ((v % 360) + 360) % 360;
    LmDoc().drawAngle = (int)((a / 45) * 45);
    [self postChanged];
}

#pragma mark - input preferences

- (BOOL)eraser              { return LmDoc().eraser ? YES : NO; }
- (void)setEraser:(BOOL)v
{
    LmDoc().eraser = v ? true : false;
    [self postChanged];
}

- (BOOL)fingerDraws         { return LmDoc().fingerDraws ? YES : NO; }
- (void)setFingerDraws:(BOOL)v
{
    LmDoc().fingerDraws = v ? true : false;
    [self postChanged];
}

- (BOOL)pressureSensitive   { return LmDoc().pressureSensitive ? YES : NO; }
- (void)setPressureSensitive:(BOOL)v
{
    LmDoc().pressureSensitive = v ? true : false;
    [self postChanged];
}

#pragma mark - mesh options

- (LMFacingMode)facing      { return (LMFacingMode)LmDoc().facing; }
- (void)setFacing:(LMFacingMode)v
{
    LmDoc().facing = (LmFacing)v;
    [self noteDisplayChanged];
}

- (BOOL)showCeiling         { return LmDoc().showCeiling ? YES : NO; }
- (void)setShowCeiling:(BOOL)v
{
    LmDoc().showCeiling = v ? true : false;
    [self noteDisplayChanged];
}

- (BOOL)showWire            { return LmDoc().showWire ? YES : NO; }
- (void)setShowWire:(BOOL)v
{
    LmDoc().showWire = v ? true : false;
    [self postChanged];      // wireframe is a draw-time flag, mesh is unaffected
}

- (BOOL)cutaway             { return LmDoc().cutaway ? YES : NO; }
- (void)setCutaway:(BOOL)v
{
    LmDoc().cutaway = v ? true : false;
    [self postChanged];      // face culling, likewise draw-time only
}

- (BOOL)smoothDiagonals     { return LmDoc().grid.SmoothDiagonals() ? YES : NO; }
- (void)setSmoothDiagonals:(BOOL)v
{
    LmDoc().grid.SetSmoothDiagonals(v ? true : false);
    [self noteGridChanged];
}

#pragma mark - level scale

- (float)cellSize           { return LmDoc().grid.CellSize(); }
- (void)setCellSize:(float)v
{
    if (v > 0.01f && v < 1000.0f)
    {
        LmDoc().grid.SetCellSize(v);
        [self noteGridChanged];
    }
}

- (float)wallHeight         { return LmDoc().grid.WallHeight(); }
- (void)setWallHeight:(float)v
{
    if (v > 0.01f && v < 1000.0f)
    {
        LmDoc().grid.SetWallHeight(v);
        [self noteGridChanged];
    }
}

#pragma mark - state

- (BOOL)modified            { return LmDoc().modified ? YES : NO; }
- (BOOL)canUndo             { return LmDoc().grid.CanUndo() ? YES : NO; }
- (BOOL)canRedo             { return LmDoc().grid.CanRedo() ? YES : NO; }
- (NSInteger)floorCount     { return LmDoc().grid.FloorCount(); }
- (NSInteger)gridWidth      { return LmDoc().grid.Width(); }
- (NSInteger)gridHeight     { return LmDoc().grid.Height(); }

- (NSInteger)triangleCount
{
    return (NSInteger)LmPreviewMesh().TriangleCount();
}

- (NSString *)displayName
{
    const std::string& p = LmDoc().filePath;
    NSString *base = @"untitled";
    if (!p.empty())
    {
        NSString *full = [NSString stringWithUTF8String:p.c_str()];
        base = full.lastPathComponent.stringByDeletingPathExtension;
    }
    return LmDoc().modified ? [base stringByAppendingString:@" •"] : base;
}

- (NSString *)statusText
{
    NSNumberFormatter *f = [[NSNumberFormatter alloc] init];
    f.numberStyle = NSNumberFormatterDecimalStyle;
    NSString *cells = [f stringFromNumber:@(self.floorCount)];
    NSString *tris  = [f stringFromNumber:@(self.triangleCount)];
    return [NSString stringWithFormat:@"%ld x %ld  ·  %@ cells  ·  %@ tris",
            (long)self.gridWidth, (long)self.gridHeight, cells, tris];
}

#pragma mark - commands

- (void)newLevel
{
    LevelDoc& doc = LmDoc();
    doc.grid.Reset(LevelGrid::kDefaultSize, LevelGrid::kDefaultSize);
    doc.filePath.clear();
    doc.modified = false;
    LmInvalidatePreviewMesh();
    [self postChanged];
}

- (void)clearLevel
{
    LmDoc().grid.Clear();
    [self noteGridChanged];
}

- (BOOL)undo
{
    if (!LmDoc().grid.Undo())
        return NO;
    [self noteGridChanged];
    return YES;
}

- (BOOL)redo
{
    if (!LmDoc().grid.Redo())
        return NO;
    [self noteGridChanged];
    return YES;
}

#pragma mark - file i/o

- (BOOL)loadFromURL:(NSURL *)url
{
    if (!url.isFileURL)
        return NO;
    LevelDoc& doc = LmDoc();
    if (!doc.grid.Load(url.fileSystemRepresentation))
        return NO;
    doc.filePath.assign(url.fileSystemRepresentation);
    doc.modified = false;
    LmInvalidatePreviewMesh();
    [self postChanged];
    return YES;
}

- (BOOL)saveToURL:(NSURL *)url
{
    if (!url.isFileURL)
        return NO;
    LevelDoc& doc = LmDoc();
    if (!doc.grid.Save(url.fileSystemRepresentation))
        return NO;
    doc.filePath.assign(url.fileSystemRepresentation);
    doc.modified = false;
    [self postChanged];
    return YES;
}

- (BOOL)exportOBJToURL:(NSURL *)url
{
    if (!url.isFileURL)
        return NO;
    const LevelDoc& doc = LmDoc();
    return doc.grid.ExportObj(url.fileSystemRepresentation,
                              doc.showCeiling, doc.facing) ? YES : NO;
}

- (BOOL)exportFBXToURL:(NSURL *)url
{
    if (!url.isFileURL)
        return NO;
    const LevelDoc& doc = LmDoc();
    return doc.grid.ExportFbxAscii(url.fileSystemRepresentation,
                                   doc.showCeiling, doc.facing) ? YES : NO;
}

@end
