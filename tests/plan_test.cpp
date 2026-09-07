/*
 * Host tests for the pure delivery planner. Builds and runs off-target:
 *   g++ -std=c++20 -Wall -Wextra -Isrc tests/plan_test.cpp -o pt && ./pt
 *
 * The point of these is the geometry and the ordering rules — the parts that
 * decide whether FreeUnit.Cell / .Spacing / random directions actually behave.
 * Anything needing the engine is stubbed by FakeMap.
 */
#include <Delivery/Plan.h>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool condition, std::string const& what)
{
    if (condition)
    {
        std::printf("  ok   %s\n", what.c_str());
    }
    else
    {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

// -----------------------------------------------------------------------------
// A map made of a blocked-cell set and a log of what got placed where.
// -----------------------------------------------------------------------------
class FakeMap : public Delivery::IPlacement
{
public:
    struct Placed
    {
        int TypeIndex;
        Delivery::Offset Where;
        int Facing;
        int Mission;
        Delivery::OwnerKind Owner;
    };

    std::set<std::pair<int, int>> Blocked;
    std::vector<Placed> Log;
    std::vector<int> LimboLog;

    // A fixed sequence standing in for the synced RNG, so tests are stable.
    std::vector<int> RandomFeed;
    size_t RandomAt = 0;

    bool canPlace(Delivery::Entry const&, Delivery::Offset offset) const override
    {
        if (Blocked.count({ offset.X, offset.Y }))
            return false;

        // Something already delivered there occupies the cell.
        for (auto const& placed : Log)
        {
            if (placed.Where == offset)
                return false;
        }
        return true;
    }

    bool place(Delivery::Entry const& entry, Delivery::Offset offset, int facing) override
    {
        Log.push_back({ entry.TypeIndex, offset, facing, entry.Mission, entry.Owner });
        return true;
    }

    bool placeLimbo(Delivery::Entry const& entry) override
    {
        LimboLog.push_back(entry.TypeIndex);
        return true;
    }

    int randomRanged(int low, int high) override
    {
        if (RandomAt < RandomFeed.size())
        {
            const int value = RandomFeed[RandomAt++];
            return value < low ? low : (value > high ? high : value);
        }
        return low;
    }
};

static Delivery::Entry foot(int typeIndex)
{
    Delivery::Entry entry;
    entry.TypeIndex = typeIndex;
    entry.What = Delivery::Kind::Foot;
    return entry;
}

// -----------------------------------------------------------------------------

static void test_parseDirection()
{
    std::printf("parseDirection\n");
    check(Delivery::parseDirection("N") == Delivery::Dir_N, "N parses to 0");
    check(Delivery::parseDirection("ne") == Delivery::Dir_NE, "lowercase ne parses");
    check(Delivery::parseDirection("SouthWest") == Delivery::Dir_SW, "long name, mixed case");
    check(Delivery::parseDirection("random") == Delivery::Dir_Random, "random sentinel");
    check(Delivery::parseDirection("96") == 96, "bare number is a raw facing byte");
    check(Delivery::parseDirection("300") == Delivery::Dir_Unset, "out-of-range number rejected");
    check(Delivery::parseDirection("banana") == Delivery::Dir_Unset, "garbage falls back to unset");
    check(Delivery::parseDirection("") == Delivery::Dir_Unset, "empty token is unset");
}

static void test_directionRayIsFirst()
{
    std::printf("candidateOffsets — the named side is searched first\n");

    auto north = Delivery::candidateOffsets(Delivery::Dir_N, 1, 3);
    check(north.front() == Delivery::Offset { 0, -1 }, "N is engine-north = cell (0,-1)");
    check(north[1] == Delivery::Offset { 0, -2 }, "N marches straight out before widening");

    auto east = Delivery::candidateOffsets(Delivery::Dir_E, 1, 2);
    check(east.front() == Delivery::Offset { 1, 0 }, "E is engine-east = cell (1,0)");

    // The ray must be followed by a ring fallback, never leaving the list short.
    check(north.size() > 3, "ray is followed by ring fallback candidates");

    // No duplicates: the ray cells must not reappear in the rings.
    std::set<std::pair<int, int>> seen;
    bool unique = true;
    for (auto const& offset : north)
    {
        if (!seen.insert({ offset.X, offset.Y }).second)
            unique = false;
    }
    check(unique, "candidate list contains no duplicate cells");

    // The centre cell is never a candidate — that is the building itself.
    bool hasCentre = false;
    for (auto const& offset : north)
    {
        if (offset == Delivery::Offset { 0, 0 })
            hasCentre = true;
    }
    check(!hasCentre, "the parent's own cell is never offered");
}

static void test_engineFrameDirections()
{
    std::printf("directionStep — compass names use the ENGINE's frame\n");

    // Deliberately the engine's compass, not the screen's. Engine-north is cell
    // (0,-1), which on an isometric map renders toward the top-RIGHT. Rex chose
    // this so .Cell and .Facing share one frame -- .Facing is the raw DirType
    // and cannot be rotated -- and so the keys match every other YR modding tag.
    struct Case { int dir; const char* name; int x; int y; };
    const Case cases[] = {
        { Delivery::Dir_N,  "N",   0, -1 },
        { Delivery::Dir_NE, "NE", +1, -1 },
        { Delivery::Dir_E,  "E",  +1,  0 },
        { Delivery::Dir_SE, "SE", +1, +1 },
        { Delivery::Dir_S,  "S",   0, +1 },
        { Delivery::Dir_SW, "SW", -1, +1 },
        { Delivery::Dir_W,  "W",  -1,  0 },
        { Delivery::Dir_NW, "NW", -1, -1 },
    };

    for (auto const& c : cases)
    {
        check(Delivery::directionStep(c.dir) == Delivery::Offset { c.x, c.y },
            std::string(c.name) + " is the engine's cell direction");
    }

    // Opposites must cancel, which catches any accidental rotation of the table
    // -- a 45-degree shift breaks this for four of the eight.
    for (int i = 0; i < 4; ++i)
    {
        auto const a = Delivery::directionStep(i * 32);
        auto const b = Delivery::directionStep((i + 4) * 32);
        check(a.X == -b.X && a.Y == -b.Y, "opposite directions cancel");
    }

    // All eight distinct, or two names would collide onto one cell.
    std::set<std::pair<int, int>> seen;
    for (int i = 0; i < 8; ++i)
    {
        auto const step = Delivery::directionStep(i * 32);
        seen.insert({ step.X, step.Y });
    }
    check(seen.size() == 8, "all eight compass points map to distinct cells");
}

static void test_splitListKeepsInternalSpaces()
{
    std::printf("splitList — outer whitespace trimmed, inner preserved\n");

    auto a = Delivery::splitList("Sleep, Area Guard ,Hunt");
    check(a.size() == 3, "three tokens");
    check(a[0] == "Sleep", "leading token clean");
    check(a[1] == "Area Guard", "INTERNAL space preserved — engine mission names need it");
    check(a[2] == "Hunt", "trailing token clean");

    auto b = Delivery::splitList("  Paradrop Approach  ");
    check(b.size() == 1 && b[0] == "Paradrop Approach", "outer whitespace trimmed both sides");

    auto c = Delivery::splitList("A,,B");
    check(c.size() == 2 && c[0] == "A" && c[1] == "B", "empty tokens dropped");

    auto d = Delivery::splitList("   ,  ");
    check(d.empty(), "all-whitespace input yields nothing");
}

static void test_parseOwner()
{
    std::printf("parseOwner\n");

    bool ok = false;
    check(Delivery::parseOwner("Invoker", &ok) == Delivery::OwnerKind::Invoker && ok,
        "Invoker parses");
    check(Delivery::parseOwner("civilian", &ok) == Delivery::OwnerKind::Civilian && ok,
        "lowercase civilian parses");
    check(Delivery::parseOwner("RandomEnemy", &ok) == Delivery::OwnerKind::RandomEnemy && ok,
        "RandomEnemy parses, mixed case");
    check(Delivery::parseOwner("RANDOMALLY", &ok) == Delivery::OwnerKind::RandomAlly && ok,
        "uppercase RandomAlly parses");

    // A typo must be reported, not silently treated as a valid choice — an
    // owner that quietly falls back is how a unit ends up on the wrong side
    // with nothing in the log to explain it.
    Delivery::parseOwner("Enemy", &ok);
    check(!ok, "unknown owner reports failure");
    check(Delivery::parseOwner("Enemy", &ok) == Delivery::OwnerKind::Invoker,
        "...and still yields a usable default");

    Delivery::parseOwner("", &ok);
    check(!ok, "empty token reports failure");
}

static void test_missionAndOwnerAreCarried()
{
    std::printf("resolve — Mission and Owner reach the adapter untouched\n");

    // The planner must not interpret these; it only carries them. Anything else
    // would put engine policy in the pure layer.
    FakeMap map;
    auto a = foot(700);
    a.Cell = Delivery::Dir_N;
    a.Mission = 11;                                  // Area_Guard
    a.Owner = Delivery::OwnerKind::RandomEnemy;

    auto b = foot(701);
    b.Cell = Delivery::Dir_N;
    // b keeps the defaults

    auto result = Delivery::resolve({ a, b }, map, Delivery::Dir_N);
    check(result.Delivered == 2, "both entries delivered");
    check(map.Log[0].Mission == 11, "explicit mission carried through");
    check(map.Log[0].Owner == Delivery::OwnerKind::RandomEnemy, "explicit owner carried through");
    check(map.Log[1].Mission == Delivery::Mission_Unset, "unset mission stays unset");
    check(map.Log[1].Owner == Delivery::OwnerKind::Invoker, "owner defaults to Invoker");
}

static void test_multipleUnitsAndSpacing()
{
    std::printf("resolve — multiple units, spacing on a shared side\n");

    FakeMap map;
    std::vector<Delivery::Entry> entries;
    for (int i = 0; i < 3; ++i)
    {
        auto entry = foot(100 + i);
        entry.Cell = Delivery::Dir_N;
        entry.Spacing = 0;
        entries.push_back(entry);
    }

    auto result = Delivery::resolve(entries, map, Delivery::Dir_S);
    check(result.Delivered == 3, "all three units delivered");
    check(map.Log.size() == 3, "three placements logged");
    check(map.Log[0].Where == Delivery::Offset { 0, -1 }, "first unit one cell north");
    check(map.Log[1].Where == Delivery::Offset { 0, -2 }, "second stacks beyond the first");
    check(map.Log[2].Where == Delivery::Offset { 0, -3 }, "third beyond the second");

    // Spacing=1 must leave a gap between consecutive units.
    FakeMap spaced;
    std::vector<Delivery::Entry> spacedEntries;
    for (int i = 0; i < 2; ++i)
    {
        auto entry = foot(200 + i);
        entry.Cell = Delivery::Dir_N;
        entry.Spacing = 1;
        spacedEntries.push_back(entry);
    }
    Delivery::resolve(spacedEntries, spaced, Delivery::Dir_N);
    check(spaced.Log[0].Where == Delivery::Offset { 0, -2 }, "Spacing=1 pushes the first out one extra cell");
    check(spaced.Log[1].Where == Delivery::Offset { 0, -4 }, "Spacing=1 leaves a gap between units");
}

static void test_blockedRayFallsBackToRing()
{
    std::printf("resolve — a blocked side still yields a unit\n");

    FakeMap map;
    // Wall off the entire northern ray. N is engine-north = cell (0,-1) per step.
    for (int y = 1; y <= 12; ++y)
        map.Blocked.insert({ 0, -y });

    auto entry = foot(300);
    entry.Cell = Delivery::Dir_N;

    auto result = Delivery::resolve({ entry }, map, Delivery::Dir_N);
    check(result.Delivered == 1, "unit still delivered when its side is blocked");
    check(map.Log.size() == 1 && !(map.Log[0].Where == Delivery::Offset { 0, -1 }),
        "it landed somewhere other than the blocked ray");
}

static void test_facingInheritanceAndRandom()
{
    std::printf("resolve — facing inheritance and random resolution\n");

    FakeMap map;
    auto inherit = foot(400);                       // Facing unset
    auto explicitFacing = foot(401);
    explicitFacing.Facing = Delivery::Dir_W;

    Delivery::resolve({ inherit, explicitFacing }, map, Delivery::Dir_SE);
    check(map.Log[0].Facing == Delivery::Dir_SE, "unset facing inherits the building's facing");
    check(map.Log[1].Facing == Delivery::Dir_W, "explicit facing wins");

    // Random facing must consume the synced RNG and land on a compass point.
    FakeMap randomMap;
    randomMap.RandomFeed = { 3 };
    auto randomFacing = foot(402);
    randomFacing.Facing = Delivery::Dir_Random;

    Delivery::resolve({ randomFacing }, randomMap, Delivery::Dir_N);
    check(randomMap.RandomAt == 1, "random facing drew exactly once from the synced RNG");
    check(randomMap.Log[0].Facing == Delivery::Dir_SE, "random index 3 maps to SE");
}

static void test_randomCellUsesRng()
{
    std::printf("resolve — random side\n");

    FakeMap map;
    map.RandomFeed = { 6 };   // W
    auto entry = foot(500);
    entry.Cell = Delivery::Dir_Random;
    entry.Facing = Delivery::Dir_N;

    Delivery::resolve({ entry }, map, Delivery::Dir_N);
    check(map.RandomAt == 1, "random side drew once from the synced RNG");
    check(map.Log[0].Where == Delivery::Offset { -1, 0 }, "random index 6 placed the unit west");
}

static void test_limboBypassesPlacement()
{
    std::printf("resolve — limbo entries never touch the map\n");

    FakeMap map;
    // Block everything a ring search could reach.
    for (int y = -20; y <= 20; ++y)
        for (int x = -20; x <= 20; ++x)
            map.Blocked.insert({ x, y });

    Delivery::Entry limbo;
    limbo.TypeIndex = 600;
    limbo.What = Delivery::Kind::Limbo;

    auto result = Delivery::resolve({ limbo }, map, Delivery::Dir_N);
    check(result.Delivered == 1, "limbo entry delivered despite a fully blocked map");
    check(map.LimboLog.size() == 1 && map.LimboLog[0] == 600, "it went through the limbo path");
    check(map.Log.empty(), "no map placement was attempted");
}

static void test_buildingRangeIsBounded()
{
    std::printf("resolve — building Range bounds the search\n");

    FakeMap map;
    // Block a 2-cell shell so a Range=1 building cannot find a home.
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            map.Blocked.insert({ x, y });

    Delivery::Entry building;
    building.TypeIndex = 700;
    building.What = Delivery::Kind::Building;
    building.Range = 1;

    auto result = Delivery::resolve({ building }, map, Delivery::Dir_N);
    check(result.Failed == 1, "building outside its Range is not placed");
    check(map.Log.empty(), "nothing was force-placed");

    // With enough Range it finds the free ring instead of giving up.
    FakeMap roomy;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            roomy.Blocked.insert({ x, y });

    building.Range = 4;
    auto ok = Delivery::resolve({ building }, roomy, Delivery::Dir_N);
    check(ok.Delivered == 1, "a wider Range reaches past the blocked shell");
}

static void test_failureIsIsolated()
{
    std::printf("resolve — one failed entry does not abort the rest\n");

    FakeMap map;
    for (int y = -20; y <= 20; ++y)
        for (int x = -20; x <= 20; ++x)
            map.Blocked.insert({ x, y });

    Delivery::Entry limbo;
    limbo.TypeIndex = 800;
    limbo.What = Delivery::Kind::Limbo;

    auto blockedFoot = foot(801);

    auto result = Delivery::resolve({ blockedFoot, limbo }, map, Delivery::Dir_N);
    check(result.Failed == 1, "the unplaceable unit is counted as failed");
    check(result.Delivered == 1, "the limbo entry after it still went through");
}

static void test_decorativeAnimIgnoresBlockedCells()
{
    std::printf("resolve — a decorative animation plays on cells nothing could occupy\n");

    // The cell a building decoration most wants is the building's own, which
    // can never pass a clear-cell test. RequireClear=no must therefore bypass
    // canPlace entirely rather than search outward for somewhere free.
    class BlockedMap final : public FakeMap
    {
    public:
        bool canPlace(Delivery::Entry const&, Delivery::Offset) const override
        {
            return false;
        }
    };

    BlockedMap map;

    Delivery::Entry anim;
    anim.What = Delivery::Kind::Animation;
    anim.AnimIndex = 0;
    anim.Cell = Delivery::Dir_N;
    anim.RequireClear = false;

    auto result = Delivery::resolve({ anim }, map, Delivery::Dir_N);
    check(result.Delivered == 1, "decoration was delivered onto a blocked cell");
    check(map.Log.size() == 1 && map.Log[0].Where == Delivery::Offset { 0, -1 },
        "and landed on the FIRST candidate rather than searching past it");
}

static void test_spawningAnimStillNeedsAClearCell()
{
    std::printf("resolve — an animation that spawns a unit still requires a free cell\n");

    // The opposite half of the same switch: MakeInfantry= has nowhere to put
    // its infantry if the anim plays inside the building, so RequireClear=yes
    // must go back to honouring canPlace.
    class BlockedMap final : public FakeMap
    {
    public:
        bool canPlace(Delivery::Entry const&, Delivery::Offset) const override
        {
            return false;
        }
    };

    BlockedMap map;

    Delivery::Entry anim;
    anim.What = Delivery::Kind::Animation;
    anim.AnimIndex = 0;
    anim.Cell = Delivery::Dir_N;
    anim.RequireClear = true;

    auto result = Delivery::resolve({ anim }, map, Delivery::Dir_N);
    check(result.Failed == 1, "a spawning animation with nowhere free is reported failed");
    check(map.Log.empty(), "and nothing was placed");
}

static void test_animEntryNeedsNoTechnoType()
{
    std::printf("resolve — an animation entry is deliverable without a TypeIndex\n");

    // Pure animations carry AnimIndex and no TypeIndex. The old guard dropped
    // any entry with TypeIndex < 0, which would have skipped every one of them.
    FakeMap map;

    Delivery::Entry anim;
    anim.What = Delivery::Kind::Animation;
    anim.TypeIndex = -1;
    anim.AnimIndex = 3;

    auto result = Delivery::resolve({ anim }, map, Delivery::Dir_N);
    check(result.Delivered == 1, "entry was not skipped for lacking a techno type");

    // ...while an entry carrying neither is still nothing at all.
    Delivery::Entry empty;
    empty.TypeIndex = -1;
    empty.AnimIndex = -1;
    auto none = Delivery::resolve({ empty }, map, Delivery::Dir_N);
    check(none.Delivered == 0 && none.Failed == 0, "an entry with neither is skipped");
}

static void test_placeFailureTriesNextCell()
{
    std::printf("resolve — a cell that passes canPlace but fails place is not fatal\n");

    // Mirrors the engine case where Unlimbo refuses a cell that the clearance
    // check approved. The entry should walk on, not be dropped.
    class PickyMap final : public FakeMap
    {
    public:
        int RefuseFirst = 1;

        bool place(Delivery::Entry const& entry, Delivery::Offset offset, int facing) override
        {
            if (RefuseFirst > 0)
            {
                --RefuseFirst;
                return false;
            }
            return FakeMap::place(entry, offset, facing);
        }
    };

    PickyMap map;
    auto entry = foot(900);
    entry.Cell = Delivery::Dir_N;

    auto result = Delivery::resolve({ entry }, map, Delivery::Dir_N);
    check(result.Delivered == 1, "entry survived a refused cell");
    check(map.Log.size() == 1 && map.Log[0].Where == Delivery::Offset { 0, -2 },
        "it moved on to the next candidate instead of giving up");
}

static void test_parentRadiusClearsFootprint()
{
    std::printf("resolve — parentRadius keeps units off the parent's footprint\n");

    // This is the regression that made the DLL look inert in-game: units were
    // delivered one cell from the building's CENTRE, i.e. underneath a 2x2 or
    // larger building, reported as successfully placed, and never seen.
    FakeMap map;
    auto entry = foot(1000);
    entry.Cell = Delivery::Dir_N;

    Delivery::resolve({ entry }, map, Delivery::Dir_N, /*parentRadius=*/2);
    check(map.Log[0].Where == Delivery::Offset { 0, -3 },
        "first unit starts beyond parentRadius, not at radius 1");

    // Spacing still composes on top of the footprint clearance.
    FakeMap spaced;
    auto a = foot(1001); a.Cell = Delivery::Dir_N; a.Spacing = 1;
    auto b = foot(1002); b.Cell = Delivery::Dir_N; b.Spacing = 1;

    Delivery::resolve({ a, b }, spaced, Delivery::Dir_N, /*parentRadius=*/2);
    check(spaced.Log[0].Where == Delivery::Offset { 0, -4 }, "footprint + spacing for the first");
    check(spaced.Log[1].Where == Delivery::Offset { 0, -6 }, "and the gap is preserved for the second");

    // A directionless entry must clear the footprint too, not just a named ray.
    FakeMap any;
    auto loose = foot(1003);
    Delivery::resolve({ loose }, any, Delivery::Dir_N, /*parentRadius=*/2);
    const auto& w = any.Log[0].Where;
    const int cheb = (std::abs(w.X) > std::abs(w.Y)) ? std::abs(w.X) : std::abs(w.Y);
    check(cheb >= 3, "ring search also starts outside the footprint");

    // Default (no parentRadius) must behave exactly as before.
    FakeMap legacy;
    auto plain = foot(1004);
    plain.Cell = Delivery::Dir_N;
    Delivery::resolve({ plain }, legacy, Delivery::Dir_N);
    check(legacy.Log[0].Where == Delivery::Offset { 0, -1 },
        "omitting parentRadius keeps the original radius-1 behaviour");
}

int main()
{
    test_parseDirection();
    test_splitListKeepsInternalSpaces();
    test_parseOwner();
    test_directionRayIsFirst();
    test_engineFrameDirections();
    test_multipleUnitsAndSpacing();
    test_missionAndOwnerAreCarried();
    test_blockedRayFallsBackToRing();
    test_facingInheritanceAndRandom();
    test_randomCellUsesRng();
    test_limboBypassesPlacement();
    test_buildingRangeIsBounded();
    test_failureIsIsolated();
    test_placeFailureTriesNextCell();
    test_parentRadiusClearsFootprint();
    test_decorativeAnimIgnoresBlockedCells();
    test_spawningAnimStillNeedsAClearCell();
    test_animEntryNeedsNoTechnoType();

    if (g_failures)
    {
        std::printf("\n%d check(s) FAILED\n", g_failures);
        return 1;
    }

    std::printf("\nall checks passed\n");
    return 0;
}
