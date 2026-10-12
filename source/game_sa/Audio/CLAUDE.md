# Audio/ — the AE audio engine

Full guide: [../../../docs/subsystems/Audio.md](../../../docs/subsystems/Audio.md) (class table, init/per-frame flow,
backends, gotchas).

## Layout
- `AudioEngine.*` — `CAudioEngine` facade, global `AudioEngine` (`0xB6BC90`). Gameplay code calls `AudioEngine.ReportXxx`
  / `AddAudioEvent`; it owns the frontend/script/collision/global-weapon/pedless-speech/door entities.
- `Entities/` — `CAEAudioEntity` subclasses (one per sound source: vehicle, ped, speech, weapon, collision, ...),
  embedded in game objects (`CPed::m_pedAudio`, `CVehicle::m_vehicleAudio`, ...).
- `Managers/` — `CAESoundManager` (300 virtual sounds -> physical channels), radio/cutscene/ambience/user track managers.
- `Hardware/` — `CAEAudioHardware` + channels (DirectSound only; channel 0 = streaming). `Loaders/` — bank/track
  loaders, decoders, data streams. `OpenAL/` is unused NOTSA scaffolding.
- Enums: `Enums/eSoundBank.h`, `eSoundBankSlot.h`, `SoundIDs.h`; `eAudioEvents` is in `source/game_sa/Enums/`.

## Conventions
- New code plays sounds with `AESoundManager.PlaySound({ .BankSlotID = ..., .SoundID = ..., .AudioEntity = this, ... })`
  (`tSoundPlayParams`); the original `m_tempSound.Initialise(...)` + `RequestNewSound` form is also accepted.
  Example: `Entities/AEExplosionAudioEntity.cpp`.
- Volumes are dB (`GetDefaultVolume(event) + delta`, -100 = silent).
- Call `AEAudioHardware.EnsureSoundBankIsLoaded(bank, slot)` before playing; `false` means "queued, skip this time".
- Randomness: `CAEAudioUtility::GetRandomNumberInRange` / `ResolveProbability`, not `CGeneral`, to match SA.
- Hooks: entities are `NOTSA_EXPORT_VTABLE` + `RH_ScopedVirtualClass`, `RH_ScopedVMTInstall(UpdateParameters, ...)`;
  categories `"Audio/Entities"`, `"Audio/Managers"`, `"Audio/Hardware"`, ...
- F7 debug modules: `source/toolsmenu/DebugModules/Audio/`.

## Pitfalls
- The stream thread and smooth-fade thread run concurrently; respect `CAEStreamThread::m_criticalSection`.
  Toggling hooks of thread-called functions in F7 can race.
- Some reversed functions are registered `{ .Reversed = false }` on purpose because our version is wrong
  (e.g. `CAEStreamingChannel` constructor, `CAEAmbienceTrackManager::UpdateAmbienceTrackAndVolume`). Read the comment.
- Negative track play times are states (`StreamingChannelState`), not durations.
- `SOUND_IS_CANCELLABLE` (bit 0x2) is used as "uncancellable" (`m_IsUnancellable`); trust usage, not the name.
- Keep the SEH `__try/__except` in `CAESound::UpdateParameters`; `m_AudioEntity` may be null.
- A game freeze during loading usually means a broken `CAEMP3BankLoader` hook (blocking load loops).
- `windows.h` defines `PlaySound`: `AESoundManager.h` does `#undef PlaySound`; include order matters.
- Unity build: keep file-local helpers in `namespace {}` with unique names.
