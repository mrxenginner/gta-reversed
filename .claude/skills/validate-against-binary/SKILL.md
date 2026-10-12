---
name: validate-against-binary
description: >
  Validate reversed GTA:SA code against the ORIGINAL machine code of gta_sa.exe, in two layers:
  (1) static - asm_validate.py disassembles the original function from the exe and checks callees, globals,
  float constants (bit-exact) and strings against our C++, and prints annotated disassembly with decoded x87 compares;
  (2) dynamic - NOTSA_DIFFTEST differential tests that call the original and our implementation with the same inputs
  inside the game and compare results. Use after reversing/changing a function, when reviewing, when a reversal
  "behaves differently", or to read a function's asm without IDA.
argument-hint: "<Class::Function | 0xADDRESS | --file path.cpp>"
---

# Validate a reversal against the binary

Target: `$ARGUMENTS`

Validation has 3 levels. Do **all that apply**, in order — each one catches different bugs:

| Level | Tool | Catches | Cost |
|---|---|---|---|
| 1. Static fingerprint | `asm_validate.py` | missing/extra calls, wrong/imprecise constants (`PI = 3.14159f` vs `3.1415927f`), unmapped globals, wrong strings | seconds, no game |
| 2. Differential test | `NOTSA_DIFFTEST` | any behavioural difference for the tested inputs: branches, math, precision, out-params, side effects | minutes (write test) + game |
| 3. In-game A/B | hooks GUI (F7) | integration issues, visuals, timing, things hard to unit-test | manual |

## Level 1 — static fingerprint (`asm_validate.py`)

Setup once: the exe path goes in `.claude/gtasa_exe_path.txt` (one line, the **compact** exe, 5,189,632 bytes), or env var
`GTASA_EXE`, or `--exe`. Requires `capstone` (`pip install capstone`).

```bash
# one function (by name or address)
python .claude/skills/validate-against-binary/scripts/asm_validate.py CGeneral::LimitAngle
python .claude/skills/validate-against-binary/scripts/asm_validate.py 0x53CB00
# every reversed hook of a file - short summary
python .claude/skills/validate-against-binary/scripts/asm_validate.py --file source/game_sa/General.cpp
# annotated disassembly (callee names, globals, float values, strings, decoded x87 compares) - great for reversing
python .claude/skills/validate-against-binary/scripts/asm_validate.py 0x53CB50 --dump --no-check
# our implementation isn't found automatically (templates, unusual names)
python .claude/skills/validate-against-binary/scripts/asm_validate.py 0x46F800 --cpp-file source/game_sa/Tasks/TaskTypes/SeekEntity/TaskComplexSeekEntity.h --cpp-func ControlSubTask
```

How it works: disassembles the original from the PE file (function extent from branches/jump tables, stops at the last
`ret`), then maps every call target / data address to repo symbols using `RH_*Install`, `// 0xADDR` comments,
`StaticRef`s, declarations like `Foo(...); // 0xADDR` and `plugin::Call`/cast-call wrappers. It searches our body **and
the helpers it calls (2 levels)**, because the original often inlined what we call through helpers.

Reading the result:

| Tag | Meaning | What to do |
|---|---|---|
| `[MISSING] 0x... X::Fn - not called by our code` | the original calls it, ours doesn't (nor its helpers) | real omission, or equivalent inline code - check the original call site with `--dump` |
| `[MISSING] 0x... - not in the repo` | callee unknown to the repo and not trivial | declare it (stub + `{ .Reversed = false }` hook) and call it, or reverse it |
| `[MISSING] ... PRECISION MISMATCH (N ULP off)` | constant in ours differs from the binary by > 2 ULP | use the exact value (`3.1415927f`, not `PI` = `3.14159f`) - **real behaviour difference** |
| `[MISSING] unknown global` | writable global with no `StaticRef` | add `StaticRef<T>(0xADDR)` / map it to the right variable |
| `[CHECK] small leaf helper` | tiny callee not in the repo (CVector ops, POD ctors) | usually an operator in ours; `--dump 0xADDR` it if unsure |
| `[CHECK] _ftol` / `CRT/library function` | float→int cast / C runtime call | make sure ours casts / calls the equivalent `std` function |
| `[CHECK] constructor/destructor of X` | implicit ctor/dtor of a local/temporary/base | normally implicit in C++ - verify object lifetime |
| `[CHECK] member/element of g` | access into a global object/array | make sure the same field/index is accessed |
| `[CHECK] ... (1-2 ULP off)` | constant computed differently (`x / 1.1f` vs `x * 0.90909f`) | usually fine; may differ in the last bit |
| `[CHECK] float not found` | constant not visible in our code | computed elsewhere? wrong value? |
| `[INFO] virtual call ... slot N` | vtable call | check ours calls the virtual at that slot (declaration order) |

