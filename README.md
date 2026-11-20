# PowerGridDoctor

F4SE plugin (CommonLibF4, Address Library) for Fallout 4 **1.11.221** that finds
workshop power grid entries pointing at objects that no longer exist, and shows
**every place** that still refers to them. That tells us why a repair such as
Workshop Framework's PowerGridTools does not survive a reload.

It checks, for each workshop:

- grid node keys (what PowerGridTools reports and removes)
- connections inside other nodes' connection sets (node X -> dead object)
- the grid's pending load data (`loadGameData`)
- the grid's currently powered list
- `ExtraPowerLinks` on every wired object in the grid

## Build (Windows)

Requirements: Visual Studio 2022 (C++ desktop workload) and [XMake](https://xmake.io) 3.0+.

```bat
git clone --recurse-submodules <this repo> PowerGridDoctor
cd PowerGridDoctor
xmake
```

The DLL ends up in `build\windows\x64\releasedbg\PowerGridDoctor.dll`.
Or run `xmake install -o <folder>` to get a `Data\F4SE\Plugins\` layout that a mod manager can install.

## Install

Copy into `Data\F4SE\Plugins\`:

- `PowerGridDoctor.dll`
- `PowerGridDoctor.ini` (from `dist\F4SE\Plugins\`)

Requires the Address Library for F4SE matching your game version.

## Use

1. **Report only (default, `bFix=0`)**: load your save, go to the settlement, open the workshop menu.
2. Read `Documents\My Games\Fallout4\F4SE\PowerGridDoctor.log`.
   For each bad id it lists every place that still references it.
3. **Fix (`bFix=1`)**: make a backup save first. Set `bFix=1`, load the save, open the
   workshop menu at the settlement, check the log says `0 problem id(s) remain`,
   then make a **new hard save**. Quit, relaunch, load that save and check again
   (with `bFix=0`).

Scans happen after a save loads and (with `bScanOnWorkshopMenu=1`) every time the workshop menu opens.
Grids of settlements you are not in may not be fully built right after loading, so the
workshop menu scan at the settlement is the one that matters.

## Notes / limits

- Fix mode removes only references to objects that are missing or deleted.
  Ids that resolve to a non-reference are reported but left alone.
- The connection set of a removed node is intentionally leaked, not freed. That's a few bytes.
- Only the objects in a grid are checked for `ExtraPowerLinks`. A wired object that is not in
  any grid but still links to a dead object would not be found.
- No change flags are set by hand. If a fix still doesn't stick, the log from the next load
  will show which structure the dead id came back in.

License: GPL-3.0-or-later (required by CommonLibF4).
