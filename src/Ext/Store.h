#pragma once
/*
 * FreeUnitExt — per-type data storage.
 *
 * We deliberately do NOT use Phobos' Container/Extension machinery: Phobos
 * PR #2291 removed PrepareStream/LoadStatic/SaveStatic from Container, and we
 * have no lifecycle to manage anyway — everything stored here comes from INI
 * and never changes at runtime.
 *
 * KEYED BY TYPE POINTER, deliberately.
 *
 * This started out keyed by ArrayIndex, which was wrong in a way that produced
 * no crash and no log line: every lookup simply missed, so all three hooks fell
 * through to vanilla and the DLL looked inert. Keying by the type object's own
 * address removes the assumption entirely — a pointer is unambiguous the moment
 * we are handed one, whereas ArrayIndex is only meaningful once the engine has
 * assigned it, and we never verified when that happens relative to LoadFromINI.
 *
 * Type objects live for the whole process, so the keys stay valid. The one
 * thing this gives up is surviving a savegame that reconstructs type objects at
 * new addresses; that is untested either way and is tracked in HOOKS_LOG.md.
 */
#include <unordered_map>

template <typename TData>
class PointerStore
{
public:
    // Get-or-create. Called from the LoadFromINI hooks.
    TData& ForKey(const void* key)
    {
        return this->Items[key];
    }

    // Lookup that never allocates: null for a type we never parsed.
    TData const* TryGet(const void* key) const
    {
        auto const it = this->Items.find(key);
        return it != this->Items.end() ? &it->second : nullptr;
    }

    std::size_t size() const { return this->Items.size(); }

private:
    std::unordered_map<const void*, TData> Items;
};
