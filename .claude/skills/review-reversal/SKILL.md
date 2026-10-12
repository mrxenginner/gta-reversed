---
name: review-reversal
description: >
  Review checklist + automated checker for reversed GTA:SA code in gta-reversed - hook registration
  (InjectHooks/InjectHooksMain/.Reversed=false/self-recursive plugin::Call), fidelity against the IDA
  decompilation, memory layout asserts, bug-fix gating and CodingGuidelines. Use before committing, before a PR,
  or when asked to review/validate a reversal.
argument-hint: "[files... | --base <git ref>]"
---

# Review a reversal

Arguments: `$ARGUMENTS` (files to check; empty = everything changed vs `master` + uncommitted)

## 1. Automated checks

```bash
python .claude/skills/review-reversal/scripts/check_hooks.py $ARGUMENTS
```

What it reports (regex heuristics — confirm before acting):

| Tag | Meaning | Usual fix |
|---|---|---|
| `[E] ... never called from source/InjectHooksMain.cpp` | class hooks are never installed | add `#include` + `Cls::InjectHooks();` to `InjectHooksMain()` |
| `[E] address 0x... is already installed` | duplicated install | remove/fix the wrong address |
| `[E] ... still forwards to GTA via plugin::Call but is installed as reversed` | **infinite recursion** at runtime: GTA addr → our hook → plugin::Call → GTA addr → ... | add `{ .Reversed = false }` or actually reverse it |
| `[E] InjectHooks is private but there is no friend` | won't compile | add `friend void InjectHooksMain();` |
| `[W] marked .Reversed = false but body has no plugin::Call` | maybe reversed and forgot to flip the flag | remove the flag if the body is complete |
| `[W] annotated with 0x... but no RH_*Install uses that address` | function never hooked | add an install (unless inlined/unused in the original) |
| `[W] RH_ScopedVMTInstall requires RH_ScopedVirtualClass` | wrong scope macro | use `RH_ScopedVirtualClass(Cls, vtbl, nVirtFns)` |

`--all` checks the whole code base (many pre-existing warnings; useful for clean-up tasks, not for PR review).

Then validate every touched function against the original binary (see the **validate-against-binary** skill):

```bash
python .claude/skills/validate-against-binary/scripts/asm_validate.py --file <changed .cpp>   # summary per hook
python .claude/skills/validate-against-binary/scripts/asm_validate.py Class::Function         # details
```
Every `[MISSING]` is blocking until explained; `PRECISION MISMATCH` is always blocking.
Check whether the change added/updated a `NOTSA_DIFFTEST` and whether it passes.

Also available:
- `python .claude/skills/review-reversal/scripts/add_missing_hooks.py [--file F] [--apply]` registers `plugin::Call`
  stubs that have no hook as `{ .Reversed = false }` (dry run by default; skips virtuals, templates and ctors).
- `check_hooks.py` also runs automatically after every Edit/Write on `source/` (PostToolUse hook in
  `.claude/settings.json`) and on PRs (`.github/workflows/hook-registration.yml`). It checks that
  `RH_ScopedVMTInstall` addresses are in the class vtable: a wrong one throws in `InjectHooksMain`.

## 2. Manual checklist

### Fidelity (most important)
- [ ] Decompiled the original at each installed address (IDA MCP `decompile`/`disasm`, or `asm_validate.py --dump`) and compared line by line.
- [ ] `asm_validate.py` has no `[MISSING]` (or each one is explained); no `PRECISION MISMATCH`.
- [ ] Testable functions have a passing `NOTSA_DIFFTEST` (boundary inputs + fuzzing; tolerance justified if not bit-exact).
- [ ] All side-effecting calls present, same order, same arguments (incl. implicit `this`).
- [ ] Conditions: `<`/`<=`, signedness, FPU compare direction (see ida-to-cpp §3), `&&`/`||` short-circuit order.
- [ ] Constants identical (float literals, enum values, array sizes, loop bounds).
- [ ] Integer widths and truncation (`int8`/`uint16` members, `_ftol` truncation, sign extension).
- [ ] RNG calls: same number, same order.
- [ ] Every return path returns the same value as the original.
- [ ] Deviations are gated (`notsa::bugfixes::*` / `notsa::IsFixBugs()`) and commented; new bugfix entries added
      to `source/reversiblebugfixes/Bugs.hpp` with `Name`, `Description`, `Credit`.

### Hooks (docs/HookRegistration.md)
- [ ] `static void InjectHooks();` in the class; called in `source/InjectHooksMain.cpp`.
- [ ] `friend void InjectHooksMain();` only when needed.
- [ ] Every function of the class has an `RH_*Install`; addresses verified in IDA.
- [ ] Stubs (`plugin::Call*`) have `{ .Reversed = false }`; reversed functions don't.
- [ ] Virtuals: `RH_ScopedVirtualClass` with the right vtable address and count; `.Overrides = false` when the
      class does not override the base function.
- [ ] Register-arg functions have `StackArgumentsToPreserve`/`PreserveRegisters`.

### Layout
- [ ] `VALIDATE_SIZE`/`VALIDATE_OFFSET` present for new types and still valid; no member reordering.
- [ ] `StaticRef` addresses match IDA; `const` tables copied as `constexpr` only if never written by the game.

### Guidelines (docs/CodingGuidelines.md)
- [ ] Naming: `m_`/`ms_`/`s_`/`g_`, camelCase locals, no hungarian.
- [ ] `auto*`, `const auto&`, explicit type for copies, `f` float suffix, fixed-width ints, no `DWORD`.
- [ ] `rng::`/`rngv::`, range-for, `std::span`/`rngv::take` for count+array, `std::array`.
- [ ] SA types instead of RW types (except `RwMatrix`), `RWRSTATE(...)` for render states.
- [ ] Helpers instead of raw member access; `override` instead of `virtual` in derived classes.
- [ ] `// 0xADDR` above every function; address comments in long functions.
- [ ] No unrelated changes / reformatting; clang-format applied to changed lines only.

### Build & test
- [ ] `cmake --build build --config Debug --target gta_reversed` passes (or the failure is reported).
- [ ] In-game test plan written: where the code runs, and toggle the hook in the hooks GUI (F7 debug menu) to A/B
      compare against the original.

## 3. Output

Use the format defined by the **gta-reversal-reviewer** agent (Verdict / Blocking / Non-blocking / Suggested
in-game test). Quote file:line and the exact fix for every blocking issue.
