#pragma once
/*
 * FreeUnitExt — per-TechnoType data for the bundled "manual facing" feature.
 *
 * An immobile unit (Speed=0) that still lets the player aim its body by
 * right-clicking a direction, the way a turret aims. On its own that is far too
 * small to justify a DLL, which is why it ships here.
 *
 * Keyed by type pointer — see src/Ext/Store.h for why not ArrayIndex.
 */
#include <Ext/Store.h>

#include <TechnoTypeClass.h>

class CCINIClass;

struct ManualFacingData
{
    // Opt-in. Without it, every Speed=0 vehicle in every mod would silently
    // change how it answers a move order.
    bool Enabled = false;

    // When >= 0, turn at this rate instead of the type's ROT.
    int ROT = -1;

    // Aim the turret (SecondaryFacing) instead of the hull (PrimaryFacing).
    bool Turret = false;
};

class TechnoTypeExt
{
public:
    // Called from each concrete type's LoadFromINI hook.
    static void LoadFromINI(TechnoTypeClass* pType, CCINIClass* pINI);

    static ManualFacingData const* Find(TechnoTypeClass const* pType);

private:
    static PointerStore<ManualFacingData> Store;

};
