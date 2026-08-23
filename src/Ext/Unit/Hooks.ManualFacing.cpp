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
#include <GeneralDefinitions.h>
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
        return 0;

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


    // Swallow the order: without this the engine assigns Mission::Move and an
    // immobile unit spends the rest of its life "moving" to a cell it can never
    // reach, which blocks it from acquiring targets.
    return SkipGameCode;
}

/*
 * =============================================================================
 * Make the click reachable — UnitClass::What_Action @ 0x740801, size 0x5
 *
 *   740801  8b 44 24 30     mov eax, [esp+0x30]   ; the decided Action
 *   740805  5f              pop edi
 *
 * The delivery hook above was firing but only ever saw `mission 16` (Unload,
 * i.e. deploy) — right-clicking a CELL with an immobile unit produced no event
 * at all. The order is suppressed before it is ever sent: What_Action (a.k.a.
 * MouseOverCell, 0x7404B0) decides what a click means, and for a unit that
 * cannot move it never resolves to Action::Move, so no MegaMission is queued
 * and there is nothing downstream to reinterpret.
 *
 * `0x740801` is the function's single final return: it loads the decided Action
 * out of [ESP+0x30] into EAX and falls into the epilogue. Overriding it to
 * Action::Move makes the click both LOOK actionable (move cursor) and actually
 * dispatch, which is what feeds the 0x4C7462 hook.
 *
 * ESI = UnitClass*.
 *
 * ⚠ WE WRITE THE STACK SLOT, NOT EAX, AND RETURN 0.
 *
 * The obvious-looking version — set EAX and `return 0x740805` to land on the
 * `pop edi` — CRASHES. A size-5 hook here patches 0x740801..0x740805 inclusive,
 * so 0x740805 is the LAST BYTE OF SYRINGE'S OWN JMP. Jumping there executes a
 * fragment of the patch as an instruction and control lands in nowhere:
 * observed live as `Exception code: C0000005 at 00005280` with EAX=00000001,
 * that EAX being the Action::Move this hook had just written.
 *
 * Returning 0 re-executes the stolen `mov eax, [esp+0x30]`, which is exactly
 * the load we want — so putting the value in the slot lets the original
 * instruction do the work and no jump into the patched range is needed.
 *
 * General rule: never return an address inside your own hook's stolen-byte
 * range. The safe targets are the hook address itself (via 0) or something at
 * or past address+size.
 *
 * Interaction with Phobos' DisallowMoving (0x740709 / 0x740744): those hooks
 * force Action::NoMove for units it considers immobile, and one of their exits
 * (`ReturnResult`) jumps straight to 0x740801 — so our hook still runs and gets
 * the last word, which is the behaviour we want for a type that explicitly asked
 * for ManualFacing. Their other exits (0x740769 / 0x7407D2) return NoMove
 * directly and bypass us; a type that is BOTH Phobos-DisallowMoving and
 * ManualFacing may therefore still be suppressed. Untested combination.
 * =============================================================================
 */
DEFINE_HOOK(0x740801, FreeUnitExt_UnitClass_WhatAction_ManualFacing, 0x5)
{
    GET(UnitClass*, pThis, ESI);

    if (!pThis)
        return 0;

    auto const pType = pThis->GetTechnoType();
    if (!pType)
        return 0;

    auto const pData = TechnoTypeExt::Find(pType);
    if (!pData || !pData->Enabled)
        return 0;

    REF_STACK(Action, decided, 0x30);


    // Only rewrite the "you cannot go there" answers. Attack, Enter, Capture,
    // Select and friends must keep working normally — ManualFacing is about
    // reclaiming the otherwise-dead move click, not about hijacking every order.
    if (decided != Action::NoMove && decided != Action::None)
        return 0;

    decided = Action::Move;
    return 0;   // stolen `mov eax,[esp+0x30]` now loads Move for us
}
