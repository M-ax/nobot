# Botmod

A standalone C++ Metamod:Source **plugin API 17** plugin for CS2 that removes the native
BOT name label from the in-game player list. It also strips literal `[BOT] `
and `BOT ` prefixes from bot names sent in entity snapshots.

This targets the scoreboard on a server you control. It does not modify the
Steam/main-menu party lobby. CounterStrikeSharp is not required.

## How it works

CS2's native bot label is separate from the name. **Entity flags alone are
insufficient:** 1.0.2 still sent `CMsgPlayerInfo.fakeplayer=true` in the `userinfo`
string table, even when both controller and pawn flags were cleared.

Version 1.1.0 uses Bot-Hider's engine-client identity approach: clear
`CServerSideClient.m_bFakePlayer`, change the fake-client connection bits, and
set both engine SteamID fields before publishing player info. Botmod scopes
those changes to `UserInfoChanged` and restores every original byte immediately
after publication. This keeps native bot detection and fake-client naming
available to VStrikeIdentity without a Harmony patch or changes to VStrike.
The hook also intercepts player-info updates caused by renaming bots.

During `PackEntities`, Botmod additionally clears the controller's
`FL_FAKECLIENT` and its player pawn's `FL_BOT`, assigns the same display ID,
and strips literal name prefixes. Controller and pawn state is restored after
packing. Pawn handles are checked by serial number after death, respawn, and
replacement. Engine clients are checked by pointer and user ID before restoration.

Bots without a SteamID receive distinct **display-only synthetic IDs** during
packing, so their scoreboard rows do not collapse onto the same zero ID.
These are not authenticated Steam accounts. Engine client identities change
only during player-info publication. Native server-browser bot counts, bot
quota, and ping values are not overridden.
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

