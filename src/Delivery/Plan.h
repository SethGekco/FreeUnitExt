#pragma once
/*
 * FreeUnitExt — the Delivery plan.
 *
 * This header is PURE: it contains no engine types and no game headers, so it
 * compiles on the host and is unit-tested off-target (tests/plan_test.cpp).
 * Everything that needs the engine (creating objects, checking cell occupancy,
 * unlimboing) lives behind the IPlacement interface and is implemented by the
 * engine adapter in src/Ext/Building/Hooks.Place.cpp.
 *
 * The ONE primitive is a Delivery Entry: a TechnoType plus where it goes, which
 * way it faces, and whether it arrives on the map or in limbo. FreeUnit=,
 * SeparateAircraft.Types= and the "comes with buildings" feature are all just
 * lists of entries resolved by the same engine.
 */
#include <vector>

namespace Delivery
{
    // ---------------------------------------------------------------------
    // Facing / placement direction
    //
    // The engine's DirType is a byte where 0 = North and the value increases
    // clockwise (64 = East, 128 = South, 192 = West). We keep the same raw
    // encoding here so the adapter can hand the value straight to DirStruct,
    // but we never include the engine header to get it.
    // ---------------------------------------------------------------------
    enum : int
    {
        Dir_N  = 0,
        Dir_NE = 32,
        Dir_E  = 64,
        Dir_SE = 96,
        Dir_S  = 128,
        Dir_SW = 160,
        Dir_W  = 192,
        Dir_NW = 224,
    };

    // Sentinels that occupy the same field as a raw direction byte. They are
    // out of the 0..255 range on purpose so a parsed direction can never
    // collide with one.
    enum : int
    {
        Dir_Unset  = -1,   // inherit: facing -> building's facing, cell -> ring scan
        Dir_Random = -2,   // pick one of the 8 compass points at spawn time
    };

    constexpr bool isConcreteDir(int dir) { return dir >= 0; }

    // Direction names in compass order, index i == raw byte i * 32.
    // Kept here (not in the parser) so the host tests can exercise parsing.
    inline int parseDirection(const char* token)
    {
        if (!token || !*token)
            return Dir_Unset;

        struct Named { const char* Name; int Dir; };
        static const Named names[] = {
            { "n",  Dir_N  }, { "north",     Dir_N  },
            { "ne", Dir_NE }, { "northeast", Dir_NE },
            { "e",  Dir_E  }, { "east",      Dir_E  },
            { "se", Dir_SE }, { "southeast", Dir_SE },
            { "s",  Dir_S  }, { "south",     Dir_S  },
            { "sw", Dir_SW }, { "southwest", Dir_SW },
            { "w",  Dir_W  }, { "west",      Dir_W  },
            { "nw", Dir_NW }, { "northwest", Dir_NW },
            { "random", Dir_Random },
            { "any",    Dir_Random },
        };

        // Case-insensitive compare without pulling in <cctype> locale behaviour.
        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
        auto equals = [&](const char* a, const char* b)
        {
            while (*a && *b)
            {
                if (lower(*a) != lower(*b))
                    return false;
                ++a; ++b;
            }
            return !*a && !*b;
        };

        for (auto const& named : names)
        {
            if (equals(token, named.Name))
                return named.Dir;
        }

        // A bare number is a raw DirType byte, letting modders aim between the
        // 8 compass points (the engine has 256 facings, ROT permitting).
        bool digits = true;
        int value = 0;
        for (const char* p = token; *p; ++p)
        {
            if (*p < '0' || *p > '9') { digits = false; break; }
            value = value * 10 + (*p - '0');
        }

        return (digits && value >= 0 && value <= 255) ? value : Dir_Unset;
    }

    // ---------------------------------------------------------------------
    // Entry
    // ---------------------------------------------------------------------
    enum class Kind
    {
        Foot,      // Infantry / Unit / Aircraft: needs a clear cell
        Building,  // needs a foundation that fits, placed in a ring around the parent
        Limbo,     // never touches the map; exists only to satisfy prerequisites
    };

