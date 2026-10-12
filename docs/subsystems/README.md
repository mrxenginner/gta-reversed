# Subsystem guides

One page per game subsystem. Each page explains how the subsystem works at run time, where it lives in the repository,
which original functions drive it, the gotchas found while reversing it, and a prioritised list of what is left. Read
the page for the area you are about to touch before opening IDA.

| Page | Covers | Source folders |
|---|---|---|
| [WorldStreamingPools.md](WorldStreamingPools.md) | Pools, `CWorld` sectors, `CStreaming`, `CModelInfo`, `CFileLoader`, the `CGame::Process` frame order | `source/game_sa/` (root), `Models/`, `Core/` |
| [Peds.md](Peds.md) | `CPed`, `CPlayerPed`, cops/medics/emergency peds, ped groups, per-frame ped update | `Entity/Ped/`, `PedGroups*`, `PedIntelligence*` |
| [Tasks.md](Tasks.md) | Task tree, `CTaskManager`, per-frame task loop, conventions for `CTask*` classes | `Tasks/` |
| [Events.md](Events.md) | Event queue, priorities, `CEventHandler`, scanners, decision makers | `Events/`, `DecisionMakers/` |
| [Vehicles.md](Vehicles.md) | Vehicle class tree, type vs sub-type, `CCarCtrl`/`CCarAI`, per-frame vehicle update | `Entity/Vehicle/`, `CarCtrl*`, `CarAI*` |
| [Camera.md](Camera.md) | `CCamera`, the three `CCam`s, mode selection, script control, the `Process_*` work plan | `Camera*`, `Cam*` |
| [Rendering.md](Rendering.md) | Frame draw order, where RenderWare takes over, pipelines, visibility | `Renderer*`, `Pipelines/`, `Fx/` |
| [Audio.md](Audio.md) | The AE engine: audio hardware, entities, banks, per-frame servicing | `Audio/` |
| [Scripts.md](Scripts.md) | SCM script engine: `main.scm` loading, `CRunningScript`/`CTheScripts`, command dispatch and argument parsing, script brains, what is left | `Scripts/`, `ScriptsForBrains*` |

## Conventions used by these pages

- Status words (REVERSED, STUB, NOHOOK, KNOWN, MISSING, DORMANT) are the ones printed by
  `idb_coverage.py`; see [../BinaryMap.md](../BinaryMap.md) §2. Numbers are snapshots and drift: regenerate with
  `python .claude/skills/find-reversing-targets/scripts/idb_coverage.py --class <Class>`.
- Items marked **(Fixed: ...)** in a Gotchas section were real bugs that have since been corrected; they are kept
  because the same mistake tends to reappear elsewhere.
- For a single class, `class_dossier.py <Class>` (skill `find-reversing-targets`) gives a fresher, more detailed view
  than any page here.

## Related

- [../ReversingGuide.md](../ReversingGuide.md): end-to-end workflow for reversing and validating a function
- [../Architecture.md](../Architecture.md): build, hooks, start-up order
- [../HookRegistration.md](../HookRegistration.md): hook macros and rules
- [../BinaryMap.md](../BinaryMap.md): layout of `gta_sa.exe`, coverage, struct and bug tables
