---
name: reverse-function
description: >
  Step-by-step workflow to reverse ONE GTA:SA function in gta-reversed - from address/name to faithful C++,
  correct RH hook registration and a test plan. Use when asked to reverse/implement/finish a function or method,
  replace a `plugin::Call*` stub, or flip `.Reversed = false`. Argument: function name (e.g. `CCam::LookBehind`)
  or GTA address (e.g. `0x520690`).
argument-hint: "<Class::Function | 0xADDRESS>"
---

# Reverse a single function

Target: `$ARGUMENTS`

Work through the phases in order. Do not skip phase 1 or 5.

## Phase 1 — Locate it in the repo

1. Search by name and address (both cases):
   ```
   Grep  pattern: "LookBehind"            path: source
   Grep  pattern: "0x520690|0x520690"     path: source   (-i)
   ```
2. Identify:
   - the **declaration** (`.h`) — signature, `static`/`virtual`/`const`, access level;
   - the **definition** (`.cpp`) — current body: `plugin::Call*` stub, `NOTSA_UNREACHABLE()`, partial code, or missing;
   - the **registration** inside `ClassName::InjectHooks()` — macro used, address, options
     (`{ .Reversed = false }`, `.Locked`, `.StackArgumentsToPreserve`, ...).
3. If the function does not exist in the repo at all, add it (declaration in the header in the right section,
   `// 0xADDR` comment, definition in the .cpp near related functions, install in `InjectHooks()`).
   If the whole class is missing, use the **reverse-class** skill instead.

## Phase 2 — Understand the original (IDA)

1. `mcp__plugin_ida-pro-mcp_idalib__idb_list` → pick the session id (`database`). If none: ask the user for
   the IDB / `gta_sa.exe` path and `idb_open` it.
2. `lookup_funcs` with the address → confirm it is a function start and the name matches.
   Addresses in this project are **GTA:SA 1.0 US** (same as the "compact" exe).
3. `decompile` (with addresses). If anything looks off, `disasm` the range. Typical red flags:
   - `__usercall`/`__userpurge` (args in registers) → needs `StackArgumentsToPreserve` / `PreserveRegisters`
     or a wrapper; ask before improvising;
   - x87 compare sequences (`fnstsw ax; test ah, 41h`) → see ida-to-cpp §FPU;
   - `qmemcpy`/`memset` of structs → usually a struct copy / ctor;
   - `LOBYTE(x) = ...` / `HIBYTE` → bitfields or `uint8`/`int8` members;
   - jump tables → `switch` on an enum.
4. `callees` → for each callee find the C++ equivalent (`Grep` the address). Note callees that are not
   in the repo yet.
5. `xrefs_to` → read 1-2 callers to learn what the arguments/return value mean (helps naming and enum choice).
6. Types: `type_inspect`, `search_structs`, `stack_frame`, `get_global_value`, `get_string` as needed.
   Cross-check member offsets with the repo headers (`VALIDATE_OFFSET`). **The repo header wins** for naming;
   if offsets disagree, stop and report.

No IDA? Use the annotated disassembly straight from the exe (callee/global names resolved from the repo, float
constants, strings and **decoded x87 compares**):
```bash
python .claude/skills/validate-against-binary/scripts/asm_validate.py 0xADDR --dump --no-check
```
It's enough for small/medium functions. For big ones without IDA, say so and ask before continuing.

For globals and enums the function touches, grep `docs/ai/Globals.md` (address, IDB type, repo name if any) and
`docs/ai/Enums.md`; for a virtual, grep `docs/ai/Vtables.md` for the class to get the slot and confirm the address.

Before translating, check `docs/ai/InlineCatalogue.md` (functions MSVC inlines, helpers missing from the repo, globals
without `StaticRef`) and the subsystem guide in `docs/subsystems/` for the area.

Rules R1-R7 of `docs/ReversingRules.md` (fidelity) apply to every line; `docs/ai/CaseStudies.md` shows how each
one was broken before.

## Phase 3 — Write the C++

Use the **ida-to-cpp** skill as the translation reference. Checklist while writing:

- Keep `// 0xADDR` directly above the definition. In long functions add `// 0xADDR` comments at important
  blocks (from the `/*0x...*/` decompiler markers) to ease cross-referencing.
