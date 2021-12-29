#ifndef CHORD_FINAL_FINGER_TABLE_H
#define CHORD_FINAL_FINGER_TABLE_H

#include "key.h"
#include "thread_safe.h"
#include <boost/uuid/uuid.hpp>
#include <map>
#include <utility>

namespace mp = boost::multiprecision;

/**
 * A finger table enables O(log(n)) lookups by mapping ranges
 * of keys/documents to the node succeeding the lower bound.
 * When seeking to CRUD a given key, a node will consult its
 * finger table, find the range containing the key, and forward
 * its request to that node. Said node will either process the req,
 * if it owns the key in question, or forward it to another node.
 */
template<typename PeerType>
struct Finger {
    /// Lower bound of finger's range.
    ChordKey lower_bound_;
    /// Upper bound of finger's range.
    ChordKey upper_bound_;
    /// Node succeeding lower bound.
    PeerType successor_;
};

template<typename PeerType>
class FingerTable : public ThreadSafe {
public:
    using FingerType = Finger<PeerType>;

    /**
     * Constructor.
     * @param starting_key First table entry minus 1.
     */
    explicit FingerTable(ChordKey starting_key)
        : starting_key_(std::move(starting_key))
        // Num keys in chord is 16 ^ hex-id-length.
        , keys_in_chord_(mp::pow(mp::cpp_int(16), 32))
        // Num entries is binary ID length of key.
        , num_entries_(ChordKey::BinaryLen())
    {}

    /**
     * Constructor 2. From JSON.
     * @param finger_json JSON list of fingers.
     */
    explicit FingerTable(const Json::Value &finger_json)
        : starting_key_(finger_json["STARTING_KEY"].asString(), true)
        , keys_in_chord_(mp::pow(mp::cpp_int(16), 32))
        // Num entries is binary ID length of key.
        , num_entries_(ChordKey::BinaryLen())
    {
        for(const auto &finger : finger_json["FINGERS"]) {
            AddFinger(FingerType {
                ChordKey(finger["LOWER_BOUND"].asString(), false),
                ChordKey(finger["UPPER_BOUND"].asString(), false),
                PeerType(finger["SUCCESSOR"])
            });
        }
    }

    /**
     * Copy constructor.
     * @param fingers Finger table to copy.
     */
    FingerTable(const FingerTable<PeerType> &fingers)
        : starting_key_(fingers.starting_key_)
        , keys_in_chord_(fingers.keys_in_chord_)
        , num_entries_(fingers.num_entries_)
        , table_(fingers.table_)
    {}


    FingerTable(FingerTable &&rhs) noexcept
    {
        WriteLock rhs_lock(rhs.mutex_);
        num_entries_ = std::move(rhs.num_entries_);
        table_ = std::move(rhs.table_);
        starting_key_ = std::move(rhs.starting_key_);
        keys_in_chord_ = std::move(rhs.keys_in_chord_);
    }

    /**
     * Add a new finger to end of the table.
     * @param finger Finger to add.
     */
    void AddFinger(const FingerType &finger)
    {
        WriteLock lock(mutex_);
        table_.push_back(finger);
    }

    /**
     * Retrieve the nth table intry.
     * @param n Index of table entry.
     * @return Nth table entry.
     */
    PeerType GetNthEntry(int n)
    {
        ReadLock lock(mutex_);
        return table_.at(n).successor_;
    }

    /**
     * Iterate through fingers in the table, find the successor of a given key.
     *
     * @param key ChordKey to lookup.
     * @return The entry in the finger table for which
     *         finger.lower_bound_ <= key <= finger.upper_bound_.
     */
    PeerType Lookup(const ChordKey &key)
    {
        ReadLock lock(mutex_);

        for (const FingerType &finger: table_) {
            bool key_in_range = key.InBetween(finger.lower_bound_,
                                              finger.upper_bound_,
                                              true);

            if(key_in_range) {
                return finger.successor_;
            }
        }

        throw std::runtime_error("ChordKey not found");
    }

    /**
     * Update the nth table entry to the given finger.
     * @param n Entry to update.
     * @param succ The correct successor of the given range's lower bound.
     */
    void EditNthFinger(int n, const PeerType &succ)
    {
        WriteLock lock(mutex_);
        table_.at(n).successor_ = succ;
    }

    /**
     * When notified of a new peer, entries in the table referring to the peer's
     * range should be updated to point to that peer.
     * @param new_peer Peer that recently entered system.
     */
    void AdjustFingers(const PeerType &new_peer)
    {
        WriteLock lock(mutex_);

        for(auto &finger : table_) {
            if(finger.lower_bound_.InBetween(new_peer.min_key_, new_peer.id_)) {
                finger.successor_ = new_peer;
            }
        }
    }

