# FreeUnitExt — design

A Syringe DLL for Red Alert 2: Yuri's Revenge that generalises what a building
brings with it when it finishes. Targets **Antares** (never Ares) alongside
Phobos.

---

## 1. The problem

Two vanilla features do almost the same thing, badly, in the same function:

- **`FreeUnit=`** gives a building exactly one vehicle. Not two. Not infantry.
  You cannot say where it appears or which way it faces. Placement is whatever
  `NearByLocation` felt like.
- **`SeparateAircraft=`** is a single global switch. When it is `no`, every
  helipad gets exactly one aircraft — the first entry of `[General]PadAircraft=`
  — regardless of how many docks it has.

Both live inside `BuildingClass::Grand_Opening`, back to back, and both are
already surrounded by Antares and Phobos hooks.

## 2. The primitive

One concept: a **Delivery Entry**.

```
Entry = { TechnoType, Kind, Facing, Cell, Spacing, Range, Dock }
```

`Kind` is `Foot` (needs a clear cell), `Building` (needs a foundation that fits),
or `Limbo` (never touches the map). A building carries an ordered *list* of
entries, and every feature in this DLL is that one list resolved by one engine:

| Feature | Is just… |
|---|---|
| multiple free units | more entries |
| free infantry | an entry whose type is an InfantryType |
| spacing | `Spacing` on consecutive entries sharing a side |
| spawn direction | `Cell` |
| unit facing / random facing | `Facing`, with a `random` sentinel |
| buildings that come with buildings | entries of `Kind::Building` |
| limbo prerequisites | entries of `Kind::Limbo` |
| aircraft on numbered pads | a second list whose entries carry `Dock` |

There is no separate code path for any of them. Adding "free units also arrive
when a building is captured" would be a guard change, not a new subsystem.

## 3. Architecture

```
src/Delivery/Plan.h        PURE. No engine types, no game headers.
                           Geometry, direction parsing, the resolve() loop.
                           Unit-tested off-target by tests/plan_test.cpp.
                                    │
                                    │ Delivery::IPlacement
                                    ▼
src/Ext/BuildingType/Body.cpp       GameMap — the ONLY engine-aware code in
                                    the delivery path. Creates objects,
                                    unlimbos them, tests cells, wraps the
                                    synced RNG.
                                    ▲
src/Ext/Building/Hooks.Place.cpp    Three hooks inside Grand_Opening.

src/Ext/Store.h            Pointer-keyed type data (see §3.1).
```

### 3.1 No Phobos extension containers

We store per-type data in a map keyed by the **type object's own pointer**,
rather than using Phobos' `Container`/`Extension` machinery.

Two reasons, one principled and one practical:

- **Principled:** everything this DLL stores is derived from INI and never
  changes at runtime. Type objects live for the whole process, so the data needs
  no allocation hooks, no destructor hooks, and nothing written into a savegame.
  A container would buy us lifecycle management we have no lifecycle to manage.
  (This was first written keyed by `ArrayIndex`, which silently missed every
  lookup and made the whole DLL look inert — a pointer cannot be wrong at the
  moment the engine hands it to us.)
- **Practical:** Phobos PR **#2291** ("Rework the extension system into a mirror
  class hierarchy") removed `PrepareStream` / `LoadStatic` / `SaveStatic` from
  `Container`. The pattern the older sibling DLLs use no longer compiles against
  `develop`. Not depending on that API means it cannot move under us again.

The cost is one indirection per lookup. The benefit is that our only per-type
hook is a single `LoadFromINI` site per type class.

The split exists because the interesting bugs in a feature like this are
geometric — "the second unit spawned on top of the first", "spacing did nothing",
"`Cell=N` put it south" — and those are exactly the bugs you cannot debug through
a Windows-only game binary over a Wine round-trip. Everything above `IPlacement`
runs `g++` on Linux in under a second.

## 4. Hook strategy: sit below, don't fight

