# Vehicles

How vehicles are structured, what runs every frame, and what is still left to reverse. Read
[Architecture.md](../Architecture.md) first for the hook system, and [ReversingGuide.md](../ReversingGuide.md) for the
reversing and validation workflow. This page covers only the vehicle subsystem.

All paths are relative to the repository root, and line numbers are correct at the time of writing. Addresses come from
`RH_Scoped*Install`, `// 0xADDR` comments or `StaticRef` in the repository.

---

## 1. Overview and class hierarchy

```
CEntity (0x38)  vtable 22 slots                       source/game_sa/Entity/Entity.h
└─ CPhysical (0x138)  vtable 23 slots                 source/game_sa/Entity/Physical.h
   └─ CVehicle (0x5A0)  vtable 0x871E80, 66 slots     source/game_sa/Entity/Vehicle/Vehicle.h:193
      ├─ CAutomobile (0x988)  0x871120, 71 slots      Automobile.h:40
      │  ├─ CHeli         (0xA18)  0x871680, 71       Heli.h:60
      │  ├─ CPlane        (0xA04)  0x871948, 71       Plane.h:40
      │  ├─ CQuadBike     (0x9BC)  0x871AE8, 71       QuadBike.h:37
      │  ├─ CMonsterTruck (0x99C)  0x8717D8, 71       MonsterTruck.h:39
      │  └─ CTrailer      (0x9F4)  0x871C28, 71       Trailer.h:39
      ├─ CBike (0x814)  0x871360, 67 slots            Bike.h:29
      │  └─ CBmx (0x838)  0x871528, 67                Bmx.h:27
      ├─ CBoat  (0x7E8)  0x8721A0, 66 slots           Boat.h:30
      └─ CTrain (0x6AC)  0x872370, 66 slots           Train.h:43
```

The sizes come from `VALIDATE_SIZE` at the bottom of each header. The vtable address and slot count come from
`RH_ScopedVirtualClass(...)` at the top of each `InjectHooks()`. Every class is `NOTSA_EXPORT_VTABLE`.

Related types and their files:

| Type | Size | File |
|---|---|---|
| `CAutoPilot` | 0x98 | `source/game_sa/AutoPilot.h` (member `CVehicle::m_autoPilot`, Vehicle.h:245) |
| `CDamageManager` | 0x18 | `source/game_sa/DamageManager.h` (member `CAutomobile::m_damageManager`, Automobile.h:42) |
| `tHandlingData` | 0xE0 | `source/game_sa/tHandlingData.h` |
| `tBikeHandlingData` / `tFlyingHandlingData` / `tBoatHandlingData` | 0x40 / 0x58 / 0x3C | `source/game_sa/t*HandlingData.h` |
| `cHandlingDataMgr` | 0xC624 | `source/game_sa/cHandlingDataMgr.h`, global `gHandlingDataMgr` at `0xC2B9C8` |
| `CVehicleModelInfo` | 0x308 (`CVehicleStructure` 0x314) | `source/game_sa/Models/VehicleModelInfo.h` |
| `CAEVehicleAudioEntity` | (member `CVehicle::m_vehicleAudio`, first field of `CVehicle`) | `source/game_sa/Audio/Entities/AEVehicleAudioEntity.cpp` |
| `CCarCtrl`, `CCarAI`, `CCarEnterExit` | static classes | `source/game_sa/CarCtrl.*`, `CarAI.*`, `CarEnterExit.*` |

### 1.1 Type versus sub-type (`IsXxx` against `IsSubXxx`)

`CVehicle` has two `eVehicleType` fields (Vehicle.h:418-419, enum in `source/game_sa/Enums/eVehicleType.h`):

- `m_nVehicleType` is set by the **base** constructor that runs: `CAutomobile` sets `VEHICLE_TYPE_AUTOMOBILE`
  (Automobile.cpp:158), `CBike` sets `BIKE` (Bike.cpp:73), `CBoat` sets `BOAT`, and `CTrain` sets `TRAIN`.
- `m_nVehicleSubType` is set again by each leaf constructor: `CHeli` sets `HELI` (Heli.cpp:28), `CPlane` sets `PLANE`,
  `CQuadBike` sets `QUAD`, `CMonsterTruck` sets `MTRUCK`, `CTrailer` sets `TRAILER`, and `CBmx` sets `BMX` (Bmx.cpp:24).

The consequence is that **a heli, plane, quad, monster truck or trailer has `IsAutomobile() == true`**, and a BMX has
`IsBike() == true`. `IsHeli()`, `IsPlane()`, `IsQuad()`, `IsMonsterTruck()`, `IsTrailer()` and `IsBMX()` on a
`CVehicle` are never true. To identify the concrete class, use `IsSubHeli()`, `IsSubPlane()` and the other `IsSub*`
helpers (Vehicle.h:703-717). Only `CVehicleModelInfo::m_nVehicleType` (VehicleModelInfo.h:138) holds the "real" type
read from `vehicles.ide`, and `CCarCtrl::GetNewVehicleDependingOnCarModel` switches on it.

The "real" handling-based checks are something else again: `IsRealHeli()`, `IsRealPlane()`, `IsRealBike()` and
`IsRealBoat()` (Vehicle.h:724-727) test the `m_bIsHeli`, `m_bIsPlane` and similar model flags in `tHandlingData`.
For example, the Vortex hovercraft is an automobile whose handling acts like a boat.

Other helpers: `GetVehicleAppearance()` (`eVehicleAppearance`, Vehicle.h:58), `IsSubRoadVehicle()`,
`IsSubFakeAircraft()` (FHELI/FPLANE are the RC aircraft), `IsMissionVehicle()`, `IsLawEnforcementVehicle()`. Casts are
unchecked `reinterpret_cast`s: `AsAutomobile()`, `AsBike()`, `AsBoat()` and `AsTrain()` in Entity.h:388-397, and
`AsPlane()` and `AsHeli()` in Vehicle.h:735-736.

### 1.2 Creation

