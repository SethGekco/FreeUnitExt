# FreeUnitExt — INI reference

Everything here is opt-in. A mod that sets none of these keys behaves exactly as
it does today.

## Every tag at a glance

Placed on a **BuildingType** unless noted.

| Tag | Type | Default | Section |
|---|---|---|---|
| `FreeUnit=` | list of TechnoTypes | none | [1](#1-freeunit--what-a-building-brings-with-it) |
| `FreeUnit.Facing=` | direction / `random` / 0-255 | building's facing | [1](#parallel-modifier-keys) |
| `FreeUnit.Cell=` | direction / `random` | nearest free cell | [1](#parallel-modifier-keys) |
| `FreeUnit.Spacing=` | integer ≥ 0 | `0` | [1](#parallel-modifier-keys) |
| `FreeUnit.Limbo=` | boolean | `no` | [1](#limbo-buildings-prerequisites-without-a-structure) |
| `FreeUnit.Range=` | integer ≥ 1 | `1` | [1](#buildings-that-come-with-buildings) |
| `FreeUnit.Anim=` | list of AnimTypes | none | [4](#4-animations) |
| `FreeUnit.Mission=` | MissionType | see §1 | [1](#freeunitmission-missiontype-default-see-below) |
| `FreeUnit.Owner=` | see §1 | `Invoker` | [1](#freeunitowner-default-invoker) |
| `FreeUnit.Script=` | ScriptType | none | [1](#freeunitscript-and-freeunitteam-default-none) |
| `FreeUnit.Team=` | TeamType | none | [1](#freeunitscript-and-freeunitteam-default-none) |
| `FreeUnit.OnlyBuilt=` | boolean | `yes` | [1](#freeunitonlybuilt-boolean-default-yes) |
| `FreeUnit.Buildings=` | list of BuildingTypes | none | [1](#buildings-that-come-with-buildings) |
| `FreeUnit.Buildings.*` | same modifiers as `FreeUnit.*` | — | [1](#buildings-that-come-with-buildings) |
| `FreeUnit.Anims=` | list of AnimTypes | none | [4](#4-animations) |
| `FreeUnit.Anims.Cell=` | direction / `random` | nearest cell | [4](#4-animations) |
| `FreeUnit.Anims.Spacing=` | integer ≥ 0 | `0` | [4](#4-animations) |
| `FreeUnit.Anims.Owner=` | see §1 | `Invoker` | [4](#4-animations) |
| `FreeUnit.Anims.RequireClear=` | boolean | `no` | [4](#4-animations) |
| `SeparateAircraft=` | boolean | `[General]` value | [2](#2-separateaircraft--free-aircraft-on-numbered-pads) |
| `SeparateAircraft.Types=` | list of AircraftTypes | none | [2](#separateaircrafttypes-list-of-aircrafttypes) |
| `SeparateAircraft.Facing=` | direction / `random` | `[General]PoseDir` | [2](#separateaircrafttypes-list-of-aircrafttypes) |
| `ManualFacing=` | boolean, on a **VehicleType** | `no` | [3](#3-manualfacing--aiming-an-immobile-unit) |
| `ManualFacing.Turret=` | boolean, on a **VehicleType** | `no` | [3](#3-manualfacing--aiming-an-immobile-unit) |
| `ManualFacing.ROT=` | integer, on a **VehicleType** | type's ROT | [3](#3-manualfacing--aiming-an-immobile-unit) |

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
| `FreeUnit.Anim=` | AnimType | none | spawn effect played where the entry lands (§4) |

### `FreeUnit.Mission=` (MissionType, default: see below)

```ini
[GAPILE]
FreeUnit=E1,E1,E1
FreeUnit.Mission=Guard,Area_Guard,Sleep
```

Any mission name the engine knows — the list is resolved through the game's own
`Mission Control` table, so every name on
[ModEnc's Mission Control page](https://modenc.renegadeprojects.com/Mission_Control)
works, and a mod that redefines the table keeps working too. An unknown name
logs a warning and leaves the default.

This is a *starting* mission. The player can override most of them by giving an
order, which is usually what you want. If you want a unit that cannot be
commanded, that is a property of the unit (`Speed=0`, `CanPassiveAquire=no`,
Phobos' immobilisation), not of this key — see `ManualFacing` below.

### `FreeUnit.Owner=` (default `Invoker`)

Who the delivered object belongs to.

| Value | Meaning |
|---|---|
| `Invoker` | the house that caused the delivery — the building's owner *(default)* |
| `Civilian` | the civilian side |
| `Special` | the special house |
| `Neutral` | the neutral house |
| `Random` | any house still in the game |
| `RandomAlly` | any house allied to the invoker, **excluding the invoker** |
| `RandomEnemy` | any non-allied, non-neutral house |

```ini
[CATECH]
FreeUnit=E1,E1
FreeUnit.Owner=Neutral        ; tech building comes with neutral defenders
```

Every `Random*` variant draws from the **synced scenario RNG**, so all clients
agree. If the requested house does not exist in this match (no neutral house, no
enemies), the delivery falls back to the invoker rather than dropping the unit.

> `Invoker` is named for the general case: today it is the building that
> finished, but the same word will mean the superweapon firer or the crate
> opener if delivery is ever driven from those.

### `FreeUnit.Script=` and `FreeUnit.Team=` (default: none)

Put the delivered unit under AI script control.

```ini
[NAHAND]
FreeUnit=E2,E2
FreeUnit.Script=PatrolPerimeter      ; a ScriptType from aimd.ini
```

```ini
[NAHAND]
FreeUnit=E2,E2
FreeUnit.Team=PerimeterGuardTeam     ; a TeamType from aimd.ini
```

**Both keys exist because a ScriptType cannot be attached to a unit.** The engine
only ever runs a script through a `TeamClass`, which is created from a
`TeamType`. So:

- **`FreeUnit.Team=`** names a TeamType you already authored — script, taskforce
  and every AI flag included. This is the robust path: the engine gets exactly
  what it expects.
- **`FreeUnit.Script=`** names a ScriptType, and a minimal TeamType is
  synthesised around it (cached per script, so repeat deliveries share one).
  Convenient, but the synthesised type has **no TaskForce** and default AI flags.
  If a script misbehaves under `.Script=`, author a TeamType and use `.Team=`.

If both are set on the same entry, **`.Team=` wins** — a TeamType already carries
a script, so honouring both would mean silently discarding one. A warning is
logged.

A team **overrides `FreeUnit.Mission=`**, because a script is a sequence of
missions; the queued mission remains only as the fallback if the team disbands.

Failures are non-fatal by design: an unknown script or team, a team that refuses
the unit, or a team that cannot be created all log and leave the unit on the map
with its mission. Losing a script is a degraded outcome; losing the unit would be
worse.

### What mission a delivered unit starts on

Harvesters start on `Harvest`. Everything else follows the type's own
**`DefaultToGuardArea=`**:

| `DefaultToGuardArea=` | Mission | Behaviour |
|---|---|---|
| `yes` (engine default) | `Area_Guard` | pursues targets within a radius |
| `no` | `Guard` | holds position, fires only at what comes to it |

FreeUnitExt does not force either one. If you do not want delivered units
wandering off after targets, set `DefaultToGuardArea=no` on the type.

Related existing tags worth knowing, none of which are ours:

| Tag | Owner | Effect |
|---|---|---|
| `CanPassiveAquire=no` | vanilla (note the spelling) | never auto-acquires a target |
| `CanRetaliate=no` | vanilla | never chases whoever shot it |
| `Speed=0` | vanilla + Phobos | genuinely immobile — blocks attack-move, scatter, hunt |

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

### Directions use the ENGINE's compass, not the screen's

`FreeUnit.Cell=` and `FreeUnit.Facing=` both use the game's own north, so they
agree with each other and with every other YR modding tag.

**The map is isometric, so the engine's north is NOT the top of your screen.**
Screen position is roughly `(cellX - cellY, cellX + cellY)`, and engine-north is
cell `(0,-1)` — which renders toward the **top-right corner**:

| Name | Cell offset | Where it appears on screen |
|---|---|---|
| `N` | (0,-1) | top-right |
| `NE` | (1,-1) | right |
| `E` | (1,0) | bottom-right |
| `SE` | (1,1) | bottom |
| `S` | (0,1) | bottom-left |
| `SW` | (-1,1) | left |
| `W` | (-1,0) | top-left |
| `NW` | (-1,-1) | **top** |

So if you want a unit at the visual top of the screen, that is **`NW`**.

`0` is north and values increase clockwise, so `64` is due east. A bare number
lets you aim between the eight compass points if the type's `ROT` can hold the
angle.

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
| `ManualFacing.Turret=` | boolean | `no` | aim the **turret** (SecondaryFacing) instead of the hull (PrimaryFacing) |

```ini
[MYBUNKER]
Speed=0
Turret=yes
ManualFacing=yes
ManualFacing.Turret=yes       ; hull stays put, turret tracks your clicks
```

`ManualFacing.Turret=yes` on a type with no turret logs a warning and aims the
hull instead.

> **Making the turret HOLD its aim is Phobos' job, not ours.** Vanilla snaps a
> turret back to the hull once it loses its target. Phobos already exposes that
> as `TurretResponse=` (it owns the hook at `0x736B60`), and already defaults it
> to `no` for `Speed=0` units — which is why an immobile emplacement holds its
> aim while a fast tank snaps back. Pair the two:
>
> ```ini
> [SOMEVEHICLE]
> ManualFacing=yes
> ManualFacing.Turret=yes   ; FreeUnitExt: let the player aim it
> TurretResponse=no         ; Phobos: and stop it snapping back
> ```
>
> FreeUnitExt deliberately does **not** add its own key for this. It would
> duplicate a shipped Phobos feature and contend for the same hook.

> **This flag is the whole opt-in.** There is deliberately no hidden `Speed=0`
> condition in the code.
>
> **But you almost certainly want `Speed=0` anyway.** `ManualFacing` only
> reinterprets the plain move click. It does **not** own movement, so on a
> `Speed>0` unit everything else still moves it: attack-move (Ctrl+Shift),
> auto-acquiring a target, scatter, hunt.
>
> `Speed=0` is what engages **Phobos'** whole immobilisation suite — its
> `UnitExt::CannotMove` returns true on `Speed == 0`, which gates its hooks on
> `Mission_Move`, `Assign_Destination`, `Scatter`, `Hunt`, `Mission_AreaGuard`,
> `GetFireError` and `Rotation_AI`. Combined:
>
> | Want | Set |
> |---|---|
> | genuinely immobile | `Speed=0` (Phobos does this) |
> | ...but still aimable by the player | `ManualFacing=yes` (we re-enable the click) |
> | ...aiming the turret, not the hull | `ManualFacing.Turret=yes` |
> | ...and the turret holds its aim | `TurretResponse=no` (Phobos; already the default at `Speed=0`) |
>
> Confirmed in-game: a `Speed=0` MCV obeys directional commands and nothing else
> moves it. A `Speed=7` tank turns its turret on command but still drives off on
> attack-move and auto-acquire — working as designed, not a defect. The intended use is `Speed=0`
> emplacements, but the DLL does not second-guess you.

**Test it on a unit that already exists at game start**, not one you build. A
`Speed=0` vehicle from a war factory is unlimboed on the exit cell and then needs
a move mission to drive clear — with `Speed=0` it never leaves and can block the
factory permanently.

`ManualFacing.ROT=` matters when the type has `ROT=0`: the body would otherwise
snap instantly to the new angle rather than swinging around.

---

## 4. Animations

Free units used to appear out of nothing. Two keys fix that, and the second one
also lets the animation itself be what creates the unit.

### `FreeUnit.Anim=` (list of AnimTypes, default: none)

A spawn effect played at the cell each delivered object lands on. It is a
parallel modifier like `FreeUnit.Facing=`: one value broadcasts to every entry,
or give one per entry.

```ini
[GAPILE]
FreeUnit=GGI,GGI
FreeUnit.Anim=S_BANG48              ; each GI arrives in a flash
```

Purely cosmetic. The effect plays **after** the object is confirmed on the map,
so a cell that refuses the unit never leaves an orphaned animation behind, and
an unknown AnimType costs you the effect but never the unit.

### `FreeUnit.Anims=` (list of AnimTypes, default: none)

Animations delivered **in their own right**, with no TechnoType involved. This
list is independent of `FreeUnit=`; a building may use either or both.

It covers two quite different jobs, which is why `RequireClear` exists:

| Job | Example | `RequireClear` |
|---|---|---|
| decoration / building addon | smoke, sparks, a glow on the structure | `no` (default) |
| spawning a unit | an AnimType with `MakeInfantry=` or `Spawns=` | `yes` |

| Key | Values | Default | Meaning |
|---|---|---|---|
| `FreeUnit.Anims.Cell=` | `N NE E SE S SW W NW`, `random` | nearest cell | which side of the building |
| `FreeUnit.Anims.Spacing=` | integer ≥ 0 | `0` | see the note on measurement below |
| `FreeUnit.Anims.Owner=` | as `FreeUnit.Owner=` | `Invoker` | house for remap, **and** who a spawned unit belongs to |
| `FreeUnit.Anims.RequireClear=` | boolean | `no` | must the cell be free? |

#### Letting the animation spawn the unit

```ini
[GADEPT]
FreeUnit.Anims=GENDEATH             ; artmd.ini gives GENDEATH MakeInfantry=0
FreeUnit.Anims.Cell=E
FreeUnit.Anims.RequireClear=yes     ; the Brute needs somewhere to stand
```

The depot delivers no unit at all. It plays an animation, and the animation
makes the unit — vanilla YR's own `MakeInfantry=` mechanism, which resolves its
index through `[General]AnimToInfantry=` in rulesmd.

> **`MakeInfantry=` and `Spawns=` live in `artmd.ini`, not `rulesmd.ini`.** An
> AnimType is declared in rules and given its behaviour in art. Looking for
> these keys in rulesmd finds nothing, and an animation without them simply
> plays and spawns no one.

`FreeUnit.Anims.Owner=` decides who the spawned unit belongs to, because the
engine hands `MakeInfantry`'s infantry to the animation's owning house. Leave it
at `Invoker` and the unit belongs to whoever built the structure.

#### Why `RequireClear` defaults to `no`, and how spacing is measured

Decoration and unit-spawning want opposite answers, and the decorative case is
both the commoner one and the one that fails most confusingly if it guesses
wrong — the cell a building addon most wants is the building's own, which no
object could ever occupy.

So with `RequireClear=no`:

- the clear-cell test is skipped entirely, and
- position is measured **from the building's centre**, not from the edge of its
  footprint. `Spacing=0` (the default) means the parent's own cell; raise it to
  push the effect outward ring by ring.

With `RequireClear=yes` an animation behaves like any delivered unit: it needs a
cell clear enough for infantry, measured outward from the footprint, and it is
reported as failed if the search finds nowhere.

Decoration never consumes the spacing budget that keeps solid objects apart, so
adding an animation to a building will not push its free units further out.
