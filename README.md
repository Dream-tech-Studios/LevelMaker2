# LevelMaker

A small Windows desktop app for blocking out levels: hold the left mouse button
in the plan view, trace a hall, and watch the walls build themselves in a live
3D preview. Export the result as a Wavefront OBJ.

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

### Files

* **Ctrl+N / Ctrl+O / Ctrl+S** — new, open, save a `.lvl` level
* **Ctrl+E** — export the mesh as `.obj` (a matching `.mtl` is written next to it)
* **Ctrl+Z / Ctrl+Y** — undo / redo, one stroke at a time
* `LevelMaker.exe path\to\level.lvl` opens that level on startup

Under **Level** you can set the wall height and the world size of one cell,
which is what the preview and the export use.

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
