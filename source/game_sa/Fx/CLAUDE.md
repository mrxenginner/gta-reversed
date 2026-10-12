# Fx/ — particle effects system (effects.fxp)

Guide: [../../../docs/subsystems/Rendering.md](../../../docs/subsystems/Rendering.md) (where Fx update/render sit
in the frame, Fx coverage and what is left).

## Layout
- Naming: plugin-sdk style `Xxx_c` classes (not `CXxx`); keep it.
- Runtime singletons: `Fx_c g_fx` (`Fx.h`, effect spawners `AddBlood`/`AddSparks`/`Trigger*`, particle quad batching
  `RenderBegin`/`RenderAddTri_`/`RenderEnd`) and `FxManager_c g_fxMan` (`FxManager.h`, loads `models\effects.fxp`,
  owns blueprints, live systems and the memory pool).
- Blueprints (data loaded from the .fxp): `FxSystemBP_c` -> `FxEmitterBP_c` / `FxPrimBP_c` -> `FxInfoManager_c` ->
  `Blueprints/FxInfo*_c` (one class per info type, `eFxInfoType.h`, keyframe interpolation in `Blueprints/Interp/`).
- Instances: `FxSystem_c` (one running effect, also holds `m_FireAE` audio), `FxEmitter_c` / `FxEmitterPrt_c`,
  `Particle.h`. `FxFrustumInfo`, `FxBox`, `FxSphere`, `FxPlane_c` are culling helpers.
- `CarFXRenderer.*` (`CCarFXRenderer`) is car dirt/pipeline setup, only loosely related.

## Conventions
- Most classes are `RH_ScopedClass` + category `"Fx"`; `FxEmitterBP_c` (`0x85A788`, 7) and `FxEmitter_c`
  (`0x85A7A4`, 6) are `RH_ScopedVirtualClass`. `FxInfo_c` is an abstract `NOTSA_EXPORT_VTABLE` base
  (`Load(FILESTREAM, version)`, `GetValue(...)`).
- Blueprint/particle memory comes from `g_fxMan.GetMemPool().GetMem(size)` (`FxMemoryPool_c`), not `new`; it is
  reset as a whole, so objects there are never individually freed.
- `.fxp` is a line-based text format read in a fixed order (no seeking). Use `ReadField<T>(file, "NAME:")` from
  `FxTools.h`: it reads one line and asserts the field name (NOTSA check), so a wrong read order fails loudly.

## Pitfalls
- `Fx.cpp` `InjectHooks`: lines `// + RH_ScopedInstall(...)` have C++ bodies that are not installed yet; plain
  `// RH_ScopedInstall(...)` ones (`AddBlood`, `AddSparks`, `AddWheel*`, `TriggerGunshot`, ...) still forward through a
  raw `((void(__thiscall*)(...))0xADDR)` cast. Grep for the address, not only `plugin::Call`.
- `FxManager_c::DestroyAllFxSystems` is kept as a stub on purpose ("broken for some reason?").
- `FxEmitterBP_c::CreateInstance` is marked "bad"; `FxEmitterBP_c::Render` (`0x4A2C40`) is the largest remaining gap.
- Particle math runs every frame for hundreds of particles: keep float vs double exactly as the original (x87).
