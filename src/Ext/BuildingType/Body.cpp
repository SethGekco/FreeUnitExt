/*
 * FreeUnitExt — BuildingTypeClass extension implementation.
 *
 * Two halves:
 *   1. INI parsing of the delivery lists (see INI_REFERENCE.md).
 *   2. GameMap — the engine adapter that Delivery::resolve() drives.
 *
 * The adapter is deliberately the only engine-aware code in the delivery path.
 * Everything above it is pure and tested off-target.
 */
#include "Body.h"

#include <AircraftTypeClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <CCINIClass.h>
#include <CellClass.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <InfantryTypeClass.h>
#include <MapClass.h>
#include <RulesClass.h>
#include <ScenarioClass.h>
#include <SessionClass.h>
#include <UnitTypeClass.h>
#include <Unsorted.h>
#include <Helpers/Cast.h>
#include <Utilities/Debug.h>

#include <cstdlib>
#include <unordered_set>
#include <string>

PointerStore<BuildingTypeData> BuildingTypeExt::Store;

namespace DeliveredBuildings
{
    namespace { std::unordered_set<const void*> Marks; }

    void Mark(const void* pBuilding)
    {
        if (pBuilding)
            Marks.insert(pBuilding);
    }

    bool ClaimWasDelivered(const void* pBuilding)
    {
        // Erase on claim so the mark cannot outlive the delivery it describes.
        // A delivered building destroyed before it ever opens leaves a stale
        // entry; the worst case is one future building reusing that address
        // skipping its delivery once. Bounded, and far cheaper than a full
        // BuildingClass instance extension.
        return Marks.erase(pBuilding) > 0;
    }
}

