# FreeUnitExt — hooks log

Derived from source and disassembly only (no Ghidra), per the standing rule:
consult the [YR Hook Encyclopedia](https://github.com/SethGekco/YR-Hook-Encyclopedia)
before choosing a hook, and report back after using one.

Toolchain target is **Antares**, not Ares.

---

## 1. The function

`BuildingClass::Grand_Opening(bool captured)` — `thiscall`, `ret 4`.

Frame at the sites we use: locals `0x58` + 4 saved registers (`ebx/ebp/esi/edi`)
= `0x68` to the return address, so the `captured` argument sits at `[ESP+0x6C]`
whenever ESP is at the frame base. Epilogue at `0x446FB6`:

```
446fb6  5f              pop edi
446fb7  5e              pop esi
446fb8  5d              pop ebp
446fb9  5b              pop ebx
446fba  83 c4 58        add esp, 0x58
446fbd  c2 04 00        ret 4
```

Two consecutive blocks live inside it:

| Range | What |
|---|---|
| `0x446AA9`–`0x446EE1` | FreeUnit: one VehicleType, one unit, `NearByLocation` placement |
| `0x446EE2`–`0x446FB5` | Pad aircraft: one aircraft from `Rules->PadAircraft[0]` |

---

## 2. Who was already there (from `registry/hooks.csv`)

| Address | Framework | Name | Size |
|---|---|---|---|
| `0x446AAF` | Antares, Ares | `BuildingClass_Place_SkipFreeUnits` | `0x6` |
| `0x446BF4` | Phobos | `BuildingClass_Place_FreeUnit_NearByLocation` | `0x6` |
| `0x446D42` | Phobos | `..._NearByLocation2` | `0x6` |
| `0x446E9F` | Antares | `BuildingClass_Place_FreeUnit_Mission` | `0x6` |
| `0x446EAD` | Phobos | `BuildingClass_GrandOpening_FreeWeeder_Mission` | `0x0` |
| `0x446EE2` | Antares, Ares | `BuildingClass_Place_InitialPayload` | `0x6` |
| `0x446F57` | Phobos | `BuildingClass_GrandOpening_PoseDir_SetContext` | `0x6` |

Two facts that shaped the design:

1. **Antares' `0x446AAF` hook is the once-only guard.** It sets
   `BuildingExt::FreeUnits_Done` and returns `0x446FB6` on the second visit, so a
   building cannot hand out free units twice. We sit *below* it and inherit that
   protection for free instead of re-implementing it in our own instance ext.
2. **Antares' `0x446EE2` hook returns `0`.** It delivers `InitialPayload` and then
   lets the vanilla aircraft block run. Taking `0x446EE2` ourselves, or jumping
   past it, would silently kill InitialPayload — so we hook one instruction later.

---

## 3. Sites we took (all previously unhooked)

### `0x446AB5` — `BuildingClass_GrandOpening_FreeUnitGate`, size `0x8`

```
446ab5  85 c0                 test eax, eax          ; eax = pType->FreeUnit
446ab7  0f 84 25 04 00 00     je   0x446ee2
```

Registers: `EBP` = `BuildingClass*`, `EAX` = `UnitTypeClass*` (vanilla `FreeUnit=`),
`ECX` = `BuildingTypeClass*`.

**Why it is needed:** vanilla parses `FreeUnit=` into a single `UnitTypeClass*`.
`FreeUnit=GGI` (infantry) or `FreeUnit=A,B` leaves that pointer NULL, so the whole
block is skipped before any later hook could run. We re-implement the two
swallowed instructions and additionally let the block proceed when our own parsed
list is non-empty.

Returns `0x446ABD` (continue the guard chain) or `0x446EE2` (skip to aircraft).
`EAX` is dead after this test — the spawn code at `0x446B78` re-reads
`[ecx+0xEA0]` fresh — so overriding the branch is safe without touching `EAX`.

### `0x446B16` — `BuildingClass_GrandOpening_Deliver`, size `0x7`

```
446b16  8b 45 00        mov eax, [ebp]
446b19  8d 4c 24 1c     lea ecx, [esp+0x1c]
```

`EBP` = `BuildingClass*`.

Chosen because **every vanilla guard is above it**, so we inherit all of them
rather than re-deriving them:

| Address | Guard |
|---|---|
| `0x446AAF` | Antares' once-only `FreeUnits_Done` |
| `0x446ABD` | `Unsorted::ScenarioInit` — no free units during map load |
| `0x446ACA` | the `captured` argument — a captured building gives nothing |
| `0x446AD6` | global `0xA8ED6B` |
| `0x446AE3`–`0x446B10` | house/upgrade-level check (`0x50B730`, `[ebp+0x300]`, type vtable `+0xAC`) |

Returns `0x446EE2` when we delivered, or `0` to fall through to the untouched
vanilla path when the building has no ext list.

### `0x446EE8` — `BuildingClass_GrandOpening_PadAircraft`, size `0x6`

```
446ee8  8a 81 e8 17 00 00     mov al, [ecx+0x17e8]   ; Rules->SeparateAircraft
```

`EBP` = `BuildingClass*`, `ECX` = `RulesClass::Instance`, `[ESP+0x6C]` = `captured`.

Deliberately **one instruction past `0x446EE2`** so Antares' InitialPayload hook
has already run. Replaces vanilla's three guards + single-aircraft spawn with a
per-building decision and a per-dock list. Returns `0x446FB6` when we handle it,
`0` otherwise.

Vanilla's spawn that we reproduce (`0x446F16`–`0x446FB0`):

```
ScenarioInit++
pAircraft = new AircraftClass(Rules->PadAircraft[0], pBld->Owner)
pAircraft->Unlimbo(pBld->GetCoords(), PoseDir())
pAircraft->QueueMission(Mission::Guard /*5*/, false)
pAircraft->SendCommand(RadioCommand::RequestLink /*2*/, pBld)
pBld->SendCommand(RadioCommand::RequestTether /*0x18*/, pAircraft)
ScenarioInit--
```

The two radio commands are the pad attachment; skipping them yields an aircraft
that never rearms. The `ScenarioInit` bracket spans the **whole** spawn, not just
the allocation — `Unlimbo` would otherwise refuse a cell occupied by the building.

### `0x4C7462` — `FreeUnitExt_EventClass_Execute_ManualFacing`, size `0x5`

`EventClass::Execute`, MegaMission dispatch. `ESI` = `EventClass*`,
`EDI` = `TechnoClass*`, skip target `0x4C74C0`.

Chosen over `UnitClass::Mission_Move` because this is the **network event path**:
the order is already serialised and replayed identically on every client, so
reinterpreting it cannot desync. The input/click path would run only on the
clicking player's machine.

`UnitClass::Mission_Move` was rejected for a second reason: it begins at
`0x740A90` (`push esi; mov esi, ecx` — 3 bytes) and Phobos already holds
`0x740A93`, so there is no room for a ≥5-byte hook at the entry.

**Chained with Phobos** (`EventClass_Execute_MegaMission_MoveCommand`, same
address, size `0x5`). Phobos bails for non-Units and otherwise acts only on
`KeepTargetOnMove` types; we act only on `Speed=0` + `ManualFacing=yes`. Disjoint
in practice, and we return `0` whenever we decline. ⚠ **Unverified:** Syringe's
ordering when both return non-zero. A `KeepTargetOnMove` type that is also
`Speed=0` + `ManualFacing` is the collision case — currently untested.

---

## 4. What our takeover bypasses

Returning `0x446EE2` from `0x446B16` skips the vanilla spawn, and with it:

- Phobos `0x446BF4` / `0x446D42` — the two `NearByLocation` movement-zone fixes.
  Replaced by our own ring search (`src/Delivery/Plan.h`), which walks the
  preferred compass ray first and widens into rings, and which is unit-tested
  off-target.
- Antares `0x446E9F` — the `Harvester ? Harvest : Area_Guard` mission fix.
  Reproduced verbatim in `GameMap::place`.
- Phobos `0x446EAD` — `FreeWeeder` mission. **Not reproduced.** A building with
  both a Phobos free weeder and our `FreeUnit=` list is untested.

Returning `0x446FB6` from `0x446EE8` skips:

- Phobos `0x446F57` + its `DEFINE_FUNCTION_JUMP(CALL, 0x446F67)` landing-dir
  patch, so `AircraftDockingDirs=` does not apply to our pad aircraft.
  `SeparateAircraft.Facing=` is the replacement.

All of this only happens for buildings that actually use our keys.

---

## 5. Engine facts verified in YRpp

| Fact | Where |
|---|---|
| `BuildingTypeClass::FreeUnit` is `UnitTypeClass*` — vehicles only | `BuildingTypeClass.h:136` |
| `RulesClass::SeparateAircraft` at `+0x17E8` | `RulesClass.h:905` |
| `RulesClass::PadAircraft` is `TypeList<AircraftTypeClass*>` at `+0xB5C` | `RulesClass.h:558` |
| `BuildingTypeClass::Helipad` at `+0x154E`, `NumberOfDocks` at `+0x1790` | `BuildingTypeClass.h:270,317` |
| `BuildingTypeClass::CanPlaceHere(CellStruct*, HouseClass*)` @ `0x464AC0` walks the foundation | `BuildingTypeClass.h:69` |
| `RadioCommand::RequestLink = 2`, `RequestTether = 24` | `GeneralDefinitions.h:1369,1393` |
| `Mission::Guard = 5` | `GeneralDefinitions.h:978` |
| `Unsorted::ScenarioInit` @ `0xA8E7AC` | `Unsorted.h:734` |
| `ScenarioClass::Instance->Random.RandomRanged` is the synced RNG | `Randomizer.h:15` |
| `FacingClass::SetDesired` only animates when `ROT.Raw > 0` | `Facing.h:48` |

**API that does NOT exist in YRpp** (cost a rewrite): there is no
`MapClass::CanBuildingTypeBePlacedHere`. Use `BuildingTypeClass::CanPlaceHere`.
`ObjectClass::UnInit` *does* exist (`JMP_THIS(0x5F65F0)`); `Limbo()` is an `R0`
stub and must be called through the vtable, per the
`yrpp-r0-stub-vs-jmpthis` footgun.

---

## 6. Type-data hooks (and a Phobos API finding)

We use exactly **two** INI hooks and nothing else — no CTOR/DTOR/stream hooks —
because our data is ArrayIndex-keyed and process-lifetime (DESIGN.md §3.1).

| Address | Size | Registers | What |
|---|---|---|---|
| `0x464A49` | `0xA` | `EBP` = `BuildingTypeClass*`, `[ESP+0x364]` = `CCINIClass*` | end of `BuildingTypeClass::LoadFromINI` |
| `0x716123` | `0x5` | `EBP` = `TechnoTypeClass*`, `[ESP+0x380]` = `CCINIClass*` | end of `TechnoTypeClass::LoadFromINI` |

Both return `0` and chain with Phobos' hooks at the same sites. Both fire at the
*end* of parsing, so native fields (`NumberOfDocks`, `Helipad`) are already
populated when we read them.

### ⚠ Finding: Phobos PR #2291 broke the old ext-container pattern

Phobos commit `2cb961e6` ("Rework the extension system into a mirror class
hierarchy") **removed `PrepareStream`, `LoadStatic` and `SaveStatic` from
`Container`**. Any DLL still calling them will not compile against current
`develop`. The replacement is a `PhobosTypeRegistry` in `src/Phobos.Ext.cpp`
that dispatches save/load across registered containers — Phobos-internal
template machinery, not obviously reusable from a third-party DLL.

**Address correction found while checking this:** Phobos now hooks
`BuildingTypeClass`'s destructor at **`0x45E732`, size `0xE`** — not `0x45E707`
size `0x6`, which is what the older sibling DLLs use. The CTOR site `0x45E50C`
is unchanged, but Phobos now calls `Allocate` rather than `TryAllocate`.

This affects the other DLLs in this family, not just this one.

### INI override semantics

The engine reads rules → game mode → scenario → map into the *same* type object,
and an absent key must leave the previous value alone. `ParseDeliveryList`
therefore bails **before** clearing when the type key is missing from the current
INI; when the type key *is* present, that file owns the whole list including its
modifier keys. Getting this backwards would make any map INI silently wipe
rules-defined free units.

---

## 7. Open / unverified

1. `[ebp+0x300]` and the type vtable slot `+0xAC` in the `0x446AE3`–`0x446B10`
   guard are not fully decoded. We inherit the guard rather than reason about it,
   so this is documentation debt, not a correctness risk.
2. Global `0xA8ED6B` (checked at `0x446AD6`) is unidentified.
3. Save/load of our limbo-delivered buildings. They are registered into
   `House::Buildings` like any other building, so vanilla serialisation *should*
   carry them, but this is untested and Phobos' limbo bookkeeping does not know
   about them.
4. Syringe chain ordering at `0x4C7462` when both Phobos and we return non-zero.
5. Whether `DockingOffsets` is populated for all pad buildings, or only those
   that declare `DockingOffset0`-style keys. We fall back to the building's
   centre when the vector is short.
