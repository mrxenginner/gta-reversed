# Peds

How peds (pedestrians, the player, cops, medics...) are put together in gta-reversed, what runs every frame, and
what is left to reverse. Read [../Architecture.md](../Architecture.md) and [../ReversingGuide.md](../ReversingGuide.md)
first. The AI side (tasks, events, decision makers) is only covered where it plugs into `CPed`.

All paths are relative to `source/game_sa/` unless they start with `source/` or `docs/`. Addresses come from the
repo (`RH_ScopedInstall`, `// 0x...`, `@addr`, `StaticRef`).

---

## 1. Overview

```
CEntity ─> CPhysical ─> CPed ─┬─> CPlayerPed      (one per player, CWorld::Players[i].m_pPed)
                              ├─> CCivilianPed    (random and mission civilians, gang members, criminals...)
                              ├─> CCopPed         (adds partner and criminals-to-kill list)
                              └─> CEmergencyPed   (medics, firemen)

CPed ──owns──> CPedIntelligence* (pooled)      ──> CTaskManager, CEventHandler, CEventGroup, scanners
     ──points─> CPlayerPedData* (players only)  ──> lives inside CPlayerInfo (CWorld::Players[])
     ──embeds─> CPedIK, CWeapon[NUM_WEAPON_SLOTS], CAcquaintance, 3 audio entities
     ──points─> CPedStat* (from CPedStats), model is a CPedModelInfo
```

- A ped is a skinned `RpClump`. Animation goes through the RpAnimBlend plugin (`Plugins/RpAnimBlendPlugin/RpAnimBlend.h`)
  and `CAnimManager` (`Animation/AnimManager.h`). `m_apBones` caches one `AnimBlendFrameData*` per `ePedNode`.
- Behaviour comes almost entirely from tasks run by `CPedIntelligence`. `CPed` itself does physics glue, weapons,
  rendering tweaks (IK, look-at, gun flash) and bookkeeping.
- Peds are allocated from the ped pool (`CPed::operator new`, `0x5E4720` / `0x5E4730`). `CPopulation` creates and
  removes random peds. Scripts create mission peds (`ePedCreatedBy::PED_MISSION`).

---

## 2. Key classes

| Class | File | Size (`VALIDATE_SIZE`) | Notes |
|---|---|---|---|
| `CPed` | `Entity/Ped/Ped.h:111` | `0x79C` (`Ped.h:655`) | Vtable `0x86C358`, 26 entries (`RH_ScopedVirtualClass`, `Ped.cpp:36`) |
| `CPlayerPed` | `Entity/Ped/PlayerPed.h:18` | `0x7A4` | Vtable `0x86D168`. Adds `m_p3rdPersonMouseTarget`, `field_7A0` |
| `CCivilianPed` | `Entity/Ped/CivilianPed.h` | `0x79C` | Vtable `0x86C0A8`. Only overrides `ProcessControl` (`0x5DDBF0`) |
| `CCopPed` | `Entity/Ped/CopPed.h:22` | `0x7C4` | Vtable `0x86C120`. `m_pCopPartner`, `m_apCriminalsToKill[5]`, `ProcessControl` `0x5DE160` |
| `CEmergencyPed` | `Entity/Ped/EmergencyPed.h:11` | `0x79C` | **No `InjectHooks` at all** (see Gotchas) |
| `CPedIntelligence` | `PedIntelligence.h:38` | `0x294` | Task manager, event handler/group, vehicle/ped/event/collision scanners, stuck checker |
| `CPlayerPedData` | `PlayerPedData.h:19` | `0xAC` | Wanted, clothes, sprint/breath, targeting, player group id |
| `CPlayerInfo` | `PlayerInfo.h:27` | `0x190` | `CWorld::Players` (`StaticRef`, `0xB7CD98`), `PlayerInFocus` `0xB7CD74` |
| `CPedGroup` | `PedGroup.h:16` | `0x2D4` | Leader (`m_pPed`), `CPedGroupMembership` (`0x28`), `CPedGroupIntelligence` (`0x2A0`) |
| `CPedGroups` | `PedGroups.h` | static | `ms_groups` = 8 groups at `0xC09920`, `ms_activeGroups` `0xC098E0` |
| `CPopulation` | `Population.h` | static | Spawning/removal, ped/car groups, counters (`ms_nTotalPeds` `0xC0EC28`, ...) |
| `CPedModelInfo` | `Models/PedModelInfo.h:23` | `0x44` | Anim group, ped type, stat type, hit col model, radio, voices |
| `CPedStat` / `CPedStats` | `PedStats.h` | `0x34` / static | `ms_apPedStats` `0xC0BBEC`, loaded from `DATA\PEDSTATS.DAT` |
| `CWeapon` | `Weapon.h:49` | `0x1C` | Type, state, clip ammo, total ammo, next-shot time |
| `CWeaponInfo` | `WeaponInfo.h` | `0x70` | `aWeaponInfo` `0xC8AAB8`, `GetWeaponInfo(type, skill)` |
| `CPedIK` | `PedIK.h:36` | `0x20` | Torso/arm aiming, slope pitch. `ms_torsoInfo` `0x8D22E4` |
| `CPedSaveStructure` | `Entity/Ped/PedSaveStructure.h` | `0x18C` | Save/load blob |