- Same order of side-effecting calls; same early-outs; same float constants; same RNG calls.
- Replace inlined engine code with the existing helper (`GetPosition()`, `IsStatic()`, `GetModellingMatrix()`,
  `CGame::CanSeeOutSideFromCurrArea()`, `RWRSTATE(...)`, `approxEqual`, `FindPlayerPed()`, pool helpers...).
- Magic numbers → existing enums (`Grep` for `enum e...` / the numeric value); else `// TODO: enum`.
- Globals → existing `StaticRef` members; new ones → `static inline auto& ms_X = StaticRef<T>(0xADDR);`
  in the class (or `static auto& s_X = StaticRef<T>(0xADDR);` inside the function if used only there).
- Use `rng::`, `rngv::enumerate`, range-for, `std::span{arr, count}`; `assert` for impossible states.
- Original bug? Keep it. If you fix it: `if (notsa::bugfixes::<Name>) { ... }` with a new entry in
  `source/reversiblebugfixes/Bugs.hpp` (`ClassName_Function_BugName`, Name/Description/Credit), or one of the
  generic ones (`GenericCrashing`, `GenericOOB`, `GenericUB`, `GenericFrameRate`). Comment the bug.
- Callees not reversed yet: call the C++ declaration anyway (it will forward to GTA through its stub) —
  never call `plugin::Call*` inline in the middle of reversed code unless there is no C++ declaration.
- Do not leave dead `plugin::Call*` lines in the reversed body.

## Phase 4 — Register the hook

In `ClassName::InjectHooks()`:

| Situation | Macro |
|---|---|
| non-virtual member / static member | `RH_ScopedInstall(Fn, 0xADDR);` |
| overloaded | `RH_ScopedOverloadedInstall(Fn, "Suffix", 0xADDR, RetT(Class::*)(Args...) [const]);` |
| free function (namespace/global) | `RH_ScopedGlobalInstall(Fn, 0xADDR);` |
| virtual | `RH_ScopedVMTInstall(Fn, 0xADDR);` (scope must be `RH_ScopedVirtualClass(Cls, vtblAddr, nVirtFns)`) |
| virtual not overriding base | add `{ .Overrides = false }` |
| ctor / dtor wrappers | `RH_ScopedInstall(Constructor, 0xADDR);` / `RH_ScopedInstall(Destructor, 0xADDR);` or `RH_ScopedVMTDestructorInstall` |
| register-passed args | `{ .StackArgumentsToPreserve = N, .PreserveRegisters = true }` |

- Now reversed → **remove** `{ .Reversed = false }`.
- Still a stub (`plugin::Call*` to its own address) → **must keep** `{ .Reversed = false }`
  (otherwise our hook jumps to GTA which jumps back to our hook: infinite recursion).
- New class → follow **reverse-class** (InjectHooks + InjectHooksMain).

## Phase 5 — Verify (against the binary)

Follow the **validate-against-binary** skill. Minimum:

1. `python .claude/skills/review-reversal/scripts/check_hooks.py <your .cpp/.h>` → zero `[E]`.
2. `python .claude/skills/validate-against-binary/scripts/asm_validate.py Class::Function` → zero `[MISSING]`;
   explain every `[CHECK]` (or fix it). Precision mismatches are real bugs - use the binary's exact constant.
3. Re-read your C++ next to `--dump` (decoded compares) line by line: conditions, constants, loop bounds,
   argument order, integer widths, return value on every path.
4. If the function is testable in isolation (pure math, copyable objects, globals you can snapshot), write a
   `NOTSA_DIFFTEST` in `source/difftests/tests/<Subsystem>DiffTests.cpp` with boundary inputs + fuzzing.
5. Build: `cmake --build build --config Debug --target gta_reversed` (re-run `cmake build` first if you added files).
   Report failures honestly.
6. For anything non-trivial, launch the **gta-reversal-reviewer** agent.

## Phase 6 — Report

Tell the user:
- `Class::Function` (0xADDR) — reversed / partially reversed;
- files changed (as links);
- hooks still `.Reversed = false` and why;
- assumptions, `TODO`s, original bugs found (and whether gated);
- validation done: asm_validate result, diff test (name, checks, seed) or why none, build result;
- **in-game test**: where this code runs (mission, area, vehicle, menu…), and that they can toggle the hook in
  the in-game hooks GUI (debug menu, F7) to A/B compare with the original.
