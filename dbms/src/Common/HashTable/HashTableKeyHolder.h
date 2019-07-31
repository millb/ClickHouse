#pragma once

#include <Common/Arena.h>

/**
  * In some aggregation scenarios, when adding a key to the hash table, we
  * start with a temporary key object, and if it turns out to be a new key,
  * we must make it persistent (e.g. copy to an Arena) and use the resulting
  * persistent object as hash table key. This happens only for StringRef keys,
  * because other key types are stored by value, but StringRef is a pointer-like
  * type: the actual data are stored elsewhere. Even for StringRef, we don't
  * make a persistent copy of the key in each of the following cases:
  * 1) the aggregation method doesn't use temporary keys, so they're persistent
  *    from the start;
  * 1) the key is already present in the hash table;
  * 3) that particular key is stored by value, e.g. a short StringRef key in
  *    StringHashMap.
  *
  * In the past, the procedure for working with temporary keys was as follows:
  * - emplace() returns whether the key is newly inserted. This information is
  *   needed regardless of the key persistence, e.g. so that we can initialize
  *   the mapped value for a new key. emplace() also returns a mutable reference
  *   to the key that is stored in hash table (techically, it returns a mutable
  *   reference to the whole pair of key and the corresponding mapped value).
  * - the caller makes a persistent copy of the key, and updates the stored key
  *   through the returned reference.
  * This worked well enough for normal HashTable, where the decision depends
  * only on whether the key is new or not (case (2) from above list). However,
  * now we are adding a compound hash table for StringRef keys, so case (3)
  * appears. The decision about persistence now depends on some properties of
  * the key, and the logic of this decision is tied to the particular hash table
  * implementation. This means that the hash table user now doesn't have enough
  * data and logic to make this decision by itself.
  *
  * In principle, we could support this new scenario by adding another output
  * value to emplace() -- say, an optional "pointer to the key that must be
  * replaced with persistent key", which is nullptr in the cases when the
  * persistent key is not needed. The caller would then branch on this value in
  * runtime, and update the key if needed.
  * The main (purported) drawback of this method is the need to have an extra
  * branch in the caller, and the need to first store some temporary key that
  * is later discarded. This code is used in aggregation, which is a very
  * performance-sensitive area, so having to do any extra work there is dubious.
  * However, this was not benchmarked, so it is only a speculation. Implementing
  * and benchmarking this idea is moderately hard, because of the code it has
  * to interface to, and unfortunately now we don't have enough time to do that.
  * Another drawback is that having three output parameters looks somewhat
  * complex.
  *
  * Instead of that, we provide a callback for the hash table to call when
  * it needs a persistent key. This means we don't have additional branching,
  * and we store the right key the first time and don't have to replace it
  * later.
  *
  * How do we inject this key persistence policy into hash table? The first
  * thing that comes to mind is putting it into the hash table class itself, and
  * initializing it in the hash table constructor, but this is not without
  * problems:
  * 1) the hash table declaration is already overloaded with all kinds of
  *    policies, and adding another one is just cumbersome.
  * 2) this can increase the size of the hash table object (e.g. because of
  *    having to store a reference to Arena), which is important for aggregate
  *    function states.
  * 3) having one Arena per hash table might be inconvenient, when we are
  *    merging the states of aggregate functions from different blocks, that
  *    refer to different arenas.
  *
  * Another approach is to wrap the key with a special holder that exposes
  * methods to manage key persistence, and pass this holder to emplace(). This
  * might incur some overhead when wrapping the keys that don't need memory
  * management -- i.e., the holder object only contains the key and does
  * nothing, but it's still constructed. This is why we take it one step
  * further, and implement the policy as a set of functions, overloaded with key
  * holder type. The default implementation matches all types and does nothing,
  * so if we don't need any memory management, we just pass the normal key to
  * emplace(). When we need do need it, we create a key holder type, overload
  * the policy functions for it, and pass the key holder to emplace() instead of
  * the key.
  *
  * This file declares the default key persistence policy, as well as two
  * different policies for storing StringRef keys to Arena.
  */


/**
  * Returns the key. Can return the temporary key initially.
  * After the call to keyHolderPersistKey(), must return the persistent key.
  */
template <typename Key>
inline Key & ALWAYS_INLINE keyHolderGetKey(Key && key) { return key; }

/**
  * Make the key persistent. keyHolderGetKey() must return the persistent key
  * after this call.
  */
template <typename Key>
inline void ALWAYS_INLINE keyHolderPersistKey(Key &&) {}

/**
  * Discard the key. Calling keyHolderGetKey() is ill-defined after this.
  */
template <typename Key>
inline void ALWAYS_INLINE keyHolderDiscardKey(Key &&) {}

namespace DB
{

/**
  * ArenaKeyHolder is a key holder for hash tables that serializes a StringRef
  * key to an Arena.
  */
struct ArenaKeyHolder
{
    StringRef key;
    Arena & pool;

};

} /* namespace DB */

inline StringRef & ALWAYS_INLINE keyHolderGetKey(DB::ArenaKeyHolder & holder)
{
    return holder.key;
}

inline void ALWAYS_INLINE keyHolderPersistKey(DB::ArenaKeyHolder & holder)
{
    // Hash table shouldn't ask us to persist a zero key
    assert(holder.key.size > 0);
    holder.key.data = holder.pool.insert(holder.key.data, holder.key.size);
}

inline void ALWAYS_INLINE keyHolderDiscardKey(DB::ArenaKeyHolder &)
{
}

namespace DB
{

/**
  * SerializedKeyHolder is a key holder for a StringRef key that is already
  * serialized to an Arena. The key must be the last allocation in this Arena,
  * and is discarded by rolling back the allocation.
  */
struct SerializedKeyHolder
{
    StringRef key;
    Arena & pool;
};

} /// namespace DB

inline StringRef & ALWAYS_INLINE keyHolderGetKey(DB::SerializedKeyHolder & holder)
{
    return holder.key;
}

inline void ALWAYS_INLINE keyHolderPersistKey(DB::SerializedKeyHolder &)
{
}

inline void ALWAYS_INLINE keyHolderDiscardKey(DB::SerializedKeyHolder & holder)
{
    [[maybe_unused]] void * new_head = holder.pool.rollback(holder.key.size);
    assert(new_head == holder.key.data);
    holder.key.data = nullptr;
    holder.key.size = 0;
}