The factory is `CCarCtrl::GetNewVehicleDependingOnCarModel` (CarCtrl.cpp:419, `0x421440`, **still a stub**). Its
intended body is in the comment right below the stub: a switch on `CVehicleModelInfo::m_nVehicleType` that runs
`new CMonsterTruck/CQuadBike/CHeli/...`. `CCarGenerator` (CarGenerator.cpp:181-224) constructs vehicles directly.
Vehicles live in the vehicle pool (`CVehicle::operator new`).

Handling pointers are set in the constructors. For example, Automobile.cpp:169-171 sets `m_pHandlingData`,
`m_pFlyingHandlingData`, and copies the handling flags into `m_nHandlingFlagsIntValue`. CBike, CBoat and CTrain fetch
the same pointers through `gHandlingDataMgr.GetVehiclePointer(mi->m_nHandlingId)` (Bike.cpp:80, Boat.cpp:83,
Train.cpp:84).

---

## 2. Virtual interface of `CVehicle`

Virtual functions inherited from `CEntity`/`CPhysical` that vehicles override: `SetModelIndex`, `DeleteRwObject`,
`ProcessControl`, `Teleport`, `SpecialEntityPreCollisionStuff`, `SpecialEntityCalcCollisionSteps`, `PreRender`,
`Render`, `SetupLighting`, `RemoveLighting`, `FlagToDestroyWhenNextProcessed`, and `CPhysical::ProcessEntityCollision`.
`CVehicle` itself does **not** override `ProcessControl`. Each leaf class does.

New virtual functions, in **declaration order, which is vtable order** (Vehicle.h:464-517). The slot index is 0-based
and assumes `CPhysical` ends at slot 22, which the counts `RH_ScopedVirtualClass(CEntity, ..., 22)` and
`(CPhysical, ..., 23)` imply. Each slot is 4 bytes. Check the address in IDA before relying on a slot number.

| Slot | Function | Slot | Function |
|---|---|---|---|
| 23 | `ProcessControlCollisionCheck(bool)` | 45 | `IsRoomForPedToLeaveCar(uint32, CVector*)` |
| 24 | `ProcessControlInputs(uint8 playerNum)` | 46 | `ProcessDrivingAnims(CPed*, bool)` |
| 25 | `GetComponentWorldPosition(int32, CVector&)` | 47 | `GetRideAnimData()` |
| 26 | `IsComponentPresent(int32)` | 48 | `SetupSuspensionLines()` |
| 27 | `OpenDoor(CPed*, int32 comp, eDoors, float, bool)` | 49 | `AddMovingCollisionSpeed(CVector&)` |
| 28 | `ProcessOpenDoor(CPed*, uint32, uint32, uint32, float)` | 50 | `Fix()` |
| 29 | `GetDooorAngleOpenRatioU32(uint32)` | 51 | `SetupDamageAfterLoad()` |
| 30 | `GetDooorAngleOpenRatio(eDoors)` | 52 | `DoBurstAndSoftGroundRatios()` |
| 31 | `IsDoorReadyU32(uint32)` | 53 | `GetHeightAboveRoad()` |
| 32 | `IsDoorReady(eDoors)` | 54 | `PlayCarHorn()` |
| 33 | `IsDoorFullyOpenU32(uint32)` | 55 | `GetNumContactWheels()` (default 4) |
| 34 | `IsDoorFullyOpen(eDoors)` | 56 | `VehicleDamage(float, eVehicleCollisionComponent, CEntity*, CVector*, CVector*, eWeaponType)` |
| 35 | `IsDoorClosedU32(uint32)` | 57 | `CanPedStepOutCar(bool)` |
| 36 | `IsDoorClosed(eDoors)` | 58 | `CanPedJumpOutCar(CPed*)` |
| 37 | `IsDoorMissingU32(uint32)` | 59 | `GetTowHitchPos(CVector&, bool, CVehicle*)` |
| 38 | `IsDoorMissing(eDoors)` | 60 | `GetTowBarPos(CVector&, bool, CVehicle*)` |
| 39 | `IsOpenTopCar()` | 61 | `SetTowLink(CVehicle*, bool)` |
| 40 | `RemoveRefsToVehicle(CEntity*)` | 62 | `BreakTowLink()` |
| 41 | `BlowUpCar(CEntity*, bool)` | 63 | `FindWheelWidth(bool rear)` |
| 42 | `BlowUpCarCutSceneNoExtras(bool, bool, bool, bool)` | 64 | `Save()` |
| 43 | `SetUpWheelColModel(CColModel*)` | 65 | `Load()` |
| 44 | `BurstTyre(uint8, bool)` | | |

`CAutomobile` adds five virtual functions (Automobile.h:194-198): slot 66 `ProcessAI(uint32& extraHandlingFlags)`,
67 `ResetSuspension`, 68 `ProcessFlyingCarStuff`, 69 `DoHoverSuspensionRatios` and 70 `ProcessSuspension`. That makes
71 slots. `CBike` adds its **own** `ProcessAI` at slot 66 (Bike.h:135), which makes 67 slots. CBike's `ProcessAI` is
unrelated to CAutomobile's.

> The door functions come in pairs that take `uint32` and `eDoors` (the `...U32` names are NOTSA names for the
> overloads). The header warns about mixing them up (Vehicle.h:473-475). Component ids (`tComponent`, `eCarNodes`) are
> not the same as `eDoors`. See `CVehicle::ProcessOpenDoor` at Vehicle.cpp:693 for the mapping.

### 2.1 Overrides per class (as declared in the headers)

