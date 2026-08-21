#pragma once
/*
 * FreeUnitExt — ArrayIndex-keyed type data.
 *
 * We deliberately do NOT use Phobos' Container/Extension machinery.
 *
 * Why: Phobos PR #2291 ("Rework the extension system into a mirror class
 * hierarchy") removed PrepareStream/LoadStatic/SaveStatic from Container, so the
 * pattern the older sibling DLLs use no longer compiles against develop. More
 * importantly, we do not need any of it — everything this DLL stores is derived
 * from INI and nothing else:
 *
 *   - Type objects live for the whole process and their ArrayIndex is assigned
 *     by rules parsing, so an ArrayIndex-keyed vector is stable without being
 *     serialised.
 *   - Nothing here changes at runtime, so there is no per-instance state to
 *     save into a savegame.
 *
 * That removes an entire class of failure (stale ext pointers after a load, a
 * container API that moves under us) at the cost of one indirection.
 */
#include <vector>

template <typename TData>
class IndexedStore
{
public:
    // Get-or-create. Called from the LoadFromINI hooks, where the index is the
    // type's own ArrayIndex.
    TData& ForIndex(int index)
    {
        if (index < 0)
            index = 0;

        if (std::size_t(index) >= this->Items.size())
            this->Items.resize(std::size_t(index) + 1);

        return this->Items[std::size_t(index)];
    }

    // Lookup that never allocates: returns null for a type we never parsed.
    TData const* TryGet(int index) const
    {
        return index >= 0 && std::size_t(index) < this->Items.size()
            ? &this->Items[std::size_t(index)]
            : nullptr;
    }

private:
    std::vector<TData> Items;
};