There is **no `CPedWeapons` class**. Weapons are `CPed::m_aWeapons` (`std::array<CWeapon, NUM_WEAPON_SLOTS>`) plus
`m_nActiveWeaponSlot`, `m_nSavedWeapon`, `m_nDelayedWeapon` (`Ped.h:312-318`).

### `CPed` layout highlights (`Ped.h`)

| Field | Line | Meaning |
|---|---|---|
| `m_pedAudio`, `m_pedSpeech`, `m_weaponAudio` | 118-120 | Protected. Use `GetAE()`, `GetSpeechAE()`, `GetWeaponAE()` |
| Flag struct (4 x 32 bits) | 128-264 | See §4 |
| `m_pIntelligence`, `m_pPlayerData`, `m_nCreatedBy` | 267-269 | Protected. Use `GetIntelligence()`, `GetPlayerData()`, `IsCreatedBy*()` |
| `m_apBones[TOTAL_PED_NODES]` | 272 | Frame data per `ePedNode` (TODO in source: should be named `m_apNodes`) |
| `m_nAnimGroup` | 273 | `AssocGroupId` copied from the model info in `SetModelIndex` |
| `m_pedIK` | 287 | `CPedIK`, constructed with `this` |
| `m_nPedState`, `m_nMoveState` | 289-290 | `ePedState` (legacy state machine, still used everywhere), `eMoveState` |
| `m_fHealth`, `m_fMaxHealth`, `m_fArmour` | 293-295 | |
| `m_fCurrentRotation`, `m_fAimingRotation` | 298-299 | Heading in radians. `CalculateNewOrientation` writes the matrix from it |
| `m_pVehicle` | 307 | May be set while not in a vehicle. Use `IsInVehicle()` / `GetVehicleIfInOne()` |
| `m_pStats` | 311 | `CPedStat*` |
| `m_pTargetedObject` | 319 | Lock-on target |

---

## 3. Virtual interface (declaration order, `Ped.h:360-378`)

Overrides of `CEntity`/`CPhysical`, then the three virtuals `CPed` adds. Hook status from `Ped.cpp:174-188`:

