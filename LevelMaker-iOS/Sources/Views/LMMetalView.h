// LMMetalView.h : the 3D preview, ported from View3D.cpp's OpenGL 1.1 pane.
//
// Camera controls follow the Win32 ones, remapped to touch:
//   one finger    orbit   (was left-drag)
//   two fingers   pan     (was right/middle-drag)
//   pinch         zoom    (was the wheel)
//
#import <MetalKit/MetalKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface LMMetalView : MTKView

/// Frame the camera on the level's floor bounds. Pass YES to frame the whole
/// grid even when nothing has been drawn yet.
- (void)resetCameraForcingWholeGrid:(BOOL)force
    NS_SWIFT_NAME(resetCamera(forcingWholeGrid:));

@end

NS_ASSUME_NONNULL_END
