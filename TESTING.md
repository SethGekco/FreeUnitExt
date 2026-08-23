# FreeUnitExt — testing

## 0. Read this first: the two silent-failure modes

**A. The DLL is not injected.** On Linux the actual Syringe inject list is
`Resources/Compatibility/Unix/wine-game.sh` (line 2), *not*
`Resources/ClientDefinitions.ini`. If `-i=FreeUnitExt.dll` is missing there, the
DLL never loads and every key below silently does nothing.

Verify: launch, then

```bash
grep -i freeunit "$RA2/syringe.log"
```

No match = not injected. Back up `wine-game.sh` first, then add `-i=FreeUnitExt.dll`.

**B. The DLL is stale.** Confirm the file in the game folder is the one CI just
built before concluding a fix "doesn't work":

```bash
md5sum "$RA2/FreeUnitExt.dll" DevBuild/FreeUnitExt.dll
```

Game folder: `/home/rex/snap/cncra2yr/common/.wine/drive_c/Westwood/RA2/`

---

## 1. Host tests (no game needed)

```bash
cd ~/Claude/FreeUnitExt
g++ -std=c++20 -Wall -Wextra -Isrc tests/plan_test.cpp -o /tmp/pt && /tmp/pt
```

Expect `all checks passed` (44 checks). These cover direction parsing, ring
geometry, spacing, random resolution through the injected RNG, limbo bypass and
per-entry failure isolation. They do **not** touch the engine.

---

## 2. In-game scenarios

Drop `test/freeunittest.ini` contents into your `rulesmd.ini` (it only uses
vanilla types) and build the named buildings.

**Every host is Allied**, so the whole set runs in one skirmish as America.

| # | Build | Expect | Catches |
|---|---|---|---|
| 1 | `GAPILE` | 4 GIs, one on each of N/E/S/W | multi-entry + `.Cell` |
| 2 | `GADEPT` | 3 GIs north, a clear cell between each | `.Spacing` |
| 3 | `GAWEAP` | 1 GI + 1 Grizzly, both facing east | mixed infantry/vehicle, `.Facing` broadcast |
| 4 | `GAOREP` | 2 Grizzlies facing different random directions each game | synced RNG, `random` |
| 5 | `GAREFN` | a harvester that **starts harvesting**, not guarding | ⚠ weak — see below |
| 6 | `GAPOWR` | one more power plant within 3 cells, and **it does not chain** | `Kind::Building` + `OnlyBuilt` |
| 7 | `GATECH` | no visible extra building, but Barracks units become buildable | `Limbo=yes` |
| 8 | `AMRADR` | pad comes with its aircraft even though `[General]SeparateAircraft=yes` | per-building override |
| 9 | `GAAIRC` | 4 aircraft, one per pad, facing N/E/S/W, not stacked | `SeparateAircraft.Types=` + per-pad `.Facing` |
| 10 | `GAOREP` | those 2 Grizzlies swing their **turrets** to a right-click, hulls unmoved | `ManualFacing.Turret=` |
| 11 | *(none)* | the starting MCV turns to face right-clicks, and still deploys | `ManualFacing=` on `[AMCV]` |

#4 and #10 share one build: the two Grizzlies show random spawn facing, then
serve as the turret subjects.

### The sharpest checks

> ⚠ **#5 is a WEAK test — do not treat it as a pass.** `FreeUnit=CMIN` is a valid
> VehicleType, so vanilla spawns it and Antares sets the harvest mission whether
> or not this DLL is loaded. A harvesting harvester therefore proves nothing; it
> looked like a pass for three debugging rounds while the DLL was doing nothing.
> Check `debug.log` for a `deliver [GAREFN]` line to know it was actually ours.

**#1 is the real canary.** `E1` is infantry, which vanilla's `FreeUnit=` cannot
express at all, so four GIs appearing can only be us.

**Always check the owner.** Free units are suppressed for human players by the
vanilla guard at `0x446AE3` (see HOOKS_LOG.md), so a feature can look like it
works while only ever firing for AI houses. Watching an AI base is not a test.

**#9** is the one most likely to fail. It depends on `DockingOffsets` actually
being populated; if all four aircraft stack on the building's centre, the vector
was empty and the fallback kicked in.

**#7** proves nothing on its own — check the tech tree, not the screen. Build
`GATECH`, then confirm the Barracks-gated units are buildable *without* a visible
Barracks on the map.

---

## 3. ManualFacing

Use `[AMCV]`, the starting MCV — **not** a factory-built unit. A `Speed=0`
vehicle produced by a war factory never drives off its exit cell and can block
the factory permanently. AMCV exists at game launch, so there is no exit to
negotiate.

Set `Speed=0` + `ManualFacing=yes` + `ManualFacing.ROT=3`, load a skirmish, and
right-click around the MCV before deploying it.

- Expected: the body swings to face the clicked cell and stops.
- Failure mode to watch for: the unit enters a permanent "moving" state and stops
  shooting. That means the `SkipGameCode` return did not take effect.

---

## 4. Regression checks (nothing set)

With the DLL loaded but **no** new keys in the INI, confirm unchanged behaviour:

- A vanilla `FreeUnit=` building still gives its one vehicle. (Our hooks return
  `0` in this case — but `FreeUnit=` alone *is* parsed by us, so this actually
  exercises the takeover path. See INI_REFERENCE §1's behaviour-change note: the
  unit's default facing changes to the building's facing.)
- `[General]SeparateAircraft=no` + a vanilla helipad still gives one aircraft.
- Antares `InitialPayload=` still works on a building that also has `FreeUnit=`.
  **This is the Antares-coexistence check** — if InitialPayload stopped working,
  our `0x446EE8` hook is firing at the wrong place relative to `0x446EE2`.

---

## 5. Known untested

- **Save/load with limbo entries.** Build `GATECH` (#7), save, reload, and check
  the tech tree still reflects the limbo building. Phobos' limbo bookkeeping does
  not know about ours, so this may not survive.
- **Multiplayer sync** with `random` facings/cells. Two clients, same map, both
  build #4; if they disagree about the facings, the RNG wiring is wrong.
- A building with both a Phobos `FreeWeeder` and our `FreeUnit=` list.
- A type that is both `KeepTargetOnMove` (Phobos) and `Speed=0` + `ManualFacing`
  — the `0x4C7462` chain-order collision case.
