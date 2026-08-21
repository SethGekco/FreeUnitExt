#include "FreeUnitExt.h"

#include <Phobos.h>
#include <Syringe.h>
#include <Utilities/Patch.h>
#include <Utilities/Debug.h>
#include <Utilities/Macro.h>

HANDLE FreeUnitExtDLL::hInstance = nullptr;

char FreeUnitExtDLL::readBuffer[FreeUnitExtDLL::readLength];
wchar_t FreeUnitExtDLL::wideBuffer[FreeUnitExtDLL::readLength];

void FreeUnitExtDLL::ExeRun()
{
    Patch::ApplyStatic();

    // We deliberately sit BELOW the Ares-lineage hooks inside Grand_Opening
    // rather than replacing them:
    //   0x446AAF  Antares  SkipFreeUnits    — the once-only guard we rely on
    //   0x446EE2  Antares  InitialPayload   — must run before our pad delivery
    // Without Antares the DLL still works, but free units are no longer
    // protected against being handed out twice if a building re-opens, and
    // InitialPayload does not exist. Say so once rather than let it surprise.
    if (!GetModuleHandleA("Antares.dll") && !GetModuleHandleA("Ares.dll"))
    {
        Debug::Log("[FreeUnitExt] Note: no Antares.dll/Ares.dll loaded. Deliveries "
                   "still work, but the once-per-building guard normally provided "
                   "at 0x446AAF is absent. Loading Antares is recommended.\n");
    }
}

bool __stdcall DllMain(HANDLE hInstance, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        FreeUnitExtDLL::hInstance = hInstance;
        Phobos::hInstance = hInstance; // needed by Patch::ApplyStatic
    }
    return true;
}

SYRINGE_HANDSHAKE(pInfo)
{
    pInfo->Message = const_cast<char*>("FreeUnitExt");
    return S_OK;
}

// Game main-loop start: apply static patches at the right time.
DEFINE_HOOK(0x7CD810, ExeRun, 0x9)
{
    FreeUnitExtDLL::ExeRun();
    return 0;
}

// Flush deferred debug log after command-line parse.
DEFINE_HOOK(0x52F639, CmdLineParse, 0x5)
{
    Debug::LogDeferredFinalize();
    return 0;
}
