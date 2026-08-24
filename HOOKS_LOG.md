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

### `0x446AE3` — `BuildingClass_GrandOpening_Deliver`, size `0x6`

```
446ae3  8b 8d 1c 02 00 00     mov ecx, [ebp+0x21c]   ; pThis->Owner
```

`EBP` = `BuildingClass*`.

**This hook was originally at `0x446B16` and that was wrong.** The guard between
`0x446AE3` and `0x446B10` is:

```
if (Owner->IsControlledByHuman()        // 0x50B730 — named in YRpp HouseClass.h:492
    && pThis->[0x300] != 0
    && pThis->[0x300] <= pType->vtable[0xAC]())
    goto 0x446EE2;                      // skip free units entirely
```

`0x50B730` is `HouseClass::IsControlledByHuman()` = `IsHumanPlayer ||
IsInPlayerControl`, confirmed against YRpp which carries the address in a
comment. So **this guard suppresses free units for human players** under a
condition involving `BuildingClass+0x300` and a BuildingTypeClass virtual at
vtable slot `0xAC` (both still unidentified).

Empirically, in a skirmish this made every free-unit delivery belong to an AI
house — `owner=Germans`/`owner=Russians`, never the player — while the player's
own buildings silently produced nothing. A player's Refinery still delivered,
so the guard is conditional rather than a blanket human block.

We now sit ABOVE it and bypass it. Vanilla can afford the rule because its
`FreeUnit=` is one incidental vehicle; a modder writing `FreeUnit=E1,E1,E1,E1`
means it.

**Still inherited** (all above `0x446AE3`): Antares' once-only guard, the
`ScenarioInit` map-load suppression, the `captured` argument, and the `0xA8ED6B`
global.

### `0x446B16` — former deliver site, size `0x7` (NO LONGER USED)

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

## 7. Audit of the never-executed paths

Reviewed after four silent bugs were found in the paths that *had* run.

**`ManualFacing` — seam verified sound, no change needed.**
`UnitClass::MouseOverCell` @ `0x7404B0`–`0x74080C` (the function that decides
what a cell click does) contains **no read of `TechnoTypeClass::Speed`**, which
lives at `+0x678` (derived from the `Speed=` tag parse at `0x71464C`, which
stores to `[ebp+0x678]`). So a `Speed=0` unit produces the same click Action as
any other and the MegaMission move event is emitted normally — our `0x4C7462`
hook will see it. No `MouseOverCell` hook is required.

The skip target `0x4C74C0` was also verified: it is the `EventClass::Execute`
epilogue (`pop edi/esi/ebp/ebx; add esp,0x370; ret`). Our 5 stolen bytes are
`mov eax,[edi]` + `push ebx` + `mov ecx,edi`; returning to the epilogue skips the
`push ebx` and the `call [eax+0x3c8]` that would have consumed it, so the stack
stays balanced.

**`placeLimbo` — REAL BUG FOUND (fixed).**
Phobos' `LimboCreate` carries the comment *"BuildingClass::Place is already
called in DiscoveredBy"*, and `BuildingClass::Place` **is** `Grand_Opening` —
Ares/Antares name it `Place`, Phobos names it `GrandOpening`, same function.

So `DiscoveredBy` re-enters our own delivery hooks **synchronously**, and a
limbo-delivered building would immediately run its own `FreeUnit=` list. With
the shipped test rules that is live: `[GATECH]` limbo-delivers a `GAPILE`, whose
list is `E1,E1,E1,E1` — four GIs would spawn onto the map from a structure that
is supposed to be invisible.

Fixed by marking the building **before** `DiscoveredBy`, and re-marking after,
because `ClaimWasDelivered` is one-shot while campaign calls `DiscoveredBy`
twice (CurrentPlayer, then owner).

Note the asymmetry worth remembering: on the **on-map** path `Grand_Opening` is
*deferred* (which is why a depth counter failed there), but through
`DiscoveredBy` it is *re-entrant within our own call*. Same function, two
different timing behaviours depending on how the building was created.

---

## 8. Open / unverified

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

---

## 8. Chain-guard for unit-level delivery (`OnlyBuilt`, the mark-built pattern)

**Context.** As of this writing the delivery trigger is *building* `Grand_Opening`
only (`0x446AE3`); a delivered unit has no trigger, so it cannot re-deliver. The
existing `FreeUnit.OnlyBuilt=` is enforced for **buildings** by identity: a
delivered building is stamped (`DeliveredBuildings::Mark`, Body.cpp) and its own
Grand_Opening consumes the stamp (`ClaimWasDelivered`, Hooks.Place.cpp). That
works because Grand_Opening is *deferred* — the stamp is set frames before it is
checked.

**The gap.** Any *unit-level* delivery feature (a unit that spawns units when it
is built) has no equivalent stamp. Symptom observed while developing such a tag:
`FreeUnit=E1,E1,E1,E1`-style delivery from a unit produces the four E1, and those
E1 then deliver their own four, ignoring `OnlyBuilt` — because nothing marks a
delivered unit as "not built," so the guard cannot distinguish a built E1 from a
delivered one.

**Do NOT copy the building pattern verbatim for units.** `DeliveredBuildings`
marks the *delivered* object and works only because the building trigger
(Grand_Opening) is deferred. A unit-creation trigger fires *synchronously as the
unit is born*, so a mark-delivered stamp races the trigger — the child can
re-deliver before it is stamped.

**Use mark-BUILT instead (fail-closed, race-free, DLL-compatible).** Invert the
default: nothing delivers unless it is known-built.

- Add a "built techno" identity set (an `unordered_set<TechnoClass*>`, same shape
  as `DeliveredBuildings`, not the Phobos container).