    /*
     * Who owns a delivered object.
     *
     * `Invoker` — whoever caused the delivery (the building's owner). Every
     * other value hands the object to somebody else, which is how you build
     * neutral tech defenders, civilian bystanders, or a hostile "ambush"
     * garrison that spawns already belonging to an enemy.
     *
     * The Random* variants resolve through the SYNCED RNG in the adapter, never
     * a local one — an owner that differed between clients would desync
     * instantly and look like a physics bug.
     */
    enum class OwnerKind
    {
        Invoker,       // default: the delivering building's house
        Civilian,      // the civilian side
        Special,       // the "special" house
        Neutral,       // the neutral house
        Random,        // any house in the game
        RandomAlly,    // any house allied to the invoker (invoker excluded)
        RandomEnemy,   // any house not allied to the invoker
    };

    inline OwnerKind parseOwner(const char* token, bool* ok = nullptr)
    {
        struct Named { const char* Name; OwnerKind Kind; };
        static const Named names[] = {
            { "invoker",     OwnerKind::Invoker     },
            { "owner",       OwnerKind::Invoker     },
            { "civilian",    OwnerKind::Civilian    },
            { "special",     OwnerKind::Special     },
            { "neutral",     OwnerKind::Neutral     },
            { "random",      OwnerKind::Random      },
            { "randomally",  OwnerKind::RandomAlly  },
            { "randomenemy", OwnerKind::RandomEnemy },
        };

        auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
        auto equals = [&](const char* a, const char* b)
        {
            while (*a && *b)
            {
                if (lower(*a) != lower(*b))
                    return false;
                ++a; ++b;
            }
            return !*a && !*b;
        };

        if (token && *token)
        {
            for (auto const& named : names)
            {
                if (equals(token, named.Name))
                {
                    if (ok) *ok = true;
                    return named.Kind;
                }
            }
        }

        if (ok) *ok = false;
        return OwnerKind::Invoker;
    }

    // No mission requested; the adapter picks its usual default.
    constexpr int Mission_Unset = -1;

    struct Entry
    {
        int  TypeIndex = -1;         // index into the engine's type array
        Kind What = Kind::Foot;

        // Raw engine Mission value, resolved from the name by the parser via
        // the engine's own MissionControlClass lookup. -1 leaves the adapter's
        // default (Harvest for harvesters, otherwise the type's
        // DefaultToGuardArea choice).
        int  Mission = Mission_Unset;

        // Who the delivered object belongs to.
        OwnerKind Owner = OwnerKind::Invoker;

        // Index into the list's parallel Teams array, or -1 for none.
        //
        // A script cannot be attached to a unit directly — the engine only runs
        // scripts through a Team — so both FreeUnit.Script= and FreeUnit.Team=
        // resolve to a TeamType here, and the adapter creates the team and adds
        // the unit. A team overrides Mission, because a script IS a sequence of
        // missions and letting both apply would just fight.
        int TeamIndex = -1;

        int  Facing = Dir_Unset;     // which way the delivered object looks
        int  Cell = Dir_Unset;       // which side of the parent it appears on

        // Buildings only: how far from the parent's edge the random adjacent
        // placement may reach, in cells.
        int  Range = 1;

        // Foot only: minimum gap in cells between this entry and the previous
        // one placed on the same side. 0 = pack them adjacent.
        int  Spacing = 0;

        // Dock index for aircraft entries; -1 for everything else.
        int  Dock = -1;
    };

    // ---------------------------------------------------------------------
    // Ring geometry
    //
    // Candidate cells are generated as offsets from the parent building's
    // centre, ordered so the search walks outward from the preferred side.
    // This is the part worth testing off-target: it is pure integer geometry
    // and it decides whether "spacing" and "direction" actually behave.
    // ---------------------------------------------------------------------
    struct Offset
    {
        int X = 0;
        int Y = 0;

        constexpr bool operator==(Offset const& other) const
        {
            return X == other.X && Y == other.Y;
        }
    };