The FreeUnit and PadAircraft blocks are crowded (7 known hooks across
Antares/Ares/Phobos). Rather than claim the entry points, every site we take was
picked so that **the guards we want run above us**:

- Antares' once-only guard at `0x446AAF` still fires, so we never have to build
  a BuildingClass instance ext just to remember "already delivered".
- Vanilla's `ScenarioInit` and `captured` guards run above `0x446AE3`, so a
  captured building still gives nothing and map load is still silent.
- **But not every guard is worth inheriting.** The check at `0x446AE3` suppresses
  free units for *human players*; sitting below it meant only AI houses ever got
  deliveries. We hook above that one deliberately. See HOOKS_LOG.md §3.
- Antares' `InitialPayload` at `0x446EE2` still runs, because we hook one
  instruction later at `0x446EE8`.

Full derivation, register layouts and the list of what our takeover bypasses:
**HOOKS_LOG.md**.

## 5. Determinism

Every `random` resolves through `ScenarioClass::Instance->Random`, the game's
network-synced RNG. Using `rand()` for a spawn cell would desync multiplayer the
first time two clients disagreed about where a free unit landed — the kind of
bug that costs a week. `IPlacement::randomRanged` exists so the pure layer can
never reach for an unsynced source by accident, and so the tests can feed a
fixed sequence.

## 6. Bundled: ManualFacing

A unit with `Speed=0` discards move orders. `ManualFacing=yes` reinterprets the
order as "turn to look at that cell", giving an immobile emplacement a
player-controllable body facing.

Too small to be its own DLL, which is why it is here.

It needs **two** hooks, which was not obvious:

1. `UnitClass::What_Action` @ `0x740801` — an immobile unit's click never
   resolves to `Action::Move`, so no event is ever queued. Rewriting the decided
   action (observed as `Action::None`, not `NoMove`) is what makes the click
   dispatch at all.
2. `EventClass::Execute` @ `0x4C7462` — the **network event** seam, where the
   now-issued move order is turned into a facing change. Chosen over the input
   path so it is sync-safe by construction.

`ManualFacing.Turret=` selects hull (`PrimaryFacing`) or turret
(`SecondaryFacing`).

## 7. Status

Phase 1 **working in-game** (2026-08-21). Confirmed by Rex: four infantry per
barracks for the human player, four aircraft on four numbered pads, and
`FreeUnit.Buildings=` delivering exactly one neighbour instead of chaining.

**Every scenario confirmed in-game as of 2026-08-24** — multi-unit and infantry
delivery, `.Cell`, `.Spacing`, `.Facing` including `random`, neighbour buildings
with `OnlyBuilt`, limbo delivery, per-building `SeparateAircraft`, per-pad
`SeparateAircraft.Types=`/`.Facing`, and `ManualFacing` on both hull and turret.

Still unconfirmed: save/load with limbo entries, and multiplayer sync of
`random`.

| Piece | State |
|---|---|
| `Delivery::Plan.h` + tests | ✅ 44/44 green on host |
| BuildingType data + INI parsing | ✅ confirmed in-game |
| `GameMap` engine adapter | ✅ confirmed in-game |
| Grand_Opening hooks (3) | ✅ confirmed in-game |
| TechnoType data + ManualFacing | ✅ confirmed in-game |
| CI (Windows MSBuild + host tests) | ✅ green, artifacts published |

TESTING.md leads with the two silent-failure modes, and now also with the two
**false-positive** modes that cost the most time here: a test whose result vanilla
would produce anyway (#5), and a feature that only ever fires for AI houses.

## 8. Not done

- `FreeUnit.Buildings` neighbours do not inherit the parent's owner-change
  behaviour (sell/capture the parent, the neighbours stay).
- Phobos `FreeWeeder` (`0x446EAD`) is not reproduced in our takeover path.
- Limbo entries do not register with Phobos' `LimboKill` bookkeeping.
- No per-house or per-difficulty filtering on entries. The obvious next step is
  to reuse PrerequisiteExt's `Requirement` primitive as an entry gate rather
  than invent a second filtering syntax.