- **Stamp at factory ejection — `BuildingClass::KickOutUnit`:**
  `0x444131` (InfantryType / E1), `0x444119` (UnitType / vehicles),
  `0x443CCA` (AircraftType). A unit that leaves a factory is "built." These
  addresses are already hooked by Phobos, so return `0` to chain; record the use
  back in the YR Hook Encyclopedia per the standing workflow.
- **Guard the unit-delivery trigger:** `if (OnlyBuilt && !WasBuilt(pUnit)) skip;`.
  Units created by `Delivery::resolve` never pass through `KickOutUnit`, so they
  are never stamped → never re-deliver. A missed stamp fails *closed* (a unit
  silently doesn't deliver) instead of *open* (runaway chain).
- Keep `MaxDeliveryDepth` as the last-resort backstop, as the building path does.

**Trade-off to document for modders.** Mark-built means only *factory-produced*
units deliver. Pre-placed, campaign-scripted and trigger-given units are not
stamped and so will not deliver unless separately marked (e.g. a one-time stamp
at scenario start). For skirmish "build from a factory," `KickOutUnit` alone is
sufficient and is usually the desired scope.

**Why this direction (design note).** Mark-built is opt-in: only technos this DLL
recognises as built participate, so units created by *other* DLLs, by the map, or
by crates are never swept into our delivery logic — the guard is compatible with
whatever else is loaded. Mark-delivered is opt-out and fails open, which on a big
delivery list means a flood. Same conclusion reached independently in the
GiftBox/Host DLL (`Host.OnlyBuilt=`), whose current mark-*spawned* guard works but
is fail-open; see that project's notes and the encyclopedia
`Map-Cell-Indexing.md` neighbours for the shared reasoning.

### Building path: IMPLEMENTED (the mark must be persistent, not consumed)

The building guard shipped a subtler version of the same failure and is now fixed.
`DeliveredBuildings::ClaimWasDelivered` used to **consume** the mark (`erase` on
claim), on the theory that a building gets exactly one Grand_Opening. It does not:
`Place`/`Grand_Opening` fires an unpredictable number of times (skirmish vs
campaign discovery counts, houses re-discovering it, the synchronous limbo
re-entry). Each fire consumed one mark; once exhausted, the delivered building was
treated as **built** and re-ran its `FreeUnit` list — free units reproducing in
spite of `FreeUnit.OnlyBuilt=yes`. The old `placeLimbo` "re-mark once for the
campaign's two DiscoveredBy calls" was a band-aid over this.

Fix (committed):
- `WasDelivered()` is **non-consuming** (`Marks.count`, not `erase`). A delivered
  building stays exempt for its whole life, however many times `Place` fires.
- `Unmark()` clears the flag on death, hooked at **`0x6F4500`
  (`TechnoClass::~TechnoClass`, ECX, size 5; shared with Ares/Antares/Phobos —
  return 0 to chain)**, so a building the engine later allocates at a freed
  address never inherits a stale flag.
- The `placeLimbo` re-mark is gone; one `Mark` before `DiscoveredBy` now suffices.

**General principle (for any DLL author — this is the reusable lesson):** a
"was-this-spawned/delivered/built" guard flag must be **stable for the object's
whole lifetime and set before the trigger can fire** — never consumed per-event
and never counted-against a guessed number of firings. Consume-on-claim and
"re-mark N times" both leak the moment the engine fires the trigger once more than
you predicted. Set at creation, clear at destruction, check without mutating.

*Status: building path implemented and CI-green. The unit path (mark-built) remains
a recommendation for when the unit-delivery trigger lands — same principle.*

### Post-fix log analysis (2026-08-23): guard works, but delivery double-fires

Reading `debug/debug.20260823-204429.log` after the persistent-mark fix shipped:

**The OnlyBuilt guard is confirmed working.** `deliver [GAPOWR]/[GAPILE]: skipped,
this one was DELIVERED not built` appears 7×, and there are **0** `ABORTED at depth`
lines — the unbounded delivery chain is gone.

**But every delivery double-fires.** Each building's deliver hook runs twice,
back-to-back, and delivers *both* times, so a delivery yields 2× its free units —
which reads in-game as "still chaining." Evidence (consecutive log lines):
`GAWEAP: 2 delivered` on 2393 **and** 2394; `GAPILE: 4 delivered` on 739/740;
`GAREFN: 1 delivered` on 1589/1590.

**Leading hypothesis.** The deliver hook (`0x446AE3`) returns `PadAircraftBlock`
(`0x446EE2`), jumping over the vanilla instruction that sets the once-only
"FreeUnits_Done" marker. Antares' guard at `0x446AAF` sits above us and only
*checks* that marker; if the *set* lives in the block we skip, a second
`Place`/`Grand_Opening` on the same building is unguarded and delivers again.

**Fix options.**
1. Set the once-only marker (`[ebp+0x300]` / FreeUnits-done field) inside the
   deliver hook before returning, so a second `Place` is a no-op. Preferred —
   idempotent across however many times `Place` fires, same principle as §8.
2. If setting the engine flag is impractical, keep a private per-building
   "already delivered its list" `set<BuildingClass*>`, checked on hook entry and
   cleared in the existing `0x6F4500` dtor hook.

**Also verify (possible second, independent gap).** `GAWEAP`/`GAREFN` deliver
while `GAPOWR`/`GAPILE` are skipped. Confirm whether the former are player-built
(legitimate) or delivered-chain buildings; if delivered, check that `place()`
actually `Mark`s them (Kind::Building) and that their `FreeUnit.OnlyBuilt` is set,
otherwise the guard has a per-type hole separate from the double-fire.

*Diagnosis only — no code change made for the double-fire; it touches the same
Grand_Opening hook logic under active development.*
