/*
 * FreeUnitExt — TechnoType data + the LoadFromINI hook that fills it.
 */
#include "Body.h"

#include <CCINIClass.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

PointerStore<ManualFacingData> TechnoTypeExt::Store;

ManualFacingData const* TechnoTypeExt::Find(TechnoTypeClass const* pType)
{
    return pType ? Store.TryGet(pType) : nullptr;
}

void TechnoTypeExt::LoadFromINI(TechnoTypeClass* pType, CCINIClass* pINI)
{
    if (!pType || !pINI)
        return;

    const char* section = pType->ID;
    if (!pINI->GetSection(section))
        return;

    // Read against the currently-stored values so a later INI in the chain
    // (game mode, scenario, map) overrides without wiping what rules set.
    // Peek first so an untouched type does not get an entry.
    auto const existing = Store.TryGet(pType);
    const bool wasEnabled = existing ? existing->Enabled : false;
    const int  wasROT = existing ? existing->ROT : -1;

    const bool wasTurret = existing ? existing->Turret : false;

    const bool enabled = pINI->ReadBool(section, "ManualFacing", wasEnabled);
    const int  rot = pINI->ReadInteger(section, "ManualFacing.ROT", wasROT);
    const bool turret = pINI->ReadBool(section, "ManualFacing.Turret", wasTurret);

    if (!enabled && rot < 0 && !turret)
        return;   // nothing of ours set on this type

    auto& data = Store.ForKey(pType);
    data.Enabled = enabled;
    data.ROT = rot;
    data.Turret = turret;

    Debug::Log("[FreeUnitExt] [%s] ManualFacing=%s ROT=%d Turret=%s\n",
        section, enabled ? "yes" : "no", rot, turret ? "yes" : "no");
}

// =============================================================================
// LoadFromINI hook.
//
// TechnoTypeClass::LoadFromINI is the shared tail every concrete leaf type runs,
// so one site covers Units, Infantry, Aircraft and Buildings.
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
