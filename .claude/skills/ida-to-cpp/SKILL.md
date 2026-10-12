---
name: ida-to-cpp
description: >
  Translation reference from IDA/Hex-Rays pseudocode and x86/x87 disassembly of GTA:SA to idiomatic gta-reversed
  C++. Covers calling conventions, FPU compares, LOBYTE/bitfields, inlined engine helpers (GetPosition, IsStatic,
  CanSeeOutSideFromCurrArea...), RenderWare state macros, pools, strings, math idioms, magic constants and
  common decompiler traps. Use whenever converting decompiled code into C++ or reviewing such a conversion.
---

# IDA pseudocode → gta-reversed C++

Also read `docs/ai/TipsAndTricks.md` (engine quirks, wrong-address list, recovered enums/structs) and
`docs/CodingGuidelines.md`. This file focuses on *mechanical translation patterns*.

## 1. Calling conventions & signatures

| IDA shows | C++ in repo | Notes |
|---|---|---|
| `int __thiscall CFoo::Bar(CFoo *this, int a)` | `int32 CFoo::Bar(int32 a)` | `this` in `ecx` |
| `int __cdecl CFoo::Baz(int a)` | `static int32 CFoo::Baz(int32 a)` | static member or free function |
| `__stdcall` | RW plugin callbacks, Win32 API, some `CB` functions | keep `__stdcall` if it is used as a callback pointer |
| `__fastcall` with unused `edx` | usually a `__thiscall` mis-detected by IDA | treat as member |
| `__usercall X@<eax>(a@<esi>, ...)` | needs `{ .StackArgumentsToPreserve = N, .PreserveRegisters = true }` or a naked wrapper | ask before inventing one |
| returns struct by hidden pointer (`CVector *__thiscall f(CFoo *this, CVector *result, ...)`) | `CVector CFoo::f(...)` | `plugin::CallMethodAndReturn<CVector, ...>` handles it |
| `char` return used as boolean | `bool` | |
| `_DWORD`, `int` | `uint32`/`int32` (from usage) | never `DWORD` (CodingGuidelines) |
| `__int16`, `_WORD` | `int16`/`uint16` | |
| `_BYTE`, `char` used as number | `uint8`/`int8` | |

Overload in hooks → `RH_ScopedOverloadedInstall(Fn, "Suffix", 0xADDR, Ret(CFoo::*)(Args) const)`.

## 2. Inlined engine helpers — collapse them

| Decompiled pattern | Replace with |
|---|---|
| `m_matrix ? &m_matrix->pos : &m_placement.m_vPosn` (or `(this->m_pCoords ? ... )`) | `GetPosition()` |
| same for right/forward/up | `GetRight()`, `GetForward()`, `GetUp()` |
| `m_bIsStatic \|\| m_bIsStaticWaitingForCollision` | `IsStatic()` |
| `!CGame::currArea` | `CGame::CanSeeOutSideFromCurrArea()` |
| `clump->parent == (RwFrame*)-16` / clump/frame null checks on peds | `ped->GetModellingMatrix()` |
| `CWorld::Players[CWorld::PlayerInFocus].m_pPed` | `FindPlayerPed()` (also `FindPlayerVehicle()`, `FindPlayerWanted()`, `FindPlayerCoors()`) |
| `(handle >> 8)` / pool `m_byteMap[i] & 0x80` loops | pool helpers: `GetPedPool()->GetAt(handle)`, `GetAtRef`, `GetAllValid()`-style range helpers (grep `Pool.h`) |
| `RwFrameDestroy(atomic->object.object.parent)` after destroy | `RpAtomicGetFrame` + `RpAtomicDestroy` + `RwFrameDestroy` |
| `RwRenderStateSet(x, (void*)1)` | `RwRenderStateSet(x, RWRSTATE(TRUE))` (use `contrib/clean-srs-calls.py` for bulk) |
| `RwTextureSetAddressing` + `RwTextureSetFilterMode` vs `LOWORD(tex->filterAddressing) = 0x1102` | they are equivalent; prefer the readable call |
| `CTimer::ms_fTimeStep` & co. | use the `CTimer` accessor functions (`CTimer::GetTimeStep()`, `GetTimeInMS()`...) — the variables are private now |
| `CMaths`/`sinf`/`cosf` tables | existing `CMaths` / `std` functions |
| Hand-rolled `x*x + y*y + z*z` | `MagnitudeSquared()` (and `Magnitude()` for `sqrt`) |
| Hand-rolled dot/cross | `.Dot()`, `.Cross()` |
| `fabs(a - b) < eps` | `approxEqual(a, b, eps)` |
| `__FYL2X__(x, 0.30102999566398119802)` | `std::log10(x)` |