| Virtual | Address | Hooked? |
|---|---|---|
| `SetModelIndex` | `0x5E4880` | VMT, reversed |
| `DeleteRwObject` | `0x5DEBF0` | VMT, reversed |
| `ProcessControl` | `0x5E8CD0` | **No.** Install is commented out (`Ped.cpp:176`), body is `plugin::CallMethod` (`Ped.cpp:3692`) |
| `Teleport` | `0x5E4110` | VMT, reversed |
| `SpecialEntityPreCollisionStuff` | `0x5E3C30` | **No** (commented out, `plugin::CallMethod`) |
| `SpecialEntityCalcCollisionSteps` | `0x5E3E90` | **No** (commented out, `plugin::CallMethod`) |
| `PreRender` | `0x5E8A20` | VMT, reversed (calls `PreRenderAfterTest` unless driving) |
| `Render` | `0x5E7680` | VMT, reversed |
| `SetupLighting` / `RemoveLighting` | `0x553F00` / `0x5533B0` | VMT, reversed |
| `FlagToDestroyWhenNextProcessed` | `0x5E7B70` | VMT, reversed |
| `ProcessEntityCollision` | `0x5E2530` | **No** (commented out, `plugin::CallMethod`) |
| `SetMoveAnim` (new) | `0x5E4A00`, vtable slot `0x86C3B4` | VMT, reversed |
| `Save` / `Load` (new) | `0x5D5730` / `0x5D4640` | VMT, reversed |

Subclass overrides: `CPlayerPed::ProcessControl` (`0x60EA90`), `SetMoveAnim` (`0x609490`, empty), `Load`/`Save`
(`PlayerPed.cpp:34-76`). `CCopPed`, `CCivilianPed`, `CEmergencyPed` override `ProcessControl` only.

---

## 4. Ped flags (bitfields)

`Ped.h:128-264` is one anonymous struct of `bool : 1` fields, grouped in four 32-bit words. The source comments
name the words `m_nPedFlags`, `m_nSecondPedFlags`, `m_nThirdPedFlags`, `m_nFourthPedFlags` (these names appear in
IDA/plugin-sdk; there are no such members in the C++). The fourth word only uses 22 bits.

- Word 1 (bits 0-31): `bIsStanding` (bit 0), `bWasStanding`, `bIsLooking`, ..., `bInVehicle` (bit 8),
  `bIsInTheAir`, `bIsLanding`, ..., `bIsDucking` (bit 26), `bAllowMedicsToReviveMe` (bit 31).
- Word 2: `bResetWalkAnims`, `bOnBoat`, ..., `bFadeOut` (bit 3), ..., `bKnockedOffBike` (bit 31).
- Word 3: `bDonePositionOutOfCollision`, `bDontRender`, ..., `CantBeKnockedOffBike` is a **2-bit** `uint8` field
  (`Ped.h:234`, values in `eCantBeKnockedOffBike`), so every flag after it in that word is shifted by one bit
  compared to a naive count.
- Word 4: `bHasBulletProofVest`, `bUsingMobilePhone`, ..., `bUsedForReplay`.

When IDA shows `this->m_nThirdPedFlags & 0x1000` or `(ped->m_nPedFlags >> 8) & 1`, count bits from the start of
the word (comments give the bit index for word 1). Default values are set with default member initialisers, so the
constructor does not repeat them. A few names are known to be wrong (`bRemoveHead`, `bKnockedOffBike`,
`bDoBloodyFootprints` vs its "bIsLeader" comment); keep the existing name and add a comment instead of renaming in a
reversal PR.

---

## 5. Per-frame flow

Driven from `CGame::Process` (`Game.cpp:716`) and the render loop in `source/app/app_game.cpp`.

```
CGame::Process
 ├─ CPopulation::Update(generatePeds)            Population.cpp:1864   spawn/remove random peds
 ├─ CWorld::Process                              World.cpp:2155
 │   ├─ CEventGlobalGroup::AddEventsToPed / AddEventsToGroup, inform-friends/group queues, Flush
 │   ├─ entity->UpdateAnim()       for each moving entity (CEntity::UpdateAnim, Entity/Entity.cpp:1559)
 │   ├─ entity->ProcessControl()   for each moving entity; m_bRemoveFromWorld peds go to CPopulation::RemovePed
 │   │                             then again for "postponed" entities with CWorld::bForceProcessControl = true
 │   ├─ ProcessCollision / ProcessShift passes
 │   ├─ g_ikChainMan.Update, ProcessAttachedEntities
 │   ├─ for peds: CPedIntelligence::ProcessAfterProcCol, attached peds -> PositionAttachedPed  (World.cpp:2339-2347)
 │   ├─ CPlayerInfo::Process(i) for each player                                               (World.cpp:2355)
 │   └─ CPedScriptedTaskRecord::Process, CPedGroups::Process, RemoveFallenPeds every 8th frame
 └─ TheCamera.Process ...
app_game.cpp:334  CRenderer::PreRender()  -> CPed::PreRender -> PreRenderAfterTest (IK, shadows, skin anim)
app_game.cpp:335  CWorld::ProcessPedsAfterPreRender() -> CPedIntelligence::ProcessAfterPreRender (World.cpp:242)
```

