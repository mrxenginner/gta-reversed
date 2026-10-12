# Animation/ — anim blend system (CAnimManager and friends)

No dedicated subsystem guide; `README.md` in this folder explains the object model, and
[../../../docs/subsystems/Peds.md](../../../docs/subsystems/Peds.md) §6 "Animation hookup" shows how peds use it.
The clump-level API is the RpAnimBlend plugin in `../Plugins/RpAnimBlendPlugin/RpAnimBlend.h`
(`RpAnimBlendClumpGetAssociation`, `RpAnimBlendClumpGetData`, ...).

## Object model (sizes from `VALIDATE_SIZE`)
- `CAnimBlendHierarchy` (0x18) — one animation, owns `CAnimBlendSequence`s (0xC, one per bone, key frames from
  `AnimSequenceFrames.h`, compressed or not). Stored in `CAnimManager::ms_aAnimations` (2500), freed only when the IFP
  block is streamed out.
- `CAnimBlock` (0x20) — one IFP block (`ms_aAnimBlocks`), ref-counted (`AddAnimBlockRef` / `RemoveAnimBlockRef`).
- `CAnimBlendAssocGroup` (0x14) — maps `AnimationId` / names to `CAnimBlendStaticAssociation`s (0x14, vtable `0x85C6CC`).
- `CAnimBlendAssociation` (0x3C, has a virtual destructor) — a running anim on a clump, with one `CAnimBlendNode`
  (0x18) per bone. `CAnimBlendClumpData` (0x14) is the per-clump list; `AnimBlendFrameData` (0x18) per frame.
- Enums: `AnimationId`, `AssocGroupId` in `../Enums/AnimationEnums.h`; group definitions in `AnimAssocDefinitions.cpp`
  (`ms_aAnimAssocDefinitions`, `0x8AA5A8`).

## Conventions
- Hooks: `RH_ScopedClass` + category `"Animation"`; overloaded constructors/`Init` use
  `RH_ScopedOverloadedInstall(Constructor0/1/2/3 | Init, "suffix", addr, signature)` (see `AnimBlendAssociation.cpp`).
- Start anims via `CAnimManager::AddAnimation`, `BlendAnimation`, `AddAnimationAndSync`; look them up with
  `RpAnimBlendClumpGetAssociation(clump, animId)`.
- Use the flag helpers (`IsPlaying()`, `IsLooped()`, `IsPartial()`, ...) and `eAnimationFlags` names instead of raw
  masks. Several flag names are known-uncertain (comments in `AnimBlendAssociation.h`); don't rename in a reversal PR.

## Pitfalls
- Callbacks: `SetFinishCallback` / `SetDeleteCallback(fn, data)` store a raw `void*` (usually a task). Whoever sets one
  must reset it to `CDefaultAnimCallback::DefaultAnimCB` (`SetDefaultFinishCallback()`) before the owner dies.
- `ANIMATION_IS_BLEND_AUTO_REMOVE` deletes the association once faded out: don't keep pointers to it across frames
  without a delete callback.
- `ANIMATION_FACIAL` associations are never destroyed.
- Anims in a block are only valid while the block is loaded; streaming code checks `GetNumRefsToAnimBlock`.
- Bone indices: use `eBoneTag` / `ePedNode`, not `AnimationId` (some older ped code mixes them).
