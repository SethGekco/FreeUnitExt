# FreeUnitExt

A Syringe DLL for **Red Alert 2: Yuri's Revenge** that makes `FreeUnit=` and
`SeparateAircraft=` do what modders always wanted them to do.

Built against **Antares** (not Ares) + Phobos.

---

## What it adds

**`FreeUnit=` becomes a list of anything.**

```ini
[GAPILE]
FreeUnit=E1,E1,E1,E1
FreeUnit.Cell=N,E,S,W        ; one guard on each side
FreeUnit.Facing=S,W,N,E      ; each looking back at the barracks
FreeUnit.Spacing=1
```

- more than one unit
- infantry, vehicles, aircraft — not just vehicles
- controlled spawn side (`N NE E SE S SW W NW`), or `random`
- controlled facing, or `random`
- proper spacing between them
- buildings that come with **real** randomly-adjacent buildings (`FreeUnit.Buildings=`, `.Range=`)
- buildings that come with **limbo** buildings, for prerequisites only (`.Limbo=yes`)
- a mission, an owner, or an aimd.ini script per entry (`.Mission=`, `.Owner=`, `.Script=`)
- a spawn animation, so units stop appearing out of thin air (`.Anim=`)

**Animations, including ones that spawn the unit themselves.**

```ini
[GADEPT]
FreeUnit.Anims=GENDEATH             ; artmd.ini gives GENDEATH MakeInfantry=0
FreeUnit.Anims.Cell=E
FreeUnit.Anims.RequireClear=yes     ; the unit it makes needs somewhere to stand
```

No unit is delivered here at all — an animation plays, and the animation makes
the unit. Leave `RequireClear` at its default `no` and the same key becomes a
decorative building addon, playing on the structure's own cell.

**`SeparateAircraft=` becomes per-building, with a list.**

```ini
[GAAIRC]
NumberOfDocks=4
SeparateAircraft=no                        ; overrides [General] for this building
SeparateAircraft.Types=ORCA,ORCA,ORCA,ORCA ; one per numbered pad
```

**Bundled: `ManualFacing=`.** A `Speed=0` unit can be aimed by right-clicking a
direction, like turning a turret — instead of silently discarding the order.

Full key list: **[INI_REFERENCE.md](INI_REFERENCE.md)**

---

## Status

⚠ **Phase 1 code-complete, never compiled on Windows and never run in-game.**

The engine-free planner is tested (37/37 on the host). Everything that touches
the game is written from source-derived addresses and has not executed once.

Start with **[TESTING.md](TESTING.md)** — it leads with the two silent-failure
modes (DLL not injected / DLL stale), which are the reason a "broken feature"
usually isn't.

---

## Building

Windows, MSVC, Win32:

```
git submodule update --init --recursive
msbuild FreeUnitExt.sln /p:Configuration=DevBuild /p:Platform=Win32
```

Host tests (Linux/macOS, no game or submodules needed):

```
g++ -std=c++20 -Wall -Wextra -Isrc tests/plan_test.cpp -o pt && ./pt
```

CI runs both — see `.github/workflows/build.yml`.

---

## Docs

| File | What |
|---|---|
| [DESIGN.md](DESIGN.md) | the one-primitive model and why the hooks sit where they do |
| [INI_REFERENCE.md](INI_REFERENCE.md) | every key, with defaults and caveats |
| [HOOKS_LOG.md](HOOKS_LOG.md) | addresses, register layouts, what our takeover bypasses |
| [TESTING.md](TESTING.md) | 9 drop-in scenarios + `test/freeunittest.ini` |
