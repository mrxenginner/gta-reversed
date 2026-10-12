# difftests/ — differential tests (ours vs the original machine code, in-process)

Workflow and guidelines: [../../docs/ReversingGuide.md](../../docs/ReversingGuide.md) "Level 2: differential tests";
skill `/validate-against-binary`. Framework: `DiffTest.hpp` (API + doc comments), runner/report in `DiffTest.cpp`.
Examples: `tests/GeneralDiffTests.cpp` (fuzzing scalar functions), `tests/VectorMatrixDiffTests.cpp` (matrix
helpers, `CompareFloats`), `tests/RectDiffTests.cpp`. New `tests/*.cpp` files are picked up by the CMake glob (re-run the
configure step after adding one). Unity build: keep helpers in `namespace {}` with names unique across test files.

## Writing a test
- `NOTSA_DIFFTEST("Class::Fn", "<Category>/<Class>/<Fn>", ...) { ... }` — the extra args are the hook paths (as shown
  in the F7 hooks GUI, overloads carry their suffix, e.g. `"Core/CMatrix/SetScale-fff"`). Those hooks are unhooked
  during the test, so `GTAFn<Sig>(0xADDR)` runs the original and a normal call runs ours. Callees keep their state.
- `NOTSA_DIFFTEST_INGAME` if the function needs the world/pools/player; such tests are skipped at startup.
- Helpers: `CompareCall` (one input), `Fuzz(ctx, label, n, gen, original, ours, tol)`, `CompareObjects` /
  `CompareBytes` (mutated objects, with `ignore` ranges), `CompareFloats` (float arrays with ULP report),
  `MemorySnapshot` / `CompareSideEffects` (globals), `ctx.Check`, `ctx.Note`.
- Inputs: `ctx.RandomFloat/RandomInt/RandomVector` (seeded, reproducible) and `ctx.InterestingFloat()`
  (0, -0, ±1, ±PI, huge, tiny...). Add hand-picked boundary values from the asm (`std::nextafter` either side).
- For mutating member functions start both objects from byte-identical copies (`memcpy`, see `MakeMatrixPair`) and
  give out-params the same initial value so untouched ones compare equal.

## Preconditions and tolerance
- Only generate inputs the original is defined for: valid enum/flag combinations (e.g. the 6 Euler orders in the
  `ConvertToEulerAngles` test), non-null pointers, domains its callers actually use. Include special cases the
  original handles explicitly (e.g. the zero vector in `Normalise`).
- Comparisons are bit-exact by default. Use `Tolerance{ .Ulps = N }` (plus `.Abs` near 0 or scaled by magnitude)
  only where x87 extended precision (original) vs SSE (ours) legitimately differs (sin/cos/atan/sqrt chains,
  cancellation), and comment why. A mismatch beyond a few ULP is a bug (wrong constant, float vs double).
- Don't diff-test functions that consume `rand()` (different CRT generators), `__usercall` functions, or functions
  with side effects you don't snapshot and restore.

## Running
- In game: F7 → Tools → Diff tests → Run all (also runs `_INGAME` tests).
- Unattended: `gta_sa.exe --run-difftests[=filter] --difftests-exit` (optional `--difftests-out=<file>`). Runs right
  after hook installation in `DllMain`; exit code 1 if any test failed.
- The report is written to `<game>/scripts/difftests.log`: the ASI loader sets the working directory to `scripts/`.
