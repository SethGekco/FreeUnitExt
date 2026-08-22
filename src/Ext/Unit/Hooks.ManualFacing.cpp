/*
 * FreeUnitExt — manual facing.
 *
 * A vehicle with Speed=0 cannot go anywhere, so a move order is normally just
 * discarded. With ManualFacing=yes the order is reinterpreted: the unit turns to
 * look at the cell you clicked, the way you would aim a turret.
 *
 * ManualFacing.Turret= chooses WHICH facing is aimed — the hull (PrimaryFacing,
 * the default) or the turret (SecondaryFacing).
 *
 * Seam: EventClass::Execute, MegaMission dispatch @ 0x4C7462.
 *
 * This is the network event path, which matters more than it looks — the order
 * has already been serialised and replayed identically on every client by the
 * time we see it, so reinterpreting it cannot desync. Doing the same work in the
 * input/click code would run only on the clicking player's machine.
 *
 * Phobos also hooks 0x4C7462 (EventClass_Execute_MegaMission_MoveCommand). It
 * bails immediately for non-Units and, for Units, only acts on KeepTargetOnMove
 * types; we act only on ManualFacing types. We return 0 whenever we decline, so
 * the chain is preserved in whichever order Syringe calls them.
 */
#include <Ext/TechnoType/Body.h>

#include <EventClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <UnitClass.h>
#include <Helpers/Cast.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

DEFINE_HOOK(0x4C7462, FreeUnitExt_EventClass_Execute_ManualFacing, 0x5)
{
    enum { SkipGameCode = 0x4C74C0 };

    GET(TechnoClass*, pTechno, EDI);
    GET(EventClass*, pThis, ESI);

    if (!pTechno)
        return 0;

    auto const pType = pTechno->GetTechnoType();
    if (!pType)
        return 0;

    auto const pData = TechnoTypeExt::Find(pType);
    if (!pData || !pData->Enabled)
        return 0;

    // Everything below only runs for a type that explicitly opted in, so it is
    // safe to be loud: if ManualFacing looks inert, this line says why.
    auto const mission = static_cast<Mission>(pThis->MegaMission.Mission);

    if (mission != Mission::Move)
    {
        Debug::Log("[FreeUnitExt] ManualFacing [%s]: event mission is %d, not Move(2)"
            " — ignoring\n", pType->ID, int(mission));
        return 0;
    }

    auto const pDestination = pThis->MegaMission.Destination.As_Abstract();
    if (!pDestination)
    {
        Debug::Log("[FreeUnitExt] ManualFacing [%s]: Move event carried no "
            "resolvable destination\n", pType->ID);
        return 0;
    }

    /*
     * NOTE: there is deliberately NO `Speed == 0` gate here.
     *
     * It used to check `pType->Speed != 0` and bail. That is a silent failure
     * waiting to happen — it depends on YRpp's TechnoTypeClass layout putting
     * Speed exactly where the engine does, and if it does not, the feature
     * simply never fires with nothing to show for it. `ManualFacing=yes` is
     * already an explicit opt-in by the modder; a second implicit condition
     * buys no safety and adds a failure mode.
     *
     * Consequence worth knowing: setting ManualFacing on a unit that CAN move
     * makes it stop moving. That is what the flag asks for.
     */

    // Hull by default; the turret when asked for and the type actually has one.
    const bool wantTurret = pData->Turret;
    const bool hasTurret = pType->Turret || pType->TurretCount > 0;
    const bool useTurret = wantTurret && hasTurret;

    if (wantTurret && !hasTurret)
    {
        Debug::Log("[FreeUnitExt] ManualFacing [%s]: ManualFacing.Turret=yes but the "
            "type has no turret — aiming the hull instead\n", pType->ID);
    }

    auto& facing = useTurret ? pTechno->SecondaryFacing : pTechno->PrimaryFacing;

    // SetROT, not a raw DirStruct assignment: ROT is stored as a BAM and SetROT
    // does the DirType->BAM scaling, matching the engine and Antares
    // (Antares src/Ext/Techno/Body.cpp:676). Matters most when the type has
    // ROT=0, where the facing would otherwise snap instantly.
    if (pData->ROT >= 0)
        facing.SetROT(pData->ROT);

    auto const target = pTechno->GetTargetDirection(pDestination);
    facing.SetDesired(target);

    Debug::Log("[FreeUnitExt] ManualFacing [%s]: aiming %s at raw %u (ROT %d)\n",
        pType->ID, useTurret ? "turret" : "hull",
        unsigned(target.Raw), pData->ROT);

    // Swallow the order: without this the engine assigns Mission::Move and an
    // immobile unit spends the rest of its life "moving" to a cell it can never
    // reach, which blocks it from acquiring targets.
    return SkipGameCode;
}