// =============================================================================
// Parsing helpers
// =============================================================================
namespace
{
    std::vector<std::string> SplitList(const char* buffer)
    {
        std::vector<std::string> out;
        std::string cur;
        for (const char* p = buffer; p && *p; ++p)
        {
            if (*p == ',')
            {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            }
            else if (*p != ' ' && *p != '\t')
            {
                cur += *p;
            }
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    std::vector<std::string> ReadList(CCINIClass* pINI, const char* section, const char* key)
    {
        char buffer[2048] = {};
        if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
            return {};
        return SplitList(buffer);
    }

    // Find a TechnoType by ID across all four type arrays. Order matters only
    // for the (pathological) case of an ID reused between arrays; Infantry and
    // Units are checked first because those are what FreeUnit= is for.
    TechnoTypeClass* FindTechnoType(const char* id)
    {
        if (auto const pType = UnitTypeClass::Find(id))
            return pType;
        if (auto const pType = InfantryTypeClass::Find(id))
            return pType;
        if (auto const pType = AircraftTypeClass::Find(id))
            return pType;
        if (auto const pType = BuildingTypeClass::Find(id))
            return pType;
        return nullptr;
    }

    Delivery::Kind KindOf(TechnoTypeClass* pType)
    {
        return pType->WhatAmI() == AbstractType::BuildingType
            ? Delivery::Kind::Building
            : Delivery::Kind::Foot;
    }

    /*
     * Read a parallel list of directions.
     *
     * A single value is broadcast to every entry, which is what modders expect
     * from `FreeUnit.Facing=N` on a four-unit list. A list shorter than the
     * type list leaves the tail unset rather than erroring — an unset direction
     * is meaningful (inherit / ring scan), not a mistake.
     */
    std::vector<int> ReadDirections(CCINIClass* pINI, const char* section,
        const char* key, size_t count)
    {
        std::vector<int> out(count, Delivery::Dir_Unset);

        auto const tokens = ReadList(pINI, section, key);
        if (tokens.empty())
            return out;

        if (tokens.size() == 1)
        {
            const int dir = Delivery::parseDirection(tokens[0].c_str());
            if (dir == Delivery::Dir_Unset)
            {
                Debug::Log("[FreeUnitExt] [%s]%s: '%s' is not a direction "
                    "(expected N/NE/E/SE/S/SW/W/NW, 'random', or 0-255)\n",
                    section, key, tokens[0].c_str());
                return out;
            }
            for (auto& slot : out)
                slot = dir;
            return out;
        }

        for (size_t i = 0; i < tokens.size() && i < count; ++i)
        {
            const int dir = Delivery::parseDirection(tokens[i].c_str());
            if (dir == Delivery::Dir_Unset && tokens[i] != "-")
            {
                Debug::Log("[FreeUnitExt] [%s]%s: entry %u ('%s') is not a direction\n",
                    section, key, unsigned(i), tokens[i].c_str());
            }
            out[i] = dir;
        }

        if (tokens.size() > count)
        {
            Debug::Log("[FreeUnitExt] [%s]%s: %u values for %u entries; extras ignored\n",
                section, key, unsigned(tokens.size()), unsigned(count));
        }

        return out;
    }

    // Same broadcast rule for a parallel int list.
    std::vector<int> ReadInts(CCINIClass* pINI, const char* section,
        const char* key, size_t count, int fallback)
    {
        std::vector<int> out(count, fallback);

        auto const tokens = ReadList(pINI, section, key);
        if (tokens.empty())
            return out;

        if (tokens.size() == 1)
        {
            const int value = std::atoi(tokens[0].c_str());
            for (auto& slot : out)
                slot = value;
            return out;
        }

        for (size_t i = 0; i < tokens.size() && i < count; ++i)
            out[i] = std::atoi(tokens[i].c_str());

        return out;
    }

    std::vector<bool> ReadBools(CCINIClass* pINI, const char* section,
        const char* key, size_t count)
    {
        std::vector<bool> out(count, false);

        auto const tokens = ReadList(pINI, section, key);
        if (tokens.empty())
            return out;

        auto parse = [](std::string const& token)
        {
            return !token.empty()
                && (token[0] == 'y' || token[0] == 'Y'
                 || token[0] == 't' || token[0] == 'T'
                 || token[0] == '1');
        };

        if (tokens.size() == 1)
        {
            const bool value = parse(tokens[0]);
            for (size_t i = 0; i < count; ++i)
                out[i] = value;
            return out;
        }

        for (size_t i = 0; i < tokens.size() && i < count; ++i)
            out[i] = parse(tokens[i]);

        return out;
    }

    /*
     * Parse one delivery list: the type list plus its parallel modifier keys.
     *
     * `prefix` is the key stem ("FreeUnit" or "SeparateAircraft"), so the two
     * lists share one parser and one documented key shape.
     */
    void ParseDeliveryList(DeliveryList& out, CCINIClass* pINI,
        const char* section, const char* typeKey, const char* prefix)
    {
        // The engine reads rules, then game mode, then scenario, then map INI
        // into the SAME type object, and an absent key must leave the previous
        // value alone. Bailing before clear() is what makes a map INI that does
        // not mention FreeUnit= keep the one from rulesmd.ini.
        auto const ids = ReadList(pINI, section, typeKey);
        if (ids.empty())
            return;

        // The type key IS present in this INI, so this file owns the whole list
        // — modifiers included.
        out.clear();

        // Resolve types first; a bad ID drops that slot entirely, and the
        // parallel keys are indexed against the SURVIVING list so a typo does
        // not silently shift everyone's facing by one.
        std::vector<TechnoTypeClass*> types;
        std::vector<size_t> sourceIndex;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (auto const pType = FindTechnoType(ids[i].c_str()))
            {
                types.push_back(pType);
                sourceIndex.push_back(i);
            }
            else
            {
                Debug::Log("[FreeUnitExt] [%s]%s: unknown TechnoType '%s' — skipped\n",
                    section, typeKey, ids[i].c_str());
            }
        }

        if (types.empty())
            return;

        auto key = [prefix](const char* suffix)
        {
            return std::string(prefix) + suffix;
        };

        // Parallel keys are read against the ORIGINAL list length so the
        // modder's indices line up with what they wrote in the INI.
        auto const facings  = ReadDirections(pINI, section, key(".Facing").c_str(), ids.size());
        auto const cells    = ReadDirections(pINI, section, key(".Cell").c_str(), ids.size());
        auto const spacings = ReadInts(pINI, section, key(".Spacing").c_str(), ids.size(), 0);
        auto const ranges   = ReadInts(pINI, section, key(".Range").c_str(), ids.size(), 1);
        auto const limbos   = ReadBools(pINI, section, key(".Limbo").c_str(), ids.size());

        for (size_t i = 0; i < types.size(); ++i)
        {
            const size_t src = sourceIndex[i];

            Delivery::Entry entry;
            entry.TypeIndex = int(i);
            entry.What = limbos[src] ? Delivery::Kind::Limbo : KindOf(types[i]);
            entry.Facing = facings[src];
            entry.Cell = cells[src];
            entry.Spacing = spacings[src] < 0 ? 0 : spacings[src];
            entry.Range = ranges[src] < 1 ? 1 : ranges[src];

            if (entry.What == Delivery::Kind::Limbo
                && types[i]->WhatAmI() != AbstractType::BuildingType)
            {
                // Limbo only means anything for buildings — that is the whole
                // point of the feature (invisible prerequisite structures).
                Debug::Log("[FreeUnitExt] [%s]%s: '%s' is not a BuildingType, so "
                    "%s.Limbo has no meaning for it — delivering normally\n",
                    section, typeKey, types[i]->ID, prefix);
                entry.What = KindOf(types[i]);
            }

            out.Types.push_back(types[i]);
            out.Entries.push_back(entry);
        }
    }
}

// =============================================================================
// INI
// =============================================================================
void BuildingTypeExt::LoadFromINI(BuildingTypeClass* pThis, CCINIClass* pINI)
{
    if (!pThis || !pINI)
        return;

    const char* section = pThis->ID;

    if (!pINI->GetSection(section))
        return;

    auto& data = BuildingTypeExt::Store.ForKey(pThis);

    // --- FreeUnit ------------------------------------------------------------
    // Vanilla already parsed FreeUnit= into pThis->FreeUnit (a UnitTypeClass*,
    // first entry only). We re-read the raw key so a list works, and we take
    // over delivery entirely — see src/Ext/Building/Hooks.Place.cpp.
    ParseDeliveryList(data.FreeUnits, pINI, section, "FreeUnit", "FreeUnit");

    // Buildings that come along as neighbours are just more entries; giving them
    // their own key keeps the common case readable. They are kept in their own
    // list and merged at delivery time (BuildingTypeData::Combined) so that a
    // second pass over the INI chain cannot append them twice.
    ParseDeliveryList(data.Neighbours, pINI, section,
        "FreeUnit.Buildings", "FreeUnit.Buildings");

    // --- SeparateAircraft ----------------------------------------------------
    if (pINI->ReadBool(section, "SeparateAircraft", false)
        || !pINI->ReadBool(section, "SeparateAircraft", true))
    {
        // The key is present (both a true-default and false-default read agree
        // only when the key really exists), so this building overrides Rules.
        data.SeparateAircraft_Set = true;
        data.SeparateAircraft = pINI->ReadBool(section, "SeparateAircraft", false);
    }

    data.OnlyBuilt = pINI->ReadBool(section, "FreeUnit.OnlyBuilt", data.OnlyBuilt);

    ParseDeliveryList(data.PadAircraft, pINI, section,
        "SeparateAircraft.Types", "SeparateAircraft");

    // Dock indices: entry i lands on dock i. Anything past NumberOfDocks has
    // nowhere to sit, so warn rather than spawn aircraft that cannot land.
    const int docks = pThis->NumberOfDocks;
    for (size_t i = 0; i < data.PadAircraft.Entries.size(); ++i)
    {
        data.PadAircraft.Entries[i].Dock = int(i);

        // Only AircraftTypes can dock. Anything else is silently skipped at
        // delivery time, so say so here where the modder can act on it.
        auto const pEntryType = data.PadAircraft.Types[i];
        if (pEntryType->WhatAmI() != AbstractType::AircraftType)
        {
            Debug::Log("[FreeUnitExt] [%s]SeparateAircraft.Types: '%s' is not an "
                "AircraftType — it cannot use a dock and will be ignored\n",
                section, pEntryType->ID);
        }

        if (docks > 0 && int(i) >= docks)
        {
            Debug::Log("[FreeUnitExt] [%s]SeparateAircraft.Types: entry %u exceeds "
                "NumberOfDocks=%d; it will spawn but has no dock to return to\n",
                section, unsigned(i), docks);
        }
    }

    if (!data.IsVanilla())
    {
        // The failure this catches: if the store is empty at delivery time,
        // every hook falls through to vanilla and the DLL looks inert with no
        // error anywhere. One line per configured building makes that visible.
        Debug::Log("[FreeUnitExt] parsed [%s]: %u free unit(s), %u neighbour(s), "
            "%u pad aircraft, SeparateAircraft%s\n",
            section,
            unsigned(data.FreeUnits.Entries.size()),
            unsigned(data.Neighbours.Entries.size()),
            unsigned(data.PadAircraft.Entries.size()),
            data.SeparateAircraft_Set ? (data.SeparateAircraft ? "=yes" : "=no") : " unset");
    }

    if (!data.PadAircraft.empty() && docks <= 0)
    {
        Debug::Log("[FreeUnitExt] [%s]SeparateAircraft.Types is set but "
            "NumberOfDocks=0 — the aircraft will have nowhere to land\n", section);
    }
}

bool BuildingTypeExt::DeliversPadAircraft(BuildingTypeClass* pType)
{
    if (!pType)
        return false;

    auto const pData = BuildingTypeExt::Find(pType);

    // Per-building override beats [General]SeparateAircraft.
    const bool separate = (pData && pData->SeparateAircraft_Set)
        ? pData->SeparateAircraft
        : RulesClass::Instance->SeparateAircraft;

    if (separate)
        return false;

    // With an explicit Types list, Helipad= is no longer the gate — a modder
    // naming the aircraft has already said what they want. Without one we
    // reproduce vanilla exactly, which only ever fed helipads.
    if (pData && !pData->PadAircraft.empty())
        return true;

    return pType->Helipad;
}

// =============================================================================
// GameMap — engine adapter
// =============================================================================
namespace
{
    // Cell coordinates of the parent building's centre.
    CellStruct ParentCell(BuildingClass* pParent)
    {
        return CellClass::Coord2Cell(pParent->GetCoords());
    }
}

bool GameMap::canPlace(Delivery::Entry const& entry, Delivery::Offset offset) const
{
    auto const pType = this->TypeOf(entry);
    if (!pType)
        return false;

    CellStruct target = ParentCell(this->Parent);
    target.X = short(target.X + offset.X);
    target.Y = short(target.Y + offset.Y);

    auto const pCell = MapClass::Instance.TryGetCellAt(target);
    if (!pCell)
        return false;   // off-map

    if (entry.What == Delivery::Kind::Building)
    {
        auto const pBuildingType = static_cast<BuildingTypeClass*>(pType);

        // The whole foundation has to fit, not just the origin cell, or we get
        // buildings overlapping cliffs and each other. CanPlaceHere walks the
        // foundation for us.
        return pBuildingType->CanPlaceHere(&target, this->Parent->Owner);
    }

    // Feet: an ordinary movement clearance test, matching what vanilla's
    // NearByLocation was asking on our behalf.
    return pCell->IsClearToMove(
        pType->SpeedType,
        /*ignoreInfantry=*/false,
        /*ignoreVehicles=*/false,
        /*zone=*/-1,
        pType->MovementZone,
        /*level=*/-1,
        /*isBridge=*/false);
}

bool GameMap::place(Delivery::Entry const& entry, Delivery::Offset offset, int facing)
{
    auto const pType = this->TypeOf(entry);
    if (!pType)
        return false;

    auto const pOwner = this->Parent->Owner;

    CellStruct target = ParentCell(this->Parent);
    target.X = short(target.X + offset.X);
    target.Y = short(target.Y + offset.Y);

    // No ScenarioInit bracket anywhere in the foot path: vanilla's free-unit
    // block (0x446AA9-0x446EE1) touches the flag exactly zero times. Only the
    // AIRCRAFT block does (0x446F21/0x446FB0), because a pad aircraft is
    // supposed to sit on top of the building. Matching vanilla here removes the
    // last behavioural difference between our spawner and the engine's.
    auto const pObject = pType->CreateObject(pOwner);

    if (!pObject)
        return false;

    const auto coords = CellClass::Cell2Coord(target);
    const auto dir = static_cast<DirType>(((facing % 256) + 256) % 256);

    // NO ScenarioInit bracket here, deliberately.
    //
    // ScenarioInit makes Unlimbo skip its placement checks ("you can put Terror
    // Drones into trees"), so wrapping it made every placement 'succeed' —
    // including ones landing inside the parent building's own footprint, where
    // the unit is created, reported delivered, and hidden under the building
    // sprite. Vanilla's free-unit path does not set it either; only the
    // aircraft path does, because an aircraft is meant to sit on the building.
    // Letting Unlimbo refuse means place() returns false and the planner walks
    // on to the next candidate cell.
    const bool ok = pObject->Unlimbo(coords, dir);

    if (!ok)
    {
        // Creation succeeded but the cell rejected it; drop the object rather
        // than leaking a limboed techno into the global arrays.
        Debug::Log("[FreeUnitExt]   Unlimbo REFUSED %s at cell (%d,%d) offset (%+d,%+d)\n",
            pType->ID, int(target.X), int(target.Y), offset.X, offset.Y);
        pObject->UnInit();
        return false;
    }

    if (entry.What == Delivery::Kind::Building)
        DeliveredBuildings::Mark(pObject);

    auto const actual = CellClass::Coord2Cell(pObject->GetCoords());
    Debug::Log("[FreeUnitExt]   placed %s want cell (%d,%d) offset (%+d,%+d) facing %d "
        "| engine says cell (%d,%d) alive=%d onMap=%d inLimbo=%d\n",
        pType->ID, int(target.X), int(target.Y), offset.X, offset.Y, facing,
        int(actual.X), int(actual.Y),
        int(pObject->IsAlive), int(pObject->IsOnMap), int(pObject->InLimbo));

    Debug::Log("[FreeUnitExt]     owner=%s (idx %d) currentPlayer=%s (idx %d) "
        "whatAmI=%d footCast=%s\n",
        pOwner && pOwner->Type ? pOwner->Type->ID : "?",
        pOwner ? pOwner->ArrayIndex : -1,
        HouseClass::CurrentPlayer && HouseClass::CurrentPlayer->Type
            ? HouseClass::CurrentPlayer->Type->ID : "?",
        HouseClass::CurrentPlayer ? HouseClass::CurrentPlayer->ArrayIndex : -1,
        int(pObject->WhatAmI()),
        abstract_cast<FootClass*>(pObject) ? "ok" : "FAILED");

    // A free unit with nothing to do should guard its birthplace. Harvesters
    // are the one type with a better default — the same distinction Antares
    // makes at 0x446E9F.
    if (auto const pFoot = abstract_cast<FootClass*>(pObject))
    {
        const auto mission = pType->WhatAmI() == AbstractType::UnitType
            && static_cast<UnitTypeClass*>(pType)->Harvester
            ? Mission::Harvest
            : Mission::Area_Guard;

        pFoot->QueueMission(mission, false);
    }

    return true;
}

bool GameMap::placeLimbo(Delivery::Entry const& entry)
{
    auto const pType = this->TypeOf(entry);
    if (!pType || pType->WhatAmI() != AbstractType::BuildingType)
        return false;

    auto const pBuildingType = static_cast<BuildingTypeClass*>(pType);
    auto const pOwner = this->Parent->Owner;

    // BuildLimit is checked before creation, mirroring Phobos' LimboDelivery.
    if (pBuildingType->BuildLimit > 0)
    {
        int owned = pOwner->CountOwnedNow(pBuildingType);
        if (auto const pUndeploy = pBuildingType->UndeploysInto)
            owned += pOwner->CountOwnedNow(pUndeploy);

        if (owned >= pBuildingType->BuildLimit)
            return false;
    }

    auto const pBuilding = static_cast<BuildingClass*>(pBuildingType->CreateObject(pOwner));
    if (!pBuilding)
        return false;

    // The flag triple that makes a building count for prerequisites and power
    // while never existing on the map. All three are mandatory.
    pBuilding->InLimbo = false;
    pBuilding->IsAlive = true;
    pBuilding->IsOnMap = true;

    if (SessionClass::IsCampaign())
        pBuilding->DiscoveredBy(HouseClass::CurrentPlayer);

    pBuilding->DiscoveredBy(pOwner);

    pOwner->RegisterGain(pBuilding, false);
    pOwner->RecheckTechTree = true;
    pOwner->RecheckPower = true;
    pOwner->Buildings.AddItem(pBuilding);

    if (pBuildingType->ConstructionYard)
        pOwner->ConYards.AddItem(pBuilding);

    if (pBuildingType->SecretLab)
        pOwner->SecretLabs.AddItem(pBuilding);

    return true;
}

int GameMap::randomRanged(int low, int high)
{
    // MUST be the scenario RNG: this decides cell and facing, so an unsynced
    // source would desync multiplayer the moment two clients disagree.
    return ScenarioClass::Instance->Random.RandomRanged(low, high);
}