    void ReplaceDeadPeer(const PeerType &dead_peer, const PeerType &replacement)
    {
        WriteLock lock(mutex_);

        for(auto &finger : table_) {
            if(finger.successor_.id_ == dead_peer.id_) {
                finger.successor_ = replacement;
            }
        }
    }

    /**
     * Return the range of keys to which the nth entry in the finger table
     * should point.
     * @param n Index of the given range.
     * @return ((starting_key + 2^n) mod 2^m)-((starting_key + 2^(n+1)) mod 2^m)
     *         where m is the number keys in ring.
     */
    std::pair<ChordKey, ChordKey> GetNthRange(int n)
    {
        ReadLock lock(mutex_);
        mp::cpp_int starting_key = starting_key_;
        mp::cpp_int lb_increment_value = mp::pow(mp::cpp_int(2), n);
        auto lower_bound = mp::uint256_t((starting_key + lb_increment_value)
                                         % keys_in_chord_);
        mp::cpp_int ub_increment_value = mp::pow(mp::cpp_int(2), n + 1);
        auto upper_bound = mp::uint256_t((starting_key + ub_increment_value)
                                         % keys_in_chord_) - 1;
        return { ChordKey(lower_bound), ChordKey(upper_bound) };
    }

    /**
     * Convert to string
     * @return Table in string form.
     */
    explicit operator std::string()
    {
        ReadLock lock(mutex_);
        // Since ranges start out so small, we need to visually condense this info.
        // To do so, we collate ranges of keys that are succeeded by the same peer.
        std::vector<FingerType> display_fingers;
        for(const auto &finger : table_) {
            if(display_fingers.empty()) {
                display_fingers.push_back(finger);
            } else if(display_fingers.back().successor_.id_ == finger.successor_.id_) {
                display_fingers.back().upper_bound_ = finger.upper_bound_;
                // If this seems redundant, bear in mind that trying to get the
                // successor of back if it's empty would cause a segfault.
            } else {
                display_fingers.push_back(finger);
            }
        }

        std::stringstream res;

        // Create upper border, column names.
        res << std::string(131, '-') << "\n";
        res << std::setfill(' ') <<
            "| " << "LOWER BOUND" <<
            std::setw(26) << "| " << "UPPER BOUND" <<
            std::setw(26) << "| " << "SUCC ID" <<
            std::setw(30) << "| " << "SUCC IP:PORT" <<
            std::setw(7) << "|\n";

        res << std::string(131, '-') << "\n";
        // Max key size (and also the normal size is 32 digits hex. We set the width
        // to 5 for 32 bit keys, so, to ensure alignment, we need to make sure that
        // the width is one char larger for every digit that is missing in the key
        // for any given column.
        for(const auto &finger : display_fingers) {
            res << std::setfill(' ') << "| " << std::setw(5)
                << std::string(finger.lower_bound_)
                << std::setw(5 + (32 - finger.lower_bound_.Size())) << "| "
                << std::string(finger.upper_bound_)
                << std::setw(5 + (32 - finger.upper_bound_.Size())) << "| "
                << std::string(finger.successor_.id_)
                << std::setw(5 + (32 - finger.successor_.id_.Size())) << "| "
                << std::string(finger.successor_.ip_addr_) << ":"
                << std::to_string(finger.successor_.port_) << std::setw(5)
                << "|\n";
        }
        res << std::string(131, '-') << "\n";
        return res.str();
    }

    /**
     * Convert to JSON.
     * @return JSON list of fingers, each of which is JSON object consisting
     *         of lower bound, upper bound, and successor.
     */
    explicit operator Json::Value()
    {
        ReadLock lock(mutex_);
        Json::Value finger_table, finger_list;
        for(const auto &finger : table_) {
            Json::Value finger_obj;
            finger_obj["LOWER_BOUND"] = std::string(finger.lower_bound_);
            finger_obj["UPPER_BOUND"] = std::string(finger.upper_bound_);
            finger_obj["SUCCESSOR"] = Json::Value(finger.successor_);
            finger_list.append(finger_obj);
        }

        finger_table["STARTING_KEY"] = std::string(starting_key_);
        finger_table["FINGERS"] = finger_list;

        return finger_table;
    }

    /**
     * @return Is table empty?
     */
    bool Empty()
    {
        ReadLock lock(mutex_);
        return table_.empty();
    }

    /// Number of entries the table should have (length of binary key ID).
    unsigned long long num_entries_;

private:
    /// The finger table itself, represented as a vector of fingers.
    std::vector<FingerType> table_;

    /// First finger table entry - 1.
    ChordKey starting_key_;

    /// Number keys in entire hash ring.
    mp::cpp_int keys_in_chord_;
};

#endif