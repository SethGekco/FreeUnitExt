/*
 * FreeUnitExt — BuildingClass::Grand_Opening (0x4468A0-ish, ret 4) hooks.
 *
 * This is the one function where a finished building hands out its free stuff.
 * It contains two consecutive vanilla blocks:
 *
 *   0x446AA9 .. 0x446EE1   FreeUnit   — one UnitType, one unit, NearByLocation
 *   0x446EE2 .. 0x446FB5   PadAircraft — one aircraft, Rules->PadAircraft[0]
 *   0x446FB6               epilogue
 *
 * We do NOT fight anyone for the entry points. Both blocks are already occupied:
 *   0x446AAF  Antares/Ares  BuildingClass_Place_SkipFreeUnits (the once-only guard)
 *   0x446BF4  Phobos        NearByLocation fix
 *   0x446D42  Phobos        NearByLocation fix 2
 *   0x446E9F  Antares       free-unit mission fix
 *   0x446EE2  Antares/Ares  InitialPayload  <- returns 0, must keep running
 *   0x446F57  Phobos        aircraft pose-dir context
 *
 * So we take three sites nobody holds, each chosen to sit AFTER the guards we
 * want to inherit rather than re-implement:
 *
 *   0x446AB5  gate    — let the block run when only OUR list is populated
 *   0x446B16  deliver — replace the spawn once every vanilla guard has passed
 *   0x446EE8  pads    — replace the aircraft spawn, after Antares' payload hook
 *
 * See HOOKS_LOG.md for the full derivation and the register layouts.
 */
#include <Ext/BuildingType/Body.h>

#include <AircraftClass.h>
#include <AircraftTypeClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <HouseClass.h>
#include <RulesClass.h>
#include <UnitTypeClass.h>
#include <Unsorted.h>
#include <algorithm>

#include <Helpers/Cast.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

namespace
{
    // Addresses inside Grand_Opening that we jump to.
    enum
    {
        ContinueFreeUnitGuards = 0x446ABD,   // next vanilla guard after the null test
        PadAircraftBlock       = 0x446EE2,   // start of the aircraft block (Antares hooks it)
        GrandOpeningEpilogue   = 0x446FB6,   // pop edi/esi/ebp/ebx; add esp,0x58; ret 4
    };

    /*
     * Last-resort stop for a runaway chain.
     *
     * NOTE: depth alone does NOT identify a delivered building. Grand_Opening is
     * DEFERRED — it runs frames after the Unlimbo that created the building, by
     * which time depth is back to 0. That is why FreeUnit.OnlyBuilt= is enforced
     * by identity (DeliveredBuildings) and not here. This counter only catches a
     * genuinely re-entrant delivery, so the game cannot hang outright.
     */
    int DeliveryDepth = 0;
    constexpr int MaxDeliveryDepth = 4;

    struct DeliveryScope
    {
        DeliveryScope() { ++DeliveryDepth; }
        ~DeliveryScope() { --DeliveryDepth; }
    };

    /*
     * Put one aircraft on one numbered pad.
     *
     * This is vanilla 0x446F16..0x446FB0 generalised from "the first entry of
     * Rules->PadAircraft, once" to "this type, on this dock". The ScenarioInit
     * bracket, the Guard mission and the two radio commands are all copied from
     * vanilla — the handshake is what actually attaches the aircraft to the pad,
     * so skipping it would give a helicopter that never rearms.
     */
    void DeliverAircraft(BuildingClass* pBuilding, AircraftTypeClass* pType,
        int dock, int facing)
    {
        auto const pOwner = pBuilding->Owner;

        // Vanilla brackets the whole spawn, not just the allocation (0x446F21
        // increments, 0x446FB0 decrements), because Unlimbo would otherwise
        // refuse a cell that is occupied by the building itself.
        ++Unsorted::ScenarioInit;

        if (auto const pAircraft = static_cast<AircraftClass*>(pType->CreateObject(pOwner)))
        {
            // Land on the declared dock offset when the building has one, so a
            // four-pad structure does not stack all four aircraft on its centre.
            auto coords = pBuilding->GetCoords();

            auto const& offsets = pBuilding->Type->DockingOffsets;
            if (dock >= 0 && dock < offsets.Capacity)
            {
                auto const& offset = offsets[dock];
                coords.X += offset.X;
                coords.Y += offset.Y;
                coords.Z += offset.Z;
            }

            // Without an explicit facing, fall back to the same global default
            // the engine's PoseDir uses.
            const auto dir = Delivery::isConcreteDir(facing)
                ? static_cast<DirType>(facing)
                : static_cast<DirType>(RulesClass::Instance->PoseDir);

            if (pAircraft->Unlimbo(coords, dir))
            {
                pAircraft->QueueMission(Mission::Guard, false);

                pAircraft->SendCommand(RadioCommand::RequestLink, pBuilding);
                pBuilding->SendCommand(RadioCommand::RequestTether, pAircraft);
            }
            else
            {
                pAircraft->UnInit();
            }
        }

        --Unsorted::ScenarioInit;
    }
}