Search before writing: if you translate a block that looks generic, `Grep` for an existing helper (`extensions/`,
`General.h`, `Vector.h`, `Matrix.h`, `Entity.h`) — CodingGuidelines asks to prefer helpers over raw member access.

## 3. x87 floating point

Tip: `python .claude/skills/validate-against-binary/scripts/asm_validate.py 0xADDR --dump --no-check` decodes these
sequences for you (`jne 0x53cb24 ; jump if st(0) < 180.0f`) and shows the exact constant values.

- `fld a; fcomp b; fnstsw ax; test ah, 5 / 41h / 1 / 44h; jp/jnz ...` encodes comparisons. Read the jump
  carefully, IDA's pseudocode often gets the direction wrong:
  - `test ah, 41h` + `jnz` → taken when `a <= b` (C0 or C3 set) ; `jz` → `a > b`
  - `test ah, 1` + `jnz` → `a < b` ; `jz` → `a >= b`
  - `test ah, 40h` + `jnz` → `a == b`
  - `test ah, 5` + `jp` → `a >= b` ; `jnp` → `a < b`
  - `if (!(c0 | c3))` in pseudocode means `st(0) > source`, i.e. NOT (`a < b` or `a == b`).
- `fcomp` compares `st(0)` (the value loaded with `fld`) against the operand; `fcompp`/`fucomip` variants swap
  roles — verify with `disasm`.
- **Precision:** MSVC 2003 kept intermediates in 80-bit registers; constants from `.rdata` may be `double`
  (`flt_`/`dbl_` prefix in IDA). If the binary loads a `double` constant (`fld qword ptr`), the original math is in
  double; prefer matching it when results could differ (e.g. `static_cast<float>(x * 0.5)`), otherwise use `f`
  literals as the guidelines require.
- **Don't use `PI`/`TWO_PI`/`HALF_PI` from `common.h` blindly**: they are `3.14159f`/`6.28318f` while the game's
  constants are `3.1415927f`/`6.2831855f` (11 ULP apart → different results). Use the value the binary loads.
- Keep the exact constant value (`0.017453292f`, `57.295776f`, `1.0f / 50.0f` vs `0.02f`) — use
  `DegreesToRadians`/`RadiansToDegrees` helpers only when the constant matches.
- `_ftol`/`_ftol2` → truncating `static_cast<int32>(f)`.
- `CTimer` time-step dependent code: keep as is; framerate fixes go behind `notsa::bugfixes::GenericFrameRate`.

## 4. Integer / bit idioms

| Pattern | Meaning |
|---|---|
| `LOBYTE(x) = ...`, `BYTE1(x)`, `HIBYTE` on a struct field | separate `uint8` members or a bitfield — fix the struct, don't keep `LOBYTE` |
| `x & 0xFF` shown as `LOBYTE(x)` | may originally be `% 256` — check intent (TipsAndTricks §3) |
| `flags & 0x40` on entity/ped/physical flags | named bitfield members (`m_bIsVisible`, `bInVehicle`, ...) — see TipsAndTricks §5 flag tables |
| `(x >> 3) & 0x1F` on `m_nStatus`-like byte | packed bitfields: `m_nType : 3`, `m_nStatus : 5` |
| `(unsigned int)(x - a) <= b - a` | range check `a <= x && x <= b` |
| `x - 10 * (x / 10)` / `__ROL4__` | `x % 10` / rotate — use `%` / `std::rotl` |
| `sar/shr` after `imul` with magic constant | division by constant (compiler idiom) |
| `v = (cond) ? -1 : 0; v &= ...` (`sbb`) | branchless ternary → write the ternary |
| `rand() * 4.656613e-10f` / `CGeneral::GetRandomNumber() ...` | `CGeneral::GetRandomNumberInRange(...)` — **only if it is exactly the same formula**; keep call count |

## 5. Memory, strings, containers