| Class | Overrides |
|---|---|
| `CAutomobile` | Almost everything: ProcessControl, ProcessControlCollisionCheck, ProcessControlInputs, all door functions, BlowUpCar(+CutScene), SetUpWheelColModel, BurstTyre, IsRoomForPedToLeaveCar, SetupSuspensionLines, AddMovingCollisionSpeed, Fix, SetupDamageAfterLoad, DoBurstAndSoftGroundRatios, GetHeightAboveRoad, PlayCarHorn, GetNumContactWheels, VehicleDamage, tow functions, FindWheelWidth, Save/Load, ProcessEntityCollision, PreRender, Render, Teleport |
| `CHeli` | ProcessControl, ProcessControlInputs, ProcessFlyingCarStuff, PreRender, Render, BlowUpCar, Fix, BurstTyre, SetUpWheelColModel, SetupDamageAfterLoad |
| `CPlane` | Same as CHeli, plus OpenDoor and VehicleDamage |
| `CQuadBike` | ProcessControl, ProcessControlInputs, ProcessAI, ProcessSuspension, ResetSuspension, SetupSuspensionLines, ProcessDrivingAnims, GetRideAnimData, PreRender, Fix, SetupDamageAfterLoad |
| `CMonsterTruck` | ProcessControl, ProcessControlCollisionCheck, ProcessSuspension, ResetSuspension, SetupSuspensionLines, ProcessEntityCollision, PreRender, BurstTyre, SetUpWheelColModel |
| `CTrailer` | ProcessControl, ProcessAI, ProcessSuspension, ResetSuspension, SetupSuspensionLines, ProcessEntityCollision, PreRender, tow functions |
| `CBike` | ProcessControl, ProcessAI, ProcessControlInputs, ProcessControlCollisionCheck, ProcessEntityCollision, ProcessDrivingAnims, ProcessOpenDoor, PreRender, Render, Teleport, SetModelIndex, VehicleDamage, BlowUpCar, BurstTyre, Fix, SetupSuspensionLines, SetUpWheelColModel, DoBurstAndSoftGroundRatios, PlayCarHorn, SetupDamageAfterLoad, RemoveRefsToVehicle, GetComponentWorldPosition, IsComponentPresent, door functions, IsRoomForPedToLeaveCar, GetRideAnimData, GetHeightAboveRoad, GetNumContactWheels, FindWheelWidth |
| `CBmx` | ProcessControl, ProcessAI, ProcessDrivingAnims, PreRender, BlowUpCar, SetUpWheelColModel, BurstTyre, FindWheelWidth |
| `CBoat` | ProcessControl, ProcessControlInputs, PreRender, Render, Teleport, SetModelIndex, GetComponentWorldPosition, IsComponentPresent, BlowUpCar, ProcessOpenDoor |
| `CTrain` | **Only ProcessControl is declared** (Train.h:94). The binary also has CTrain overrides of SetModelIndex `0x6F5530`, PreRender `0x6F5570`, GetDooorAngleOpenRatio `0x6F5930`/`0x6F59C0`, IsRoomForPedToLeaveCar `0x6F5EE0`, OpenDoor `0x6F6370` and Teleport `0x6F7190` (addresses from the IDB coverage tool, which reports them as MISSING). Declare them as `override` when you reverse them |

---

## 3. Per-frame flow

### 3.1 Game loop entry points (`CGame::Process`, Game.cpp)

```
CTrain::UpdateTrains()            Game.cpp:749   (empty in the original: "NOP", Train.cpp:185)
CHeli::UpdateHelis()              Game.cpp:750   (police/news heli spawning, address known but not hooked)
CTheCarGenerators::Process()      Game.cpp:769   (parked cars)
CWorld::Process()                 Game.cpp:781   ──> per-entity ProcessControl / ProcessCollision / ProcessShift
CCarCtrl::PruneVehiclesOfInterest Game.cpp:787
CCarCtrl::GenerateRandomCars()    Game.cpp:833   ──> GenerateOneRandomCar() (0x430050, 7.9 KB, NOHOOK)
CCarCtrl::RemoveDistantCars()     Game.cpp:836
CCarCtrl::RemoveCarsIfThePoolGetsFull()  Game.cpp:837
```

`CWorld::Process` (World.cpp:2205-2320) walks the moving list. It first calls `entity->ProcessControl()` on every
entity (World.cpp:2221), then `ProcessCollision()` on every entity, then `ProcessShift()`. Rendering is separate:
`CRenderer::PreRender` (Renderer.cpp:275) calls `entity->PreRender()` on the visible lists and then
`CHeli::SpecialHeliPreRender()`. The render lists call `vehicle->SetupRender()`, then `Render()`, then
`ResetAfterRender()` (Renderer.cpp:118-139).

### 3.2 `CAutomobile::ProcessControl` (Automobile.cpp:391, `0x6B1880`, reversed)

This function is the reference implementation. CHeli, CPlane, CQuadBike and CTrailer all call it as their base
(for example Plane.cpp:400, Trailer.cpp:407 and QuadBike.cpp:248). The sequence:

1. Audio: `m_vehicleAudio.Service()` (:402).
2. Crashing aircraft (`MISSION_PLANE/HELI_CRASH_AND_BURN`), nitro cheats, bus doors (`ProcessAutoBusDoors`), then
   `ProcessCarAlarm`, `UpdateClumpAlpha` and `UpdatePassengerList` (:404-431).
3. **`ProcessAI(extraHandlingFlags)`** (:447 → Automobile.cpp:1134, `0x6B4800`). This step switches on `GetStatus()`
   (:1212):
   - `STATUS_SIMPLE`: `CCarAI::UpdateCarAI` and `CCarCtrl::UpdateCarOnRails`. The car runs "on rails" without
     physics and returns `true`, which means skip physics.
   - `STATUS_PHYSICS` / `STATUS_GHOST`: `CCarAI::UpdateCarAI`, `CCarCtrl::SteerAICarWithPhysics` and
     `CCarCtrl::ReconsiderRoute`.
   - `STATUS_ABANDONED`, `STATUS_WRECKED`, `STATUS_REMOTE_CONTROLLED`, `STATUS_FORCED_STOP`, `STATUS_IS_TOWED`: each has
     a special case.
   - `STATUS_PLAYER`: `PruneReferences`, then **`ProcessControlInputs(padNum)`** (:1363-1370), which reads the pad and
     sets the gas, brake and steer values.
