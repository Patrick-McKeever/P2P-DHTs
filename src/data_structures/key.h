/**
 * key.h
 *
 * The ChordKey class exists as a wrapper for values on a circular node. Its primary
 * reason for existing is to implement a clockwise-between method
 * (ChordKey::InBetween), which will allow for peers to determine the location of
 * keys in relation to other keys in a chord ring through a bit of modular
 * arithmetic.
 */
#ifndef CHORD_FINAL_KEY_H
#define CHORD_FINAL_KEY_H

#include <boost/uuid/uuid.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <string>
#include <cmath>
#include "thread_safe.h"

namespace mp = boost::multiprecision;

/**
 * Generate a SHA-1 hash (stored as boost::uuids::uuid) from plaintext.
 *
 * @param plaintext Plaintext to hash.
 * @return Sha1 hash of plaintext.
 */
static boost::uuids::uuid GenerateSha1Hash(const std::string &plaintext)
{
    boost::uuids::name_generator_sha1 generator(boost::uuids::ns::dns());
    return generator(plaintext);
}

/**
 * Convert integer to hex string.
 *
 * @param val Some sort of integer value.
 * @return Int as hexadecimal stirng.
 */
template<typename int_type>
static std::string IntToHexStr(int_type val)
{
    std::stringstream hex_stream;
    hex_stream << std::hex << val;
    return hex_stream.str();
}

/**
 * Generic class for keys in a logical ring. The behavior of this type depends
 * upon the size of the ring, which will be computed from template parameters.
 *
 * @tparam key_base The base of the key. E.g. 16 for a hex key.
 * @tparam key_len The maximum number of digits in the key.
 */
template<int key_base, int key_len>
class GenericKey {
public:
    using KeyType = GenericKey<key_base, key_len>;

    GenericKey() = default;

    /**
     * Constructor 1: Generate a key from a string.
     *
     * @param key Either a numeric string or an unhashed string.
     * @param hashed Is string already-hashed (i.e. numeric),
     *               or do we need to hash it?
     */
    explicit GenericKey(const std::string &key, bool hashed = true)
        : plaintext_(hashed ? "" : key)
    {
        if(hashed) {
            // Boost interprets numeric strings beginning with 0x as hashes.
            value_ = boost::multiprecision::uint256_t("0x" + key);
        } else {
            boost::uuids::uuid uuid = GenerateSha1Hash(key);
            value_ = boost::multiprecision::uint256_t (uuid);
        }

        string_ = IntToHexStr(value_);
    }

    /**
     * Constructor 2: Generate a key from a boost::multiprecision::uint128_t.
     *
     * @param key A numeric value.
     */
    template<typename T>
    explicit GenericKey(T key)
        : value_(std::move(key))
        , string_(IntToHexStr(value_))
    {}

    /**
     * Is key "in between" lower_bound and upper_bound on a logical ring?
     *
     * @param lower_bound Lower bound of query.
     * @param upper_bound Upper bound of query.
     * @param inclusive Is range inclusive?
     * @return Whether or not this key is within specified range on logical ring.
     */
    bool InBetween(const boost::multiprecision::uint256_t &lower_bound,
                   const boost::multiprecision::uint256_t &upper_bound,
                   bool inclusive = true) const
    {
        // If upper and lower bound are same value, see if value is equal to either.
        if (lower_bound == upper_bound) {
            if (value_ == upper_bound) {
                return true;
            }
            return false;
        }

        // Modulo the upper bound, lower bound, and value by number keys in ring.
        mp::cpp_int mod_lower_bound = lower_bound % keys_in_ring_;
        mp::cpp_int mod_upper_bound = upper_bound % keys_in_ring_;
        mp::cpp_int mod_value = value_ % keys_in_ring_;

        // Now compare.
        if (lower_bound < upper_bound) {
            return (inclusive ?
                    mod_lower_bound <= mod_value && mod_value <= upper_bound :
                    mod_lower_bound < mod_value && mod_value < upper_bound);
        } else {
            // if in [b, a] then not in [a, b]
            return !(inclusive ?
                     mod_upper_bound < mod_value && mod_value < mod_lower_bound :
                     mod_upper_bound <= mod_value && mod_value <= mod_lower_bound);
        }
    }

    /**
     * Return key length.
     */
    static unsigned long long Size()
    {
        return key_len;
    }

    /**
     * Return base system of key (e.g. 16 for hex, 2 for binary).
     */
    static unsigned long long Base()
    {
        return key_base;
    }

    /**
     * Return max length (i.e. num digits) of key in binary.
     */
    static unsigned long long BinaryLen()
    {
        return log2(key_base) * key_len;
    }

    /**
     * Non-const version of above function.
     *
     * @param lower_bound Lower bound of query.
     * @param upper_bound Upper bound of query.
     * @param inclusive Is range inclusive?
     * @return Whether or not this key is within specified range on logical ring.
     */
    bool InBetween(const boost::multiprecision::uint256_t &lower_bound,
                   const boost::multiprecision::uint256_t &upper_bound,
                   bool inclusive)
    {
        // Cast away the const from the non-const version of this method.
        return (const_cast<const KeyType*>(this)->InBetween(lower_bound,
                                                            upper_bound,
                                                            inclusive));
    }