### `CPed::ProcessControl` (`0x5E8CD0`, not reversed)

Order of the main calls, read from the IDA decompilation of `0x5E8CD0`. Use it as a map, not as truth; verify
every branch when you reverse it.

1. `CAEPedAudioEntity::Service` on `m_pedAudio`.
2. Fire/molotov FX on the ped (`FxManager_c::CreateFxSystem("molotov_flame", ...)` / `DestroyFxSystem`).
3. Every 32 frames (offset by the entity random seed): `CEntity::PruneReferences`. Fade-out alpha handling through
   `CVisibilityPlugins::GetClumpAlpha` / `SetClumpAlpha` (`bFadeOut`).
4. Mission-ped/player-group flag fix-ups, `ProcessBuoyancy` (`0x5E1FA0`, reversed), player wetness counter.
5. **Postpone check**: if standing on an entity that has not been processed yet and `CWorld::bForceProcessControl`
   (`0xB7CD6E`) is false, it sets the entity's "was postponed" flag and skips the rest. `CWorld::Process` calls it
   again later in the frame.
6. `CPedIntelligence::ProcessFirst` (`0x6073A0`), vertical speed clamp, then either `CPhysical::SkipPhysics` (ped
   standing still, see the long condition) or `CPhysical::ProcessControl`.
7. `RequestDelayedWeapon` (`0x5E8910`), `PlayFootSteps` (`0x5E57F0`, stub), **`CPedIntelligence::Process`**
   (`0x608260`), `CalculateNewVelocity` (`0x5E4C50`, stub), `UpdatePosition` (`0x5E1B10`, stub).
8. Bleeding (`CShadows::AddPermanentShadow`, `CLocalisation::Blood`), `CPopulation::UpdatePedCount`, death speech
   (`Say`), chainsaw idle audio, `CAEPedWeaponAudioEntity::Service`.

`CPedIntelligence::Process` (`PedIntelligence.cpp:950`) is reversed: vehicle scanner, ped scanner, event scanner,
`m_eventHandler.HandleEvents()`, `m_TaskMgr.ManageTasks()`, relationship recorder, `LookAtInterestingEntities`.

### Subclass `ProcessControl`

