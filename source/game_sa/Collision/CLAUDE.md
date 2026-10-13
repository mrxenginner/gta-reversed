# Collision/ — col shapes, col models and the CCollision tests

Full guide: [../../../docs/subsystems/CollisionPhysics.md](../../../docs/subsystems/CollisionPhysics.md) (col models,
Test vs Process, `CPhysical` response, the `CWorld::Process` collision loop, open bugs). Physics response lives in
`../Entity/Physical.cpp`, not here.

## Files to look at first
- `Collision.h/.cpp` — `CCollision`, all-static: `Test*` (bool only) and `Process*` (fills a `CColPoint` and shrinks
  `maxTouchDistance`/`mindistsq`) for sphere/box/line/triangle/disk pairs, `ProcessColModels`, line of sight,
  camera collision (`CheckCameraCollision*`, `SphereCastVs*`). ~3,500 lines; read only the function you need.
- Shapes: `ColSphere`, `ColBox` (+ `Box`), `ColLine`, `ColDisk`, `ColTriangle`, `ColTrianglePlane`, `ColPoint`
  (contact result with `m_nSurfaceTypeA/B`), `ColSurface` (material, piece, lighting, light; 4 bytes).
- `ColModel.h` — `CColModel` (0x30): bounding box/sphere, `m_nColSlot`, flags, `CCollisionData* m_pColData`.
- `CollisionData.h` — `CCollisionData` (0x30): counts + pointers; use the span helpers `GetSpheres()`, `GetBoxes()`,
  `GetTris()`, `GetTriPlanes()`, `GetLines()`, `GetShdwTris()`. Vertices are `CompressedVector` (`../CompressedVector.h`,
  fixed point /128); use the type's conversions, not the deprecated `CompressVector`/`UncompressVector`.
- `ColStore.*` (col file streaming, `ColDef` pool), `ColAccel.*` (col cache for faster loading), `TempColModels.*`
  (built-in models: ped, door, bbox, ...), `CollisionEventScanner.*` (collision -> AI events, see Events.md).

## Conventions and pitfalls
- Classes here use `RH_ScopedClass` + category `"Collision"` (`CCollision` uses `RH_ScopedCategoryGlobal()`).
- `CCollision::s_DebugSettings` is NOTSA: per shape-pair enable switches and `AllowLineOriginInsideSphere` (default
  `false` = original behaviour: a line starting and ending inside a sphere does not hit it). Keep the
  `IsEnabled(...)` early-outs when touching those functions, and keep the default equal to the original.
- Face groups: when `bHasFaceGroups` is set, a `uint32` count and `FaceGroup[]` sit *before* `m_pTriangles` with no
  pointer to them (see the long comment in `CollisionData.h`). Only valid if the col model is a single allocation.
- `m_pTrianglePlanes` is allocated lazily (`CalculateTrianglePlanes` / `RemoveTrianglePlanes`) and is the only
  pointer outside the single data buffer.
- `m_pLines` and `m_pDisks` share a union, selected by `bUsesDisks`.
- `TEST_COLLISION_FUNCS` enables randomized self-tests (`CCollision::Tests`) run before hooks are installed; for
  checks against the original prefer a difftest (`source/difftests/`).
- Many functions use `DotProduct`/`GetMatrix` inlined; see `docs/ai/InlineCatalogue.md` when matching the asm.
