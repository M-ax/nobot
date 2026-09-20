# Validation: 1.1.0

## Environment

- Windows x64: MSVC 19.51, Release build.
- Linux x64: GCC 13.3 on Ubuntu 24.04 (WSL), Release build.
- Isolated Windows CS2 dedicated server: 1.41.8.1 / patch 14181,
  server version 2000908, Metamod plugin API 17.
- CounterStrikeSharp 1.0.374, bundled .NET 10.0.3 runtime.
- User-supplied VStrikeIdentity 0.5.0 binary, unchanged (references CSS API 1.0.373).
- Bot-Hider reference: commit `2fe6742c96e64e599baf7054c08075d7bb6741ea`.

## Root cause and approach

The 1.0.2 recordings correctly cleared controller `FL_FAKECLIENT` and pawn
`FL_BOT`, but their separate `userinfo` records still contained
`CMsgPlayerInfo.fakeplayer=true`, `xuid=0`, and native bot SteamIDs.
Testing entity flags alone did not validate the complete client identity.

1.1.0 implements Bot-Hider's engine-client flag/SteamID publication approach,
scoped to the engine's `UserInfoChanged` call. This original function writes
the player-info string table. The fake-player byte, connection flags, and
both unaligned SteamID fields are restored immediately afterward. Controller
and pawn overrides remain scoped to entity packing. Thus VStrike's `IsBot`
checks and `SetFakeClientConVar` calls see the native bot state.

An intermediate version shared GameFrame/connection hooks with CSS; the
combined unload/reload/map-change test exposed stalled VStrike NextFrame
callbacks. The final version uses Metamod level notifications and the existing
snapshot hook for lifecycle work. It registers no GameFrame or connection
hooks. The same rename sequence passed after that change.

## Verified

- Windows and Linux builds compile; both CTest tests pass on each platform.
  Portable tests cover unaligned engine SteamID fields, both client markers,
  exact restoration, nested overrides, exceptions, abandoned clients/pawns,
  invalid layouts, entity identities, names, and signatures. The detour test
  installs a real hook and verifies forwarding and repeated removal.
- The Windows server loads Botmod 1.1.0 with `API 017`. Engine client offsets
  resolve and the UserInfoChanged hook publishes display identities.
- SourceTV demo player-info records contain `fakeplayer=false` with matching,
  distinct `xuid` and `steamid` for eight bots. SourceTV retains its original
  `fakeplayer=true`, `ishltv=true` record.
- Both `vstrike_setname` and `vstrike_setteamname` succeed using the unmodified
  VStrikeIdentity binary. `vstrike_identity_status` continues to report
  `bot=True`. Recordings contain `VStrikeAlpha` and `VStrikeBravo` with
  `fakeplayer=false` and the assigned display IDs.
- Unload republishes native player info: recordings return to `fakeplayer=true`,
  `xuid=0`, and native bot SteamIDs while retaining VStrike's assigned names.
- In the final build, VStrike renaming works before unload (`FinalBeforeUnload`),
  while unloaded (`FinalAfterUnload`), after reload (`FinalAfterReload`), and
  after changing from Dust II to Inferno (`FinalMapRoster`). Bots still report
  `bot=True`. Round restart and player-info publication survive map changes.

## Remaining acceptance checks

The scoreboard and spectator HUD have not been visually checked with a human
client connected to 1.1.0. Player-info and entity recordings establish network
state, not the final rendered UI. Active bot combat/movement also needs a
connected-client check. Linux is compiled and unit tested; no Linux CS2 runtime
test was performed here.

Two behaviors were reproduced in a fresh CSS/VStrike-only process with Botmod
disabled for its entire lifetime: `bot_kick` did not recognize VStrike's new
display name but did recognize the original bot-profile name, and `quit`
produced a shutdown access-violation dump. These are baseline limitations of
this test stack, not evidence of a clean shutdown or renamed-name kick support
in the combined setup.

The Linux build requires glibc 2.38+ (Ubuntu 24.04 qualifies). Build on the
target distribution for an older runtime. Engine updates can require new
SDKs and gamedata; install the 1.1.0 gamedata with the new binary.

## Reproduce

```sh
cmake -S . -B build-tests -DBOTMOD_BUILD_PLUGIN=OFF
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

On an API 17 test server, install Botmod, CSS, and VStrikeIdentity; remove
Bot-Hider/BotHiderImpl if present. Add bots and run the following commands
separately, allowing each NextFrame rename to finish before checking status:

```text
meta list
vstrike_identity_status
vstrike_setname <bot-slot> VStrikeAlpha
vstrike_setteamname ct 1 VStrikeBravo
vstrike_identity_status
tv_record botmod_check
mp_restartgame 1
tv_stoprecord
meta unload botmod
vstrike_setname <bot-slot> WhileUnloaded
meta refresh
vstrike_setname <bot-slot> AfterReload
changelevel de_inferno
vstrike_setteamname ct 1 AfterMapChange
vstrike_identity_status
```

Inspect the `userinfo` table's CMsgPlayerInfo payloads in the demo, not only
the controller fields or a parser's cached player-name column. SourceTV can
include delayed frames; inspect the newest table records.

With a human connected, confirm separate prefix-free scoreboard rows, overhead
names and spectator HUD, unchanged human names, AI movement/combat, bot quota,
and targeted `bot_kick`. Repeat after respawn, map change, unload, and reload.