A clean result is **necessary, not sufficient**: it doesn't check control flow. Always also read `--dump` next to our
C++ for branch conditions (the `jump if st(0) <= X` annotations decode x87 compares for you).

Exit code: 0 clean, 1 findings (`--strict` also fails on `[CHECK]`), 2 environment error.

### Whole repository
`python .claude/skills/validate-against-binary/scripts/validate_all.py` runs Level 1 on every reversed function
(about 20 minutes, writes `.claude/cache/validate_all.jsonl`); `inline_catalogue.py --out docs/ai/InlineCatalogue.md`
turns it into the catalogue. Check the catalogue before reporting a "missing call": the callee may be one the
original inlines (section 1).

## Level 2 — differential tests (`NOTSA_DIFFTEST`)

Framework: `source/difftests/DiffTest.hpp`. Tests live in `source/difftests/tests/<Subsystem>DiffTests.cpp`
(globbed by CMake — re-run `cmake build` after adding a file). Example file: `source/difftests/tests/GeneralDiffTests.cpp`.

While a test runs, the hooks it lists are **Unhooked**: GTA's original code and ours are both unpatched, so
`GTAFn<Sig>(0xADDR)` calls the original and a normal C++ call runs ours. Callees keep their hook state, so both
versions call the same callees — only the function under test differs.

### Hook path
`<Category>/<Scope>/<Name>` exactly as in the hooks GUI: category from `RH_ScopedCategory`/`RH_ScopedCategoryGlobal()`
(`Global`), scope = class/namespace, name = function (overloads: `Name-Suffix`, e.g. `GetRadianAngleBetweenPoints-`).
Examples: `Global/CGeneral/LimitAngle`, `Entity/Ped/CPed/SetMoveAnim`. A wrong path makes the test *Skipped*.

### Patterns

```cpp
#include "StdInc.h"
#include <difftests/DiffTest.hpp>
using namespace notsa::difftest;

// 1. Pure function: fuzz the return value
NOTSA_DIFFTEST("CGeneral::LimitRadianAngle", "Global/CGeneral/LimitRadianAngle") {
    Fuzz(ctx, "LimitRadianAngle", 10'000,
        [](Context& c) { return std::tuple{ c.InterestingFloat(-50.f, 50.f) }; },  // args generator (seeded)
        GTAFn<float(__cdecl*)(float)>(0x53CB50),                                   // original
        &CGeneral::LimitRadianAngle);                                              // ours
}

// 2. Hand-picked edge cases (boundaries of every comparison you saw in the asm!)
for (const auto a : { -180.f, 180.f, -0.f, 540.f }) {
    CompareCall(ctx, "LimitAngle", GTAFn<float(__cdecl*)(float)>(0x53CB00), &CGeneral::LimitAngle, std::tuple{ a });
}

// 3. Member function on a copyable object: run each on its own copy, compare the object bytes
//    (full file: source/difftests/tests/RectDiffTests.cpp)
NOTSA_DIFFTEST("CRect::Restrict", "Core/CRect/Restrict") {
    const auto original = GTAFn<void(__thiscall*)(CRect*, const CRect*)>(0x404200); // `const CRect&` is a pointer in the ABI
    for (size_t i = 0; i < 5'000; i++) {
        const CRect initial = RandomRect(ctx), restriction = RandomRect(ctx);
        CRect orig = initial, ours = initial;
        original(&orig, &restriction);   // original
        ours.Restrict(restriction);      // ours
        CompareObjects(ctx, "Restrict", orig, ours); // reports differing byte ranges -> map them to members via VALIDATE_OFFSET
    }
}
//    const methods returning a value: wrap both in lambdas and use CompareCall (see IsPointInside in the same file)

// 4. Out-params: compare manually with ctx.Check + ApproxEqual
// 5. Globals / side effects: CompareSideEffects(ctx, "Fn", [&](bool orig) { orig ? GTAFn<...>(0x...)() : Fn(); },
//        std::array{ std::pair<void*, size_t>{ &CFoo::ms_Array, sizeof(CFoo::ms_Array) } });
//    -> snapshots the regions, runs the original, restores, runs ours, compares the regions.
// 6. Needs the world (peds, vehicles, pools...): NOTSA_DIFFTEST_INGAME(...) - only run from the F7 menu in-game.
```

