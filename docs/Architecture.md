# Architecture

How gta-reversed is built, how it gets into the game, and where everything lives. For *how to reverse* see
[ReversingGuide.md](ReversingGuide.md). For the layout of the original executable see [BinaryMap.md](BinaryMap.md).

Per-subsystem run-time details are in [subsystems/README.md](subsystems/README.md).
---

## 1. The big picture

```
gta_sa.exe (1.0 US compact) ──loads──> ASI loader (vorbisFile.dll) ──loads──> scripts/gta_reversed.asi
                                                                                   │ DllMain
                                                                                   ▼
                                     every reversed function: GTA address ──jmp──> our C++ (reversible hook)
```

- The project builds **`gta_reversed.asi`**, a DLL. An ASI loader injects it into the unmodified game.
- While the DLL loads, `InjectHooksMain()` installs about 8,000 **reversible hooks**. Each one patches 5 bytes
  (`E9 rel32`) at the start of an original function, so the game runs our re-implementation instead.
- Functions not yet reversed are still declared in C++. They forward to the original code (`plugin::Call*`) and
  their hook stays redirected to GTA.
- Every hook can be switched at runtime from the in-game F7 menu, so you can A/B-compare ours against the original.
- The long-term goal is a **standalone executable**. The `GTASA_STANDALONE` option exists, but for now it only
  builds a helper executable used by CI to list the hooks.

---

## 2. Build

| Item | Value |
|---|---|
| Toolchain | Visual Studio 2026 (MSVC 19.5), x86 / Win32 only, C++23, static CRT |
| Package manager | Conan 2 (`conanfile.py`, profile `conan/profiles/windows-msvc.txt`) |
| Configure | `python setup.py` (wraps `conan install`; `-b` also builds) |
| Build | `cmake --build build --config Debug --target gta_reversed` |
| Output | `bin/<Debug|Release>/gta_reversed.asi` (+ `.pdb`), or `.exe` in standalone mode |
| Dependencies | spdlog 1.17 (std::format), nlohmann_json 3.12, Dear ImGui 1.92.9 docking, SDL3 3.4 (optional, on by default through Conan), Tracy, boost (header-only), ogg/vorbis, libjpeg-turbo, imgui-filebrowser (FetchContent) |

Notable build settings (`source/CMakeLists.txt`):
- **Warnings are errors** (`COMPILE_WARNING_AS_ERROR ON`, `/W3`, with a few warnings disabled).
- **No `/fp` or `/arch` flag**, so MSVC's x86 default applies (SSE2). The original game uses x87, so tiny float
  differences are possible (see the difftest tolerance rules).
- Always-on defines: `FIX_BUGS`, `USE_GTASA_ALLOCATOR`, `EXTRA_DEBUG_FEATURES`, `NOTSA_WINDOWED_MODE`, `USE_D3D9`,
  `SA_SKINNED_PEDS`.
- **Sources are globbed** (`GLOB_RECURSE`). After adding a file, re-run `cmake build` (configure) before building.
- PCH is `StdInc.h`/`StdInc.cpp`. `notsa.natvis` adds debugger visualisers for `CPool`, the linked lists, `FixedFloat`
  and friends, `WEnum`...

CMake options:

| Option | Default | Effect |
|---|---|---|
| `GTASA_UNITY_BUILD` | OFF | Unity build in batches of 16. CI builds both modes, so file-local names (`namespace {}` / `static`) must not clash between files |
| `GTASA_WITH_OPENAL` | OFF | OpenAL audio backend instead of DirectSound |
| `GTASA_USE_SDL3` | OFF (Conan: ON) | SDL3 window/input instead of Win32 |
| `GTASA_STANDALONE` | OFF | Build an `.exe` instead of the `.asi` (`NOTSA_STANDALONE`) |
| `GTASA_STANDALONE_DUMP_HOOKS_ONLY` | OFF | Standalone build that only registers hooks (as `NullHook`s) and dumps them. Used by CI to generate `docs/hooks.json` |
| `GTASA_WITH_SCRIPT_COMMAND_HOOKS` | OFF | Register script command handlers as toggleable hooks |
| `GTASA_WITH_LTO` | OFF | Link-time optimisation |
| `GTASA_NO_EDIT_AND_CONTINUE` | OFF | `/Z7` instead of `/ZI` (and `/MAP`), used by the Wine build |
| `GTASA_WITH_CLEO_COMMANDS` | OFF | **Has no effect today.** Nothing maps it to the `NOTSA_WITH_CLEO_SCRIPT_COMMANDS` define, and the CLEO headers that define would include don't exist |

