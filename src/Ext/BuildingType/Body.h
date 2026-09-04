#pragma once
/*
 * FreeUnitExt — per-BuildingType delivery data.
 *
 * Two lists, one engine:
 *   FreeUnits   — what the building brings with it when it finishes.
 *                 Generalises vanilla FreeUnit= (one VehicleType, one unit).
 *   PadAircraft — what lands on the numbered docks when this building is not
 *                 separate-aircraft. Generalises Rules->PadAircraft[0].
 *
 * The planning model itself is engine-free and unit-tested off-target; see
 * src/Delivery/Plan.h and tests/plan_test.cpp.
 */
#include <Delivery/Plan.h>
#include <Ext/Store.h>

#include <BuildingTypeClass.h>
#include <TechnoTypeClass.h>

#include <string>
#include <vector>

class BuildingClass;
class CCINIClass;
class HouseClass;
class TeamTypeClass;
class FootClass;

// Buildings this DLL delivered, so their own Grand_Opening can tell "delivered"
// from "built". A depth counter does NOT work here: Grand_Opening is DEFERRED,
// not called inside our Unlimbo, so by the time it runs the delivery that
// created the building has long since returned. Identity is the only signal
// that survives the gap.
namespace DeliveredBuildings
{
    void Mark(const void* pBuilding);

    // NON-consuming, deliberately. A delivered building stays flagged for its
    // whole life, so OnlyBuilt skips it no matter how many times Grand_Opening /
    // Place fires — and it fires an unpredictable number of times (skirmish vs
    // campaign discovery differ, houses re-discover, limbo re-entry, etc.). The
    // old consume-on-claim design leaked exactly here: once the mark was used up
    // a delivered structure was mistaken for a built one and ran its FreeUnit
    // list, reproducing its free units. The flag is cleared on destruction
    // (Unmark, from the techno dtor hook) so a reused address never inherits it.
    bool WasDelivered(const void* pBuilding);

    // Drop the flag when the building dies. Called from the TechnoClass dtor
    // hook; erasing an absent key is a harmless no-op.
    void Unmark(const void* pBuilding);
}

// What FreeUnit.Team= / FreeUnit.Script= named for one entry, kept as text
// until delivery time. Empty strings mean "not set".
struct TeamRef
{
    std::string Team;
    std::string Script;
};

// One parsed delivery list. Entries[i].TypeIndex indexes into Types, which keeps
// Delivery::Plan.h free of engine types while the adapter still gets a pointer.
struct DeliveryList
{
    std::vector<TechnoTypeClass*> Types;
    std::vector<Delivery::Entry>  Entries;

    // Parallel to Entries via Entry::TeamIndex.
    //
    // Holds the NAMES, not resolved pointers. ScriptTypes and TeamTypes come
    // from aimd.ini, which the engine reads AFTER rulesmd.ini — so at
    // BuildingTypeClass::LoadFromINI time both arrays are still empty and any
    // lookup returns null. Resolving here produced "unknown ScriptType" for a
    // script that plainly existed. Names are resolved lazily at delivery, by
    // which point Grand_Opening is running and the AI data is long loaded.
    std::vector<TeamRef>          Teams;

    bool empty() const { return this->Entries.empty(); }
    void clear() { this->Types.clear(); this->Entries.clear(); this->Teams.clear(); }
};

struct BuildingTypeData
{
    // FreeUnit= and FreeUnit.Buildings= are stored SEPARATELY rather than
    // appended into one list at parse time. The engine runs LoadFromINI once per
    // INI in the chain, so appending would duplicate the neighbours on every
    // pass that did not also re-clear FreeUnits (i.e. any INI that sets
    // FreeUnit.Buildings= without also setting FreeUnit=).
    DeliveryList FreeUnits;
    DeliveryList Neighbours;

    DeliveryList PadAircraft;

    // Per-building override of [General]SeparateAircraft. Unset means "use the
    // global", which is what every unmodified building does.
    bool SeparateAircraft_Set = false;
    bool SeparateAircraft = false;