4. Settle/idle logic for abandoned and wrecked cars. This logic can decide to skip physics.
5. `VehicleDamage(0, DEFAULT, ...)` (:543) applies the collision damage accumulated during the last collision pass.
6. Model specials: fire truck water cannon, tank turret, packer/dozer/forklift/towtruck "misc component" arms, and
   `NitrousControl` (:544-583).
7. If physics is skipped, `SkipPhysics()` runs (:588). Otherwise:
   - `CPhysical::ProcessControl()` (:602), plus Skimmer boat handling and water entry.
   - **`ProcessSuspension()`** (:635 → :1511, `0x6AFB10`). It computes spring and damping forces per wheel from
     `m_fWheelsSuspensionCompression[]` and `m_wheelColPoint[]`.
   - Gear and engine logic, perfect-handling cheat, then **`ProcessCarWheelPair`** for the front and rear pairs
     (:801-860). The order depends on `handlingFlags.bProcRearwheelFirst`. `ProcessCarWheelPair` (:4561) calls
     `CVehicle::ProcessWheel` (Vehicle.cpp:3167) for each wheel to apply traction, brake and skid.
8. `ProcessCarOnFireAndExplode` (:879) and the siren (`CCarAI::MakeWayForCarWithSiren`).
9. Per-wheel bookkeeping. The current compression is copied to `...Prev` and **reset to 1.0** (:917-918), so the next
   collision pass can fill it again. Wheel spin goes to the audio.
10. `CCarCtrl::ScanForPedDanger` (:967), hydraulics, tow links (`m_pTowingVehicle` / `m_pVehicleBeingTowed`, :1000),
    earthquake, harvester and Rhino.

### 3.3 Wheels, suspension and colpoints

- Suspension rays are **collision lines** in the vehicle's col model. `CAutomobile::SetupSuspensionLines`
  (Automobile.cpp:2535) builds them from `tHandlingData::m_fSuspensionUpperLimit/LowerLimit` and the wheel size.
  `m_aSuspensionSpringLength` and `m_aSuspensionLineLength` are at Automobile.h:87-88.
- `CAutomobile::ProcessEntityCollision` (Automobile.cpp:1686) runs during `CWorld`'s collision pass, **after**
  ProcessControl. `CCollision::ProcessColModels` returns a touch distance for each line, in the range [0,1]. When a
  line hits closer than the stored value, the code writes `m_fWheelsSuspensionCompression[i]` and
  `m_wheelColPoint[i]` (around :1774-1782). In that array, 1.0 means fully extended and no contact.
- The next frame's `ProcessSuspension` and `ProcessCarWheelPair` read those values. Helpers that decode them are in
  Automobile.h:305-380 (`GetAllWheelsOffGround`, sand, rail-track and shallow-water checks on
  `m_wheelColPoint[i].m_nSurfaceTypeB`).
- Bikes have their own `CVehicle::ProcessBikeWheel` (Vehicle.cpp:3297, `0x6D73B0`, **NOHOOK**). Monster trucks,
  trailers and quads override ProcessSuspension and SetupSuspensionLines.

### 3.4 Other classes

| Class | ProcessControl | Notes |
|---|---|---|
| `CBike` | `0x6B9250`, **stub, 14 KB** (Bike.cpp:523) | Rider anims, lean (`CalculateLeanMatrix` is reversed), and its own ProcessAI (`0x6BC930`, stub) |
| `CBmx` | Reversed (Bmx.cpp:77). Calls `CBike::ProcessControl` | PreRender and ProcessAI are stubs |
| `CBoat` | Reversed (Boat.cpp:511) | Calls `CVehicle::ProcessBoatControl` (Vehicle.cpp:4013) and `m_vehicleAudio.Service()` (:512) |
| `CTrain` | Reversed (Train.cpp:415, `0x6F86A0`) | Track following: `FindPositionOnTrackFromCoors` and the track-file loaders are stubs |
| `CHeli` | `0x6C7050`, **NOHOOK** (Heli.cpp:347) | ProcessControlInputs, ProcessFlyingCarStuff and PreRender are also NOHOOK |
| `CPlane` | `0x6C9260`, **stub** (Plane.cpp:377) | Has untested reversed code after the `return plugin::CallMethod` |
| `CQuadBike` / `CTrailer` | Quad is a stub (`0x6CDCC0`). Trailer is reversed | Both call `CAutomobile::ProcessControl` |
| `CMonsterTruck` | Stub (`0x6C8250`) | ProcessSuspension (`0x6C83A0`) is also a stub |

Flight physics live in `CVehicle::FlyingControl(eFlightModel, ...)` (Vehicle.cpp:3390), which is shared by planes,
helis and the RC aircraft. Rotor blade collision is in `CVehicle::BladeColSectorList` (Vehicle.cpp:3792).

### 3.5 PreRender and Render

- `CVehicle::PreRender` (Vehicle.cpp:651) handles collision lighting, `PreRenderDriverAndPassengers`, 2dfx lights and
  the env-map coefficient. `CVehicle::Render` (:669) sets the dirt textures and then calls `CEntity::Render`.
- `CAutomobile::PreRender` (`0x6AAB50`, **8.5 KB, NOHOOK**) and `CAutomobile::Render` (`0x6A2B10`, NOHOOK) are still
  `plugin::CallMethod` stubs (Automobile.cpp:6417-6424). They handle wheel matrices (`UpdateWheelMatrix`, KNOWN),
  lights through `CVehicle::DoVehicleLights` (Vehicle.cpp:4903), exhaust and nitro effects, and dust. Every
  automobile-derived class depends on them.
- Lights and headlight beams: `DoHeadLightBeam` (:4666) and `DoVehicleLights` (:4903).

### 3.6 Audio hookup

