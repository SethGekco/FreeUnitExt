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
#include <MissionClass.h>
#include <ScriptTypeClass.h>
#include <TaskForceClass.h>
#include <TeamClass.h>
#include <TeamTypeClass.h>
#include <RulesClass.h>
#include <ScenarioClass.h>
#include <SessionClass.h>
#include <UnitTypeClass.h>
#include <Unsorted.h>
#include <Helpers/Cast.h>
#include <Utilities/Debug.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>
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

    bool WasDelivered(const void* pBuilding)
    {
        // Non-consuming: the flag must survive every Grand_Opening/Place this
        // building ever gets, not just the first. Cleared by Unmark on death.
        return Marks.count(pBuilding) > 0;
    }

    void Unmark(const void* pBuilding)
    {
        Marks.erase(pBuilding);
    }
}

// =============================================================================
// Parsing helpers
// =============================================================================
namespace
{
    /*
     * Split a comma list, trimming only each token's OUTER whitespace.
     *
     * Internal spaces are SIGNIFICANT. This originally stripped every space,
     * which is harmless for type IDs and direction names but silently destroys
     * the engine's own mission names — "Area Guard", "Paradrop Approach",
     * "Spyplane Overfly" — turning them into unmatchable garbage that would
     * have looked like FreeUnit.Mission= simply not working.
     */
    std::vector<std::string> SplitList(const char* buffer)
    {
        std::vector<std::string> out;

        auto flush = [&out](std::string token)
        {
            const auto first = token.find_first_not_of(" \t");
            if (first == std::string::npos)
                return;                       // all whitespace: not a token
            const auto last = token.find_last_not_of(" \t");
            out.push_back(token.substr(first, last - first + 1));
        };

        std::string cur;
        for (const char* p = buffer; p && *p; ++p)
        {
            if (*p == ',')
            {
                flush(cur);
                cur.clear();
            }
            else
            {
                cur += *p;
            }
        }
        flush(cur);
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
     * Mission names, resolved by the ENGINE's own table.
     *
     * MissionControlClass::FindIndex is the same lookup the game uses for
     * [General]/mission INI keys, so every name ModEnc documents works here for
     * free and stays correct if a mod redefines the table. Hand-rolling a
     * name->enum map would drift from the engine the first time anyone touched
     * Mission Control.
     */
    std::vector<int> ReadMissions(CCINIClass* pINI, const char* section,
        const char* key, size_t count)
    {
        std::vector<int> out(count, Delivery::Mission_Unset);

        auto const tokens = ReadList(pINI, section, key);
        if (tokens.empty())
            return out;

        auto resolve = [&](std::string const& token) -> int
        {
            const auto mission = MissionControlClass::FindIndex(token.c_str());
            if (mission == Mission::None)
            {
                Debug::Log("[FreeUnitExt] [%s]%s: '%s' is not a known mission "
                    "(see Mission Control) — leaving the default\n",
                    section, key, token.c_str());
                return Delivery::Mission_Unset;
            }
            return int(mission);
        };

        if (tokens.size() == 1)
        {
            const int m = resolve(tokens[0]);
            for (auto& slot : out)
                slot = m;
            return out;
        }

        for (size_t i = 0; i < tokens.size() && i < count; ++i)
            out[i] = resolve(tokens[i]);

        return out;
    }

    std::vector<Delivery::OwnerKind> ReadOwners(CCINIClass* pINI, const char* section,
        const char* key, size_t count)
    {
        std::vector<Delivery::OwnerKind> out(count, Delivery::OwnerKind::Invoker);

        auto const tokens = ReadList(pINI, section, key);
        if (tokens.empty())
            return out;

        auto resolve = [&](std::string const& token)
        {
            bool ok = false;
            auto const kind = Delivery::parseOwner(token.c_str(), &ok);
            if (!ok)
            {
                Debug::Log("[FreeUnitExt] [%s]%s: '%s' is not a known owner "
                    "(Invoker/Civilian/Special/Neutral/Random/RandomAlly/"
                    "RandomEnemy) — using Invoker\n", section, key, token.c_str());
            }
            return kind;
        };

        if (tokens.size() == 1)
        {
            auto const kind = resolve(tokens[0]);
            for (auto& slot : out)
                slot = kind;
            return out;
        }

        for (size_t i = 0; i < tokens.size() && i < count; ++i)
            out[i] = resolve(tokens[i]);

        return out;
    }

    /*
     * Resolve FreeUnit.Team= / FreeUnit.Script= to a TeamTypeClass.
     *
     * A ScriptType cannot be attached to a unit: the engine only ever runs a
     * script through a TeamClass, which comes from a TeamType. So a named team
     * is used directly, and a named script gets a minimal TeamType synthesised
     * around it — cached, so N deliveries of the same script share one type
     * rather than leaking a TeamType per unit.
     *
     * ScriptTypeClass::Find, NOT FindOrAllocate: FindOrAllocate would silently
     * manufacture an empty script for a typo, which then does nothing forever.
     * A miss should be a log line, not a phantom script.
     *
     * Called at DELIVERY time (see AttachTeam), so this allocates a TeamType
     * mid-game. That is sync-safe because every client runs the same building's
     * Grand_Opening on the same frame and the cache makes it happen exactly
     * once. It is NOT known to survive a save/load — see TESTING.md §5.
     */
    TeamTypeClass* SynthesiseTeamFor(ScriptTypeClass* pScript, TechnoTypeClass* pType)
    {
        static std::map<std::pair<ScriptTypeClass*, TechnoTypeClass*>,
            TeamTypeClass*> cache;

        // A null type would just relocate the same null deref into the task
        // force, so refuse rather than build a team that cannot survive.
        if (!pScript || !pType)
            return nullptr;

        auto const cacheKey = std::make_pair(pScript, pType);

        auto const it = cache.find(cacheKey);
        if (it != cache.end())
            return it->second;

        // A REAL TaskForce, not nullptr.
        //
        // TaskForce=nullptr crashed at 0x6EA6B4, inside the team's
        // member-consideration path (Phobos hooks 0x6EA6BE there): creating a
        // team walks its TaskForce entries unconditionally, so a null one is an
        // immediate access violation the moment CreateTeam runs. Resolving at
        // delivery time is what makes the honest version possible — by then we
        // know the exact TechnoType, so the task force can describe precisely
        // the one unit we are about to hand it.
        char tfId[0x18] = {};
        std::snprintf(tfId, sizeof(tfId), "FUXT%.17s", pScript->ID);

        auto const pTaskForce = GameCreate<TaskForceClass>(tfId);
        if (!pTaskForce)
            return nullptr;

        pTaskForce->Group = -1;
        pTaskForce->IsGlobal = false;
        pTaskForce->CountEntries = 1;
        pTaskForce->Entries[0].Amount = 1;
        pTaskForce->Entries[0].Type = pType;

        // The ID only has to be unique and recognisable in a crash dump.
        char id[0x18] = {};
        std::snprintf(id, sizeof(id), "FUX_%.18s", pScript->ID);

        auto const pTeam = GameCreate<TeamTypeClass>(id);
        if (!pTeam)
            return nullptr;

        pTeam->ScriptType = pScript;
        pTeam->TaskForce = pTaskForce;
        pTeam->Group = -1;
        pTeam->Max = 1;
        pTeam->Priority = 5;
        pTeam->TechLevel = 0;
        pTeam->VeteranLevel = 1;
        pTeam->Autocreate = false;    // must never be picked up by the AI's own team logic
        pTeam->Prebuild = false;
        pTeam->Reinforce = false;
        pTeam->Recruiter = false;
        pTeam->Loadable = false;

        cache[cacheKey] = pTeam;
        return pTeam;
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
        auto const missions = ReadMissions(pINI, section, key(".Mission").c_str(), ids.size());
        auto const owners   = ReadOwners(pINI, section, key(".Owner").c_str(), ids.size());
        auto const teamIds   = ReadList(pINI, section, key(".Team").c_str());
        auto const scriptIds = ReadList(pINI, section, key(".Script").c_str());

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
            entry.Mission = missions[src];
            entry.Owner = owners[src];

            // Team wins over Script when both name something for the same entry:
            // a TeamType already carries a script, so honouring both would mean
            // silently discarding one. A single value broadcasts, like the other
            // parallel keys.
            auto pick = [&](std::vector<std::string> const& list) -> const char*
            {
                if (list.empty())
                    return nullptr;
                if (list.size() == 1)
                    return list[0].c_str();
                return src < list.size() ? list[src].c_str() : nullptr;
            };

            // NOTE: the names are STORED, not resolved. ScriptTypes and
            // TeamTypes live in aimd.ini, which the engine reads after
            // rulesmd.ini — at this point both arrays are still empty, so
            // resolving here reported "unknown ScriptType" for scripts that
            // plainly existed. Resolution happens at delivery time instead.
            TeamRef ref;

            if (auto const teamId = pick(teamIds))
                ref.Team = teamId;

            if (auto const scriptId = pick(scriptIds))
                ref.Script = scriptId;

            // Team wins over Script; a TeamType already carries a script, so
            // honouring both would silently discard one.
            if (!ref.Team.empty() && !ref.Script.empty())
            {
                Debug::Log("[FreeUnitExt] [%s]%s: both .Team and .Script are set; "
                    "the team wins (it already carries a script)\n", section, prefix);
                ref.Script.clear();
            }

            if (!ref.Team.empty() || !ref.Script.empty())
            {
                entry.TeamIndex = int(out.Teams.size());
                out.Teams.push_back(ref);
            }

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

/*
 * Resolve FreeUnit.Owner= to an actual house.
 *
 * Every Random* variant draws from the SYNCED scenario RNG, because ownership
 * decides who shoots whom — two clients disagreeing would desync the match, not
 * merely look odd. Falling back to the invoker on any miss is deliberate: a
 * match with no neutral house should still deliver the unit to somebody rather
 * than drop it or crash.
 */
HouseClass* GameMap::ResolveOwner(Delivery::OwnerKind kind) const
{
    auto const pInvoker = this->Parent->Owner;

    switch (kind)
    {
    case Delivery::OwnerKind::Invoker:
        return pInvoker;

    case Delivery::OwnerKind::Civilian:
        if (auto const pHouse = HouseClass::FindCivilianSide())
            return pHouse;
        break;

    case Delivery::OwnerKind::Special:
        if (auto const pHouse = HouseClass::FindSpecial())
            return pHouse;
        break;

    case Delivery::OwnerKind::Neutral:
        if (auto const pHouse = HouseClass::FindNeutral())
            return pHouse;
        break;

    case Delivery::OwnerKind::Random:
    case Delivery::OwnerKind::RandomAlly:
    case Delivery::OwnerKind::RandomEnemy:
    {
        std::vector<HouseClass*> pool;
        for (auto const pHouse : HouseClass::Array)
        {
            if (!pHouse || pHouse->Defeated)
                continue;

            switch (kind)
            {
            case Delivery::OwnerKind::Random:
                pool.push_back(pHouse);
                break;
            case Delivery::OwnerKind::RandomAlly:
                // Allies only, and never the invoker itself — "give it to a
                // friend" means somebody else, otherwise this is just Invoker.
                if (pHouse != pInvoker && pInvoker->IsAlliedWith(pHouse))
                    pool.push_back(pHouse);
                break;
            default:  // RandomEnemy
                if (pHouse != pInvoker && !pInvoker->IsAlliedWith(pHouse)
                    && !pHouse->IsNeutral())
                    pool.push_back(pHouse);
                break;
            }
        }

        if (!pool.empty())
        {
            const int pick = ScenarioClass::Instance->Random.RandomRanged(
                0, int(pool.size()) - 1);
            return pool[std::size_t(pick)];
        }
        break;
    }
    }

    return pInvoker;
}

/*
 * Put a delivered unit onto a team so its script actually runs.
 *
 * Every failure here is deliberately non-fatal. The unit already exists on the
 * map with a mission; losing its script is a degraded outcome, but destroying
 * or leaking it would be worse, and a modder chasing "my script did not run"
 * is far better served by a log line than by a missing unit.
 */
void GameMap::AttachTeam(Delivery::Entry const& entry, FootClass* pFoot,
    HouseClass* pOwner) const
{
    auto const pRef = this->TeamOf(entry);
    if (!pRef || !pFoot || !pOwner)
        return;

    // Resolve HERE, not at parse time. See DeliveryList::Teams: aimd.ini has
    // definitely loaded by the time a building finishes, so this is the first
    // moment either lookup can succeed.
    TeamTypeClass* pTeamType = nullptr;

    if (!pRef->Team.empty())
    {
        pTeamType = TeamTypeClass::Find(pRef->Team.c_str());
        if (!pTeamType)
        {
            Debug::Log("[FreeUnitExt]   unknown TeamType '%s'\n",
                pRef->Team.c_str());
        }
    }
    else if (!pRef->Script.empty())
    {
        if (auto const pScript = ScriptTypeClass::Find(pRef->Script.c_str()))
        {
            pTeamType = SynthesiseTeamFor(pScript, this->TypeOf(entry));
        }
        else
        {
            Debug::Log("[FreeUnitExt]   unknown ScriptType '%s' (%d script(s) "
                "loaded)\n", pRef->Script.c_str(), ScriptTypeClass::Array.Count);
        }
    }

    if (!pTeamType)
        return;

    auto const pTeam = pTeamType->CreateTeam(pOwner);
    if (!pTeam)
    {
        Debug::Log("[FreeUnitExt]   could not create a team from '%s'\n",
            pTeamType->ID);
        return;
    }

    if (!pTeam->AddMember(pFoot, true))
    {
        Debug::Log("[FreeUnitExt]   team '%s' refused the delivered unit\n",
            pTeamType->ID);
    }
}

bool GameMap::place(Delivery::Entry const& entry, Delivery::Offset offset, int facing)
{
    auto const pType = this->TypeOf(entry);
    if (!pType)
        return false;

    auto const pOwner = this->ResolveOwner(entry.Owner);

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
    // MARK BEFORE UNLIMBO, not after.
    //
    // For a BuildingType, Unlimbo runs Grand_Opening synchronously — the very
    // function this DLL hooks. Marking afterwards means the delivered building
    // executes its own FreeUnit list while still looking "built", so
    // [GAPOWR]FreeUnit.Buildings=GAPOWR chained until the depth-4 abort caught
    // it. (Observed: 4 plants, then "ABORTED at depth 4".)
    //
    // Marking a techno that then fails to Unlimbo is harmless: the object is
    // destroyed immediately below, and Unmark is called from the dtor hook.
    if (entry.What == Delivery::Kind::Building)
        DeliveredBuildings::Mark(pObject);

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



    // A free unit with nothing to do should guard its birthplace. Harvesters
    // are the one type with a better default — the same distinction Antares
    // makes at 0x446E9F.
    //
    // Guard vs Area_Guard is NOT ours to force. Area_Guard pursues targets
    // within a radius; Guard holds position and only fires at what comes to it.
    // The engine already exposes that choice as DefaultToGuardArea=, so honour
    // it rather than hardcoding the chasing variant — otherwise a modder who set
    // DefaultToGuardArea=no would find their delivered units chasing anyway,
    // with no way to stop it short of not using this DLL.
    if (auto const pFoot = abstract_cast<FootClass*>(pObject))
    {
        const bool harvester = pType->WhatAmI() == AbstractType::UnitType
            && static_cast<UnitTypeClass*>(pType)->Harvester;

        // An explicit FreeUnit.Mission= wins outright. It is the modder saying
        // what this unit is for, which beats any default we could infer.
        const auto mission = entry.Mission != Delivery::Mission_Unset
            ? static_cast<Mission>(entry.Mission)
            : (harvester
                ? Mission::Harvest
                : (pType->DefaultToGuardArea ? Mission::Area_Guard : Mission::Guard));

        pFoot->QueueMission(mission, false);

        // After the mission, not before: the team's script takes the unit over
        // from here, and the queued mission is only what it falls back to if
        // the team is ever disbanded.
        this->AttachTeam(entry, pFoot, pOwner);
    }

    return true;
}

bool GameMap::placeLimbo(Delivery::Entry const& entry)
{
    auto const pType = this->TypeOf(entry);
    if (!pType || pType->WhatAmI() != AbstractType::BuildingType)
        return false;

    auto const pBuildingType = static_cast<BuildingTypeClass*>(pType);
    auto const pOwner = this->ResolveOwner(entry.Owner);

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

    // MUST be marked BEFORE DiscoveredBy.
    //
    // DiscoveredBy calls BuildingClass::Place — which IS Grand_Opening, the
    // function this DLL hooks — synchronously, right here. Without the mark, a
    // limbo-delivered building immediately runs its own delivery: a limbo
    // GAPILE would spawn its four free GIs onto the map from a structure that
    // is supposed to be invisible. Unlike the on-map path, where Grand_Opening
    // is deferred, this one is re-entrant within our own call.
    //
    // The mark is now persistent (WasDelivered does not consume it), so a single
    // Mark covers every Grand_Opening this building will ever get — the two
    // DiscoveredBy calls below and any later re-discovery. No re-mark needed.
    DeliveredBuildings::Mark(pBuilding);

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