Download the [latest Linux ZIP](https://github.com/M-ax/nobot/releases/latest/download/botmod-linuxsteamrt64.zip)
from [GitHub Releases](https://github.com/M-ax/nobot/releases/latest). It contains
the full directory layout, required gamedata, and license notices.

1. Use a CS2 Metamod build with **plugin interface version 17**, as reported by
   `meta version`, such as [2.0.0-dev+1411](https://github.com/alliedmodders/metamod-source/releases/tag/2.0.0.1411).
   Keep your existing API 17 installation. Botmod 1.1.0 bundles its own detour
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

For individual Linux downloads, install **all three** files at these paths:

| Download | Path relative to `game/csgo` |
| --- | --- |
| [botmod.so](https://github.com/M-ax/nobot/releases/latest/download/botmod.so) | `addons/botmod/bin/linuxsteamrt64/botmod.so` |
| [botmod.vdf](https://github.com/M-ax/nobot/releases/latest/download/botmod.vdf) | `addons/metamod/botmod.vdf` |
| [gamedata.ini](https://github.com/M-ax/nobot/releases/latest/download/gamedata.ini) | `addons/botmod/gamedata.ini` |

The ZIP is recommended for a first install. Releases also include `SHA256SUMS`.

Existing bots have player info refreshed on the next snapshot after a late
load, alongside their entity state. `meta unload botmod`
republishes native player info and restores entity display on the next
snapshot when loaded through its VDF alias (otherwise use the plugin number).
`meta refresh` loads it again. No configuration commands are needed.

To update from 1.0.x, stop the server, overwrite its Botmod files with the
**1.1.0** package for your platform, including **gamedata.ini**, and restart.
`meta info <number>` should report version 1.1.0 and plugin API 17. The new
engine-client offsets are required; using old gamedata rejects loading.
After bots connect, look for `Published bot player info with fakeplayer=false`
as well as the controller/pawn snapshot messages. Reconnect before checking
the scoreboard and spectator HUD. Run Botmod as the label-removal plugin;
remove the original Bot-Hider/BotHiderImpl if installed, since they modify
the same identity fields and are not a supported combination.

This also retains the fix for 1.0.0's `Plugin requires newer Metamod version
(18 > 17)` error. The build rejects other API header versions, including stale
API 18 paths cached from a previous build. Metamod's marketing version (such as
2.0) and plugin interface version (17) are separate; Metamod 1.12 uses API 16.

### VStrikeIdentity

The supplied VStrikeIdentity 0.5.0 source renames existing bots through
`SetFakeClientConVar("name", ...)` and `m_iszPlayerName`; it does not create
bots or remove CS2's native label. Its `vstrike_hidebotlabels` command explicitly
does nothing. Keep using `vstrike_setname` / `vstrike_setteamname` for your roster
names with Botmod loaded. Botmod restores native flags before the next game
frame and restores engine fake-client state after each player-info update,
preserving VStrikeIdentity's `IsBot` checks and `SetFakeClientConVar` calls.
The supplied, unmodified VStrikeIdentity 0.5.0 binary was tested alongside
Botmod 1.1.0 on an API 17 Windows server: both naming commands succeeded,
status continued to report `bot=True`, and recorded player info contained the
new names with `fakeplayer=false`. No VStrike source changes are required.
The rendered UI still needs a connected-client acceptance check.

## GitHub release builds

[Build and release Linux](https://github.com/M-ax/nobot/actions/workflows/release.yml)
builds with GCC 13 on Ubuntu 24.04, fetches the pinned dependencies, runs CTest,
and uploads install files as an Actions artifact on pushes to `main` and pull
requests. These artifacts are retained for 14 days.

To publish a release, update the CMake project version and plugin version,
commit the changes, and push a matching version tag:

```sh
git tag v1.1.0
git push origin main v1.1.0
```

The tag must match `CMakeLists.txt`. After the build and tests pass, the workflow
publishes the Linux ZIP, `.so`, `.vdf`, required `gamedata.ini`, and checksums on
GitHub Releases. Download names stay the same across releases so the latest
install links above keep working. The workflow uses GitHub's automatic token;
no repository secrets or external build worker are needed.

You can also use **Run workflow** with an existing version tag to publish it,
or leave the tag empty to build an artifact without publishing. Re-running a
published tag leaves its release downloads unchanged. Windows packages can
still be built locally with the commands above.

## Compatibility and verification

CS2 updates can change engine signatures and SDK interfaces. The plugin
requires exactly one signature match and validates schema field sizes before
installing its snapshot hook; a mismatch fails loading with a diagnostic.
Non-schema entity-system and engine-client offsets live in
`addons/botmod/gamedata.ini` together with the platform signatures. The
player-info detour resolves its target through the SDK's `UserInfoChanged`
virtual method. Update SDKs and gamedata against the matching game build.

The portable tests cover engine-client, controller, and pawn restoration, nested display overrides,
human-name preservation, Unicode names, deleted/reused identities, and signature
validation. See [TESTING.md](TESTING.md) for the tested build and runtime checks.
A build alone does not prove the rendered scoreboard behavior.

For an in-game acceptance check, verify multiple bots have separate rows and
prefix-free names, human names remain unchanged, bot AI and `bot_kick` work,
and the behavior survives a round restart, map change, unload, and reload.
Test other bot-identity plugins with Botmod before deploying the combination.

## References

- [AlliedModders Metamod:Source](https://github.com/alliedmodders/metamod-source)
  and [CS2 SDK](https://github.com/alliedmodders/hl2sdk/tree/cs2) supply the plugin,
  schema, and entity interfaces.
- [SafetyHook](https://github.com/cursey/safetyhook/tree/v0.6.10) and
  [Zydis](https://github.com/zyantific/zydis/tree/v4.1.0) implement the bundled detour.
  Their license notices are included in the packages.
- [CS2-Bot-Hider gamedata](https://github.com/XBribo/CS2-Bot-Hider/blob/main/configs/addons/BotHider/gamedata.json)
  and [engine-client identity fields](https://github.com/XBribo/CS2-Bot-Hider/blob/2fe6742c96e64e599baf7054c08075d7bb6741ea/src/core/cs2_sdk/entity/serversideclient_ref.h)
  document the engine identity approach and offsets used as research references.
  Botmod implements scoped publication using API 17 SourceHook and bundled
  SafetyHook. Lifecycle work uses Metamod level notifications and the snapshot
  hook, avoiding shared GameFrame/connection hooks with CounterStrikeSharp.
  It does not use Bot-Hider's API 18 KHook or its managed identity
  overrides, persona roster, or fake ping.

Botmod source is MIT licensed; SDK dependencies retain their upstream licenses.