`CVehicle::m_vehicleAudio` is a `CAEVehicleAudioEntity`. The constructors call `m_vehicleAudio.Initialise(this)` (for
example Automobile.cpp:378, Bike.cpp:159, Boat.cpp:125) and the destructors call `Terminate()`. `Initialise` reads
per-model settings with `GetVehicleAudioSettings` (AEVehicleAudioEntity.cpp:117). Each frame, `Service()`
(AEVehicleAudioEntity.cpp:973) runs from the vehicle's ProcessControl and calls `ProcessVehicle` (:1014 → :4550).
One-off sounds use `m_vehicleAudio.AddAudioEvent(AE_..., volume)`, for example tyre bursts, doors and hydraulics. Only
a handful of audio functions are still stubs, for example `ProcessGenericJet` and `ProcessPlayerBicycle`
(AEVehicleAudioEntity.cpp:4840-4850).

---

## 4. Handling

- The `HANDLING.CFG` file is parsed by `cHandlingDataMgr::LoadHandlingData` (cHandlingDataMgr.cpp:179) into fixed
  arrays (cHandlingDataMgr.h:232-235): `tHandlingData[210]`, `tBikeHandlingData[13]`, `tFlyingHandlingData[24]` and
  `tBoatHandlingData[12]`. The index is `CVehicleModelInfo::m_nHandlingId`. Use the accessors `GetVehiclePointer`,
  `GetBikeHandlingPointer`, `GetFlyingPointer` and `GetBoatPointer`. `ConvertDataToGameUnits` converts the values
  after loading.
- `tHandlingData` (tHandlingData.h:22) holds mass, drag, centre of mass, buoyancy, traction, the `cTransmission`
  (gears, drive type `'F'/'R'/'4'`, engine), brakes, steering lock, the suspension block, damage multiplier and two
  flag words:
  - `m_nModelFlags` (`eVehicleHandlingModelFlags`): IS_VAN, IS_BUS, NO_DOORS, the axle types, and
    `m_bIsBike/IsHeli/IsPlane/IsBoat`, which `IsReal*()` reads.
  - `m_nHandlingFlags` (`eVehicleHandlingFlags`): 1G/2G boost, NPC anti-roll, wheel widths, HYDRAULIC_INST, NOS_INST,
    offroad, PROC_REARWHEEL_FIRST and similar.
- **The handling data is shared by every vehicle of the model.** The vehicle stores a pointer, `m_pHandlingData`.
  Per-vehicle state lives in `CVehicle::m_nHandlingFlagsIntValue` / `handlingFlags` (union at Vehicle.h:210-243), a copy that the
  constructor takes and that upgrades toggle (for example `bNosInst` and `bHydraulicInst`).
- `ProcessAI` returns per-frame extra flags (`eExtraHandlingFlags`, Automobile.h:31): PERFECT, NITROS,
  WHEELS_TOUCHING_SAND and TAXI_BOOST.

---

## 5. Damage

- **Health**: `CVehicle::m_fHealth` (Vehicle.h:370) starts at 1000. At or below 0 the vehicle burns and explodes
  (`CAutomobile::ProcessCarOnFireAndExplode`, Automobile.cpp:4857, then `BlowUpCar` → `BlowUpCar_Impl` at :2292).
- **Collision damage**: `CPhysical`'s collision code stores `m_fDamageIntensity` and `m_pDamageEntity`. The next
  ProcessControl calls `VehicleDamage(0, ...)`. `CAutomobile::VehicleDamage` (Automobile.cpp:2805, `0x6A7650`) checks
  `CanVehicleBeDamaged` (Vehicle.cpp:1311), emits `CEventVehicleDamageCollision`, subtracts health, and calls
  `m_damageManager.ApplyDamage(...)`.
- **`CDamageManager`** (DamageManager.h:103, fully reversed) packs the state of the wheels (`eCarWheelStatus`), doors
  (`eDoorStatus`), lights (2 bits each), panels (4 bits each) and engine (`m_nEngineStatus`, 0-250).
  `ApplyDamage(vehicle, tComponent, intensity, colDmgMult)` (DamageManager.cpp:79) maps the component to a group,
  scales the intensity, and only acts above 150. It then progresses the panel, wheel or door damage and calls back
  into `CAutomobile::SetBumperDamage/SetPanelDamage/SetDoorDamage` (Automobile.cpp:6028-6118) to swap in the damaged
  atomics or spawn flying components (`SpawnFlyingComponent`, :4982).
- **Weapons and explosions**: `CVehicle::InflictDamage` (Vehicle.cpp:3318, `0x6D7C90`) is still a raw `__thiscall`
  function-pointer call with its hook commented out. Bikes have `CBike::VehicleDamage` (stub) and
  `DamageKnockOffRider` (reversed). Planes have `CPlane::VehicleDamage` (stub) and aeroplane component damage
  (`ProgressAeroplaneDamage`).

---

## 6. AI driving

### 6.1 The pieces

| Piece | Role |
|---|---|
| `CAutoPilot` (AutoPilot.h) | Per-vehicle AI state: current/next path nodes and links, lanes, `m_nCarMission` (`eCarMission`), `m_nCarDrivingStyle` (`eCarDrivingStyle`), `m_nTempAction` (`eAutoPilotTempAction`, for reversing, handbrake turns and swerving), cruise speed, target entity, destination, and an 8-entry pathfind node cache. The constructor is reversed but **not hooked** (AutoPilot.cpp:6, `0x6D5E20`). `ModifySpeed` (`0x41B980`) and `RemoveOnePathNode` (`0x41B950`) are NOHOOK stubs |
| `CCarAI` (CarAI.cpp) | **Fully reversed** (17/17). This is the high-level mission state machine. `UpdateCarAI` (CarAI.cpp:596) switches on status and mission, and moves between the `_FARAWAY` and `_CLOSE` variants (for example `MISSION_RAMPLAYER_FARAWAY` becomes `_CLOSE` once the car is near). It also covers police mission selection by wanted level, `GetCarToGoToCoors*`, `TellCarTo*` and `MakeWayForCarWithSiren` |
| `CCarCtrl` (CarCtrl.cpp) | The low-level layer: traffic spawning and removal, path-node picking, steering, collision avoidance, and boat, heli and plane steering. **Mostly unreversed** (see §8) |