The Linux cross-build through msvc-wine lives in `contrib/msvc-wine/`.

---

## 3. Startup sequence (ASI mode)

`source/dllmain.cpp`, `DLL_PROCESS_ATTACH`. This runs inside the loader lock, before the game's own initialisation:

1. Refuse to load if RenderWare is already running (`*(RwCamera**)0xC1703C`).
2. Open a console, load debug symbols (dbghelp), start logging (`logs/log.log` plus console). Logging also installs
   an unhandled-exception filter that logs a stack walk.
3. `CommandLine::Load`. Wait for a debugger if `--debug` was passed.
4. Read `gta-reversed.ini` from the working directory (`[FastLoader]`, `[Misc]`).
5. `RHManager::CreateInstance()`, then **`InjectHooksMain()`**:
   - windowed-mode hooks
   - then every class's `InjectHooks()` (`source/InjectHooksMain.cpp`, grouped by subsystem)
   - then `CommandLine::PostHooksInjected()`, which applies `--use-gta-for` and friends, `--dump-hooks-to` and
     `--run-difftests`

The game then reaches its `WinMain` (`0x748710`). That function is itself hooked, to `NOTSA_WinMain`
(`source/app/platform/win/WinMain.cpp`), which runs: RenderWare/skeleton initialisation, SDL or Win32 window,
input, `gta_sa.set`, `UIRenderer` (ImGui), and finally the main loop.

In standalone mode the order differs (command line before console). No ini is loaded, and hooks become `NullHook`s
that only record addresses. `StaticRef` also returns a dummy buffer per type, so standalone builds can't run game
code yet.

---

## 4. Reversible hooks (`source/reversiblehooks/`)