| Pattern | C++ |
|---|---|
| `qmemcpy(&a, &b, sizeof)` of a struct | `a = b;` |
| `memset(this, 0, N)` in ctor | member initialisers / `= {}` |
| `strcmp(a, b) == 0` | `std::string_view{a} == b` |
| `strncmp(a, b, len) == 0` with `len == strlen(b)` | `std::string_view{a}.starts_with(b)` |
| `_strcmpi`/`_stricmp` | existing case-insensitive helper (grep `ci_string_view`), else keep |
| `sprintf(buf, ...)` | keep semantics; `std::format_to`/`notsa::format` only if output identical |
| fixed array + `m_nCount` loops | `std::span{ m_Arr, m_nCount }` or `m_Arr \| rngv::take(m_nCount)` |
| `for (i = 0; i < N; ++i) arr[i]` | range-for / `rngv::enumerate` |
| linked list `for (n = list.first; n; n = n->next)` | existing iterator helpers (`CPtrListSingleLink`/`CLink` ranges) if present |
| GXT strings (`GxtChar*`) | see CodingGuidelines "Handling translated (GXT) text" (`AsciiToGxtChar`, `""_gxt`) |

## 6. Globals and statics

- IDA `dword_B7CB64` → find existing `StaticRef` in the repo by address (`Grep -i "0xB7CB64"`).
- New global: `static inline auto& ms_Name = StaticRef<T>(0xADDR);` in the owning class; file-local:
  `static auto& s_Name = StaticRef<T>(0xADDR);`; arrays: `StaticRef<std::array<T, N>>(0xADDR)`.
- Constant tables in `.rdata` that are never written: copy them into `constexpr std::array` (CodingGuidelines allows it).
- Function-local `static` with a guard byte in IDA (`if (!(byte_C1234 & 1)) { byte_C1234 |= 1; ... }`) →
  `ScopedStaticRef` / a `static` local referencing the original (grep `ScopedStaticRef` for examples).

## 7. Virtual calls & RTTI-less type checks

- `(*(void (__thiscall **)(CEntity *))(*(_DWORD *)ent + 0x2C))(ent)` → find the vtable slot: slot = 0x2C/4 = 11;
  look up the base class virtual order in its header (declaration order == vtable order) → call it by name.
- `ent->m_nType == 3` → `ent->GetIsTypePed()` (`GetIsTypeVehicle()`, `GetIsTypeObject()`, `GetIsTypeBuilding()`,
  see `Entity.h`); `veh->m_nVehicleSubType` → `IsSubAutomobile()`, `IsSubBike()`, etc.; then `AsPed()`/`AsVehicle()` casts.
- Task type checks `task->GetTaskType() == 0x2BC` → `TASK_...` enum from `eTaskType`.

## 8. Things the decompiler hides or invents

- **Dead stores / duplicated loads** — drop them.
- **Variables reused for unrelated values** (one stack slot, two meanings) — split into two named variables.
- **Inlined small functions** (often duplicated across the binary): if an inlined block matches a reversed
  function, call the function and add `// Inlined` or `// NOTE: inlined in original`.
- **Wrong argument count** for callees with unknown prototypes — check the `add esp, N` after the call / `retn N`.
- **`this` adjusted for multiple inheritance** (`this - 4`, `this + 0x34`) → cast to the right base class.
- **Tail calls** shown as `return f(...)` with mismatched return type → plain call + return.
- **SEH / `_EH_prolog`** in ctors → irrelevant; translate the member initialisation only.
- **Switch with `default` dropping through to the next case** — check the jump table, write explicit `case`s and
  `NOTSA_UNREACHABLE_CASE(v)` for impossible defaults only if the original also can't reach them.

## 9. Fixes and annotations

- Original bug → keep it; comment `// BUG: <explanation>`. Fix only behind `notsa::bugfixes::<Name>` (add the entry to
  `source/reversiblebugfixes/Bugs.hpp`) or a generic one (`GenericCrashing`, `GenericOOB`, `GenericUB`, `GenericFrameRate`).
- Code that does not exist in the original → mark `// NOTSA` (helpers, asserts are fine without it).
- Using `MagnitudeSquared` where the original used `Magnitude` → `// NOTE: Using MagSq here`.
- Long functions → sprinkle `// 0xADDR` comments taken from the decompiler `/*0x...*/` markers.