### 6.2 Missions

`eCarMission` (`source/game_sa/Enums/eCarMission.h`) has about 69 values. The groups are:

- Cruising and stopping: `MISSION_NONE`, `CRUISE`, `WAITFORDELETION`, `STOP_FOREVER`, `EMERGENCYVEHICLE_STOP`.
- Going to coordinates: `GOTOCOORDINATES`, plus the `_STRAIGHTLINE`, `_ACCURATE`, `_ASTHECROWSWIMS` and `_RACING`
  variants, and `FOLLOW_RECORDED_PATH`.
- Chasing the player or a car: `RAM/BLOCK/APPROACH PLAYER_FARAWAY/_CLOSE`, `RAMCAR/BLOCKCAR/FOLLOWCAR_*`,
  `KILLPED_*`, `DO_DRIVEBY_*`.
- Formation: `ESCORT_*`, `PROTECTION_*`.
- Parking: `PARK_PERPENDICULAR/PARALLEL(_2)`.
- Air: `HELI_*` (fly to coordinates or in a direction, follow, police, news, land, keep entity in view, crash and
  burn), `PLANE_*` (fly to coordinates, attack the player, dog fight, crash and burn).
- Boats: `BOAT_ATTACKPLAYER`, `BOAT_CIRCLEPLAYER`.

### 6.3 Call chain for an AI car with physics

```
CAutomobile::ProcessAI (STATUS_PHYSICS)
 ├─ CCarAI::UpdateCarAI                        mission transitions (reversed)
 ├─ CCarCtrl::SteerAICarWithPhysics            0x437C20 NOHOOK (CarCtrl.cpp:849)
 │   └─ SteerAICarWithPhysics_OnlyMission      0x436A90 NOHOOK, 4.2 KB: big switch on m_nCarMission
 │       ├─ SteerAICarWithPhysicsFollowPath(_Racing), ...HeadingForTarget, ...TryingToBlockTarget(_Stop)
 │       ├─ SteerAICarParkParallel/Perpendicular, SteerAICarTowardsPointInEscort, ...FollowPreRecordedPath
 │       ├─ SteerAIBoatWithPhysics*  /  SteerAIHeli* / FlyAIHeli*  /  SteerAIPlane* / FlyAIPlaneInCertainDirection
 │       └─ FindMaximumSpeedForThisCarInTraffic → SlowCarDownFor{Cars,Peds,Objects}SectorList, WeaveThrough*
 └─ CCarCtrl::ReconsiderRoute                  0x42FC40 NOHOOK
STATUS_SIMPLE: CCarCtrl::UpdateCarOnRails      0x436540 NOHOOK (CarCtrl.cpp:984)
                └─ SlowCarOnRailsDownForTrafficAndLights (reversed)
Route choice: PickNextNodeAccordingStrategy (0x432B10) → PickNextNodeRandomly (3.5 KB) / ToFollowPath / ToChaseCar
Physics LOD: SwitchVehicleToRealPhysics, SwitchBetweenPhysicsAndGhost, JoinCarWithRoadSystem(GotoCoors)
```

The script-facing helpers `CCarCtrl::JoinCarWithRoadAccordingToMission` (CarCtrl.cpp:483) and `CreateCarForScript`
are reversed.

---

## 7. Enter and exit

- Peds use the task system. The tasks are `CTaskComplexEnterCar` (with the AsDriver/AsPassenger and Timed variants),
  `CTaskSimpleCarOpenDoorFromOutside`, `CTaskSimpleCarGetIn`, `CTaskSimpleCarAlign`, `CTaskSimpleCarCloseDoor*`,
  `CTaskComplexLeaveCar*` and `CTaskSimpleCarDrive`, all in `source/game_sa/Tasks/TaskTypes/`.
- `CCarEnterExit` (CarEnterExit.cpp, 23/28 reversed) holds the shared geometry and rules. These include
  `GetNearestCarDoor`, `ComputeTargetDoorToExit`, `IsCarDoorReady`/`InUse`, `IsCarQuickJackPossible`,
  `IsCarSlowJackRequired`, `SetPedInCarDirect` and `IsVehicleStealable`. **Still stubs**: `GetNearestCarPassengerDoor`
  (`0x650BB0`), `GetPositionToOpenCarDoor` (`0x64E740`), `IsPlayerToQuitCarEnter` (`0x64F240`),
  `IsRoomForPedToLeaveCar` (`0x6504C0`) and `MakeUndraggedPassengerPedsLeaveCar` (`0x64F540`).
- On the vehicle side: `CVehicle::SetDriver` (Vehicle.cpp:1435), `AddPassenger` (:1372/:1389), `RemoveDriver` and
  `RemovePassenger`, and the virtual door functions `OpenDoor`, `ProcessOpenDoor`, `IsDoor*` and
  `CanPedStepOutCar/JumpOutCar`. Door ids: `eDoors` (DamageManager.h:82). Bikes have no real doors, so
  `CBike::IsDoor*` are inline header overrides that return constants (Bike.h:119 onwards).

---

## 8. Model info, components and dummies

- `CVehicleModelInfo` (VehicleModelInfo.h, 67/76 reversed) holds `m_nVehicleType`, `m_nHandlingId`,
  `m_nVehicleClass`, the wheel model and sizes, colours, the upgrade list (`m_anUpgrades`) and
  `m_pVehicleStruct` (`CVehicleStructure`). That struct holds `m_avDummyPos[eVehicleDummy]` (lights, seats, exhaust,
  engine, gas cap, trailer attach, vehicle gun and others; enum at VehicleModelInfo.h:26) and
  `m_aUpgrades[eVehicleUpgradePosn]`. Use `GetModelDummyPosition(dummy)`.
- `SetClump` and `PreprocessHierarchy` find the frames by name through `ms_vehicleDescs[eVehicleType]` (`0x8A7740`).
  Extras are chosen by `ChooseComponent` and `ms_compsToUse`. The loaders are `SetupCommonData`,
  `LoadVehicleColours` and `LoadVehicleUpgrades`.
