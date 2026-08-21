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

/*
 * ArrayIndex is declared on each CONCRETE leaf type, not on TechnoTypeClass or
 * any shared base — so it cannot be read through a TechnoTypeClass*. Downcast
 * on WhatAmI() to reach it. Single inheritance throughout, so static_cast is
 * safe once WhatAmI has identified the leaf.
 */
int TechnoTypeExt::IndexOf(TechnoTypeClass const* pType)
{
    if (!pType)
        return -1;

    switch (pType->WhatAmI())
    {
    case AbstractType::UnitType:
        return static_cast<UnitTypeClass const*>(pType)->ArrayIndex;
    case AbstractType::InfantryType:
        return static_cast<InfantryTypeClass const*>(pType)->ArrayIndex;
    case AbstractType::AircraftType:
        return static_cast<AircraftTypeClass const*>(pType)->ArrayIndex;
    case AbstractType::BuildingType:
        return static_cast<BuildingTypeClass const*>(pType)->ArrayIndex;
    default:
        return -1;
    }
}

ManualFacingData const* TechnoTypeExt::Find(TechnoTypeClass const* pType)
{
    auto const pStore = StoreFor(pType);
    if (!pStore)
        return nullptr;

    const int index = IndexOf(pType);
    return index >= 0 ? pStore->TryGet(index) : nullptr;
}

void TechnoTypeExt::LoadFromINI(TechnoTypeClass* pType, CCINIClass* pINI)
{
    if (!pType || !pINI)
        return;

    auto const pStore = StoreFor(pType);
    if (!pStore)
        return;

    const int index = IndexOf(pType);
    if (index < 0)
        return;

    const char* section = pType->ID;
    if (!pINI->GetSection(section))
        return;

    auto& data = pStore->ForIndex(index);

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