    // Only deliver when this building was genuinely BUILT — not when it was
    // itself delivered by another building's FreeUnit list.
    //
    // Without this, [NAPOWR]FreeUnit.Buildings=NAPOWR is an infinite chain: the
    // delivered plant's Grand_Opening fires during our own Unlimbo and delivers
    // another, forever. Named to match Host.OnlyBuilt= in the GiftBox/Host DLL.
    // Defaults to YES because the runaway case is catastrophic and the
    // recursive case has no known use.
    bool OnlyBuilt = true;

    bool HasDelivery() const
    {
        return !this->FreeUnits.empty() || !this->Neighbours.empty();
    }

    bool IsVanilla() const
    {
        return !this->HasDelivery()
            && this->PadAircraft.empty()
            && !this->SeparateAircraft_Set;
    }

    // The single ordered list the planner actually resolves: units first, then
    // neighbouring buildings. Built per delivery, which happens once when a
    // building finishes — not on any hot path.
    DeliveryList Combined() const
    {
        DeliveryList out = this->FreeUnits;

        for (std::size_t i = 0; i < this->Neighbours.Entries.size(); ++i)
        {
            auto entry = this->Neighbours.Entries[i];
            auto const& src = this->Neighbours.Entries[i];

            entry.TypeIndex = int(out.Types.size());
            out.Types.push_back(this->Neighbours.Types[std::size_t(src.TypeIndex)]);

            // Teams are a separate parallel array, so the index needs remapping
            // too — otherwise a neighbour would inherit whichever team happened
            // to sit at its old index in the other list.
            if (src.TeamIndex >= 0
                && std::size_t(src.TeamIndex) < this->Neighbours.Teams.size())
            {
                entry.TeamIndex = int(out.Teams.size());
                out.Teams.push_back(this->Neighbours.Teams[std::size_t(src.TeamIndex)]);
            }
            else
            {
                entry.TeamIndex = -1;
            }

            out.Entries.push_back(entry);
        }

        return out;
    }
};

class BuildingTypeExt
{
public:
    static PointerStore<BuildingTypeData> Store;

    // Called from the BuildingTypeClass::LoadFromINI hook, once per type per
    // INI file the engine reads (rules, game mode, scenario, map).
    static void LoadFromINI(BuildingTypeClass* pType, CCINIClass* pINI);

    static BuildingTypeData const* Find(BuildingTypeClass const* pType)
    {
        return pType ? Store.TryGet(pType) : nullptr;
    }

    // Does this building deliver free aircraft onto its docks?
    // Per-building SeparateAircraft= wins over [General]SeparateAircraft=.
    static bool DeliversPadAircraft(BuildingTypeClass* pType);
};

// -----------------------------------------------------------------------------
// GameMap — the engine adapter behind Delivery::IPlacement.
//
// This is the ONLY place the planner touches the game.
// -----------------------------------------------------------------------------
class GameMap final : public Delivery::IPlacement
{
public:
    GameMap(BuildingClass* pParent, DeliveryList const& list)
        : Parent(pParent), List(list)
    { }

    // FreeUnit.Owner= -> a real house. Random* variants use the synced RNG.
    HouseClass* ResolveOwner(Delivery::OwnerKind kind) const;

    // Put a delivered unit on a team so its script runs. No-op when the entry
    // has no team. Failure is non-fatal: the unit keeps its mission.
    void AttachTeam(Delivery::Entry const& entry, FootClass* pFoot,
        HouseClass* pOwner) const;

    bool canPlace(Delivery::Entry const& entry, Delivery::Offset offset) const override;
    bool place(Delivery::Entry const& entry, Delivery::Offset offset, int facing) override;
    bool placeLimbo(Delivery::Entry const& entry) override;
    int  randomRanged(int low, int high) override;

private:
    TeamRef const* TeamOf(Delivery::Entry const& entry) const
    {
        return entry.TeamIndex >= 0 && std::size_t(entry.TeamIndex) < this->List.Teams.size()
            ? &this->List.Teams[std::size_t(entry.TeamIndex)]
            : nullptr;
    }

    TechnoTypeClass* TypeOf(Delivery::Entry const& entry) const
    {
        return entry.TypeIndex >= 0 && std::size_t(entry.TypeIndex) < this->List.Types.size()
            ? this->List.Types[std::size_t(entry.TypeIndex)]
            : nullptr;
    }

    BuildingClass* Parent;
    DeliveryList const& List;
};