Choosing inputs:
- Take every constant from the `--dump` compares and test **exactly at, just below and just above** it
  (`std::nextafter`). Boundary bugs (`<` vs `<=`) only show there.
- Include `0`, `-0`, negatives, huge values, NaN only if the game can produce it.
- Keep fuzz counts moderate (≤ 10k) - functions with loops on the input (`LimitAngle(1e9)`) can be slow.

Tolerance: default is **bit-exact**. Our build may use SSE while the original uses x87 80-bit intermediates; only for
transcendental functions (`atan2`, `sqrt` chains) use `Tolerance{ .Ulps = 4 }` and say why in a comment. Never raise the
tolerance to hide a constant mismatch reported by level 1.

Don't diff-test (or isolate carefully):
- functions consuming the RNG — our `rand()` and GTA's CRT `rand()` are different generators;
- functions with irreversible side effects (spawning, freeing, streaming requests, audio) unless you snapshot/restore;
- `__usercall` functions (custom register ABI) - calling them via `GTAFn` is undefined.

### Lessons from the existing tests
- Float results: use `CompareFloats`/`Tolerance` with ULPs, not `CompareBytes`. The original keeps x87 extended
  precision; an accumulating loop (e.g. `LimitRadianAngle`) needs `double` in our code to match bit for bit.
- A function whose hook is not registered is still compared directly against the GTA address (the report adds a
  note); register it anyway.
- Random inputs must respect preconditions (e.g. `CRect` must not be flipped, or the assert fires).
- Run unattended with `gta_sa.exe --run-difftests[=filter] --difftests-exit`; asserts go to stderr and the report is
  written to `<game>/scripts/difftests.log` (the ASI loader sets the working directory to `scripts/`).

### Running

- **In-game:** F7 → *Tools* → *Diff tests* → *Run all* (or per test). Failures show the inputs and both outputs; the seed
  makes them reproducible.
- **Command line (automation):** start the game with
  `--run-difftests[=filter] [--difftests-out=difftests.log] [--difftests-exit]`.
  Tests run right after the hooks are injected - inside `DllMain`, before the game initialised anything, so only
  self-contained tests make sense there (`*_INGAME` tests are skipped). The report is written to the file (default
  `difftests.log` in the working dir) and with `--difftests-exit` the process exits with code 1 if any test failed.

## Level 3 — in-game A/B

F7 → hooks GUI: toggle the function between ours / GTA while reproducing the scenario (see the reverse-function skill's
report section for picking a scenario). Watch for visual/behaviour differences, asserts and the log.

## Report

```
### Validation: <Class::Fn> @ 0xADDR
- Static (asm_validate): clean | N findings (list + verdict for each)
- Diff test: <test name> - passed (N checks, seed 0x...) | failed (first mismatch: inputs, original, ours) | not written (why)
- In-game: tested <scenario> | not tested
```
