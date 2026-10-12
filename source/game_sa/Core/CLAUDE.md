# Core/ — math types and engine containers

Guide: [../../../docs/subsystems/WorldStreamingPools.md](../../../docs/subsystems/WorldStreamingPools.md) §4 (pools
and handles) and §3 (world lists built from these containers). Everything else depends on this folder: a wrong
result here shows up everywhere, so difftest changes (`source/difftests/tests/VectorMatrixDiffTests.cpp`,
`RectDiffTests.cpp`).

## Contents
- Math: `Vector.h` (`CVector`), `Vector2D.h`, `Matrix.h` (`CMatrix`, 0x48: right/forward/up/pos + `m_pAttachMatrix`
  to an `RwMatrix`, `UpdateRW()` writes back), `Quaternion.h`, `Rect.h` (`CRect`: default-constructed flipped; the 4-float constructor
  takes `(left, bottom, right, top)` = minX, minY, maxX, maxY, a legacy naming quirk).
- Containers: `Pool.h` (`CPool<T, S>`: handles = `(index << 8) | state byte`, `GetAtRef` rejects stale handles,
  NOTSA `GetAllValid()`), `Store.h` (`CStore<T, N>` fixed arrays used by model infos), `LinkList.h`/`Link.h`,
  `PtrList*`/`PtrNode*` (single/double linked entity lists of the world sectors, node pools), `EntryInfoList/Node`
  (which sector lists a physical is in), `MatrixLink*`, `QuadTreeNode.h`, `OctTree*`, `SArray.h`, `List_c`/`ListItem_c`.
- `KeyGen.h` — `CKeyGen::GetKey` / `AppendStringToKey` (name hashes for model infos, anims); `constexpr`.
- Hooks: `RH_ScopedClass` + category `"Core"`.

## Conventions
- Prefer the NOTSA helpers that replace the original free functions: `TransformPoint` / `TransformVector` /
  `InverseTransformPoint` / `InverseTransformVector` (instead of `MultiplyMatrixWithVector`, `Multiply3x3`),
  `a.Cross(b)` / `a.Dot(b)`. The original in-place forms (`Cross_OG`, ...) stay hooked for GTA callers.
- Small members (`Magnitude`, `operator+`, `GetPosition`, ...) are inlined everywhere in the original: see
  `docs/ai/InlineCatalogue.md` before concluding a call is missing.

## Precision (most bugs here)
- `PI`, `TWO_PI`, `HALF_PI`, `SQRT_2`, `LOG2_E` in `source/game_sa/common.h` are 5-6 digit roundings (`3.14159f`),
  not the game's float constants (`3.1415927f`, 11 ULP apart). Where the original loads the exact value, write the
  exact literal instead of using them.
- The original runs on x87 with extended precision; sums of products and sin/cos/sqrt chains are only rounded to
  float when stored. Where the original accumulates in registers across a loop, accumulate in `double` and round at
  the same point. Keep the operation order of the original for dot/cross products.
- `Normalise`/`NormaliseAndMag` special-case a non-positive squared length (sets `x = 1`); keep it.
- Difftests compare with small ULP tolerances (`CompareFloats`, `Tolerance{ .Ulps = 4 }`); anything larger is a
  real difference (e.g. a wrong index in `CMatrix::ConvertToEulerAngles` was found this way).
