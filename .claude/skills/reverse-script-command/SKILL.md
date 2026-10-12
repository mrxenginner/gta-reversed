---
name: reverse-script-command
description: >
  Implement (reverse) a GTA:SA SCM script command/opcode in gta-reversed's command system
  (source/game_sa/Scripts/Commands/*.cpp) - find the original handler in CRunningScript::ProcessCommandsXXXToYYY,
  write a typed C++ handler using the ReadArg/StoreArg conventions, document it with the opcode doc-comment block
  and swap REGISTER_COMMAND_UNIMPLEMENTED for REGISTER_COMMAND_HANDLER. Use for any request mentioning an opcode
  (e.g. 0x02AB) or a COMMAND_* name.
argument-hint: "<COMMAND_NAME | opcode>"
---

# Reverse a script command

Target: `$ARGUMENTS`

Read `docs/ScriptCommandReversing.md` first (ReadArg / StoreArg tables).

## 0. Fast path: `script_cmd.py`

```bash
S=.claude/skills/reverse-script-command/scripts/script_cmd.py
python $S export                       # once, with IDA open: opcode -> case address map (.claude/cache/script_cases.json)
python $S locate SET_CHAR_PROOFS       # name/opcode, repo status (+ handler file:line), original case address + size
python $S locate 0x02AB --decompile    # Hex-Rays pseudocode of JUST that case (needs IDA running)
python $S locate 0x02AB --dump         # annotated disassembly of the case from the exe (no IDA)
python $S stats                        # repo status x what the original does
python $S list --status unregistered --original real --sort size --top 40   # real work, smallest first
```

`stats` crosses the repo status (HANDLER / NOP / UNIMPLEMENTED / UNREGISTERED) with what the **original** does:
`real` (unique case code), `real/shared` (2-7 opcodes share the code, eg. VAR/LVAR variants), `default?` (8+ share it),
`no-op` (returns `OR_CONTINUE` without reading params) and `not-impl` (returns `OR_INTERRUPT`, the default path).
- **Work left = UNREGISTERED × real** (about 925 commands). The original dispatcher still runs them.
- `NOP × real` (27) and `UNIMPLEMENTED × real` (3-5) may be mis-registered. Check them against `--decompile`
  before trusting the registration.
- `UNIMPLEMENTED` commands whose original is a `no-op` return `OR_CONTINUE` in vanilla, while
  `REGISTER_COMMAND_UNIMPLEMENTED` returns `OR_INTERRUPT` (and debug-breaks). That only matters if a script uses them.

## 1. Locate

1. `Grep "COMMAND_<NAME>\b" source/game_sa/Scripts` → find
   - the enum value (opcode) in the commands enum header,
   - the current registration: `REGISTER_COMMAND_UNIMPLEMENTED(COMMAND_X);` (or `REGISTER_COMMAND_NOP`) in the
     `Commands/<Category>Commands.cpp` file that groups similar commands (Character, Vehicle, Object, Camera, ...).
2. If the command is grouped elsewhere by convention (e.g. all `*_CHAR_*` in CharacterCommands.cpp), follow it.

## 2. Find the original implementation (IDA)

- In the original, commands are dispatched in blocks of 100 opcodes by giant `switch` functions (usually named
  `CRunningScript::ProcessCommands600To699` etc. in community IDBs; in the repo dispatch goes through
  `CRunningScript::ProcessOneCommand`). Example: opcode `0x02AB` = 683 → the `600To699` block.
  Find it with IDA `lookup_funcs`/`find` by name, decompile it and locate the `case 683:` (or `case 0x2AB:`).
- Typical original shape:
  ```
  CollectParameters(N);                          // N = number of input params
  ped = GetPedPool()->GetAtRef(ScriptParams[0].iParam);
  ... logic ...
  ScriptParams[0].fParam = result; StoreParameters(1);   // outputs
  UpdateCompareFlag(cond);                        // boolean result
  ```
- `CollectParameters(N)` → N function arguments; `StoreParameters(K)` → K return values (`MultiRet<...>` if > 1);
  `UpdateCompareFlag(x)` → return `bool`.
- `CTheScripts::UsedObjectArray[-x]` access → arg type `script::Model`; pool handles → `CPed&`/`CVehicle&`/`CObject&`
  (use `T*` only if the original accepts an invalid/-1 handle).
- Strings read with `ReadTextLabelFromScript` → `const char*` / `std::string_view`.
- Mission cleanup (`if (m_bUseMissionCleanup) MissionCleanUp.AddEntityToList(...)`) → reuse the helper used in the file
  (`HandleEntityMissionCleanup(S, entity)` in CharacterCommands.cpp).

## 3. Write the handler

Inside the anonymous namespace of the commands file, in the matching section:

```cpp
/*
 * @opcode 02AB
 * @command SET_CHAR_PROOFS
 * @class Char
 * @method SetProofs
 *
 * @brief Sets the character's immunities
 *
 * @param {Char} self
 * @param {bool} bulletProof
 * @param {bool} fireProof
 * ...
 * @returns {float} heading      (only if it returns something)
 */
void SetCharProofs(CPed& ped, bool bullet, bool fire, bool explosion, bool collision, bool melee) {
    ...
}
```

- Doc block fields (`@opcode`, `@command`, `@class`, `@method`, `@brief`, `@param`, `@returns`) follow the
  Sanny Builder library naming — keep them consistent with neighbouring commands.
- Parameter types per `docs/ScriptCommandReversing.md` (by value for arithmetic/enums, `T&` for non-null pooled
  types, `T*` when null/-1 is valid, `CRunningScript& S` first if you need the script itself).
- Return: `bool` = compare flag; arithmetic/`CVector`/pooled ptr = stored value; `MultiRet<A, B>` for several.
- Same fidelity rules as any function: same side effects, same order, original bugs kept (gate fixes with
  `notsa::bugfixes`).

## 4. Register

In the file's `REGISTER_COMMAND_HANDLER_BEGIN("<Category>")` block:

```cpp
REGISTER_COMMAND_HANDLER(COMMAND_SET_CHAR_PROOFS, SetCharProofs);
```

and delete the old `REGISTER_COMMAND_UNIMPLEMENTED(COMMAND_SET_CHAR_PROOFS);` line.
Commands that are no-ops in the original use `REGISTER_COMMAND_NOP(COMMAND_X)`.

## 5. Verify & test

- Build (`cmake --build build --config Debug --target gta_reversed`).
- Find a mission/script that uses the command (search a decompiled `main.scm` if the user has one, or ask) and
  describe how to trigger it; CLEO scripts can also exercise a single opcode.
- Report: opcode, name, handler signature, file, how it was tested.
