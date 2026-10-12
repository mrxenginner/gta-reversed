---
name: find-reversing-targets
description: >
  Find and prioritise what to reverse next in gta-reversed. Lists every hook still marked `.Reversed = false`
  (grouped per file, with addresses), quick wins (classes 1-3 functions from completion), stubs that still
  call plugin::Call, NOTSA_UNREACHABLE placeholders and unimplemented script commands. Use when the user asks
  "what should I reverse next", "what is left in <class/subsystem>", or wants progress numbers.
argument-hint: "[subsystem/class filter, e.g. Ped, Audio, Cam]"
---

# Find reversing targets

Filter: `$ARGUMENTS` (may be empty)

## 1. Collect data (repo root)

```bash
# per-file summary, most missing first
python .claude/skills/find-reversing-targets/scripts/list_unreversed.py --top 30
# restricted to a subsystem / class, with every function + address
python .claude/skills/find-reversing-targets/scripts/list_unreversed.py --filter "$ARGUMENTS" --details
# quick wins: files with 1-3 functions left (finishes a category in ReversedClasses.md)
python .claude/skills/find-reversing-targets/scripts/list_unreversed.py --almost-done --top 25
```

### With the IDA database (recommended — sees functions nobody has registered yet)
`list_unreversed.py` only knows hooks that exist. The IDB knows every function of the binary. With IDA open
(ida-pro-mcp plugin running):

```bash
python .claude/skills/find-reversing-targets/scripts/ida_rpc.py export                 # once per IDB change -> .claude/cache/idb_export.json
python .claude/skills/find-reversing-targets/scripts/idb_coverage.py                   # real coverage of GAME class methods
python .claude/skills/find-reversing-targets/scripts/idb_coverage.py --classes 40      # classes with most bytes left
python .claude/skills/find-reversing-targets/scripts/idb_coverage.py --class CGarage   # every method of a class + status
python .claude/skills/find-reversing-targets/scripts/idb_coverage.py --untouched 30    # classes absent from the repo
python .claude/skills/find-reversing-targets/scripts/idb_coverage.py --missing 50 --filter Task
```
Scope is the **game's own code only**: methods of game classes in the game region. RenderWare, D3DX, libpng, CRT,
static initialisers, FLIRT library functions, free functions and unnamed `sub_`s are excluded (`--scope game` adds free
and unnamed functions back). Statuses: `REVERSED`, `STUB` (hook with `.Reversed = false`), `NOHOOK` (`plugin::Call`
stub **without** a hook - fix by adding the `RH_ScopedInstall(..., { .Reversed = false })`), `KNOWN` (address in a
comment/declaration only), `MISSING`. Don't target `CRunningScript::ProcessCommandsXToY` - script commands are
reversed one by one (`/reverse-script-command`). See `docs/BinaryMap.md` for the address map and the latest snapshot.

Other sources (use Grep):
- **Script commands not implemented:** `REGISTER_COMMAND_UNIMPLEMENTED(` in `source/game_sa/Scripts/Commands/*.cpp`
  (→ use the **reverse-script-command** skill).
- **Placeholders:** `NOTSA_UNREACHABLE()` bodies whose install is `.Reversed = false` (function not yet
  written at all; may be unused in the original — check xrefs in IDA before spending time).
- **Commented-out installs** (`//RH_Scoped...Install`) often mean "address unknown / incorrect" — good research tasks.
- **TODOs:** `Grep "TODO" source/game_sa/<Subsystem>` for partially reversed logic.
- `docs/ReversedClasses.md` / `docs/hooks.json` — CI-generated progress (may lag behind the source).

### Per-class and per-struct views
- `python .claude/skills/find-reversing-targets/scripts/class_dossier.py <Class> [--ida] [--out f.md]`: every method
  of the class with status and size, vtable slots, fields, callers and repo files. Run it before planning a class.
- `python .claude/skills/find-reversing-targets/scripts/struct_check.py [--type T] [--markdown]` (after `--export`):
  repo `VALIDATE_SIZE`/`VALIDATE_OFFSET` against the IDB structs. Current findings are in `docs/BinaryMap.md` §3.
- `python .claude/skills/find-reversing-targets/scripts/repo_to_idb.py [--apply]`: copy repo names to unnamed IDB
  functions (dry run by default). Never save the IDB without the user's consent.
- The subsystem guides (`docs/subsystems/README.md`) end with a prioritised "what is left" list per area.

### Catalogues
`python .claude/skills/find-reversing-targets/scripts/catalogues.py` regenerates `docs/ai/Vtables.md`, `Globals.md`,
`Enums.md` and `FrameCallGraph.md` (run `ida_rpc.py export` and `ida_rpc.py export-xrefs` first). `FrameCallGraph.md`
starts with the biggest unreversed functions that run every frame: good high-impact targets.

## 2. Prioritise

Rank candidates using these criteria (state the reasoning to the user):

1. **Unblocks others** — leaf functions called by many unreversed functions (IDA `xrefs_to` / `callees`).
   Reversing them first makes their callers easier.
2. **Quick wins** — the last 1-3 functions of a class (`--almost-done`) complete a category.
3. **Size** — use IDA `lookup_funcs`/`func_profile` to get function size; prefer < ~150 instructions for a
   first contribution, flag > 1000-instruction monsters (`CCam::Process*`, `CAutomobile::ProcessControl`-like).
4. **Testability** — code that runs every frame or in easily reachable places (free-roam, a specific vehicle,
   a menu) is easier to verify than an obscure mission-only path.
5. **Risk** — avoid `__usercall` functions, mid-function hooks and save/load layout code unless the user asks.
6. **No overlap** — check `git log --oneline -20 -- <file>` and open PRs (if `gh` is available:
   `gh pr list --search "<Class>"`) so you don't duplicate someone else's work.

## 3. Report format

```
### <filter or "Whole project"> — <N> unreversed hooks in <M> files

| # | Target | Address | File | Size | Why |
|---|--------|---------|------|------|-----|
| 1 | CFoo::Bar | 0x5EF420 | source/game_sa/Foo.cpp:12 | ~80 insns | leaf, used by 4 unreversed fns |
...

Recommended next step: reverse #1 with the reverse-function skill (or delegate to the gta-reverser agent).
```

Offer to start reversing the top pick; do not start without the user's go-ahead if they only asked for a list.
