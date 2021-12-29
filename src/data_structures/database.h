/**
 * database.h
 *
 * This file aims to implement a simple database class, which acts as a
 * thread-safe wrapper for CSMerkleNode. This class should handle the thorny
 * pointers associated with CS MerkleNode, provide thread-safe operations, and
 * provide a "size" field which allows quick lookup of the number of elements
 * in the database.
 */

#ifndef CHORD_FINAL_DATABASE_H
#define CHORD_FINAL_DATABASE_H

#include "thread_safe.h"
// When the time comes, substitute this out with the new data block file and
// see what happens
//#include "data_block.h"
 #include "../ida/data_block.h"
#include "merkle_tree.h"

/**
 * Database to index and store values. Acts primarily as a thread-safe wrapper
 * for the compact sparse merkle node implementation, locking and managing ptrs
 * in an appropriate way.
 * @tparam ValueType Type of value being stored in the database (e.g. string,
 *                   DataFragment).
 */
template<class ValueType>
class GenericDB : public ThreadSafe {
public:
    using KeyValMap = std::map<ChordKey, ValueType>;
    using KeyValPair = std::pair<ChordKey, ValueType>;
    using DbType = GenericDB<ValueType>;

    /**
     * Constructor 1, make a new db, initialize merkel tree root.
     */
    GenericDB()
        : size_(0)
    {}

    /**
     * Constructor 2, construct from JSON.
     */
    GenericDB(const Json::Value &json_db)
        : index_(json_db["INDEX"])
        , size_(0)
    {
        for(auto kv_pair : json_db["DATA"]) {
            ++size_;
        }
    }

    /**
     * Copy constructor. (Implicitly deleted in thread safe, got to declare it
     * here.)
     */
    GenericDB(const DbType &db)
        : ThreadSafe()
        , index_(db.index_)
        , size_(db.size_)
    {};

    GenericDB(GenericDB &&rhs) noexcept
    {
        WriteLock rhs_lock(rhs.mutex_);
        index_ = std::move(rhs.index_);
        size_ = std::move(rhs.size_);
    }

    /**
     * Destructor - call manual destruct for merkel tree root.
     */
    ~GenericDB() = default;

    /**
     * Insert key value pair to database and index it.
     * @param key_frag_pair {[KEY], [VALUE]}
     */
    void Insert(const KeyValPair &key_val_pair)
    {
        WriteLock lock(mutex_);
        index_.Insert(key_val_pair);
        ++size_;
    }

    /**
     * Return value corresponding to key if it exists in db, otherwise
     * throw error.
     *
     * @param key ChordKey whose value will be returned.
     * @return Value corresponding to key.
     */
    ValueType Lookup(const ChordKey &key)
    {
        ReadLock lock(mutex_);
        // Searching merkel tree is quicker than calling map::find.
        return index_.Lookup(key);
    }

    /**
     * Update value of specified key to equal second element of KeyFragPair if
     * it exists, otherwise throw error.
     * @param key_val_pair {[KEY], [VALUE]}
     */
    void Update(const KeyValPair &key_val_pair)
    {
        WriteLock lock(mutex_);

        if(index_.Contains(key_val_pair.first)) {
            index_.Update(key_val_pair.first, key_val_pair.second);
        } else {
            throw std::runtime_error("ChordKey does not exist in database.");
        }
    }

    /**
     * Delete given key from DB and index if it exists, else give error.
     * @param key ChordKey to delete
     */
    void Delete(const ChordKey &key)
    {
        WriteLock lock(mutex_);
        if(index_.Contains(key)) {
            index_.Delete(key);
            --size_;
        }
        else {
            throw std::runtime_error("ChordKey does not exist in database.");
        }
    }

    /**
     * List all entries in data_ between lower_bound and upper_bound.
     * @param lower_bound Lower bound of range.
     * @param upper_bound Upper bound of range.
     * @return Map including all keys from data_ within specified range.
     */
    KeyValMap ReadRange(const ChordKey &lower_bound, const ChordKey &upper_bound)
    {
        ReadLock lock(mutex_);
        return index_.ReadRange(lower_bound, upper_bound);
    }

    /**
     * State whether the database contains given key.
     * @param key ChordKey to lookup.
     * @return Is key indexed in index_?
     */
    bool Contains(const ChordKey &key)
    {
        ReadLock lock(mutex_);
        return index_.Contains(key);
    }

    /**
     * Get KV pair immediately after given key
     */
    std::optional<KeyValPair> Next(const ChordKey &key)
    {
        ReadLock lock(mutex_);
        return index_.Next(key);
    }

    /**
     * Accessor for compact sparse merkle tree.
     * @return Database index.
     */
    MerkleTree<ValueType> GetIndex()
    {
        ReadLock lock(mutex_);
        return index_;
    }

    /**
     * @return Num entries in DB.
     */
    unsigned long Size()
    {
        return size_;
    }

    /**
     * Comparison operator (for unit testing).
     * @param left_db Left hand side.
     * @param right_db Right hand side.
     * @return Are they equivalent?
     */
    friend bool operator == (const DbType &left_db, const DbType &right_db)
    {
        return left_db.index_.hash_ == right_db.index_.hash_ &&
               left_db.data_ == right_db.data_;
    }

private:
    MerkleTree<ValueType> index_;
    unsigned long size_;
};

using FragmentDb = GenericDB<DataFragment>;
using TextDb = GenericDB<std::string>;

#endif
