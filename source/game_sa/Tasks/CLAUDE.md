# Tasks/ — ped AI task classes

Full guide: [../../../docs/subsystems/Tasks.md](../../../docs/subsystems/Tasks.md) (vtable slot tables, templates,
gotchas). Related: [../../../docs/subsystems/Events.md](../../../docs/subsystems/Events.md) (events create most tasks).

## Files to look at first
- `Task.h` (`CTask`, `eAbortPriority`, `classof`/`CloneIfIs`), `TaskSimple.h`, `TaskComplex.h`, `TaskManager.h/.cpp`.
- Reference implementations: `TaskTypes/TaskComplexLeaveBoat.*` (complex), `TaskTypes/TaskSimpleStandStill.*`
  (simple), `TaskTypes/TaskSimpleHitHead.*` (anim finish callback).
- `TaskTypes/` holds one class per file pair; `Interior/`, `SeekEntity/` (templated on a `PosCalculators/` type),
  `Allocators/` (group task allocators, NOT tasks: own 6-entry vtable).

## Class shape
- `class NOTSA_EXPORT_VTABLE CTaskXxx : public CTaskSimple | CTaskComplex` — the export is required, VMT hooks find our
  vtable through the exported `??_7CTaskXxx@@6B@` symbol. A few headers still lack it; add it before hooking.
- `static constexpr auto Type = TASK_...;` + `GetTaskType() const override { return Type; }` (used by `notsa::isa/cast`,
  `CTaskManager::Find<T>()`).
- `Clone()` copies construction parameters only (plus `SafeRegisterRef` for entity pointers), like the original.
- `VALIDATE_SIZE` must be <= `0x80`: `new`/`delete` go through the task pool (`CTask::operator new`, 500 x 0x80 slots).
- Simple: `ProcessPed` returns `true` when finished. Complex: `CreateFirstSubTask` / `CreateNextSubTask` return
  `nullptr` = finished; `ControlSubTask` returns `m_pSubTask` to keep it. Complex tasks usually have a non-virtual
  `CreateSubTask(eTaskType, CPed*)` switch.

## Hooks
- `RH_ScopedVirtualClass(CTaskXxx, 0x<vtable>, N)`: N = 9 for simple, 11 for complex, more if the class appends
  virtuals. Category `"Tasks/TaskTypes"` (or `"Tasks/TaskTypes/Interior"`, `"Tasks/Allocators"`, ...).
- Virtuals use `RH_ScopedVMTInstall`; constructor/destructor use private `Constructor`/`Destructor` wrappers with
  `RH_ScopedInstall`. Add `{ .Overrides = false }` when GTA has its own copy of a virtual we only inherit.
- `CTaskComplex::SetSubTask` is `virtual ... final` on purpose (it is a GTA vtable slot); do not de-virtualise it.

## Pitfalls
- `ControlSubTask` returning a new task deletes the old one even if `MakeAbortable` refused; mirror the original's
  `if (m_pSubTask->MakeAbortable(ped))` check exactly.
- `CTaskComplex::MakeAbortable` does not null-check `m_pSubTask`.
- Tasks that call `SetFinishCallback(..., this)` must reset the callback in the destructor and in `MakeAbortable`.
- Destructors must release everything registered: entity refs, anim block refs, IK chains, callbacks.
- `FindActiveTaskByType` only searches the active primary tree and secondaries.
