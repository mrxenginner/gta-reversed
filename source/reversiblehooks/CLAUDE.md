# reversiblehooks/ — the hook machinery behind `RH_Scoped*`

Usage rules for reversed classes: [../../docs/HookRegistration.md](../../docs/HookRegistration.md); internals overview:
[../../docs/Architecture.md](../../docs/Architecture.md). Changes here affect every hook: keep them minimal.

## Files
- `ReversibleHooks.h` — the macros. `RH_ScopedClass/NamedClass/Namespace/VirtualClass` open a scope,
  `RH_ScopedCategory("A/B")` / `RH_ScopedCategoryGlobal()` set the GUI path `<Category>/<Scope>/<Fn>` (the same path
  difftests and `--unhook...` options use). Overloads get `"<fn>-<suffix>"` names.
- `RHManager.h/.cpp` — singleton; `InstallStatic`, `InstallVirtual`, `InstallConstructor`, `InstallVirtualDestructor`,
  `InstallScriptCommand`; `HookInstallOptions` (`Reversed`, `Unhooked`, `State`, `Locked`, `Overrides`,
  `StackArgumentsToPreserve`, `PreserveRegisters`). `CheckAll()` re-validates patches every 500 ms.
- `VMTInfo.h/.cpp` — vtable lookup: `FindByClassName` resolves our vtable from the exported MSVC symbol
  `??_7<Class>@@6B@` (requires `NOTSA_EXPORT_VTABLE`; single-parameter templates are mangled too), `FindIndexOf`
  scans GTA's vtable for the given address.
- `ReversibleHook/` — hook kinds: `StaticTwoWayHook` (two `StaticOneWayHook` jumps), `VirtualHook` (vtable slot +
  direct-call jump), `VMTRedirectHook` (`.Overrides = false`), `VirtualDestructorHook`, `ScriptCommandHook`, `NullHook`
  (standalone/dump builds). `HookCategory*`, `RootHookCategory.h` — the tree shown in F7 → Hooks
  (`source/toolsmenu/DebugModules/HooksDebugModule*`).

## How states work
- `RedirectToOurs` = GTA's function jumps to ours. `RedirectToGTA` = **our** function jumps to GTA's (default when
  `.Reversed = false`, and locked). `Unhooked` = nothing patched; difftests use it to call both versions.
- Hence a body that still calls `plugin::Call<own address>` must be `.Reversed = false`, or GTA's address jumps to ours
  and ours calls GTA's address again: infinite recursion.
- Virtual hooks: the GTA address is looked up in GTA's vtable and the **same slot index** is used in ours, so our
  virtual declaration order must match GTA's, and the `nVirtFns` count must cover the slot.

## Pitfalls
- `VMTInfo` throws `std::runtime_error` when our vtable symbol is missing or the GTA address is not in the vtable
  (e.g. `CLodTimeModelInfo`, see `game_sa/Models/CLAUDE.md`). `check_hooks.py` catches most of these statically.
- `InjectHooksMain()` runs from `DllMain` under the loader lock. An exception escaping it used to hang the game on
  the console; `dllmain.cpp` now catches it, shows a message box and terminates. Still: never add code that throws,
  waits on threads, or loads DLLs on the install path; validate with `check_hooks.py` instead of relying on the catch.
- `StackArgumentsToPreserve` / `PreserveRegisters` exist for callers that rely on non-standard conventions; setting
  `PreserveRegisters` without the stack count throws at install time.
- Script-command hooks only exist with `NOTSA_WITH_SCRIPT_COMMAND_HOOKS`; otherwise `RH_ScopedInstallScriptCommand`
  is a no-op.
