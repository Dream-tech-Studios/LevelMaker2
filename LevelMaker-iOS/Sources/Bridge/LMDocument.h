// LMDocument.h : the document as Swift sees it.
//
// Deliberately free of C++ types so it can be imported from Swift through the
// bridging header. The views do not go through this class -- they include
// LevelDoc.h and talk to the C++ document directly, which is the whole point
// of building them as Objective-C++.
//
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Posted whenever the grid or a display option changes. The plan view, the
/// Metal view and the SwiftUI shell all observe it.
extern NSString * const LMDocumentChangedNotification;

typedef NS_ENUM(NSInteger, LMFacingMode) {
    LMFacingInward  = 0,
    LMFacingOutward = 1,
    LMFacingBoth    = 2
};

@interface LMDocument : NSObject

/// One document per process, matching the Win32 build's single global.
@property (class, nonatomic, readonly) LMDocument *shared;

// --- brush and drawing ---------------------------------------------------
@property (nonatomic) NSInteger   brush;          // stamp width in cells, 1...16
@property (nonatomic) BOOL        straightMode;   // lock strokes to one direction
@property (nonatomic) NSInteger   drawAngle;      // that direction, 45 degree steps

// --- input preferences ---------------------------------------------------
/// Strokes clear floor cells instead of setting them. Also toggled by a
/// Pencil double-tap, which is why it lives here rather than on the view.
@property (nonatomic) BOOL eraser;
/// One finger paints instead of panning -- for an iPad with no Pencil, and
/// for iPhone. Two-finger pan and pinch still navigate either way.
@property (nonatomic) BOOL fingerDraws;
/// Pencil force scales the stamp width around the brush setting.
@property (nonatomic) BOOL pressureSensitive;

// --- mesh options --------------------------------------------------------
@property (nonatomic) LMFacingMode facing;
@property (nonatomic) BOOL         showCeiling;
@property (nonatomic) BOOL         showWire;
@property (nonatomic) BOOL         cutaway;
@property (nonatomic) BOOL         smoothDiagonals;

// --- level scale ---------------------------------------------------------
@property (nonatomic) float cellSize;
@property (nonatomic) float wallHeight;

// --- state ---------------------------------------------------------------
@property (nonatomic, readonly) BOOL      modified;
@property (nonatomic, readonly) BOOL      canUndo;
@property (nonatomic, readonly) BOOL      canRedo;
@property (nonatomic, readonly) NSInteger floorCount;
@property (nonatomic, readonly) NSInteger triangleCount;
@property (nonatomic, readonly) NSInteger gridWidth;
@property (nonatomic, readonly) NSInteger gridHeight;
/// "untitled" or the file's last path component, with a trailing dot when dirty.
@property (nonatomic, readonly) NSString *displayName;
/// One line for the status bar: size, floor cells, triangle count.
@property (nonatomic, readonly) NSString *statusText;

// --- commands ------------------------------------------------------------
- (void)newLevel;
- (void)clearLevel;
- (BOOL)undo;
- (BOOL)redo;

/// Reads a .lvl file. The caller brackets any security-scoped access around
/// the URL -- see handleImport() in ContentView.swift.
/// The Swift names are pinned rather than left to the importer's inference.
- (BOOL)loadFromURL:(NSURL *)url    NS_SWIFT_NAME(load(from:));
- (BOOL)saveToURL:(NSURL *)url      NS_SWIFT_NAME(save(to:));
- (BOOL)exportOBJToURL:(NSURL *)url NS_SWIFT_NAME(exportOBJ(to:));
- (BOOL)exportFBXToURL:(NSURL *)url NS_SWIFT_NAME(exportFBX(to:));

/// Views call this after editing the grid: marks the document dirty, drops the
/// cached preview mesh and posts LMDocumentChangedNotification.
- (void)noteGridChanged;
/// Same, for a change that does not dirty the document (a view option).
- (void)noteDisplayChanged;

@end

NS_ASSUME_NONNULL_END