- `CPlayerPed::ProcessControl` (`PlayerPed.cpp:994`, reversed). Drunk blur and breath, then **`CPed::ProcessControl()`**
  (so it still runs GTA's code), wanted level update, minigun spin, target marker (`CWeaponEffects::MarkTarget`),
  sprint energy, `GetActiveWeapon().Update(this)`, weapon switch (`ProcessWeaponSwitch`, NOHOOK), `ProcessAnimGroups`
  (NOHOOK), look direction from the active camera (`TheCamera.GetActiveCamera().m_nMode == MODE_FOLLOWPED`),
  `ProcessGroupBehaviour` (NOHOOK), water cover, "keep up" group speech.
- `CCivilianPed::ProcessControl` (`CivilianPed.cpp:21`): skips everything if `GetCreatedBy() == (PED_MISSION | PED_GAME)`,
  otherwise `CPed::ProcessControl` then `GetActiveWeapon().Update(this)`.
- `CCopPed::ProcessControl` (`CopPed.cpp:239`): wanted-level/decision-maker tweaks, then `CPed::ProcessControl`,
  then (unless postponed or dead) `GetActiveWeapon().Update(this)` and target handling.
- `CEmergencyPed::ProcessControl` (`EmergencyPed.cpp:20`, not hooked): base call, then weapon update.

### Rendering side

- `CPed::PreRenderAfterTest` (`Ped.cpp:2827`, reversed): swim/jetpack roll and pitch, `m_pedIK.PitchForSlope()`,
  `UpdateRpHAnim()`, minigun barrel rotation, shadows (`CShadows::StoreShadowForPedObject`, real-time shadows),
  `ShoulderBoneRotation`, wind/rain cloth scaling and earthquake sway on bone matrices, blood/splash FX,
  `CPedModelInfo::AnimatePedColModelSkinnedWorld`.
- `CPed::Render` (`Ped.cpp:3752`, reversed), with NOTSA cheats `RenderBigHead` / `RenderThinBody`.

---

## 6. Subsystems that hang off peds

### Animation hookup

- `CPed::SetModelIndex` (`Ped.cpp:3633`): `RpAnimBlendClumpInit`, `RpAnimBlendClumpFillFrameArray(clump, m_apBones)`,
  stats from the model info, `m_nAnimGroup = mi->m_nAnimType`, `CAnimManager::AddAnimation(clump, group, ANIM_ID_IDLE)`,
  sets `RpAnimBlendClumpGetData(clump)->m_PedPosition` to `&m_vecAnimMovingShiftLocal` (root motion goes there),
  creates the skinned hit col model, `UpdateRpHAnim()`.
- `CPed::SetMoveAnim` (`Ped.cpp:389`) picks walk/run/sprint associations with `RpAnimBlendClumpGetAssociation` and
  blends idle with `CAnimManager::BlendAnimation`. `SetMoveAnimSpeed` scales them unless
  `bMoveAnimSpeedHasBeenSetByTask`.
- Most animation is started by tasks (`Tasks/TaskTypes/*`), not by `CPed`.
- `GetAnimHierarchy()` / `GetAnimBlendData()` (`Ped.cpp:621-626`) are the accessors to use.

### IK

- `CPedIK` (`PedIK.cpp`, fully hooked): `RotateTorso`, `RotateTorsoForArm`, `PointGunInDirection`,
  `PointGunAtPosition`, `PitchForSlope`, `MoveLimb`. Callers are mostly tasks (`TaskSimpleUseGun.cpp:324-326`,
  `TaskSimpleArrestPed.cpp:80`, `TaskSimpleSwim.cpp:216`) plus `PreRenderAfterTest`.
- Look-at and other chains go through `IKChainManager_c` (`Ragdoll/IKChainManager.h`, global `g_ikChainMan`), updated
  once per frame in `CWorld::Process`.

### Weapons

- `CPed::GiveWeapon` (`0x5E6080`), `SetCurrentWeapon` (`0x5E61F0`/`0x5E6280`), `AddWeaponModel`/`RemoveWeaponModel`,
  `GetWeaponSkill`, `DoGunFlash`, `SetGunFlashAlpha`, `GiveDelayedWeapon` are all reversed.
- `CWeapon::Update(owner)` is called from the subclass `ProcessControl`s. `CWeapon::Fire*` live in `Weapon.cpp`.
  Still stubs there: `FireInstantHit` (`0x73FB10`), `FireInstantHitFromCar` (`0x73EC40`), `DoTankDoomAiming`,
  `DoDriveByAutoAiming`, `FindNearestTargetEntityWithScreenCoors`, `CheckForShootingVehicleOccupant`
  (`Weapon.cpp:50-62`).
- Data: `CWeaponInfo::GetWeaponInfo(type, skill)` or the NOTSA overload `GetWeaponInfo(CPed*)` (`WeaponInfo.h:118,145`).

### Player

- `CPlayerInfo` holds `m_pPed`, `m_PlayerData` (the `CPlayerPedData` that `CPed::m_pPlayerData` points to), money,
  `m_pRemoteVehicle`, state. `CPlayerInfo::Process` (`0x56F8D0`) is still a stub (`PlayerInfo.cpp:16`).
- `CPlayerPed::GetPlayerGroup()` returns `CPedGroups::GetGroup(GetPlayerData()->m_nPlayerGroup)`.
- `CPlayerPed::UpdateCameraWeaponModes` (`PlayerPed.cpp:317`) is where the ped asks the camera for weapon modes.

### Groups

- `CPedGroups::Process` (`0x5FC800`) and `RemoveGroup` are hooked. The other 9 statics (`AddGroup`, `Init`,
  `IsGroupLeader`, `GetPedsGroup`, `GetGroupId`, `IsInPlayersGroup`, `AreInSameGroup`, ...) are `plugin::Call`
  without a hook (`PedGroups.cpp:24-116`): easy NOHOOK targets.
- `CPedGroup` stubs: `PlayerGaveCommand_Gather` (`0x5FAB60`), `FindDistanceToFurthestMember` (`0x5FB010`).

### Population

- `CPopulation::Update` (`Population.cpp:1864`): `ManagePopulation`, `RemovePedsIfThePoolGetsFull`, start-of-game
  generation countdown, `UpdatePedCounts`, then `AddToPopulation` (`0x614720`, **stub**, `Population.cpp:66`) and
  `GeneratePedsAtAttractors`. Spawn radii scale with `TheCamera.m_fGenerationDistMultiplier`.
- `ManagePed` deletes peds that are far enough (`(pos - player).Magnitude2D() * m_fRemovalDistMultiplier`) and
  `CanBeDeleted()`. `RemovePed` (`Population.cpp:271`) is the only correct way to delete a random ped.
- `AddPed` (`Population.cpp:812`) creates `CCopPed`/`CEmergencyPed`/`CCivilianPed` by ped type.

### Models and stats

- `CPedModelInfo`: `m_nAnimType`, `m_nPedType`, `m_nStatType`, `m_nCarsCanDriveMask`, hit col model
  (`m_pHitColModel`, created lazily by `CreateHitColModelSkinned`), radio stations, voices. Use
  `ped->GetPedModelInfo()`.
- `CPedStats::GetPedStatInfo(name)`, `GetPedStatByArrayIndex(i)`. `CPed::SetPedStats(ePedStats)` (`0x5DEBC0`).

---

## 7. Helpers to use

| Instead of | Use |
|---|---|
| `ped->m_pIntelligence->m_TaskMgr` | `ped->GetTaskManager()`, `GetIntelligence()->FindTaskByType(...)`, `GetTaskManager().HasAnyOf<TASK_...>()` |
| `ped->m_aWeapons[ped->m_nActiveWeaponSlot]` | `GetActiveWeapon()`, `GetWeaponInSlot(slot)`, `GetWeapon(type)` |
| `bInVehicle && m_pVehicle` | `IsInVehicle()`, `IsInVehicle(veh)`, `GetVehicleIfInOne()`, `GetRealPosition()` |
| raw state compares | `IsStateDriving()`, `IsStateDead()`, `IsStateDying()`, `IsStateDeadForScript()`, `IsAlive()` |
| ped type compares | `IsCop()`, `IsGangster()`, `IsCivilian()`, `IsPlayer()`, `AsCop()`, `AsPlayer()`, ... |
| `m_nCreatedBy == 2` | `IsCreatedByMission()`, `IsCreatedBy(PED_MISSION)` |
| `(CPedModelInfo*)GetModelInfo()` | `GetPedModelInfo()` |
| audio members | `GetAE()`, `GetSpeechAE()`, `GetWeaponAE()` |
| raw pointers to peds that may die | `CPed::Ref` (`notsa::EntityRef<CPed>`), or `CEntity::SetEntityReference` / `ClearReference` |
| player wanted | `GetPlayerWanted()`, `CPlayerPed::GetWanted()` |
| bone world position | `GetBonePosition(eBoneTag)`, `GetTransformedBonePosition(RwV3d&, bone)` |
| `rand()` style choices | `CGeneral::GetRandomNumberInRange` (as `SetModelIndex` does) |

Also: `FindPlayerPed()`, `FindPlayerVehicle()`, `CWorld::Players[i]`, `CPedGroups::GetGroup(id)`,
`CPopulation::GetModelsInPedGroup(group)`.

---

## 8. Gotchas

- **`CPed::ProcessControl` is not hooked.** `CPlayerPed`, `CCopPed`, `CCivilianPed` and `CEmergencyPed` call
  `CPed::ProcessControl()`, which forwards to GTA. When you reverse it, register it with `RH_ScopedVMTInstall`
  (see the commented line `Ped.cpp:176`) and difftest it hard: it is the hottest ped function.
- **The postpone mechanism.** `ProcessControl` can return early and mark the entity as postponed (step 5 above).
  Keep the exact condition and the `CWorld::bForceProcessControl` check, or peds standing on vehicles break.
- **`CEmergencyPed` is completely unhooked.** It has no `InjectHooks` and is not listed in `source/InjectHooksMain.cpp`.
  Its constructor (`0x5DE340`), `ProcessControl` (`0x5DE400`) and `InRange` (`0x5DE470`) are only comments. The
  `ProcessControl` copy also has a "bad OG expression" comment (`EmergencyPed.cpp:29`).
- **`CPlayerPed::CPlayerPed` (`0x60D5B0`) and `CPed::CanWeRunAndFireWithWeapon` (`0x5E88E0`) have C++ bodies but
  no hook** (`PlayerPed.cpp:124`, `Ped.cpp:3253`). Register and verify them before relying on them.
- **`CPedIntelligence::GetActivePrimaryTask` looks buggy** (`PedIntelligence.cpp:965-972`): the loop variable `i` is
  never used, it always queries `TASK_PRIMARY_PHYSICAL_RESPONSE`. Check against `0x4B85B0` before trusting it.
- **Bone IDs typed as `AnimationId`.** In `PreRenderAfterTest` the helper `GetHierMatrix(AnimationId id)`
  (`Ped.cpp:2931`) passes the value to `RpHAnimIDGetIndex`, which expects a bone tag. The `ANIM_ID_*` names there
  are meaningless; use `eBoneTag` in new code.
- **Flag word 3 shift** caused by the 2-bit `CantBeKnockedOffBike` (§4).
- `m_pVehicle` is not cleared when the ped leaves the car. Always test `bInVehicle` too.
- `CPed::Teleport` ignores `resetRotation` and flushes tasks only for players or peds leaving a car (`Ped.cpp:3701`).
- `CCivilianPed::ProcessControl` compares `GetCreatedBy()` with `PED_MISSION | PED_GAME` (= `PED_GAME_MISSION`,
  replay peds). Keep the expression as is.
- Several stubs use raw casts instead of `plugin::CallMethod` (`UpdatePosition`, `CalculateNewVelocity`,
  `DoFootLanded`, `SortPeds`). They behave the same, but grep for `0x5E` addresses, not only for `plugin::`.
- `PlayFootSteps` (`Ped.cpp:2218`) keeps half-working reversed code after an early `return plugin::CallMethod`.
  Don't assume it is correct.
- `IsPedPointerValid(ped)` / `CPed::IsPointerValid()` check the pool slot. Use them on pointers that come from
  scripts or old frames.

---

## 9. Reversing status

From `python .claude/skills/find-reversing-targets/scripts/idb_coverage.py` (IDA database vs repo):

| Class | Functions | Reversed | Stub | NOHOOK | Known | Missing | Bytes left |
|---|---|---|---|---|---|---|---|
| `CPed` | 148 | 130 | 7 | 4 | 2 | 5 | 23,370 |
| `CPlayerPed` | 66 | 48 | 1 | 14 | 1 | 2 | 16,020 |
| `CPedGeometryAnalyser` | 36 | 7 | 29 | 0 | 0 | 0 | 18,694 |
| `CPedGroups` | 12 | 2 | 0 | 9 | 1 | 0 | 677 |
| `CPedGroupIntelligence` | 29 | 26 | 0 | 1 | 2 | 0 | 559 |
| `CPlayerPedData` | 6 | 5 | 0 | 0 | 0 | 1 | 537 |
| `CPedStuckChecker` | 1 | 0 | 0 | 1 | 0 | 0 | 2,174 |
| `CPedGroupPlacer` | 4 | 1 | 3 | 0 | 0 | 0 | 3,661 |
| `CPedDamageResponseCalculator` | 7 | 2 | 4 | 0 | 0 | 1 | 1,437 |
| `CWeapon` | 40 | 34 | 6 | 0 | 0 | 0 | 12,498 |

(`--class CPed` prints 149 rows because it also matches `CPool::CPed::AtIndex`. Several "MISSING" rows such as the
pool `operator new/delete` are registered in `Ped.cpp:46-48` under overloaded names the script does not match.)

Remaining `CPed` work (size in bytes):
- Stubs: `CalculateNewVelocity` 1679, `UpdatePosition` 1167, `PlayFootSteps` 1755, `DoFootLanded` 1130,
  `PositionPedOutOfCollision` 2966, `KillPedWithCar` 4009, `PositionAttachedPed` 767.
- NOHOOK: `ProcessControl` 2145, `ProcessEntityCollision` 4345, `SpecialEntityPreCollisionStuff` 603,
  `SpecialEntityCalcCollisionSteps` 342.
- Known/unhooked: `SortPeds` (unused), `MakeTyresMuddySectorList` 1891 (stub install exists, template instance).

Remaining `CPlayerPed` work: NOHOOK `SetRealMoveAnim` 2711, `FindWeaponLockOnTarget` 2270,
`DrawTriangleForMouseRecruitPed` 1593, `FindNextWeaponLockOnTarget` 1362, `Compute3rdPersonMouseTarget` 1062,
`MakeThisPedJoinOurGroup` 1029, `ProcessWeaponSwitch` 1012, `ProcessAnimGroups` 909, `KeepAreaAroundPlayerClear` 817,
`SetInitialState` 727, `ProcessGroupBehaviour` 589, `ControlButtonSprint` 526, `EvaluateNeighbouringTarget` 388,
`PlayerWantsToAttack` 200; stub `GetButtonSprintResults` 113; known constructor 465; missing destructors.

### Suggested order

1. **Quick wins (register and verify):** `CPedGroups` NOHOOKs, `CPed::CanWeRunAndFireWithWeapon`, the
   `CPlayerPed` constructor, `CEmergencyPed` hooks, `CPlayerPed::GetButtonSprintResults` / `ControlButtonSprint` /
   `PlayerWantsToAttack`.
2. **Collision leaves:** `SpecialEntityCalcCollisionSteps`, `SpecialEntityPreCollisionStuff` (VMT installs).
3. **Movement core:** `CalculateNewVelocity`, then `UpdatePosition`. Both run for every ped every frame. Write
   difftests (`source/difftests/`) before swapping.
4. **Footsteps:** `DoFootLanded`, then `PlayFootSteps` (partial code exists).
5. **`CPed::ProcessControl`** once 3-4 are done, so its callees are already ours.
6. **Player input/targeting:** `ProcessAnimGroups`, `SetRealMoveAnim`, `ProcessWeaponSwitch`, then the targeting
   chain `EvaluateNeighbouringTarget` -> `FindNextWeaponLockOnTarget` -> `FindWeaponLockOnTarget` ->
   `Compute3rdPersonMouseTarget`, then group behaviour (`MakeThisPedJoinOurGroup`, `ProcessGroupBehaviour`,
   `DrawTriangleForMouseRecruitPed`), `KeepAreaAroundPlayerClear`, `SetInitialState`.
7. **Big leaves last:** `ProcessEntityCollision`, `PositionPedOutOfCollision`, `KillPedWithCar`,
   `PositionAttachedPed`, `CPopulation::AddToPopulation`, `CPlayerInfo::Process`.

Use the skills in `.claude/skills/` (`reverse-function`, `validate-against-binary`, `review-reversal`) for each step.