- At runtime each class keeps an `RwFrame*` table indexed by its node enum, filled by `SetupModelNodes()`: CAutomobile
  uses `m_aCarNodes[eCarNodes]` (Automobile.h:44, `source/game_sa/Enums/eCarNodes.h`), CBike uses
  `m_aBikeNodes[eBikeNodes]` (Bike.h:14), and CBoat and CTrain have their own tables. `GetComponentWorldPosition` and
  `IsComponentPresent` index these tables.

---

## 9. Gotchas

1. **`CCarCtrl` stubs are now registered** (83 `.Reversed = false` stubs, 20 reversed, 6 still NOHOOK after
   `add_missing_hooks.py`). A NOHOOK function cannot be toggled in the F7 menu, and **the original code never
   reaches it**. Only our own C++ callers do. When you reverse one, flip its stub hook or add `RH_ScopedInstall(Name, 0xADDR)` to
   `CCarCtrl::InjectHooks` (CarCtrl.cpp:26). See [HookRegistration.md](../HookRegistration.md). CHeli (ProcessControl,
   PreRender, ProcessControlInputs, ProcessFlyingCarStuff, BlowUpCar) and `CAutomobile::PreRender/Render` have the
   same problem.
2. **Type versus sub-type** (§1.1). `vehicle->IsPlane()` and `vehicle->IsTrailer()` are never true for
   `CPlane`/`CTrailer` objects. Two places look suspicious: `TaskSimpleCarJumpOut.cpp:154` (`m_veh->IsPlane()`; the original checks the
   sub-type at `0x64DE90`) and `Physical.cpp:409` (`vehicle->IsTrailer()`, unverified). Check them against the original before you copy the pattern.
3. **`IsSubFlyingVehicle()` is always false.** It is written as `IsSubHeli() && IsSubPlane()` (Vehicle.h:717) instead of
   `||` (`CGangWars::Update` checks heli OR plane at `0x446DB3`). Used in GangWars.cpp:917.
4. **Some reversed code is not hooked**, so the coverage tool reports it as KNOWN. Examples are the `CHeli` and
   `CAutoPilot` constructors and CHeli's `GenerateHeli`, `SendDownSwat` and `UpdateHelis`. Vehicle.cpp also has
   16 raw `((void(__thiscall*)(...))0xADDR)` calls and 22 commented-out `// RH_Scoped...` lines, for example
   `InflictDamage`, `ProcessWeapons`, `ProcessSirenAndHorn`, `UpdateTrailerLink`, `DoSunGlare` and
   `AddVehicleUpgrade` (Vehicle.cpp:183-216). Some of these have bodies that only forward. Do not assume that a body
   is a real reimplementation.
5. **Stubs with dead code after them.** `CPlane::ProcessControl` (Plane.cpp:377) returns through
   `plugin::CallMethod` and has "untested" reversed code below the return. Treat that code as a draft.
6. **Handling data is shared.** Writing through `m_pHandlingData` changes every car of that model.
   Copy the value first, or change the per-vehicle `handlingFlags`.
7. **Missing CTrain overrides** (§2.1). CTrain objects carry the original vtable (the constructor `0x6F6030` is a
   stub), so at runtime the original overrides still run. Our headers simply do not declare them.
8. **Order of ProcessControl and ProcessCollision.** Wheel colpoints read in ProcessControl come from the *previous*
   collision pass. ProcessControl resets the compression to 1.0 at the end (Automobile.cpp:917-918).
9. **Coverage tool caveats.** Template-hooked functions, for example
   `RH_ScopedInstall(BladeColSectorList<CPtrListSingleLink<CEntity*>>, 0x6DAF00)` (Vehicle.cpp:188), and inline
   header overrides show as MISSING. So do the 30-byte "deleting destructor" thunks (`~CBike` at `0x6B8950` and
   similar). Ignore those.

---

## 10. Reversing status (IDB coverage)

Generated with `python .claude/skills/find-reversing-targets/scripts/idb_coverage.py --class <C>` and `--markdown
--classes 400`. The statuses are REVERSED (hooked and reversed), STUB (hooked with `.Reversed = false`), NOHOOK
(`plugin::Call` without a hook), KNOWN (address in the repository but not hooked) and MISSING (not in the repository).

| Class | Funcs | Reversed | Stub | NoHook | Known | Missing | Bytes left |
|---|---|---|---|---|---|---|---|
| `CCarCtrl` | 110 | 20 | 83 | 6 | 0 | 1 | ~81,000 |
| `CBike` | 56 | 22 | 18 | 0 | 1 | 15 | 34,010 |
| `CVehicle` | 203 | 137 | 0 | 3 | 17 | 46 | 19,976 |
| `CPlane` | 25 | 14 | 9 | 0 | 1 | 1 | 17,765 |
| `CAutomobile` | 117 | 99 | 0 | 2 | 11 | 5 | 16,020 |
| `CHeli` | 27 | 11 | 0 | 5 | 10 | 1 | 13,686 |
| `CTrain` | 45 | 17 | 19 | 0 | 0 | 9 | 10,852 |
| `CBmx` | 14 | 7 | 3 | 0 | 3 | 1 | 8,440 |
| `CCarEnterExit` | 28 | 23 | 5 | 0 | 0 | 0 | 4,789 |
| `CMonsterTruck` | 13 | 5 | 6 | 0 | 0 | 2 | 3,041 |
| `CAutoPilot` | 3 | 0 | 0 | 2 | 1 | 0 | 1,024 |
| `CQuadBike` | 12 | 9 | 1 | 0 | 0 | 2 | 964 |
| `CBoat` | 23 | 22 | 0 | 0 | 0 | 1 (dtor thunk) | ~0 |
| `CCarAI`, `CDamageManager` | 17, 27 | all | 0 | 0 | 0 | 0 | 0 |
| `CTrailer`, `cHandlingDataMgr`, `CVehicleModelInfo` | 16, 11, 76 | 13, 9, 67 | 0 | 0 | 0, 1, 0 | 3, 1, 9 | small |