    /*
     * Unit cell offset for each of the 8 compass directions, in the ENGINE's
     * frame — the same north the game itself uses.
     *
     * Worth knowing, because it surprises people: the map is isometric, so
     * screen position is roughly (cellX - cellY, cellX + cellY). Engine-north,
     * cell (0,-1), therefore renders toward the TOP-RIGHT of the screen, not
     * the top. `FreeUnit.Cell=N` puts the unit up-and-right.
     *
     * This was briefly changed to screen-relative and changed back on purpose.
     * Rex's call: match the engine, because
     *   - `FreeUnit.Facing=` is the engine's raw DirType and cannot sensibly be
     *     rotated, so a screen-relative `.Cell` made the same letters mean two
     *     different things across two keys;
     *   - every other YR modding tag and doc uses the engine's compass, so a
     *     private rotation here would be the odd one out.
     *
     * If you want a unit at the visual top of the screen, that is `NW`.
     */
    inline Offset directionStep(int dir)
    {
        switch (((dir % 256) + 256) % 256 / 32)
        {
        case 0: return {  0, -1 }; // N  — engine north, screen up-RIGHT
        case 1: return {  1, -1 }; // NE
        case 2: return {  1,  0 }; // E
        case 3: return {  1,  1 }; // SE
        case 4: return {  0,  1 }; // S
        case 5: return { -1,  1 }; // SW
        case 6: return { -1,  0 }; // W
        default: return { -1, -1 }; // NW — screen UP
        }
    }

    /*
     * Build the ordered list of candidate offsets for one entry.
     *
     * With a concrete direction the list marches straight out along that
     * compass ray first (so `FreeUnit.Cell=N` really does put the unit north of
     * the building), then widens into full rings as a fallback so a blocked ray
     * still produces a unit instead of silently dropping it — vanilla's
     * NearByLocation already had that "somewhere nearby" behaviour and losing it
     * would be a regression.
     *
     * `startRadius` is how far out the first candidate sits, which is how
     * spacing is applied: the caller passes the running distance so successive
     * units on the same side do not stack.
     */
    inline std::vector<Offset> candidateOffsets(int dir, int startRadius, int maxRadius)
    {
        std::vector<Offset> out;

        if (startRadius < 1)
            startRadius = 1;
        if (maxRadius < startRadius)
            maxRadius = startRadius;

        if (isConcreteDir(dir))
        {
            const Offset step = directionStep(dir);
            for (int r = startRadius; r <= maxRadius; ++r)
                out.push_back({ step.X * r, step.Y * r });
        }

        // Ring fallback (and the whole search when no direction was given):
        // every cell at Chebyshev distance r, nearest ring first.
        for (int r = startRadius; r <= maxRadius; ++r)
        {
            for (int y = -r; y <= r; ++y)
            {
                for (int x = -r; x <= r; ++x)
                {
                    // Only the perimeter of this ring; inner cells belong to
                    // an earlier, closer ring.
                    const int ax = x < 0 ? -x : x;
                    const int ay = y < 0 ? -y : y;
                    if (ax != r && ay != r)
                        continue;

                    const Offset candidate { x, y };

                    bool already = false;
                    for (auto const& seen : out)
                    {
                        if (seen == candidate) { already = true; break; }
                    }
                    if (!already)
                        out.push_back(candidate);
                }
            }
        }

        return out;
    }

    // ---------------------------------------------------------------------
    // Engine adapter interface
    //
    // The resolver drives delivery through this; the real implementation talks
    // to MapClass/BuildingClass, and the test implements a grid in memory.
    // ---------------------------------------------------------------------
    class IPlacement
    {
    public:
        virtual ~IPlacement() = default;

        // Can `entry` legally occupy the cell at the parent's centre + offset?
        virtual bool canPlace(Entry const& entry, Offset offset) const = 0;

