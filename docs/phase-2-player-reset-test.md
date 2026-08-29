# Phase-two player-board reset test

The direct `GI_Quantum_C:getActiveDecklist()` path is disabled because UE4SS 3.0.1
crashes while marshalling its large returned structure.

The next bounded experiment calls only the game's existing no-argument
`CardEngine:resetPlayerBoard()` function. `Ctrl+F3` is guarded so that it works only
after `Ctrl+F5` has captured a checkpoint in the same live battle instance. After the
call returns, the probe waits 1.5 seconds and records the resulting deck, hand,
storage, live cards, and health.

This experiment intentionally does not call `loadDeck`, `getActiveDecklist`,
`spawnWaveIndex`, or any function that transfers a `Decklist` structure across the
Lua boundary. It mutates the current battle and must be tested only in a disposable
run.

## Observed reset result

Three live tests returned without a crash. Deck, hand, storage, trash, and player
field were emptied; enemy cards and player health remained intact. Existing player
card actors were reset to `tag=None`, zero stats, rather than immediately destroyed.

## Internal reload follow-up

`Ctrl+F2` is now available only after the guarded reset test completes. It calls the
Blueprint function `BP_CardEngine:LoadPlayerCardsStart()` with no arguments, keeping
the `Decklist` getter and structure entirely inside the game's own Blueprint/native
execution. The probe then records the resulting zones after 2.5 seconds.

The live result was a complete initial deck reload followed by a new shuffle and five
new draws. It did not reproduce the saved deck/hand order, player field, or trash.
This matches the game's normal Reprogram restart behavior and is usable when the
checkpoint deliberately adopts that behavior instead of exact player-zone recovery.
