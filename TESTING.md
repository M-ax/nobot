# Validation

## Environment

- Windows x64: MSVC 19.51, Release build.
- Linux x64: GCC 13.3 on Ubuntu 24.04 (WSL), Release build.
- CS2 Windows dedicated server: 1.41.8.1 / patch 14181, Steam build 25218825.
- Botmod 1.0.1, compiled against unmodified Metamod plugin API 17 headers.
- Metamod:Source 2.0.0-dev+1411, reporting `Plugin interface version: 17:14`.
- Bundled SafetyHook 0.6.10 and Zydis 4.1.0; no runtime KHook requirement.

## Checks

- Both native binaries compile; both CTest tests pass on each system.
- The detour integration test installs a real hook, forwards each call to the
  original function exactly once, disables it, and repeats three times.
- A configure attempt using the old API 18 SDK is rejected before compilation.
- The plugin loads at server startup and resolves schema fields and the
  PackEntities hook on the installed CS2 build. `meta info` reports Botmod 1.0.1,
  `API 017`, and `Plugin ... is running` on the API 17 runtime.
- Two bot controllers receive display overrides; the SourceTV controller is excluded.
- Repeated unload / `meta refresh` cycles succeed within the same process.
- A transition from Dust II to Inferno preserves operation, and a round restart
  does not crash the server. Targeted bot kicking also succeeds.
- A SourceTV recording parsed with demoparser2 contains separate bot rows,
  unprefixed names, `CCSPlayerController.m_fFlags = 0`, and distinct display
  SteamIDs. The pawn retains its native `FL_BOT` flag.
- After unloading, recorded controller updates restore `m_fFlags = 256` and
  `m_steamID = 0`. SourceTV can include older buffered frames, so inspect the
  latest field values, not only the parser's cached player identity column.

The rendered scoreboard has not been visually checked with a connected human
client. Linux was compiled and unit tested, but was not loaded into a Linux
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
prefix. Then unload and reload the plugin and repeat after a map change.