        // Actually create + unlimbo the object. Returns false if creation
        // failed (out of memory / engine refused), which aborts that entry
        // rather than the whole delivery.
        virtual bool place(Entry const& entry, Offset offset, int facing) = 0;

        // Limbo entries bypass placement entirely.
        virtual bool placeLimbo(Entry const& entry) = 0;

        // Deterministic, network-synced RNG. MUST be the game's ScenarioClass
        // random in the engine adapter — using rand() here would desync
        // multiplayer the moment two players' buildings pick different cells.
        virtual int randomRanged(int low, int high) = 0;
    };

    struct Result
    {
        int Delivered = 0;
        int Failed = 0;

        // Where each delivered entry actually landed, relative to the parent.
        //
        // Exists so the caller can print the real offsets in ONE line. Whether
        // Spacing worked is a question about cell arithmetic, and asking a human
        // to count empty tiles on an isometric screen is unreliable — "is there
        // a gap" was misread as "is there a Gap Generator" once already.
        std::vector<Offset> Placed;
    };

    /*
     * Resolve a whole delivery list against the map.
     *
     * `parentFacing` is the building's own facing, used when an entry leaves
     * Facing unset — a free unit popping out of a factory looking the same way
     * as the factory reads far better than everything defaulting to north.
     */
    inline Result resolve(std::vector<Entry> const& entries, IPlacement& map,
        int parentFacing, int parentRadius = 0)
    {
        Result result;

        if (parentRadius < 0)
            parentRadius = 0;

        // Running distance already consumed per compass direction, so that
        // Spacing separates successive units sharing a side.
        //
        // Seeded with parentRadius, NOT zero: the parent building occupies more
        // than its centre cell, and starting the search one cell from the centre
        // drops units INSIDE the footprint of anything bigger than 1x1, where
        // they are hidden under the building's own sprite. That failure is
        // invisible — placement reports success and the unit is simply never
        // seen — so the radius has to come from the caller's foundation size.
        int usedRadius[8];
        for (auto& slot : usedRadius)
            slot = parentRadius;
        int usedAny = parentRadius;

        for (auto const& entry : entries)
        {
            if (entry.TypeIndex < 0)
                continue;

            if (entry.What == Kind::Limbo)
            {
                if (map.placeLimbo(entry))
                    ++result.Delivered;
                else
                    ++result.Failed;
                continue;
            }

            // Resolve the two "random" sentinels through the synced RNG.
            int cellDir = entry.Cell;
            if (cellDir == Dir_Random)
                cellDir = map.randomRanged(0, 7) * 32;

            int facing = entry.Facing;
            if (facing == Dir_Random)
                facing = map.randomRanged(0, 7) * 32;
            else if (facing == Dir_Unset)
                facing = parentFacing;

            int* slot = isConcreteDir(cellDir) ? &usedRadius[(cellDir % 256) / 32] : &usedAny;
            const int startRadius = *slot + 1 + entry.Spacing;

            // Buildings search their own Range; feet get a generous ring so a
            // crowded base still yields a unit.
            const int maxRadius = entry.What == Kind::Building
                ? (startRadius + (entry.Range > 0 ? entry.Range : 1))
                : (startRadius + 8);

            bool placed = false;
            for (auto const& offset : candidateOffsets(cellDir, startRadius, maxRadius))
            {
                if (!map.canPlace(entry, offset))
                    continue;

                // A cell can pass canPlace and still be refused by Unlimbo, so
                // keep walking the candidates rather than dropping the entry on
                // the first disagreement.
                if (!map.place(entry, offset, facing))
                    continue;

                placed = true;
                result.Placed.push_back(offset);

                // Advance the running distance by the Chebyshev radius we
                // actually used, so the next entry on this side starts beyond it.
                const int ax = offset.X < 0 ? -offset.X : offset.X;
                const int ay = offset.Y < 0 ? -offset.Y : offset.Y;
                *slot = ax > ay ? ax : ay;
                break;
            }

            if (placed)
                ++result.Delivered;
            else
                ++result.Failed;
        }

        return result;
    }
}
