/*
 * FreeUnitExt — TechnoType data + the LoadFromINI hooks that fill it.
 */
#include "Body.h"

#include <AircraftTypeClass.h>
#include <BuildingTypeClass.h>
#include <CCINIClass.h>
#include <InfantryTypeClass.h>
#include <UnitTypeClass.h>
#include <Utilities/Macro.h>

IndexedStore<ManualFacingData> TechnoTypeExt::UnitStore;
IndexedStore<ManualFacingData> TechnoTypeExt::InfantryStore;
IndexedStore<ManualFacingData> TechnoTypeExt::AircraftStore;
IndexedStore<ManualFacingData> TechnoTypeExt::BuildingStore;

IndexedStore<ManualFacingData>* TechnoTypeExt::StoreFor(TechnoTypeClass const* pType)
{
    if (!pType)
        return nullptr;

    switch (pType->WhatAmI())
    {
    case AbstractType::UnitType:      return &UnitStore;
    case AbstractType::InfantryType:  return &InfantryStore;
    case AbstractType::AircraftType:  return &AircraftStore;
    case AbstractType::BuildingType:  return &BuildingStore;
    default:                          return nullptr;
    }
}

ManualFacingData const* TechnoTypeExt::Find(TechnoTypeClass const* pType)
{
    auto const pStore = StoreFor(pType);
    return pStore ? pStore->TryGet(pType->ArrayIndex) : nullptr;
}

void TechnoTypeExt::LoadFromINI(TechnoTypeClass* pType, CCINIClass* pINI)
{
    if (!pType || !pINI)
        return;

    auto const pStore = StoreFor(pType);
    if (!pStore)
        return;

    const char* section = pType->ID;
    if (!pINI->GetSection(section))
        return;

    auto& data = pStore->ForIndex(pType->ArrayIndex);

    // Defaults are the currently-stored values, so a later INI in the chain
    // (game mode, scenario, map) overrides without wiping what rules set.
    data.Enabled = pINI->ReadBool(section, "ManualFacing", data.Enabled);
    data.ROT = pINI->ReadInteger(section, "ManualFacing.ROT", data.ROT);
}

// =============================================================================
// LoadFromINI hook.
//
// TechnoTypeClass::LoadFromINI is the shared tail every concrete leaf type runs,
// so one site covers Units, Infantry, Aircraft and Buildings. `StoreFor` then
// routes to the right per-array store, because ArrayIndex is per-array — a
// UnitTypeClass and an InfantryTypeClass can both be index 3.
//
// EBP = TechnoTypeClass*, [ESP+0x380] = CCINIClass*. Address and layout from
// Phobos src/Ext/TechnoType/Body.cpp; we return 0 so we chain with its hook.
// =============================================================================
DEFINE_HOOK(0x716123, FreeUnitExt_TechnoTypeClass_LoadFromINI, 0x5)
{
    GET(TechnoTypeClass*, pItem, EBP);
    GET_STACK(CCINIClass*, pINI, 0x380);

    TechnoTypeExt::LoadFromINI(pItem, pINI);
    return 0;
}
