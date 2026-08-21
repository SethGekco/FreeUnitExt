/*
 * FreeUnitExt — manual facing for immobile units.
 *
 * A vehicle with Speed=0 cannot go anywhere, so a move order is currently just
 * discarded. With ManualFacing=yes the order is reinterpreted: the unit turns
 * its body to look at the cell you clicked, the way you would aim a turret.
 *
 * Seam: EventClass::Execute, MegaMission dispatch @ 0x4C7462.
 *
 * This is the network event path, which matters more than it looks — the order
 * has already been serialised and replayed identically on every client by the
 * time we see it, so reinterpreting it here cannot desync. Doing the same work
 * in the input/click code would run only on the clicking player's machine.
 *
 * Phobos also hooks 0x4C7462 (EventClass_Execute_MegaMission_MoveCommand). It
 * bails immediately for non-Units and, for Units, only acts on KeepTargetOnMove
 * types; we only act on Speed=0 + ManualFacing types. The two sets are disjoint
 * in practice, and we return 0 whenever we decline, so the chain is preserved
 * in whichever order Syringe calls them. See HOOKS_LOG.md.
 */
#include <Ext/TechnoType/Body.h>

#include <EventClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <UnitClass.h>
#include <Helpers/Cast.h>
#include <Utilities/Macro.h>

DEFINE_HOOK(0x4C7462, FreeUnitExt_EventClass_Execute_ManualFacing, 0x5)
{
    enum { SkipGameCode = 0x4C74C0 };

    GET(TechnoClass*, pTechno, EDI);
    GET(EventClass*, pThis, ESI);

    if (static_cast<Mission>(pThis->MegaMission.Mission) != Mission::Move)
        return 0;

    auto const pType = pTechno->GetTechnoType();
    if (!pType || pType->Speed != 0)
        return 0;

    auto const pData = TechnoTypeExt::Find(pType);
    if (!pData || !pData->Enabled)
        return 0;

    auto const pDestination = pThis->MegaMission.Destination.As_Abstract();
    if (!pDestination)
        return 0;

    // An explicit turn rate lets an otherwise ROT=0 emplacement swing slowly
    // instead of snapping. Applied before SetDesired, which reads ROT to decide
    // whether to animate the turn at all.
    // SetROT, not a raw DirStruct assignment: ROT is stored as a BAM and
    // SetROT does the DirType->BAM scaling for us, matching how the engine and
    // Antares set it (Antares src/Ext/Techno/Body.cpp:676).
    if (pData->ROT >= 0)
        pTechno->PrimaryFacing.SetROT(pData->ROT);

    pTechno->PrimaryFacing.SetDesired(pTechno->GetTargetDirection(pDestination));

    // Swallow the order: without this the engine assigns Mission::Move and the
    // unit spends the rest of its life "moving" to a cell it can never reach,
    // which blocks it from acquiring targets.
    return SkipGameCode;
}
