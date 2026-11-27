// LevelMaker-Bridging-Header.h : what Swift can see.
//
// Only these three headers are exposed. The C++ engine is deliberately not
// here -- Swift never touches LevelGrid or LevelDoc, which is what keeps the
// interop surface to one Objective-C class and two views.
//
#import "LMDocument.h"
#import "LMPlanView.h"
#import "LMMetalView.h"
