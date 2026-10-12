# Entity/Vehicle/ — CVehicle and its subclasses

Full guide: [../../../../docs/subsystems/Vehicles.md](../../../../docs/subsystems/Vehicles.md) (vtable layout,
per-frame flow, handling, damage, AI driving, gotchas). AI driving code is outside this folder: `CarCtrl.*`,
`CarAI.*`, `CarEnterExit.*`, `AutoPilot.h` in `source/game_sa/`.

## Hierarchy (vtable address, virtual count as in each `RH_ScopedVirtualClass`)
```
CVehicle      0x871E80 66   Vehicle.h/.cpp (huge; read the relevant part only)
├─ CAutomobile 0x871120 71  ── CHeli 0x871680, CPlane 0x871948, CQuadBike 0x871AE8,
│                              CMonsterTruck 0x8717D8, CTrailer 0x871C28 (all 71)
├─ CBike      0x871360 67   ── CBmx 0x871528 (67)
├─ CBoat      0x8721A0 66
└─ CTrain     0x872370 66
```
All classes are `NOTSA_EXPORT_VTABLE`; sizes are in the `VALIDATE_SIZE` at the bottom of each header.

## Type vs sub-type (most common bug here)
- `m_nVehicleType` is set by the base constructor (AUTOMOBILE / BIKE / BOAT / TRAIN); `m_nVehicleSubType` by the leaf.
- So a heli/plane/quad/monster truck/trailer has `IsAutomobile() == true`, a BMX has `IsBike() == true`, and
  `IsPlane()`, `IsHeli()`, `IsQuad()`, `IsTrailer()`, `IsBMX()` ... are never true on a live vehicle.
- Use `IsSubPlane()`, `IsSubHeli()`, `IsSubTrailer()`, ... to test the concrete class. Check which field the original
  reads (offset of `m_nVehicleType` vs `m_nVehicleSubType`) and pick the matching helper.
- `IsRealHeli/Plane/Bike/Boat()` test `tHandlingData` flags, a third, different notion (e.g. the Vortex).
- `AsAutomobile()/AsBike()/AsPlane()/AsHeli()` are unchecked `reinterpret_cast`s.

## Pitfalls
- `m_pHandlingData` is shared per model: never write through it for a single vehicle.
- Several reversed-looking bodies only forward (raw `((void(__thiscall*)(...))0xADDR)` calls in `Vehicle.cpp`) or have
  commented-out `// RH_Scoped...` lines. `CPlane::ProcessControl` returns via `plugin::CallMethod` with an untested
  draft below it. Don't treat these as finished reimplementations.
- `CTrain` objects carry the original vtable (constructor is still a stub), so overrides missing from `Train.h` still
  run as original code.
- Wheel colpoints read in `ProcessControl` come from the previous collision pass.
- Template-hooked functions (e.g. `BladeColSectorList<...>`) and deleting-destructor thunks show as MISSING in the
  coverage tool; ignore those.
- Vehicle audio is `CAEVehicleAudioEntity` (`../../Audio/Entities/`), the first field of `CVehicle`.
