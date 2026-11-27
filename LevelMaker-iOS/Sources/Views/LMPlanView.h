// LMPlanView.h : the top-down plan pane, ported from View2D.cpp.
//
// Interaction, as decided for the iPad:
//   Pencil             draws (or erases when `eraser` is set)
//   One finger         pans -- unless `fingerDraws` is on, for a no-Pencil iPad
//   Two fingers        pan
//   Pinch              zooms about the gesture's focal point
//   Pencil double-tap  toggles `eraser`
//
// The brush footprint is drawn from the last known Pencil or touch position at
// all times, and Pencil hover merely keeps that position fresh before contact.
// None of the two iPads without hover lose anything but the preview-before-
// touch, which is why the indicator is the default path rather than a fallback.
//
#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface LMPlanView : UIView

// The eraser, finger-draws and pressure settings live on LMDocument, so a
// Pencil double-tap and a SwiftUI toggle are the same change and every pane
// hears about it. This view only reads them.

/// Points per cell. Clamped to the same 1.5...48 range the Win32 view used.
@property (nonatomic, readonly) CGFloat zoom;

/// Centre the view on the level's floor bounds, or on the whole grid when empty.
- (void)fitToLevel;

/// Re-read the document and redraw. Hooked to LMDocumentChangedNotification.
- (void)reloadFromDocument;

@end

NS_ASSUME_NONNULL_END