// =============================================================================
// Gate — 0x446AB5, size 0x8
//
//   446ab5  85 c0              test eax, eax          ; eax = pType->FreeUnit
//   446ab7  0f 84 25 04 00 00  je   0x446ee2
//
// Vanilla parses FreeUnit= as a single UnitTypeClass*, so `FreeUnit=GGI` (an
// infantry type) or `FreeUnit=A,B` leaves that pointer NULL and the whole block
// is skipped before our delivery hook could ever run. We re-implement the two
// swallowed instructions and additionally let the block through when our own
// parsed list has something in it.
// =============================================================================
DEFINE_HOOK(0x446AB5, BuildingClass_GrandOpening_FreeUnitGate, 0x8)
{
    GET(BuildingClass*, pThis, EBP);
    GET(UnitTypeClass*, pVanillaFreeUnit, EAX);

    if (pVanillaFreeUnit)
        return ContinueFreeUnitGuards;

    auto const pData = BuildingTypeExt::Find(pThis->Type);
    const bool ours = pData && pData->HasDelivery();


    Debug::Log("[FreeUnitExt] gate [%s]: vanilla FreeUnit=null, ours=%s\n",
        pThis->Type->ID, ours ? "yes" : "NO DATA");

    return ours ? ContinueFreeUnitGuards : PadAircraftBlock;
}

// =============================================================================
// Deliver — 0x446B16, size 0x7
//
//   446b16  8b 45 00     mov eax, [ebp]        ; pThis->vtable
//   446b19  8d 4c 24 1c  lea ecx, [esp+0x1c]
//
// Everything above this point is vanilla's guard chain, and we deliberately sit
// below it so we inherit all of it for free:
//   - Antares' once-only FreeUnits_Done guard (0x446AAF)
//   - ScenarioInit suppression        (0x446ABD)
//   - the `captured` argument         (0x446ACA) — captured buildings give nothing
//   - the 0xA8ED6B global             (0x446AD6)
//   - the human-player / upgrade-level check (0x446AE3 .. 0x446B10)
//
// EBP = BuildingClass* pThis.
//
// Returning PadAircraftBlock skips vanilla's NearByLocation spawn entirely,
// which also skips Phobos' two fixes at 0x446BF4/0x446D42 and Antares' mission
// fix at 0x446E9F. That is intended — our planner does its own ring search and
// sets Harvest/Area_Guard itself — but it does mean those three become dead
// code whenever a building uses our keys. Recorded in HOOKS_LOG.md.
// =============================================================================
DEFINE_HOOK(0x446B16, BuildingClass_GrandOpening_Deliver, 0x7)
{
    GET(BuildingClass*, pThis, EBP);

    auto const pData = BuildingTypeExt::Find(pThis->Type);
    if (!pData || !pData->HasDelivery())
    {
        Debug::Log("[FreeUnitExt] deliver [%s]: no data, falling through to vanilla\n",
            pThis->Type->ID);
        return 0;   // vanilla FreeUnit= only — leave the engine (and Phobos) alone
    }

    // Identity, not depth: Grand_Opening is deferred, so a delivered building
    // opens long after the delivery that created it has returned. The mark is
    // consumed here whether or not OnlyBuilt is set, so it can never go stale.
    const bool wasDelivered = DeliveredBuildings::ClaimWasDelivered(pThis);

    if (wasDelivered && pData->OnlyBuilt)
    {
        Debug::Log("[FreeUnitExt] deliver [%s]: skipped, this one was DELIVERED "
            "not built (FreeUnit.OnlyBuilt=yes)\n", pThis->Type->ID);
        return PadAircraftBlock;
    }

    if (DeliveryDepth >= MaxDeliveryDepth)
    {
        Debug::Log("[FreeUnitExt] deliver [%s]: ABORTED at depth %d — runaway "
            "delivery chain. Set FreeUnit.OnlyBuilt=yes on it.\n",
            pThis->Type->ID, DeliveryDepth);
        return PadAircraftBlock;
    }

    DeliveryScope scope;

    // Units first, then neighbouring buildings, in one ordered list so spacing
    // is tracked across both.
    auto const list = pData->Combined();

    GameMap map(pThis, list);

    // How far the parent's own footprint reaches from its centre cell. Without
    // this the search starts one cell from the centre, which is still INSIDE
    // anything bigger than 1x1 — units then spawn under the building and are
    // never seen. Deliberately rounded up: a unit appearing one cell further
    // out than ideal is vastly better than an invisible one.
    auto const pType = pThis->Type;
    const int foundation = (std::max)(
        int(pType->GetFoundationWidth()),
        int(pType->GetFoundationHeight(false)));
    const int parentRadius = foundation / 2 + 1;

    auto const result = Delivery::resolve(
        list.Entries,
        map,
        int(pThis->PrimaryFacing.Current().GetDir()),
        parentRadius);

    Debug::Log("[FreeUnitExt] deliver [%s]: %d delivered, %d failed (of %u), "
        "foundation %d -> parentRadius %d\n",
        pType->ID, result.Delivered, result.Failed,
        unsigned(list.Entries.size()), foundation, parentRadius);

    return PadAircraftBlock;
}

