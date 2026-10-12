# Entity/Ped/ — CPed and its subclasses

Full guide: [../../../../docs/subsystems/Peds.md](../../../../docs/subsystems/Peds.md) (layout, flags, per-frame flow,
helpers table, gotchas). Ped behaviour lives mostly in tasks/events: see
[Tasks.md](../../../../docs/subsystems/Tasks.md) and [Events.md](../../../../docs/subsystems/Events.md).
Related code outside this folder: `PedIntelligence.*`, `PedGroup*.*`, `Population.*`, `PlayerPedData.*`,
`PlayerInfo.*`, `PedIK.*`, `Weapon.*` in `source/game_sa/`, `Models/PedModelInfo.*`.

## Classes (vtable, all `RH_ScopedVirtualClass(..., 26)`)
- `CPed` `0x86C358` (`Ped.h/.cpp`, size 0x79C) — base; `CPlayerPed` `0x86D168` (0x7A4), `CCopPed` `0x86C120`,
  `CCivilianPed` `0x86C0A8`, `CEmergencyPed` (no `InjectHooks` at all, not in `InjectHooksMain.cpp`).
- Subclasses mostly override `ProcessControl` only; `CPlayerPed` also `SetMoveAnim`, `Save`, `Load`.
- `PedSaveStructure.*` = save blob, `PlayerSkin.*` = `RH_ScopedClass` (no vtable).

## Conventions
- Use the accessors instead of raw members: `GetIntelligence()`, `GetTaskManager()`, `GetActiveWeapon()`,
  `IsInVehicle()` / `GetVehicleIfInOne()`, `IsStateDead()`/`IsAlive()`, `IsCop()`/`IsPlayer()`/`AsPlayer()`,
  `IsCreatedByMission()`, `GetPedModelInfo()`, `GetAE()`/`GetSpeechAE()`/`GetWeaponAE()`.
- No `CPedWeapons` class: weapons are `CPed::m_aWeapons` + `m_nActiveWeaponSlot`.
- Ped flags are one anonymous bitfield struct (four 32-bit words; IDA calls them `m_nPedFlags`...`m_nFourthPedFlags`).
  When IDA shows `flags & 0x1000`, count bits from the start of the word. Word 3 contains the 2-bit
  `CantBeKnockedOffBike`, so every later flag in that word is shifted by one. Don't rename wrong flag names in a
  reversal PR; add a comment.
- Use `eBoneTag` for bones (not `AnimationId`, as some older helpers do).

## Pitfalls
- `CPed::ProcessControl` (`0x5E8CD0`) is not hooked (install commented out in `Ped.cpp`); every subclass calls it, so
  it forwards to GTA. Same for `ProcessEntityCollision` and the `SpecialEntity*` virtuals.
- `m_pVehicle` is not cleared when the ped leaves the car; test `bInVehicle` too (or use `IsInVehicle()`).
- Some stubs use raw `((void(__thiscall*)...)0x5E....)` casts instead of `plugin::CallMethod`; grep addresses too.
- `PlayFootSteps` has half-reversed code after an early `return plugin::CallMethod`; treat it as a draft.
- Pointers from scripts or older frames: validate with `IsPedPointerValid` / `CPed::IsPointerValid()`.
