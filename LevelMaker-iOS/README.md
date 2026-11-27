# LevelMaker for iPad

An iPad/iPhone port of the Win32 LevelMaker in `../LevelMaker`. The level
format is unchanged, so `.lvl` files move between the two builds in either
direction.

## Building

Xcode will not open a project that does not exist yet — the `.xcodeproj` is
generated from `project.yml` rather than committed, so there is no pbxproj to
merge:

```sh
brew install xcodegen          # once
cd LevelMaker-iOS
xcodegen generate
open LevelMaker.xcodeproj
```

Then pick your team under Signing & Capabilities once. On the next
`xcodegen generate` it gets written back into `project.yml`.

Build on the **M4 mini**, not the 2019 MacBook Pro: the mini's GPU is the same
architecture family as the iPad's, so Metal shader behaviour and validation
output match what the device does. The MacBook Pro's AMD GPU is a different
driver stack that can both hide real bugs and invent fake ones.

Make sure Xcode is fully updated before deploying to the mini 8 — it shipped
in November 2026 and an Xcode older than its iPadOS will refuse to install,
with an unhelpful error.

## Layer map

| Path | Language | Notes |
|---|---|---|
| `Sources/Engine/LevelGrid.{h,cpp}` | C++17 | **Unchanged logic.** Only the platform layer was touched. |
| `Sources/Engine/LevelDoc.{h,cpp}` | C++17 | Replaces `LevelApp.h`'s `AppState`; no platform types. |
| `Sources/Bridge/LMDocument.{h,mm}` | Objective-C++ | The only thing Swift can see. |
| `Sources/Views/LMPlanView.{h,mm}` | Objective-C++ | Core Graphics port of `View2D.cpp`. |
| `Sources/Views/LMMetalView.{h,mm}` | Objective-C++ | Metal port of `View3D.cpp`. |
| `Sources/Views/Shaders.metal` | Metal | Two functions; flat colour is baked CPU-side. |
| `Sources/App/*.swift` | Swift / SwiftUI | Toolbar, options, file pickers. Never touches C++. |

The split is deliberate: everything that talks to the engine per-touch or
per-frame is Objective-C++, where a `.mm` file can call `LevelGrid` directly
with no bridging. Swift handles only the shell, which touches no C++ at all.

## What changed in the engine

The geometry half of `LevelGrid.cpp` (lines 1–813: greedy quad merging,
diagonal chamfers, the cut map) is **byte-identical** to the Windows version.
It was ported by patching only the platform layer, not by retyping:

- `CreateFileW`/`ReadFile`/`WriteFile` → `std::ofstream`/`std::ifstream`,
  keeping the original 64 MB sanity cap on reads.
- `const wchar_t*` paths → `const char*` UTF-8 POSIX paths, which is what
  `NSURL.fileSystemRepresentation` hands over.
- `std::wstring` path helpers → `std::string`; `NarrowAscii` became
  `SanitizeAscii`, since the OBJ `mtllib` line is whitespace-delimited.
- `framework.h` (and with it all of `windows.h`) is gone.

Verified clean at `/W4` with MSVC 14.51 as pure C++17 — zero warnings, no
Win32 references left.

## Interaction model

| Input | Plan view | 3D preview |
|---|---|---|
| Pencil | draws / erases | — |
| One finger | pans (draws if **One finger draws** is on) | orbit |
| Two fingers | pan | pan |
| Pinch | zoom about the focal point | zoom |
| Pencil double-tap | toggles the eraser | — |

Two settings exist because of the specific hardware:

- **One finger draws** makes the app fully usable with no Pencil at all —
  needed on the iPad 10 and on iPhone. Two-finger pan and pinch still
  navigate, so nothing is lost.
- **Pencil pressure sets width** scales the stamp between 0.4× and 1.6× of the
  brush setting. Pencil only; fingers report no usable force.

The brush footprint is drawn from the last known position **at all times**,
not only on hover. Of the three iPads only the mini 8 supports Pencil hover,
so hover is treated as an enhancement that keeps the indicator fresh before
contact rather than as the mechanism itself.

## Layout

`ContentView` switches on horizontal size class:

- **Regular** (12.9" iPad Pro, landscape): panes side by side, as on Windows.
- **Compact** (mini 8, iPhone): one pane at a time with a segmented toggle.
  Both panes stay alive, so each keeps its camera across a swap.

The compact path is the primary one, not a fallback — at 8.5" the mini is a
compact canvas, and it is the device this gets used on.

## Deployment target

iOS 17.0, which covers all three iPads including the 2018 A12X Pro.

Nothing here needs Metal 3: the renderer is one vertex buffer, two shader
functions, an MVP matrix and depth testing, which is Metal 1.0-era
functionality. Staying inside it costs nothing and keeps the A12X supported,
so there was no reason to raise the floor.

## Known gaps

Things deliberately left for a second pass:

1. **No Pencil Pro squeeze or barrel roll yet.** Squeeze should pop the brush
   picker; barrel roll should drive `drawAngle`, which is the best input fit in
   the project. Both are mini-8-only, so they are enhancements on a UI that
   works without them.
2. **Palm handling is blunt.** Any second touch arriving mid-stroke abandons
   the stroke (reverting it, so no stray smear and no undo entry). That is
   safe but aggressive; it wants testing against real Pencil use before
   tuning.
3. **No autosave or state restoration.** Closing the app loses an unsaved
   level without warning. The Windows build asked; this one does not yet.
4. **`Shift`-equivalent angle snapping is unreachable.** `_snapDrag` is wired
   up in `LMPlanView` but nothing sets it, because there is no modifier key.
   It needs a gesture or a toolbar control.
5. **Untested on device.** Written on Windows; the engine is compile-verified
   but the UIKit, Metal and SwiftUI layers have never been built. Expect a
   first pass of compile errors.
6. **No app icon.** `UILaunchScreen` is a bare colour.

## Next step

Build it and fix the first round of errors. After that, the highest-value work
is item 2 — palm handling is the one thing that will decide whether tracing a
hall with the Pencil feels right.