| Piece | Role |
|---|---|
| `StaticOneWayHook` | Patches `from` with `E9 rel32` → `to` (5 bytes, saved for restoring). It follows one `EB`/`E9` thunk at the target (incremental linking or Edit & Continue). With `StackArgumentsToPreserve`/`PreserveRegisters` it jumps to a generated `pushad`… `call`… `popad; ret n` thunk instead (for register-passed arguments) |
| `StaticTwoWayHook` | Two one-way hooks: `_FromGTA` (GTA→ours) and `_ToGTA` (ours→GTA). States: `RedirectToOurs`, `RedirectToGTA`, `Unhooked` |
| `VirtualHook` | `StaticTwoWayHook` for direct calls plus a `VMTRedirectHook` that swaps the vtable entry for virtual dispatch. Our vtable is found through the exported `??_7Class@@6B@` symbol, which is why polymorphic classes need `NOTSA_EXPORT_VTABLE` |
| `VMTRedirectHook` | Vtable swap only. Used with `{ .Overrides = false }` |
| `VirtualDestructorHook<T>` | Vtable slot 0 (scalar deleting destructor) |
| `ScriptCommandHook` | With `NOTSA_WITH_SCRIPT_COMMAND_HOOKS`: switches a script command between our handler and GTA's |
| `NullHook` | Standalone/dump mode. Never touches memory |
| `RHManager` | Singleton. Tree of categories (`Global/CGeneral`, `Entity/Ped/CPed`, …) built by the `RH_Scoped*` macros. `CheckAll()` re-applies patches that something else overwrote, at most every 500 ms (called from the UI's pre-render update). `WriteHooksToFile` dumps **JSON**, not the CSV its comment claims |

`HookInstallOptions`:
- `Reversed` (true)
- `Unhooked` (false)
- `State`
- `Locked` (defaults to `!Reversed`)
- `Overrides` (true)
- `StackArgumentsToPreserve`
- `PreserveRegisters`
- `InstallSrcLoc`

**Hook states are not persisted.** Each run starts from the install options, adjusted by
`--all-use-gta-hooks`, `--all-use-gta-hooks-except=a,b` and `--use-gta-for=a,b`. Entries are `Cat/Sub` or
`Cat/Sub::Item`.

Rules for using the macros: [HookRegistration.md](HookRegistration.md). The most important one: a function still
calling `plugin::Call*` on its own address must be `.Reversed = false`, otherwise it recurses forever.

---

## 5. Source tree

| Path | Contents |
|---|---|
| `source/game_sa/` | **The game**, about 2,100 files. Root files belong to the `Global` hook category; subfolders hold subsystems (below) |
| `source/app/` | Re-implemented RenderWare "skeleton" app layer: `app_game.cpp` (game init, main-loop render), input, camera, light, `app_debug` (logging, crash handler). `app/platform/win/`: `WinMain`, `WinPs` (`PsGlobal` `0xC8CF88`, `PSGLOBAL(x)`), `WndProc`, DirectInput, video modes, windowed mode, DirectShow `VideoPlayer` |
| `source/oswrapper/` | The OS abstraction the game uses (files, threads, timers, `OS_DebugOut`) |
| `source/reversiblehooks/` | Hook system (§4) |
| `source/reversiblebugfixes/` | Switchable bug fixes (§7) |
| `source/difftests/` | Differential tests against the original code (see ReversingGuide §5) |
| `source/toolsmenu/` | ImGui debug UI (§6) |
| `source/extensions/` | NOTSA helpers (§8) |
| `source/libs/` | Vendored bits (ImGui backends copied by Conan, …) |
| `source/InjectHooksMain.cpp` | Calls every `InjectHooks()` (and includes every header) |
| `source/Base.h`, `PluginBase.h`, `StdInc.h`, `config.h` | Core macros, original-function call helpers, PCH, feature flags |

`source/game_sa/` subsystems:

| Folder | Subsystem |
|---|---|
| `Tasks/` (about 650 files) | Ped AI tasks: `TaskTypes/`, `Allocators/`, `TaskManager` |
| `Events/` (about 180) | AI events and event handling |
| `Entity/` | `CEntity`, `CPhysical`, `CBuilding`, `Dummy/`, `Object/`, `Ped/`, `Vehicle/` |
| `Audio/` | AE audio engine: entities, managers, hardware, loaders (`OpenAL/` optional) |
| `Fx/` | Particle system and `Blueprints/` |
| `Scripts/` | SCM virtual machine and command handlers (§9) |
| `Core/` | Containers and math: `CPool`, `CVector`, `CMatrix`, `CRect`, link lists, `KeyGen`, `OctTree` |
| `Collision/` | Collision shapes, `CColModel`, `CCollision` |
| `Models/` | Model infos (`CBaseModelInfo`, vehicle, ped, …) |
| `Animation/` | Anim blend associations, hierarchies, `CAnimManager` |
| `RenderWare/` | RW headers (`rw/`) and wrappers that forward to the RW code in the exe |
| `Pipelines/`, `Plugins/` | Custom render pipelines; RW plugins (breakable, collision, node name, 2D effects, anim blend) |
| `Pools/` | Typed pools: `GetPedPool()`, `GetVehiclePool()`, …, `notsa::PoolOf<T>()` |
| `Interior/`, `Attractors/`, `DecisionMakers/`, `Conversations/`, `Ragdoll/`, `Plant/` | Interiors and furniture, ped attractors, decision makers, conversations, bone nodes and IK, procedural objects |
| `Frontend/`, `Text/` | `CMenuManager` (split into files), GXT text |
| `Enums/` | Game enums, including `eScriptCommands.h` |
| `Memory/`, `Handling/`, `Data/` | Memory heap, handling data, weather definitions |

---

## 6. Debug UI (`source/toolsmenu/`)

- `notsa::ui::UIRenderer` is ImGui (DX9, plus the Win32 or SDL3 backend), created in `NOTSA_WinMain`. **F7**
  toggles it. The menu bar mentions Ctrl+M, but that shortcut isn't implemented.
- Modules (`DebugModules::CreateModules()`):

  | Menu | Modules |
  |---|---|
  | Tools | Teleport, Spawner, Mission, Cheat, **Diff tests** |
  | Settings | **Hooks**, PostEffects, **Bugs** |
  | Visualization | Collision, Ped, … |
  | Stats | Pools, Streaming, … |
  | Extra | Many more: audio, script, particles, time cycle, cull zones, … |

- **Hooks** window: category tree with tri-state checkboxes, drag-to-set, a context menu (Ours / GTA / Unhooked,
  copy addresses), a fuzzy filter, and JSON export (Ctrl+S).
- Module state (open windows, filters) is saved every 5 s to `DebugModules.json` in the game folder. Hook states
  are not saved.
- Adding a module: see `source/toolsmenu/DebugModules/!README.md`.

---

## 7. Switchable bug fixes (`source/reversiblebugfixes/`)

- `notsa::bugfixes::ReversibleBugFix` has the fields `Name`, `Description`, `Credit`, `Enabled` (true), `Locked`
  and `RequiresRestart`.
- Each `inline const` instance in `Bugs.hpp` adds itself to an intrusive list, which the F7 → Settings → Bugs window
  lists.
- Use one as a bool: `if (notsa::bugfixes::GenericCrashing) { ... }`.
- `notsa::IsFixBugs()` is different: a **compile-time** switch (`FIX_BUGS`, always defined).

---

## 8. Helpers you should know (`source/Base.h`, `source/extensions/`)

| Helper | Use |
|---|---|
| `StaticRef<T>(0xADDR)` | Reference to a game global (`static inline auto& ms_X = StaticRef<int32>(0x...)`) |
| `ScopedStaticRef<T>(var, flags, mask, init)` | Function-local static that uses a guard flag in the original |
| `VALIDATE_SIZE` / `VALIDATE_OFFSET` | `static_assert`s that pin struct layouts |
| `NOTSA_UNREACHABLE(fmt, ...)` / `NOTSA_UNREACHABLE_CASE(v)` | Debug: message box, log, exit. Release: `__assume(false)` |
| `plugin::Call` / `CallAndReturn` / `CallMethod` / `CallMethodAndReturn` / `CallVirtualMethod*` | Call original code by address (stubs) |
| `NOTSA_LOG_{TRACE,DEBUG,INFO,WARN,ERR,CRIT}` | spdlog → `logs/log.log` and the console. Named loggers via `NOTSA_MAKE_LOGGER` |
| `notsa::EntityRef<T>` | RAII entity pointer (registers and cleans up references automatically) |
| `notsa::WEnum<Int, Enum>` | Enum stored in a fixed-width integer (to keep original layouts) |
| `FixedFloat` / `FixedVector` / `FixedRect` / `FixedQuat` | Fixed-point compressed values with implicit float conversion |
| `notsa::cast` / `dyn_cast` / `isa` (`Casting.hpp`) | LLVM-style casts |
| `ci_string(_view)` | Case-insensitive strings |
| `notsa::ScopeGuard`, `notsa::contains`, `SpatialQuery`, `string_copy`, … (`utility.hpp`) | General utilities |
| `notsa::Singleton<T>` | Lazily created singleton (not thread-safe) |
| `g_ConfigurationMgr` (`Configuration.hpp`) | Reads `gta-reversed.ini` |

`config.h` feature flags: `EXT_FAST_LOADER`, `USE_ADDITIONAL_CHEATS`, `IMPLEMENT_UNSUPPORTED_OPCODES`,
`MORE_LANGUAGES`, `USE_ORIGINAL_CODE`.

Command-line arguments:

| Argument | Effect |
|---|---|
| `--debug` | Wait for a debugger before loading |
| `--all-use-gta-hooks` | Redirect every hook to GTA |
| `--all-use-gta-hooks-except=` | Same, except for the listed categories/items |
| `--use-gta-for=` | Redirect only the listed categories/items to GTA |
| `--dump-hooks-to=path` | Write all hooks to a JSON file |
| `--run-difftests[=filter]`, `--difftests-out=`, `--difftests-exit` | Run the differential tests at startup (see ReversingGuide §5) |

---

## 9. Script engine (`source/game_sa/Scripts/`)

- `CRunningScript::Process` (`0x469F00`) loops `ProcessOneCommand` (`0x469EB0`). For each opcode, the dispatcher
  checks for a registered **custom handler** first. Otherwise it calls the **original** `ProcessCommands<N>To<N+99>`
  through `s_OriginalCommandHandlerTable` (`0x8A6168`, 27 entries).
  - So the original dispatchers stay in use for every command we haven't implemented.
  - They're reversed command by command, never as whole functions.
- Each `Commands/<Group>Commands.cpp` file has a `RegisterHandlers()` that calls:
  - `REGISTER_COMMAND_HANDLER(COMMAND_X, Fn)` for implemented commands
  - `REGISTER_COMMAND_NOP` for commands that do nothing
  - `REGISTER_COMMAND_UNIMPLEMENTED` (logs and breaks; it is **not** a fallback to GTA's code)
- Handlers are plain C++ functions. `CommandParser<Cmd, Fn>` reads each parameter with `notsa::script::Read<T>`
  (`ReadArg.hpp`) and stores the return value with `StoreArg.hpp`. See
  [ScriptCommandReversing.md](ScriptCommandReversing.md).
- `contrib/scripts-hooks-gen.py` generates handler stubs.

---

## 10. CI (`.github/workflows/`) and `contrib/`

| Workflow | What it does |
|---|---|
| `build.yml` | Every push and PR: builds x86 {Debug, Release} × {Unity, NoUnity} through Conan, and uploads `bin/` |
| `static-analysis.yml` | MSVC Code Analysis, with results sent to code scanning |
| `codeql.yml` | CodeQL (C++, Actions, Python) on push and PR to master, and weekly |
| `docs.yml` | Doxygen to GitHub Pages |
| `update-re-progress.yml` | On master: builds `standalone=dump_hooks_only`, runs `gta_reversed.exe --dump-hooks-to=docs/hooks.json`, regenerates `docs/ReversedClasses.md` (`contrib/hooks2md.py`) and commits it |

`contrib/` contents:
- `install.py`: run as admin. Unzips `plugins.zip` (ASI loader, mouse fix, ini files) into the game folder and
  symlinks the `.asi`/`.pdb` into `scripts/`.
- `hooks2md.py`: generates the progress page.
- `scripts-hooks-gen.py`: generates script command handler stubs.
- `clean-srs-calls.py`: rewrites raw `RwRenderStateSet` calls with enum names.
- `msvc-wine/`: Linux cross-build.

---

## 11. Runtime files (in the game folder / working directory)

| File | Purpose |
|---|---|
| `scripts/gta_reversed.asi` (+ `.pdb`) | The DLL |
| `vorbisFile.dll` | ASI loader. The original file is kept as `vorbisHooked.dll` |
| `dinput8.dll` | Mouse fix |
| `gta-reversed.ini` | `[FastLoader]` (skip intros, auto-load a save), `[Misc]` (save directory, load mismatched saves). **Gotcha:** the DLL opens it relative to the *working directory* (normally the game folder), while `contrib/plugins.zip` installs it into `scripts/`. As installed it isn't read; copy it to the game folder |
| `logs/log.log` | Log file |
| `DebugModules.json`, `imgui.ini` | Debug UI state |
| `difftests.log` | Report written by `--run-difftests` |
| `hooks.json` | Written by `--dump-hooks-to` (or Ctrl+S in the Hooks window) |
