# PowerGridDoctor

F4SE plugin for Fallout 4 that finds
workshop power grid entries pointing at objects that no longer exist, and shows
**every place** that still refers to them. That tells us why a repair such as
Workshop Framework's PowerGridTools does not survive a reload.

It checks, for each workshop:

- grid node keys (what PowerGridTools reports and removes)
- connections inside other nodes' connection sets (node X -> dead object)
- the grid's pending load data (`loadGameData`)
- the grid's currently powered list
- `ExtraPowerLinks` on every wired object in the grid

## Supported game versions

One DLL for all three, built on the multi-runtime
[Dear-Modding-FO4 CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4) fork:

| Game version | F4SE | Status |
|---|---|---|
| Anniversary 1.11.x (tested on 1.11.221) | 0.7.x | tested in game |
| Next-gen 1.10.984 | 0.7.2 | **untested**, should work |
| Old-gen 1.10.163 | 0.6.23 | **untested**, should work |

If you're on old-gen or next-gen, please report whether it loads. Send `PowerGridDoctor.log`:
its first lines say which game version it detected.

## Build (Windows)

Requirements: Visual Studio 2022 (C++ desktop workload) and [XMake](https://xmake.io) 3.0+.

From the unzipped `PowerGridDoctor` folder (or a git clone of it):

```bat
git clone --recurse-submodules https://github.com/Dear-Modding-FO4/commonlibf4 lib\commonlibf4
xmake
```

(If you cloned this project with `--recurse-submodules`, skip the first line.)

The DLL ends up in `build\windows\x64\releasedbg\PowerGridDoctor.dll`.
Or run `xmake install -o <folder>` to get a `Data\F4SE\Plugins\` layout that a mod manager can install.

## Install

Copy into `Data\F4SE\Plugins\`:

- `PowerGridDoctor.dll`
- `PowerGridDoctor.ini` (from `dist\F4SE\Plugins\`)

Requires F4SE and the Address Library for F4SE matching your game version.
[F4SE Menu Framework](https://github.com/DCCStudios/F4SEMenuFramework) is optional and adds the settlement page with Repair buttons.

## Use

The plugin scans every settlement after a save loads and (with `bScanOnWorkshopMenu=1`)
every time the workshop menu opens. It never changes anything unless you ask it to.
A console message tells you when a settlement has problems.

### Repairing a settlement (with F4SE Menu Framework)

1. Make a backup save.
2. Open F4SE Menu Framework and go to **Power Grid Doctor > Settlements**. The table lists every
   settlement, its object count and its power grid status.
3. Click **Repair** next to the settlement with problems. Only that settlement is changed.
   The row then shows what was removed.
4. Make a new hard save. Reload it and check the settlement shows **OK** (use **Rescan all**).

### Without F4SE Menu Framework

Set `bFix=1` in the ini, load the save, make a new hard save, then set `bFix=0` again.
This repairs **every** settlement with problems, so use it with care.

### Log

`Documents\My Games\Fallout4\F4SE\PowerGridDoctor.log` lists each problem id and every place that
still references it, the objects that hold the reference, and everything a repair removed.
It's kept short (a few lines per problem) so it fits on pastebin; set `bDumpAll=1` to also write
every settlement's full power grid.

Grids of settlements you are not in may not be fully built right after loading. If a settlement
looks wrong, scan or repair it while you're there.

## Notes / limits

- Fix mode removes only references to objects that are missing or deleted.
  Ids that resolve to a non-reference are reported but left alone.
- The connection set of a removed node is intentionally leaked, not freed. That's a few bytes.
- Only the objects in a grid are checked for `ExtraPowerLinks`. A wired object that is not in
  any grid but still links to a dead object would not be found.
- No change flags are set by hand. If a fix still doesn't stick, the log from the next load
  will show which structure the dead id came back in.

License: GPL-3.0-or-later (required by CommonLibF4).
`include/F4SEMenuFramework.h` comes from [F4SEMenuFrameworkExample](https://github.com/DCCStudios/F4SEMenuFrameworkExample) (MIT, see `include/F4SEMenuFramework.LICENSE.txt`).