// =============================================================================
// Pads — 0x446EE8, size 0x6
//
//   446ee8  8a 81 e8 17 00 00  mov al, [ecx+0x17e8]   ; ecx = Rules, +0x17e8 = SeparateAircraft
//
// We sit one instruction PAST 0x446EE2 on purpose: that address belongs to
// Antares' InitialPayload hook, which returns 0 and must keep running. By the
// time we execute, the payload has been delivered and ECX already holds Rules.
//
// Vanilla from here is: bail if Rules->SeparateAircraft; bail if !Type->Helipad;
// bail if `captured`; then create exactly ONE aircraft from PadAircraft[0].
// We replace all of that with a per-building decision and a per-dock list.
//
// [ESP+0x6C] is the `captured` argument (frame 0x68 + 4); ESP is still at the
// frame base here, no pushes have happened since the block started.
// =============================================================================
DEFINE_HOOK(0x446EE8, BuildingClass_GrandOpening_PadAircraft, 0x6)
{
    GET(BuildingClass*, pThis, EBP);
    GET_STACK(bool, captured, 0x6C);

    auto const pType = pThis->Type;
    auto const pData = BuildingTypeExt::Find(pType);

    // Nothing of ours to say: let the vanilla block decide exactly as before.
    if (!pData || (!pData->SeparateAircraft_Set && pData->PadAircraft.empty()))
    {
        Debug::Log("[FreeUnitExt] pads [%s]: no data, falling through to vanilla\n",
            pType->ID);
        return 0;
    }

    Debug::Log("[FreeUnitExt] pads [%s]: captured=%d delivers=%d types=%u docks=%d\n",
        pType->ID, int(captured), int(BuildingTypeExt::DeliversPadAircraft(pType)),
        unsigned(pData->PadAircraft.Entries.size()), pType->NumberOfDocks);

    if (captured || !BuildingTypeExt::DeliversPadAircraft(pType))
        return GrandOpeningEpilogue;

    // No explicit list -> reproduce vanilla's choice (PadAircraft[0]) so that a
    // bare per-building `SeparateAircraft=no` behaves the way a modder expects.
    if (pData->PadAircraft.empty())
    {
        auto const& padTypes = RulesClass::Instance->PadAircraft;
        if (padTypes.Count <= 0)
            return GrandOpeningEpilogue;

        DeliverAircraft(pThis, padTypes[0], 0, Delivery::Dir_Unset);
        return GrandOpeningEpilogue;
    }

    const int docks = pType->NumberOfDocks;

    for (size_t i = 0; i < pData->PadAircraft.Entries.size(); ++i)
    {
        auto const& entry = pData->PadAircraft.Entries[i];

        auto const pAircraftType = abstract_cast<AircraftTypeClass*>(
            pData->PadAircraft.Types[std::size_t(entry.TypeIndex)]);

        if (!pAircraftType)
            continue;   // not an AircraftType; the parser logged it at load time

        // One aircraft per numbered pad; a longer list than the building has
        // docks would produce aircraft with nowhere to return to.
        if (docks > 0 && int(i) >= docks)
            break;

        DeliverAircraft(pThis, pAircraftType, int(i), entry.Facing);
    }

    return GrandOpeningEpilogue;
}
