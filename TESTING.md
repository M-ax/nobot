# Validation

## Environment

- Windows x64: MSVC 19.51, Release build.
- Linux x64: GCC 13.3 on Ubuntu 24.04 (WSL), Release build.
- CS2 Windows dedicated server: 1.41.8.1 / patch 14181, Steam build 25218825.
- Botmod 1.0.2, compiled against unmodified Metamod plugin API 17 headers.
- Metamod:Source 2.0.0-dev+1411, reporting `Plugin interface version: 17:14`.
- Bundled SafetyHook 0.6.10 and Zydis 4.1.0; no runtime KHook requirement.

## Checks

- Both native binaries compile; both CTest tests pass on each system. The
  regression test requires controller `FL_FAKECLIENT` and pawn `FL_BOT` to be
  clear in the same snapshot, then checks exact restoration, nested overrides,
  exception cleanup, and abandoned/reused pawns.
- The detour integration test installs a real hook, forwards each call to the
  original function exactly once, disables it, and repeats three times.
- A configure attempt using the old API 18 SDK is rejected before compilation.
- The plugin loads at server startup and resolves schema fields and the
  PackEntities hook on the installed CS2 build. `meta info` reports Botmod 1.0.2,
  `API 017`, and `Plugin ... is running` on the API 17 runtime.
- Bot controllers and their `player` pawns receive display overrides; the
  SourceTV controller is excluded. Pawn handles resolve through runtime schema.
- Repeated unload / `meta refresh` cycles succeed within the same process.
- A transition from Dust II to Inferno preserves operation, and a round restart
  does not crash the server. Targeted bot kicking also succeeds.
- A SourceTV recording on Inferno parsed with demoparser2 contains eight separate bot rows,
  unprefixed names, `CCSPlayerController.m_fFlags = 0`, and distinct display
  SteamIDs. Pawn flags are `65665`, with `FL_BOT` (16) clear, including after a
  round restart. A controller-only recording had pawn flags `65681`, which
  exposed the missing override in 1.0.1.
- After unloading, recorded controller updates restore `m_fFlags = 256` and
  `m_steamID = 0`, pawn flags restore to `65681` (including `FL_BOT`), and literal
  `[BOT]` names return. The parser can collapse zero-ID bots into one row and
  SourceTV can include older buffered frames, so inspect the latest raw field
  values, not only the parser's cached player identity column.

The user's screenshot shows the native BOT labels remaining with the earlier
approach. The rendered scoreboard and spectator HUD with 1.0.2 have not been
visually checked with a connected human client. These snapshot checks do not
establish rendered UI behavior. VStrikeIdentity 0.5.0's naming and `IsBot` paths
were inspected from source; the two plugins were not run together here.
The empty-server recordings did not establish active bot movement/combat, so
that remains part of the connected-client acceptance check.

Linux was compiled and unit tested, but was not loaded into a Linux
CS2 server here. The supplied Linux binary requires glibc 2.38 or newer (Ubuntu
24.04 qualifies); build on your target distribution when using an older runtime.
The bundled SafetyHook build introduces the `__isoc23_sscanf` dependency; it is
independent of Metamod's plugin API version.

## Reproduce automated tests

```sh
cmake -S . -B build-tests -DBOTMOD_BUILD_PLUGIN=OFF
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

For the remaining visual acceptance check, connect a human player, add bots,
open the scoreboard, and confirm each bot has its own row without a BOT name
prefix. Check the overhead name and spectator HUD too. With VStrikeIdentity,
rename a bot using `vstrike_setname`, confirm `vstrike_identity_status` still
reports it as a bot, and confirm AI movement/combat and targeted `bot_kick` work.
Then unload and reload the plugin and repeat after a map change and respawn.
