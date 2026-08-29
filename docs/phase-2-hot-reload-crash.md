# Phase-two hot-reload crash

Date: 2026-08-29 14:49:09 (Asia/Shanghai)

## Observed failure

- The game crashed immediately after `Ctrl+R` reinstalled the Lua mod.
- Unreal reported `EXCEPTION_ACCESS_VIOLATION` while reading
  `0x00000196aac45598`.
- The crash reporter stack contained repeated UE4SS frames.
- `UE4SS.log` reached `All mods re-installed`, so the updated Lua file parsed and
  started successfully before the native crash.
- Immediately before reinstall, UE4SS was unregistering the probe's native hooks.

Crash evidence remains at:

`C:\Users\zaofenMachine\AppData\Local\Quantum\Saved\Crashes\UE4CC-Windows-2754BDDD4AF30F7003AD43BDA55D66FC_0000`

## Working diagnosis

The failure matches an UE4SS native hook-unregistration problem, not a Lua syntax
error. The probe had accumulated several rounds of native and Blueprint observation
hooks through repeated hot reloads. UE4SS upstream has reports of access violations
in `UnregisterHook`, including on Unreal Engine 4.27.

## Mitigation applied

- Removed all `RegisterHook` use from the manual phase-two prototype.
- Removed automatic scans and full object dumps on mod load and GameState init.
- Kept only manual key binds for checkpoint capture, health restoration, card-zone
  comparison, manual inventory, and manual object dump.
- Replaced the incorrect `getPlacementLocation({})` experiment with the no-argument
  `CardPlacementComponent:getPlacedFieldSlot()` read path.
- Development builds must now be loaded by a complete game restart, not `Ctrl+R`.

## Game-instance structure-return crash

Date: 2026-08-29 15:31:39 (Asia/Shanghai)

- Pressing the former `Ctrl+F4` diagnostic caused
  `EXCEPTION_ACCESS_VIOLATION` while reading `0x0000000000000022`.
- `UE4SS.log` successfully recorded the live `GI_Quantum_C` instance and stopped
  before the next planned log message.
- The immediately following operation was
  `GI_Quantum_C:getActiveDecklist()`, whose return type is the large `Decklist`
  structure.
- This identifies UE4SS reflection marshalling for that structure-return call as
  the failing boundary. Lua `pcall` cannot catch a native access violation.
- Removed the entire `Ctrl+F4` diagnostic and marked `getActiveDecklist()` unsafe.
  Player-zone reconstruction must avoid the game-instance `Decklist` path and use
  already validated controller/card objects or another boundary.

Crash evidence remains at:

`C:\Users\zaofenMachine\AppData\Local\Quantum\Saved\Crashes\UE4CC-Windows-C0A2E2A34D027971702EAFA197702C08_0000`
