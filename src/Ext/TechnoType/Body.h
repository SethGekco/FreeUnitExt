#pragma once
/*
 * FreeUnitExt — per-TechnoType data for the bundled "manual facing" feature.
 *
 * An immobile unit (Speed=0) that still lets the player aim its body by
 * right-clicking a direction, the way a turret aims. On its own that is far too
 * small to justify a DLL, which is why it ships here.
 *
 * Stored per concrete type array, because ArrayIndex is per-array — a
 * UnitTypeClass and an InfantryTypeClass can both be index 3.
 */
#include <Ext/Store.h>

#include <TechnoTypeClass.h>

class CCINIClass;

struct ManualFacingData
{
    // Opt-in. Without it, every Speed=0 vehicle in every mod would silently
    // change how it answers a move order.
    bool Enabled = false;

    // When >= 0, the body turns at this rate instead of the type's ROT.
    int ROT = -1;
};

class TechnoTypeExt
{
public:
    // Called from each concrete type's LoadFromINI hook.
    static void LoadFromINI(TechnoTypeClass* pType, CCINIClass* pINI);

    static ManualFacingData const* Find(TechnoTypeClass const* pType);

private:
    // One store per array: Unit, Infantry, Aircraft, Building.
    static IndexedStore<ManualFacingData> UnitStore;
    static IndexedStore<ManualFacingData> InfantryStore;
    static IndexedStore<ManualFacingData> AircraftStore;
    static IndexedStore<ManualFacingData> BuildingStore;

    static IndexedStore<ManualFacingData>* StoreFor(TechnoTypeClass const* pType);
};
