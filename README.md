# Botmod

A standalone C++ Metamod:Source **plugin API 17** plugin for CS2 that removes the native
BOT name label from the in-game player list. It also strips literal `[BOT] `
and `BOT ` prefixes from bot names sent in entity snapshots.

This targets the scoreboard on a server you control. It does not modify the
Steam/main-menu party lobby. CounterStrikeSharp is not required.

## How it works

CS2's native bot label is separate from the name. Botmod temporarily clears
the controller's `FL_FAKECLIENT` bit while `PackEntities` serializes the
network snapshot. It restores the original flags, SteamID, and name as soon
as packing finishes, so the server's bot simulation retains its native state.

Bots without a SteamID receive distinct **display-only synthetic IDs** during
packing, so their scoreboard rows do not collapse onto the same zero ID.
These are not authenticated Steam accounts. The plugin does not change engine
client identities, server-browser bot counts, bot quota, or ping values.
Client UI that uses the same controller flag may also stop displaying its bot
icon; hiding the native name label is not a text-only change.

## Build

Requirements: Git, Python 3, CMake 3.28+, and a 64-bit C++23 compiler. Windows
requires recent Visual Studio 2022 or 2026 with the C++ desktop workload.
Linux requires GCC 13+ or an equivalent compiler and standard library.
The packaged Linux binary requires glibc 2.38+; compile on the target distribution
if its runtime is older.

Fetch the pinned SDKs, SafetyHook 0.6.10, and Zydis 4.1.0 sources:

```sh
python scripts/fetch-deps.py
```

Windows (Visual Studio 2026; use the corresponding generator for VS 2022):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DMMSOURCE="$PWD/.deps/metamod-source-api17"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/windows
cpack --config build/CPackConfig.cmake -C Release -B dist
```

Linux:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release -DMMSOURCE="$PWD/.deps/metamod-source-api17"
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
cmake --install build-linux --prefix dist/linux
cpack --config build-linux/CPackConfig.cmake -B dist
```

Build portable tests without the game SDKs using `-DBOTMOD_BUILD_PLUGIN=OFF`.

## Install

1. Use a CS2 Metamod build with **plugin interface version 17**, as reported by
   `meta version`, such as [2.0.0-dev+1411](https://github.com/alliedmodders/metamod-source/releases/tag/2.0.0.1411).
   Keep your existing API 17 installation. Botmod 1.0.1 bundles its own detour
   library and does not require Metamod's newer KHook interface.
2. Copy the packaged `addons` folder into the server's `game/csgo` folder.
3. Restart the server, then run `meta list` in the server console. Botmod should
   be listed as loaded. For details use `meta info <number>` with its list number.
4. Add bots with `bot_add_t` / `bot_add_ct`, join the server, and open the scoreboard.

Package layout:

```text
addons/
  metamod/botmod.vdf
  botmod/gamedata.ini
  botmod/bin/win64/botmod.dll               (Windows)
  botmod/bin/linuxsteamrt64/botmod.so        (Linux)
```

Existing bots are handled on the next network snapshot, including after a
late load. `meta unload botmod` restores the native display on the next
snapshot when loaded through its VDF alias (otherwise use the plugin number).
`meta refresh` loads it again. No configuration commands are needed.

To replace Botmod 1.0.0 after the `Plugin requires newer Metamod version (18 > 17)`
error, stop the server, overwrite its Botmod files with the **1.0.1** package for
your platform, and restart. `meta info <number>` should report version 1.0.1 and
plugin API 17. The build rejects other API header versions, including stale
API 18 paths cached from a previous build. Metamod's marketing version (such as
2.0) and plugin interface version (17) are separate; Metamod 1.12 uses API 16.

## Compatibility and verification

CS2 updates can change engine signatures and SDK interfaces. The plugin
requires exactly one signature match and validates schema field sizes before
installing its hook; a mismatch fails loading with a diagnostic. The one
non-schema entity-system offset lives in `addons/botmod/gamedata.ini` together
with the platform signatures. Update these only against the matching game build.

The portable tests cover byte-for-byte restoration, nested display overrides,
human-name preservation, Unicode names, deleted/reused identities, and signature
validation. See [TESTING.md](TESTING.md) for the tested build and runtime checks.
A build alone does not prove the rendered scoreboard behavior.

For an in-game acceptance check, verify multiple bots have separate rows and
prefix-free names, human names remain unchanged, bot AI and `bot_kick` work,
and the behavior survives a round restart, map change, unload, and reload.
Do not run another bot-identity plugin alongside Botmod without testing the
combination.

## References

- [AlliedModders Metamod:Source](https://github.com/alliedmodders/metamod-source)
  and [CS2 SDK](https://github.com/alliedmodders/hl2sdk/tree/cs2) supply the plugin,
  schema, and entity interfaces.
- [SafetyHook](https://github.com/cursey/safetyhook/tree/v0.6.10) and
  [Zydis](https://github.com/zyantific/zydis/tree/v4.1.0) implement the bundled detour.
  Their license notices are included in the packages.
- [CS2-Bot-Hider gamedata](https://github.com/XBribo/CS2-Bot-Hider/blob/main/configs/addons/BotHider/gamedata.json)
  documents the PackEntities signatures and entity-system service offset used
  as research references. Botmod is an independent implementation with a
  narrower scope.

Botmod source is MIT licensed; SDK dependencies retain their upstream licenses.
