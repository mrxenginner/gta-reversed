# Scripts/ — SCM virtual machine and script command handlers

Guides: [../../../docs/subsystems/Scripts.md](../../../docs/subsystems/Scripts.md) (subsystem overview) and
[../../../docs/ScriptCommandReversing.md](../../../docs/ScriptCommandReversing.md) (ReadArg/StoreArg type tables).
To implement an opcode use the `/reverse-script-command <COMMAND | opcode>` skill
(`.claude/skills/reverse-script-command/`, helper `scripts/script_cmd.py locate|list|stats`).

## Layout
- `RunningScript.*` — `CRunningScript` (one script thread). `ProcessOneCommand` (`0x469EB0`) calls our handler from
  `CustomCommandHandlerOf(cmd)` if one is registered, otherwise the original block dispatcher
  `s_OriginalCommandHandlerTable[cmd / 100]` (`0x8A6168`). `InjectHooks` calls every `commands::<x>::RegisterHandlers()`.
- `TheScripts.*` — `CTheScripts` (script space, used-object array, mission cleanup, script-owned arrays).
- `Commands/<Category>Commands.cpp` — handlers grouped by category (Character, Vehicle, Object, Camera, ...);
  `Commands/CLEO/` — CLEO extension opcodes (NOTSA). `Commands/Commands.hpp` declares the `RegisterHandlers`.
- `CommandParser/` — `ReadArg.hpp` (arg decoding), `StoreArg.hpp` (return values), `Parser.hpp` (registration macros).
- Opcode enum: `source/game_sa/Enums/eScriptCommands.h`. Results: `OpcodeResult.h` (`OR_CONTINUE`, `OR_WAIT`, `OR_INTERRUPT`).

## Writing a handler
- A plain function in the file's anonymous namespace, typed args: arithmetic/enum by value, `CPed&`/`CVehicle&` for
  handles that must be valid, `T*` only when the original accepts -1, `CRunningScript& S` first if needed,
  `script::Model` only where the original reads `UsedObjectArray`.
- Return `bool` = compare flag, a value = `StoreParameters`, `MultiRet<A, B>` for several outputs.
- Add the `@opcode/@command/@class/@method/@brief/@param/@returns` doc block like neighbouring handlers.
- Register in the file's `REGISTER_COMMAND_HANDLER_BEGIN("<Category>")` block with
  `REGISTER_COMMAND_HANDLER(COMMAND_X, Fn)`, replacing `REGISTER_COMMAND_UNIMPLEMENTED(COMMAND_X)`.
  `REGISTER_COMMAND_NOP` is for commands that do nothing in the original. No address/`RH_ScopedInstall` is needed;
  the macro registers the hook (category `"Scripts/Commands"`).
- Mission cleanup: reuse `HandleEntityMissionCleanup(S, entity)` (`CharacterCommands.cpp`) where the original calls
  `MissionCleanUp.AddEntityToList`.

## Pitfalls
- In the original, several opcodes share one `case` (VAR/LVAR variants); check every opcode that falls into it.
- `REGISTER_COMMAND_UNIMPLEMENTED` returns `OR_INTERRUPT` (and debug-breaks), but some originals are no-ops returning
  `OR_CONTINUE`; don't mark a vanilla no-op as unimplemented.
- Keep side-effect order and original bugs (gate fixes with `notsa::bugfixes`), as for any reversed function.