    /**
     * Overload typecast to boost:multiprecision::uint256_t for const instances of ChordKey.
     * @return this->value_
     */
    operator boost::multiprecision::uint256_t() const
    {
        return value_;
    }

    /**
     * Overload typecast to boost::multiprecision::cpp_int.
     *
     * @return ChordKey->value_ as cpp_int.
     */
    operator boost::multiprecision::cpp_int() const
    {
        return boost::multiprecision::cpp_int(value_);
    }

    /**
     * Overload typecast to std::string.
     *
     * @return ChordKey::string_ (a string version of ChordKey::value_).
     */
    operator std::string() const  {
        return string_;
    }

    /// Overload operators for numeric comparison using ChordKey::value_.
    friend bool operator == (const KeyType &key1, const KeyType &key2)
    {
        return key1.value_ == key2.value_;
    }

    friend bool operator != (const KeyType &key1, const KeyType &key2)
    {
        return key1.value_ != key2.value_;
    }

    friend bool operator <  (const KeyType &key1, const KeyType &key2)
    {
        return key1.value_ < key2.value_;
    }

    friend bool operator >  (const KeyType &key1, const KeyType &key2)
    {
        return key1.value_ > key2.value_;
    }

    friend bool operator <= (const KeyType &key1, const KeyType &key2)
    {
        return key1.value_ <= key2.value_;
    }

    friend bool operator >= (const KeyType &key1, const KeyType &key2)
    {
        return key1.value_ >= key2.value_;
    }


    // Implement addition/subtraction operators for key.
    template<typename T>
    friend KeyType operator + (const KeyType &key, T number)
    {
        return KeyType((key.value_ + number) % keys_in_ring_);
    }

    template<typename T>
    friend KeyType operator - (const KeyType &key, T number)
    {
        mp::cpp_int diff = key.value_ - number;
        if(diff > 0) {
            return KeyType(diff);
        }
        return KeyType(keys_in_ring_ + diff);
    }

    friend KeyType operator + (const KeyType &key1, const KeyType &key2)
    {
        assert(key1.keys_in_ring_ == key2.keys_in_ring_);
        return KeyType((key1.value_ + key2.value_) % keys_in_ring_);
    }

    friend KeyType operator - (const KeyType &key1, const KeyType &key2)
    {
        assert(key1.keys_in_ring_ == key2.keys_in_ring_);

        // Since values are unsigned, we must cast them to signed cpp_ints
        // before subtracting.
        mp::cpp_int diff = mp::cpp_int(key1.value_) - mp::cpp_int(key2.value_);
        if(diff > 0) {
            return KeyType(diff);
        }

        return KeyType(keys_in_ring_ + diff);
    }

private:
    /// Numeric value of key stored as boost uuid.
    boost::multiprecision::uint256_t value_;

    /// String representation of key.
    std::string string_, plaintext_;

    static const inline mp::cpp_int keys_in_ring_ = mp::pow(mp::cpp_int(key_base),
                                                            key_len);
};

/**
 * In most cases, it is not necessary to protect a key with read-write mutex
 * locking, since most keys in our program are static. However, since a few
 * cases exist in which keys will be read and modified by several threads,
 * it is pertinent that we protect these keys under read-write locks.
 * This class simply implements a wrapper of GenericKey with a read-write
 * locked mutator and accessor for this Key instance.
 *
 * @tparam key_base
 * @tparam key_len
 */
template<int key_base, int key_len>
class ThreadSafeKey : public ThreadSafe {
public:
    using KeyType = GenericKey<key_base, key_len>;

    /**
     * Constructor 1. Construct from string.
     * @param key String to be made into key.
     * @param hashed Is string a hash to be converted into a uint256_t id,
     *               or should we hash the string using a boost UUID generator?
     */
    ThreadSafeKey(const std::string &key, bool hashed)
        : key_(key, hashed)
    {}

    /**
     * Constructor 2. Construct from number.
     * @param key Numerical uuid of key.
     */
    explicit ThreadSafeKey(const boost::multiprecision::uint256_t &key)
        : key_(key)
    {}

    /**
     * Constructor 3. Construct thread-safe-key from a key.
     * @param key Key to be placed as key_.
     */
    explicit ThreadSafeKey(const KeyType &key)
        : key_(key)
    {}

    ThreadSafeKey(ThreadSafeKey &&rhs) noexcept
    {
        WriteLock rhs_lock(rhs.mutex_);
        key_ = std::move(rhs.key_);
    }

    /**
     * Acquire write lock, set key_.
     * @param key Value to set key_ to.
     */
    void Set(const KeyType &key)
    {
        WriteLock lock(mutex_);
        key_ = key;
    }

    /**
     * Acquire read lock, read key_.
     * @return key_.
     */
    KeyType Get() const
    {
        return key_;
    }

private:
    /// Key to access/mutate.
    KeyType key_;
};

using ChordKey = GenericKey<16, 32>;
using ThreadSafeChordKey = ThreadSafeKey<16, 32>;

#endif
