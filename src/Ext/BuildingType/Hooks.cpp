/*
 * FreeUnitExt — BuildingTypeClass INI hook.
 *
 * Only one site is needed. Because our data is ArrayIndex-keyed and lives for
 * the process (see src/Ext/Store.h), there is no container to allocate on
 * construction, nothing to free on destruction, and nothing to serialise into a
 * savegame.
 *
 * Address and register layout from Phobos src/Ext/BuildingType/Body.cpp
 * (develop). We return 0, so we chain with Phobos' hook at the same site.
 */
#include "Body.h"

#include <BuildingTypeClass.h>
#include <CCINIClass.h>
#include <Utilities/Macro.h>

// EBP = BuildingTypeClass*, [ESP+0x364] = CCINIClass* (deep stack frame).
//
// This is the end of BuildingTypeClass::LoadFromINI, after every native field —
// inherited and own alike — has been parsed, so NumberOfDocks and Helipad are
// already populated when we read them.
DEFINE_HOOK(0x464A49, FreeUnitExt_BuildingTypeClass_LoadFromINI, 0xA)
{
    GET(BuildingTypeClass*, pItem, EBP);
    GET_STACK(CCINIClass*, pINI, 0x364);

    BuildingTypeExt::LoadFromINI(pItem, pINI);
    return 0;
}
