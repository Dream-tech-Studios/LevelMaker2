# LevelMaker

A small Windows desktop app for blocking out levels: hold the left mouse button
in the plan view, trace a hall, and watch the walls build themselves in a live
3D preview. Export the result as a Wavefront OBJ or an ASCII FBX.

Plain Win32 + OpenGL 1.1 from the Windows SDK. No CMake, no vcpkg, no external
dependencies — open the solution and press F5.

## Building

1. Open `LevelMaker/LevelMaker.slnx` in Visual Studio 2022 or newer
   (Desktop development with C++ workload).
2. Pick `x64` / `Debug` or `Release`.
3. Build and run (F5).

The project links `opengl32.lib`, which ships with the Windows SDK. Nothing
else needs installing.

## Using it

The window is split: the **plan view** on the left is where you draw, the
**3D preview** on the right shows the generated geometry. Drag the bar between
them to resize.

### Plan view

| Input | Action |
| --- | --- |
| Hold **left** and drag | trace a hall |
| Hold **right** and drag | erase |
| **Middle** drag | pan |
| **Wheel** | zoom around the cursor |
| **[** / **]** | narrower / wider hall |
| **1** – **9** | set hall width directly |
| **F** | fit the view to the level |
| **S** | straight-line mode on / off |
| **A** / **D** | turn the locked direction 45° left / right |
| **Shift** + drag | one straight hall, angle snapped to the drag |

### Drawing straight

Freehand tracing follows the mouse exactly, wobble included. Press **S** and
the stroke is locked to one direction instead: drag and the hall runs dead
straight from where you pressed, growing and shrinking as you move. **A** and
**D** turn that direction in 45° steps, during the drag as well as before it,
and a dotted guide line shows where the next hall will go. If you only want
the occasional straight run, hold **Shift** while dragging and the angle snaps
to whichever of the eight directions you are pulling toward.

Runs at 0°, 90°, 180° and 270° come out perfectly rectangular — one floor quad
and four wall quads. A 45° run is a regular staircase of cells, because the
level is a grid; it is tidy, but it is not a smooth diagonal.

### 3D preview

| Input | Action |
| --- | --- |
| **Left** drag | orbit |
| **Right** drag | pan |
| **Wheel** | zoom |
| **R** | reset the camera |
| **C** | ceilings on / off |
| **W** | quad outlines on / off |
| **X** | cutaway (cull back faces) on / off |

Cutaway is off by default, so the level reads as solid. Turn it on to drop the
near walls and look down into the halls.

### Files

* **Ctrl+N / Ctrl+O / Ctrl+S** — new, open, save a `.lvl` level
* **Ctrl+E** — export the mesh as Wavefront `.obj` (a matching `.mtl` goes next to it)
* **Ctrl+F** — export the mesh as ASCII `.fbx` (FBX 7.4, one mesh, three materials)
* **Ctrl+Z / Ctrl+Y** — undo / redo, one stroke at a time
* `LevelMaker.exe path\to\level.lvl` opens that level on startup

Everything else lives under **Settings**: hall width, straight-line mode and
its angle, which way the surfaces face, ceilings, wall height, and the world
size of one cell.

### Which way the faces look

A hall is interior space, so by default every surface faces the space you walk
in: floors up, ceilings down, walls toward the floor. **Settings → Wall Faces**
switches this:

* **Inward** (default) — correct for a level you walk around inside.
* **Outward** — the reverse, if you want the halls read as a solid object.
* **Both Sides** — each quad is emitted twice, back to back, so the mesh is
  solid from either side. The polygon count doubles.

Note that Blender's FBX importer only reads binary FBX, so use the OBJ export
for Blender. ASCII FBX imports fine in Unity, 3ds Max and Maya.

## How the geometry works

The level is a grid of floor cells. Painting stamps a square brush along the
mouse path, so a drag leaves a corridor behind it. The mesh is then rebuilt
from scratch:

* **Floors and ceilings** are merged greedily into the largest rectangles that
  fit, so a straight 40-cell hall is one quad, not forty.
* **Walls** appear on every cell boundary with floor on exactly one side, and
  runs of those boundaries merge into single quads too. Junctions and doorways
  come out of this for free — where two halls cross, there is no boundary, so
  no wall.

A corridor with a side room and a couple of branches typically lands around
30 quads / 60 triangles. The exported OBJ is quads with per-face normals,
grouped into `floor`, `wall` and `ceiling` materials.
