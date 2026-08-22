# FreeUnitExt — INI reference

Everything here is opt-in. A mod that sets none of these keys behaves exactly as
it does today.

---

## 1. FreeUnit — what a building brings with it

### `FreeUnit=` (list of TechnoTypes, default: none)

Vanilla accepted **one VehicleType** and gave you **one** vehicle. It is now a
list, and it accepts any TechnoType — infantry, vehicles, aircraft, buildings.

```ini
[GAWEAP]
FreeUnit=GGI,GGI,ENGINEER,MTNK      ; three infantry and a tank
```

> **Behaviour change:** as soon as `FreeUnit=` resolves to anything, FreeUnitExt
> takes over delivery entirely, including for a plain one-vehicle `FreeUnit=HARV`.
> The unit's default facing becomes the *building's* facing instead of vanilla's
> fixed south-west, and placement uses our ring search instead of the engine's
> `NearByLocation`. See HOOKS_LOG.md §4 for what this bypasses.

### Parallel modifier keys

Each key below takes **either one value** (applied to every entry) **or one value
per entry** in `FreeUnit=`. A short list leaves the remaining entries at their
default; write `-` to skip an entry explicitly.

| Key | Values | Default | Meaning |
|---|---|---|---|
| `FreeUnit.Facing=` | `N NE E SE S SW W NW`, `random`, `0`-`255` | building's facing | which way the delivered object looks |
| `FreeUnit.Cell=` | `N NE E SE S SW W NW`, `random` | nearest free cell | which side of the building it appears on |
| `FreeUnit.Spacing=` | integer ≥ 0 | `0` | cells left empty between consecutive entries on the same side |
| `FreeUnit.Limbo=` | boolean | `no` | deliver into limbo instead of onto the map (buildings only) |
| `FreeUnit.Range=` | integer ≥ 1 | `1` | how far a *building* entry may be placed from the parent |

### `FreeUnit.OnlyBuilt=` (boolean, default `yes`)

Deliver only when this building was genuinely **built**, not when it was itself
delivered by another building's list.

```ini
[NAPOWR]
FreeUnit.Buildings=NAPOWR     ; a power plant that comes with a power plant
FreeUnit.OnlyBuilt=yes        ; ...but the delivered one does NOT bring another
```

Without it that example is an **infinite chain**: unlimboing a delivered building
runs its Grand_Opening immediately, which delivers another, forever. The default
is `yes` because the runaway case hangs the game and the recursive case has no
known use.

Named to match `Host.OnlyBuilt=` in the GiftBox/Host DLL, and it means the same
thing there.

`FreeUnit.OnlyBuilt=no` re-enables chaining, but a **hard depth cap of 4** still
applies — a runaway chain aborts and logs rather than locking the game up.

Note that map-preplaced buildings never deliver regardless: vanilla's own
`ScenarioInit` guard above our hook already suppresses that.

```ini
[NAHAND]
FreeUnit=CONSCRIPT,CONSCRIPT,CONSCRIPT,CONSCRIPT
FreeUnit.Cell=N,E,S,W               ; one guard on each side
FreeUnit.Facing=random              ; broadcast: all four face randomly
FreeUnit.Spacing=1                  ; (no effect here — one unit per side)
```

Direction names are the compass as seen on screen; `0` is north and values
increase clockwise, so `64` is due east. A bare number lets you aim between the
eight compass points if the type's `ROT` can hold the angle.

`random` is resolved through the game's **scenario RNG**, so every client in a
multiplayer game picks the same cell and the same facing. Do not expect it to
differ between two buildings placed on the same frame by different players — it
is deterministic by design.

### Buildings that come with buildings

```ini
[GAPOWR]
FreeUnit.Buildings=GAPILE,GAWALL
FreeUnit.Buildings.Range=3          ; placed within 3 cells of the power plant
FreeUnit.Buildings.Cell=SE          ; prefer the south-east side
FreeUnit.Buildings.Facing=S
FreeUnit.Buildings.Limbo=no,no
```

`FreeUnit.Buildings=` is a second list with the same five modifier keys, using
the `FreeUnit.Buildings.` stem. It exists purely for readability — entries are
appended to the same delivery and resolved by the same engine, so you can equally
well put a BuildingType directly in `FreeUnit=`.

A building entry is placed only where its **whole foundation** fits, searching
outward from the preferred side up to `Range` cells. If nothing fits, that entry
is skipped and the rest of the delivery still happens.

### Limbo buildings (prerequisites without a structure)

```ini
[GATECH]
FreeUnit.Buildings=GAPILE
FreeUnit.Buildings.Limbo=yes        ; exists for prerequisites only, never on the map
```

A limbo entry never touches the map, so it cannot fail for lack of space. It
counts toward prerequisites, tech tree, power and build limits, matching Phobos'
`LimboDelivery`. `Limbo=yes` on a non-building is ignored with a log line.

> **Interoperability:** these are created by *our* code, not Phobos', so Phobos'
> `LimboKill` will not remove them and they are not in Phobos'
> `OwnedLimboDeliveredBuildings` list. Save/load behaviour is **untested** — see
> TESTING.md §5.

---

## 2. SeparateAircraft — free aircraft on numbered pads

### `SeparateAircraft=` (boolean, per BuildingType)

`[General]SeparateAircraft=` is unchanged and remains the global default. Setting
the key **on a building** overrides the global for that building only.

```ini
[General]
SeparateAircraft=yes                ; globally, aircraft are built separately

[NAHPAD]
SeparateAircraft=no                 ; ...but this pad still comes with its aircraft
```

`no` means "this building comes with free aircraft". With no `SeparateAircraft.Types=`
it delivers exactly what vanilla would: the first entry of `[General]PadAircraft=`,
once.

### `SeparateAircraft.Types=` (list of AircraftTypes)

One aircraft per numbered dock, in order — entry 0 lands on dock 0.

```ini
[GAAIRC]
NumberOfDocks=4
SeparateAircraft=no
SeparateAircraft.Types=ORCA,ORCA,ORCA,ORCA
SeparateAircraft.Facing=N,E,S,W     ; optional, per pad
```

- Entries beyond `NumberOfDocks` are dropped (with a log line) — an aircraft with
  no dock has nowhere to rearm.
- Setting `SeparateAircraft.Types=` implies this building delivers aircraft even
  if `Helipad=no`; naming the aircraft is taken as saying what you want.
- `SeparateAircraft.Facing=` defaults to `[General]PoseDir`. Phobos'
  `AircraftDockingDirs=` does **not** apply to these, because we bypass the call
  site Phobos patches — set `SeparateAircraft.Facing=` if you need per-pad angles.
- Each aircraft is attached to its pad with the same radio handshake vanilla
  uses, so rearming and returning work normally.

---

## 3. ManualFacing — aiming an immobile unit

Bundled here because it is far too small to be its own DLL.

```ini
[MYTURRET]
Speed=0
ManualFacing=yes
ManualFacing.ROT=3                  ; optional; default is the type's ROT=
```

| Key | Values | Default | Meaning |
|---|---|---|---|
| `ManualFacing=` | boolean | `no` | a move order makes this unit turn to face the clicked cell instead of being discarded |
| `ManualFacing.ROT=` | integer ≥ 0 | type's `ROT=` | turn rate for that rotation |

Only applies to units whose `Speed=0`. It is opt-in precisely so that existing
Speed=0 vehicles in a mod do not silently change how they answer orders.

`ManualFacing.ROT=` matters when the type has `ROT=0`: the body would otherwise
snap instantly to the new angle rather than swinging around.
