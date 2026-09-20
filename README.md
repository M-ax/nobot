# Botmod

A standalone C++ Metamod:Source **2.0** plugin for CS2 that removes the native
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

Requirements: Git, Python 3, CMake 3.24+, and a 64-bit C++17 compiler. Windows
requires Visual Studio with the C++ desktop workload. Linux uses GCC/Clang.

Fetch the pinned SDK headers:

```sh
python scripts/fetch-deps.py
```

Windows (Visual Studio 2026; use the corresponding generator for VS 2022):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/windows
cpack --config build/CPackConfig.cmake -C Release -B dist
```

Linux:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure
cmake --install build-linux --prefix dist/linux
cpack --config build-linux/CPackConfig.cmake -B dist
```

Build portable tests without the game SDKs using `-DBOTMOD_BUILD_PLUGIN=OFF`.

## Install

1. Install [Metamod:Source 2.0](https://www.metamodsource.net/downloads.php/?branch=master)
   on the CS2 server. This build uses KHook and does **not** support Metamod 1.12.
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
  schema, entity, and hook interfaces.
- [CS2-Bot-Hider gamedata](https://github.com/XBribo/CS2-Bot-Hider/blob/main/configs/addons/BotHider/gamedata.json)
  documents the PackEntities signatures and entity-system service offset used
  as research references. Botmod is an independent implementation with a
  narrower scope.

Botmod source is MIT licensed; SDK dependencies retain their upstream licenses.
