---
name: reverse-class
description: >
  Create or complete a whole reversed GTA:SA class/struct in gta-reversed - memory layout with VALIDATE_SIZE /
  VALIDATE_OFFSET, vtable, static members via StaticRef, ctor/dtor wrappers, InjectHooks with every function
  registered, and registration in source/InjectHooksMain.cpp. Use when a class does not exist yet, when a class
  has many `.Reversed = false` hooks to finish, or when a struct layout must be recovered.
argument-hint: "<ClassName> [vtable address] [size]"
---

# Reverse / complete a class

Target: `$ARGUMENTS`

## 1. Inventory

1. Does it exist? `Glob source/game_sa/**/<Name>.h` and `Grep "class <Name>\b|struct <Name>\b"`.
2. If it exists: `python .claude/skills/find-reversing-targets/scripts/list_unreversed.py --filter <File> --details`
   to list what is missing, then reverse functions one by one with the **reverse-function** skill
   (smallest / leaf functions first — they unblock callers).
3. If it is new: decide the directory by subsystem — it determines the hook category
   (`source/game_sa/Entity/Ped/...` → `RH_ScopedCategory("Entity/Ped")`; files directly in `source/game_sa`
   → `RH_ScopedCategoryGlobal()`).

The vtable address, slot count and each slot's status for already-registered classes are in `docs/ai/Vtables.md`
(grep `### <Class>`); use it to fill `RH_ScopedVirtualClass(cls, vtable, slots)` and to check `RH_ScopedVMTInstall` addresses.

Shortcut: `python .claude/skills/find-reversing-targets/scripts/class_dossier.py <Class> --ida` prints most of this
inventory in one go. For many functions at once, the named workflow `.claude/workflows/reverse-class.js`
(args `{cls, max, maxBytes}`) runs one `gta-reverser` per function in a worktree and reviews each; start it only
when the user asks.

## 2. Recover the layout (IDA)

- Find the size: ctor/`operator new` call sites (`push 0xNN; call operator new`), pool sizes, `memset` in ctor,
  array strides in loops (`imul reg, 0xNN`).
- Find members: every `[this+0xNN]` access in ctor, dtor and methods. Note width (byte/word/dword/float) and use.
- Vtable: the ctor writes it (`mov dword ptr [esi], offset off_86C5B4`). Count its entries for `nVirtFns`
  (include inherited ones; compare with the base class's `RH_ScopedVirtualClass` count).
- Tools: `decompile`, `disasm`, `xrefs_to_field`, `search_structs`, `type_inspect`, `read_struct`.
- Cross-check with `docs/ai/TipsAndTricks.md` (§5/§6 contain recovered enums/structs).

## 3. Header template

```cpp
#pragma once

#include "Base.h"          // or the base class header

class CEntity;             // forward declare instead of including when possible

class NOTSA_EXPORT_VTABLE CFoo : public CFooBase { // NOTSA_EXPORT_VTABLE only for polymorphic classes
public:
    static inline auto& ms_Count = StaticRef<uint32>(0xC0FFEE); // statics reference the original address

    int16    m_Id;         // 0x4
    uint8    m_Flags;      // 0x6
    // ... (keep padding explicit if needed: uint8 _pad7;)
    CVector  m_Pos;        // 0x8
    CEntity* m_Target;     // 0x14

public:
    static void InjectHooks();

    CFoo(int16 id);                      // 0xADDR
    ~CFoo() override;                    // 0xADDR (or plain ~CFoo() if not virtual)

    void Update();                       // 0xADDR
    bool IsActive() const;               // 0xADDR  (const-correct!)

    // NOTSA helpers go in a clearly marked section
    auto GetPosition() const { return m_Pos; }

private: // Wrappers for hooks
    // 0xADDR
    CFoo* Constructor(int16 id) {
        this->CFoo::CFoo(id);
        return this;
    }

    // 0xADDR
    CFoo* Destructor() {
        this->CFoo::~CFoo();
        return this;
    }

    friend void InjectHooksMain(); // only needed because InjectHooks/wrappers are private
};
VALIDATE_SIZE(CFoo, 0x18);
VALIDATE_OFFSET(CFoo, m_Pos, 0x8);   // add for every member you are not 100% sure about
```

Rules (docs/CodingGuidelines.md): classes use `m_`/`ms_`; plain structs use bare `PascalCase` members;
fixed-width ints; `std::array` for arrays (`std::array<CVector, 4>` keeps layout); SA types over RW types
(except `RwMatrix`); bitfields for flag bytes (`uint8 bFoo : 1;`); enums with explicit underlying type matching
the member width (`enum eFooState : uint8`).

## 4. Source template

```cpp
#include "StdInc.h"

#include "Foo.h"

void CFoo::InjectHooks() {
    RH_ScopedVirtualClass(CFoo, 0x86C5B4, 6); // RH_ScopedClass(CFoo) if not polymorphic
    RH_ScopedCategory("Entity/Object");       // follows the directory

    RH_ScopedInstall(Constructor, 0x5EE7C0);
    RH_ScopedVMTDestructorInstall(0x5EE830);  // or RH_ScopedInstall(Destructor, 0x...) for non-virtual

    RH_ScopedInstall(Update, 0x5EF000);
    RH_ScopedInstall(IsActive, 0x5EF100, { .Reversed = false }); // still a stub
    RH_ScopedVMTInstall(Process, 0x5EF200);
}

// 0x5EE7C0
CFoo::CFoo(int16 id) :
    CFooBase{},
    m_Id{ id }
{
}

// 0x5EF100
bool CFoo::IsActive() const {
    return plugin::CallMethodAndReturn<bool, 0x5EF100, const CFoo*>(this);
}
```

Stub forms for functions you are not reversing now (always with `{ .Reversed = false }`):
- member: `plugin::CallMethod<0xADDR, CFoo*, Args...>(this, args...);`
- member with return: `plugin::CallMethodAndReturn<Ret, 0xADDR, CFoo*, Args...>(this, args...);`
- static/free: `plugin::Call<0xADDR, Args...>(args...);` / `plugin::CallAndReturn<Ret, 0xADDR, Args...>(args...);`

## 5. Register the class

1. Add `#include "Foo.h"` in `source/InjectHooksMain.cpp` near related includes.
2. Call `CFoo::InjectHooks();` inside `InjectHooksMain()` in the matching subsystem group (base classes before
   derived ones is a good habit).
3. `friend void InjectHooksMain();` only if `InjectHooks` (or the wrappers it uses) is private.
4. Add the `.cpp` to the build if needed — check whether `source/CMakeLists.txt` globs sources
   (it usually does; re-run CMake configure for new files).

## 6. Verify

- `python .claude/skills/review-reversal/scripts/check_hooks.py source/game_sa/.../Foo.cpp source/game_sa/.../Foo.h`
- Build (`cmake --build build --config Debug --target gta_reversed`) — `VALIDATE_SIZE/OFFSET` failures show
  up here as `static_assert`s.
- Ask the **gta-reversal-reviewer** agent for a review.