### What is left, by class

- **CCarCtrl (81 KB).** Nearly the whole driving AI. The biggest functions are `GenerateOneRandomCar` (7.9 KB),
  `SteerAICarWithPhysics_OnlyMission` (4.2 KB), `SteerAICarWithPhysicsFollowPath` (3.7 KB), `PickNextNodeRandomly`
  (3.6 KB), `SteerAICarWithPhysicsFollowPath_Racing` (3.3 KB), `FlyAIHeliInCertainDirection` (3.0 KB),
  `GenerateCarCreationCoors2` (2.8 KB) and `PickNextNodeToFollowPath`/`ToChaseCar` (2.1-2.2 KB). The factory
  `GetNewVehicleDependingOnCarModel` is a STUB with its body already drafted in a comment.
- **CBike (34 KB).** `ProcessControl` (14 KB), `ProcessRiderAnims` (3.3 KB), `PreRender` (3.5 KB), `FixHandsToBars`
  (2.5 KB), `ProcessControlInputs` (1.8 KB), `ProcessAI` (1.6 KB), `SetupSuspensionLines`, `DoBurstAndSoftGroundRatios`,
  `DoSoftGroundResistance`, `VehicleDamage`, `BurstTyre`, `PlaceOnRoadProperly`, `GetCorrectedWorldDoorPosition`. It
  also needs `CVehicle::ProcessBikeWheel` (2 KB, NOHOOK).
- **CVehicle (20 KB).** `AddSingleWheelParticles` (2.9 KB), `DoSunGlare` (2.2 KB), `ProcessBikeWheel`,
  `InflictDamage` (1.6 KB), `UpdateTrailerLink`/`UpdateTractorLink`, the weapon helpers (`ProcessWeapons`,
  `FireHeatSeakingMissile`, `GetPlaneWeaponFiringStatus`, `FireFixedMachineGuns`, `DoDriveByShootings`), the upgrade
  helpers and `ProcessSirenAndHorn`.
- **CPlane (18 KB).** `PreRender` (5.7 KB), `ProcessFlyingCarStuff` (3.3 KB), `ProcessControlInputs` (2.5 KB),
  `DoPlaneGenerationAndRemoval` (2.3 KB), `VehicleDamage`, `BlowUpCar`, `FindPlaneCreationCoors`, `ProcessControl`.
- **CAutomobile (16 KB).** `PreRender` (8.5 KB) and `Render` (1 KB), both NOHOOK, plus KNOWN functions:
  `UpdateWheelMatrix` (2.2 KB), `DoHeliDustEffect`, `SetRandomDamage`, `DoNitroEffect`,
  `HasCarStoppedBecauseOfLight`, `SetUpWheelColModel` and `BurstTyre`.
- **CHeli (14 KB).** `ProcessControl` (2.4 KB), `ProcessControlInputs`, `ProcessFlyingCarStuff`, `PreRender` and
  `BlowUpCar` (all NOHOOK). `SearchLightCone` (3.1 KB), `GenerateHeli`, `SendDownSwat` and `UpdateHelis` are KNOWN
  (check whether the existing body is complete, then hook it).
- **CTrain (11 KB).** Track and station logic (`FindPositionOnTrackFromCoors`, `ReadAndInterpretTrackFile`,
  `DoTrainGenerationAndRemoval` at 2.2 KB, `CreateMissionTrain`, `AddNearbyPedAsRandomPassenger`, `InitTrains`, the
  `Find*` helpers), the constructor, and the undeclared virtual functions listed in §2.1.

### Suggested order

1. **Hook what already exists.** Add the RH installs for KNOWN bodies (CHeli constructor, `GenerateHeli`,
   `SendDownSwat`, `UpdateHelis`, `CAutoPilot` constructor, the commented-out CVehicle hooks) after you check each one
   against IDA. This is cheap, and it makes the existing code testable in the F7 menu.
2. **The CCarCtrl leaves.** Pick small, self-contained NOHOOK helpers first: `FindSpeedMultiplier`,
   `FindPercDependingOnDistToLink`, `ThisVehicleShouldTryNotToTurn`, `StopCarIfNodesAreInvalid`,
   `SwitchVehicleToRealPhysics`, `UpdateCarCount`, `ClearInterestingVehicleList`, `RegisterVehicleOfInterest`,
   `CAutoPilot::ModifySpeed`/`RemoveOnePathNode`, `GetNewVehicleDependingOnCarModel`. Then move to the
   `SlowCarDownFor*`/`WeaveThrough*` sector lists, the `PickNextNode*` functions, the
   `SteerAICarWithPhysics*` family, and finally `SteerAICarWithPhysics`, `_OnlyMission` and `GenerateOneRandomCar`.
3. **CBike, bottom-up.** `SetupSuspensionLines`, `DoBurstAndSoftGroundRatios`, `DoSoftGroundResistance`,
   `CVehicle::ProcessBikeWheel`, `ProcessControlInputs`, `ProcessAI`, then `ProcessControl`, and last
   `ProcessRiderAnims`, `FixHandsToBars` and `PreRender`. After CBike, the CBmx stubs follow.
4. **Air vehicles.** `CVehicle::FlyingControl` is already reversed, so the CHeli and CPlane
   `ProcessControlInputs`/`ProcessFlyingCarStuff`/`ProcessControl` functions are tractable. Do PreRender later. The
   CCarCtrl heli and plane steering functions pair naturally with this step.
5. **CTrain track logic,** then **`CAutomobile::PreRender`** (large, mostly visual, so A/B it in-game).

For every function, follow [ReversingGuide.md](../ReversingGuide.md) §5. Do the static fingerprint first, then a
difftest where the function is deterministic (`source/difftests/`), then an in-game A/B with the hook toggled.
Steering and physics code is float-heavy, so read the x87 cheat-sheet (ReversingGuide §2).
