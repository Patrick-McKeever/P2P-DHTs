/**
 * merkle_tree.h
 *
 * Cates' thesis for DHash requires that two peers be able to efficiently
 * compare their databases. In order to do that, they must store keys inside
 * a tree which subdivides the keyspace into ranges. Cates specifies that
 * DHash implementations ought to use a Merkle tree, a type of tree in which
 * each node is assigned a hash by hashing the concatenation of the keys it
 * holds (leaf nodes) or the concatenation of its children's hashes (internal
 * or root node). This file should implement such a data structure.
 *
 * This file now replaces the functionality contained within merkle_node.h,
 * which implements a compact sparse merkle tree.
 */

#ifndef CHORD_AND_DHASH_MERKLE_TREE_H
#define CHORD_AND_DHASH_MERKLE_TREE_H

#include <utility>
#include <json/json.h>
#include "key.h"


/**
 * Merkle tree covering the keyspace represented by ChordKey.
 * @tparam ValType The type of the values being associated with ChordKeys.
 */
template<typename ValType>
class MerkleTree {
public:
    using KvPair = std::pair<ChordKey, ValType>;
    using KvMap = std::map<ChordKey, ValType>;
    using KvSet = std::set<ChordKey>;
    using NodeType = MerkleTree<ValType>;


    /**
     * Constructor 1. Construct a merkle tree node covering the entire keyspace
     *                represented by ChordKey.
     */
    MerkleTree()
        : max_key_(mp::pow(mp::cpp_int(ChordKey::Base()), ChordKey::Size()))
    {
        CreateChildren();
    }

    /**
     * Constructor 2. Construct a merkle tree node with a specified key range
     *                and position.
     * @param min_key Minimum key which can be held in this (sub)tree.
     * @param max_key Maximum key which can be held in this (sub)tree.
     * @param position The position of this node, represented as a deque of ints,
     *                 with each denoting the branch of a level used to arrive
     *                 at this node. E.g. If this node is the 14th child of the
     *                 3rd child of the root, position would be { 3, 14 }.
     */
    MerkleTree(ChordKey min_key, ChordKey max_key, std::deque<int> position)
        : min_key_(std::move(min_key))
        , max_key_(std::move(max_key))
        , position_(std::move(position))
    {}

    /**
     * Constructor 3. Construct a node from a JSON representation of it.
     * @param json_node The JSON representation of the node in question.
     */
    explicit MerkleTree(const Json::Value &json_node)
        : min_key_(json_node["MIN_KEY"].asString(), true)
        , max_key_(json_node["KEY"].asString(), true)
        , hash_(json_node["HASH"].asString(), true)
    {
        for(const auto &dir : json_node["POSITION"]) {
            position_.push_back(dir.asInt());
        }

        for(const auto &child : json_node["CHILDREN"]) {
            child_nodes_.push_back(MerkleTree<ValType>(child));
        }

        bool node_has_kvs = json_node.get("KV_PAIRS", NULL) != NULL &&
                            ! json_node["KV_PAIRS"].isNull();

        if(node_has_kvs) {
            Json::Value kv_pairs = json_node["KV_PAIRS"];
            Json::Value::const_iterator it;
            for (it = kv_pairs.begin(); it != kv_pairs.end(); ++it) {
                ChordKey key(it.key().asString(), true);
                data_.insert({key, ValType(it->asString())});
            }
        }
    }

    /**
     * Insert a key value pair into the (sub)tree.
     * @param kv_pair Key value pair to insert.
     */
    void Insert(const std::pair<ChordKey, ValType> &kv_pair)
    {

        // We need to maintain a value indicating the largest key in the subtree,
        // in order to aid lookups.
        if(largest_key_.has_value() && kv_pair.first > largest_key_ ||
           ! largest_key_.has_value())
        {
            largest_key_ = kv_pair.first;
        }

        if(IsLeaf()) {
            if(Contains(kv_pair.first)) {
                throw std::runtime_error("Key already exists");
            }

            data_.insert(kv_pair);

            // If we have more than the allotted number of children, it's time
            // to subdivide this tree's range among num_children_ new subtrees.
            if(data_.size() > num_children_) {
                ToInternal();
            }
        } else {
            // Calculate which child holds the range in which the key resides,
            // then instruct that child to insert the key inside its subtree.
            unsigned long node_index = ChildNum(kv_pair.first);
            child_nodes_.at(node_index).Insert(kv_pair);
        }

        // Now that this subtree has been potentially changed, ensure that
        // this subtree's hash is up-to-date.
        Rehash();
    }

    /**
     * Find the specified key in the subtree, return its value.
     * @param key Key to lookup.
     * @return The value corresponding to the key if it exists in subtree, else
     *         throw an error.
     */
    ValType Lookup(const ChordKey &key) const
    {
        if(IsLeaf()) {
            if(Contains(key)) {
                return data_.at(key);
            } else {
                throw std::runtime_error("Key does not exist in subtree");
            }
        }

        unsigned long node_index = ChildNum(key);

        return child_nodes_.at(node_index).Lookup(key);
    }

    /**
     * Read all keys in subtree that are within the given range.
     * @param lower_bound LB of range to read.
     * @param upper_bound UB of range to read.
     * @return Map of keys in subtree within specified range.
     */
    KvMap ReadRange(const ChordKey &lower_bound, const ChordKey &upper_bound)
    {
        KvMap keys_in_range;

        // If this node is a leaf, return all kvs it holds within the specified
        // range.
        if(IsLeaf()) {
            for(const auto &[key, val] : data_) {
                if(key.InBetween(lower_bound, upper_bound, true)) {
                    keys_in_range.insert({ key, val });
                }
            }

            return keys_in_range;
        }

        // Otherwise, determine the child which holds the upper bound and the
        // child which holds the lower bound.
        unsigned long lb_index = ChildNum(lower_bound),
                      ub_index = ChildNum(upper_bound);

        // Read all leaves of children in between the child containing the
        // lower bound and the child containing the upper bound.
        if(lb_index < ub_index) {
            for(int i = (int) lb_index; i <= ub_index; ++i) {
                MerkleTree nth_child = child_nodes_.at(i);
                ChordKey lower = lower_bound < nth_child.GetMinKey() ?
                                     nth_child.GetMinKey() : lower_bound;
                ChordKey upper = upper_bound > nth_child.GetMaxKey() /*- 1*/ ?
                                 nth_child.GetMaxKey() /*- 1*/ : upper_bound;

                KvMap keys_in_node = nth_child.ReadRange(lower, upper);
                keys_in_range.insert(keys_in_node.begin(), keys_in_node.end());
            }

            return keys_in_range;
        }

        // If the lower bound is less than the upper bound (note that ChordKey
        // is intended to represent a circular keyspace), then return the ranges
        // [lb, MAX_KEY] and [0, ub].
        else if(lb_index > ub_index) {
            ChordKey max_key(mp::pow(mp::cpp_int(ChordKey::Base()),
                                     ChordKey::Size()) - 1);
            KvMap below_ub = ReadRange(ChordKey(0), upper_bound),
                  above_lb = ReadRange(lower_bound, max_key);
            below_ub.insert(above_lb.begin(), above_lb.end());
            return below_ub;
        }

        return child_nodes_.at(lb_index).ReadRange(lower_bound, upper_bound);
    }

    /**
     * Find the specified key if it exists in the subtree, update its value.
     * @param kv_pair { key inside subtree, new value for key }
     */
    void Update(const std::pair<ChordKey, ValType> &kv_pair)
    {
        if(IsLeaf()) {
            if(Contains(kv_pair.first)) {
                data_.at(kv_pair.first) = kv_pair.second;
            } else {
                throw std::runtime_error("Key does not exist in subtree");
            }

            Rehash();
            return;
        }

        unsigned long node_index = ChildNum(kv_pair.first);
        child_nodes_.at(node_index).Update(kv_pair);
        // Data has been altered, have to rehash.
        Rehash();
    }

    /**
     * Delete the specified key from the subtree if it exists, else throw error.
     * @param key Key to delete.
     */
    void Delete(const ChordKey &key)
    {
        if(IsLeaf()) {
            if(Contains(key)) {
                data_.erase(key);
                Rehash();
            } else {
                throw std::runtime_error("Key does not exist in subtree");
            }

            return;
        }

        unsigned long node_index = ChildNum(key);
        child_nodes_.at(node_index).Delete(key);
        Rehash();
        std::optional<KvPair> new_largest_entry = GetLargestEntry();

        // Maintain largest key entry. Deletes are more expensive than other
        // ops due to needing to look this up.
        if(new_largest_entry.has_value()) {
            largest_key_ = new_largest_entry->first;
        } else {
            largest_key_.reset();
        }
    }

    /**
     * Identify the first key stored in the tree that is greater than key.
     * @param key The key for which the next-greatest kv pair will be returned.
     * @return The next greatest kv pair in the tree, if it exists, or nullopt.
     */
    std::optional<KvPair> Next(const ChordKey &key) const
    {
        // If the tree is totally empty, then there is no "next" key to return.
        if(hash_ == ChordKey(0)) {
            return std::nullopt;
        }

        // If this is the root, and the key is larger than the largest key,
        // the "next" key is the smallest key in the tree, because we treat
        // keyspace as a logical ring.
        if(key >= largest_key_ && position_.empty()) {
            return GetSmallestEntry();
        }

        if(IsLeaf()) {
            // If this is a leaf, iterate through keys until we find one larger
            // than the specified bound.
            for(auto it = data_.begin(); it != data_.end(); ++it) {
                if(it->first > key) {
                    return std::make_optional(*it);
                }
            }

            // If a larger key does not exist, then there is no "next" key in
            // this (sub)tree.
            return std::nullopt;
        }


        unsigned long node_ind = ChildNum(key);

        // It only makes sense to loop around if this node is the root.
        for(unsigned long i = node_ind; i < num_children_; ++i)
        {
            std::optional<KvPair> next = child_nodes_.at(i).Next(key);
            if(next.has_value()) {
                return next;
            }
        }

        return std::nullopt;
    }

    /**
     * Given a deque of ints, in which int corresponds to the number of the child
     * n layers deep into this (sub)tree needed to reach the desired position,
     * return the desired node.
     * @param dirs Deque of ints as described above.
     * @return The node in the position specified by dir.
     */
    std::optional<NodeType> LookupByPosition(std::deque<int> dirs)
    {
        if(dirs.empty()) {
            return *this;
        }

        // If the directions have a greater depth than the tree itself, then
        // the requested node does not exist.
        else if(IsLeaf()) {
            return std::nullopt;
        }

        NodeType next_node = child_nodes_.at(dirs.front());
        dirs.pop_front();
        if(dirs.empty()) {
            return next_node;
        }

        return next_node.LookupByPosition(dirs);
    }

    /**
     * Does (sub)tree contain the specified key?
     * @param key Key to return.
     * @return Whether or not the subtree contains the given key.
     */
    [[nodiscard]] bool Contains(const ChordKey &key) const
    {
        if(IsLeaf()) {
            return data_.find(key) != data_.end();
        }

        unsigned long node_index = ChildNum(key);
        return child_nodes_.at(node_index).Contains(key);
    }

    /**
     * Does the given range overlap with the range of keys held in this
     * (sub)tree?
     * @param lower_bound Lower bound of the range.
     * @param upper_bound Upper bound of the range.
     * @return Is this tree's minimum or maximum key inside the given range?
     */
    [[nodiscard]] bool Overlaps(const ChordKey &lower_bound,
                                const ChordKey &upper_bound) const
    {
        bool min_key_overlaps = min_key_.InBetween(lower_bound, upper_bound,
                                                   true);
        bool max_key_overlaps = max_key_.InBetween(lower_bound, upper_bound,
                                                   true);
        return min_key_overlaps || max_key_overlaps;
    }

    /**
     * Overload of above function in which range is a pair of ChordKeys.
     * @param range Range of keys.
     * @return Does given key range overlap with the range of keys held in this
     *         (sub)tree?
     */
    [[nodiscard]] bool Overlaps(const std::pair<ChordKey, ChordKey> &range) const
    {
        return Overlaps(range.first, range.second);
    }

    /**
     * @return Is this node a leaf?
     */
    [[nodiscard]] bool IsLeaf() const
    {
        return child_nodes_.empty();
    }

    /**
     * @return All kv pairs contained within this subtree inside a map.
     */
    KvMap GetEntries() const {
        KvMap result;

        if(hash_ == ChordKey(0)) {
            return {};
        }

        if(IsLeaf()) {
            return data_;
        }

        for(const auto &child : child_nodes_) {
            KvMap child_data = child.GetEntries();
            result.insert(child_data.begin(), child_data.end());
        }

        return result;
    }

    /**
     * Get the smallest kv pair contained within this subtree.
     * @return Smallest kv pair in subtree if this subtree contains any, else
     *         std::nullopt.
     */
    std::optional<KvPair> GetSmallestEntry() const
    {
        // Note that the first key of leftmost leaf node (i.e. smallest index)
        // with non-empty data_ field will be smallest in (sub)tree.
        if(hash_ == ChordKey(0)) {
            return std::nullopt;
        }

        if(IsLeaf()) {
            if(data_.empty()) {
                return std::nullopt;
            }
            return *data_.begin();
        }

        for(int i = 0; i < num_children_; ++i) {
            std::optional<KvPair> res = child_nodes_.at(i).GetSmallestEntry();
            if(res.has_value()) {
                return res;
            }
        }

        return std::nullopt;
    }

    /**
     * Find largest key value pair in subtree.
     * @return Largest key-value pair in subtree if this subtree has any keys,
     *         else nullopt.
     */
    std::optional<KvPair> GetLargestEntry() const
    {
        if(hash_ == ChordKey(0))
            return std::nullopt;

        if(IsLeaf()) {
            if(data_.empty()) {
                return std::nullopt;
            }
            return *(--data_.end());
        }

        for(int i = child_nodes_.size() - 1; i >= 0; --i) {
            std::optional<KvPair> res = child_nodes_.at(i).GetLargestEntry();
            if(res.has_value()) {
                return res;
            }
        }

        return std::nullopt;
    }

    /**
     * Accessors. Not going to comment on them.
     */
    static int GetNumChildren()
    {
        return num_children_;
    }

    MerkleTree<ValType> GetNthChild(int n) const
    {
        return child_nodes_.at(n);
    }

    [[nodiscard]] ChordKey GetMinKey() const
    {
        return min_key_;
    }

    [[nodiscard]] ChordKey GetMaxKey() const
    {
        return max_key_;
    }

    [[nodiscard]] std::pair<ChordKey, ChordKey> GetRange() const
    {
        return { min_key_, max_key_ };
    }

    [[nodiscard]] std::optional<ChordKey> GetLargestKey() const
    {
        return largest_key_;
    }

    [[nodiscard]] ChordKey GetHash() const
    {
        return hash_;
    }

    [[nodiscard]] std::deque<int> GetPosition() const
    {
        return position_;
    }

    [[nodiscard]] int GetDepth() const
    {
        return position_.size();
    }

    /**
     * Represent the position of this node in a human-readable format. Good
     * for debugging.
     * @return "Root" or "[DIR0] [DIR1] [DIR2]...[DIRN] "
     */
    [[nodiscard]] std::string GetPosStr() const
    {
        if(position_.empty()) {
            return "Root";
        }

        std::string pos_str;
        for(int pos : position_) {
            pos_str += std::to_string(pos) + " ";
        }
        return pos_str;
    }

    /**
     * Give visually-representative string repr of tree.
     * @return Serialized version of tree.
     */
    [[nodiscard]] std::string ToString(int level = 0) const
    {
        // Primarily for debugging.
        std::string res, tabs(level, '\t');
        res += tabs + "HASH: " + std::string(hash_) + "\n";
        res += tabs + "KEY: " + std::string(max_key_);

        if(IsLeaf()) {
            res += tabs + "DATA:\n";
            for(const auto &[key, val] : data_) {
                res += tabs + '\t' + std::string(key) + ":" + std::string(val) +
                       '\n';
            }
        }
        else {
            res += tabs + "CHILDREN:\n";
            for(int i = 0; i < num_children_; ++i) {
                res += tabs + "CHILD " + std::to_string(i) + ": {";

                if(child_nodes_.at(i).hash_ == ChordKey("O", true)) {
                    res += " EMPTY ";
                } else {
                    res += "\n" + child_nodes_.at(i).ToString(level + 1);
                }
                res += "}\n";
            }
        }

        return res;
    }

    /**
     * You can't send a whole merkle tree over a network easily, so it's better
     * to have a method where we can serialize only a given node and its children.
     * If, upon inspection, a node wishes to see one of the children, it can
     * subsequently request its children. This method simply serializes a node
     * and its children in JSON format.
     * @param children Should we include this node's children (allows a recursive
     *                 call).
     * @return Node and its children (w/o their children) as JSON.
     */
    [[nodiscard]] Json::Value NonRecursiveSerialize(bool children = true) const
    {
        Json::Value node;
        node["HASH"] = std::string(hash_);
        node["MIN_KEY"] = std::string(min_key_);
        node["KEY"] = std::string(max_key_);

        if(IsLeaf()) {
            Json::Value kv_pairs;
            for(const auto &[key, val] : data_) {
                kv_pairs[std::string(key)] = std::string(val);
            }
            node["KV_PAIRS"] = kv_pairs;
        }

        else if(children) {
            node["CHILDREN"] = Json::arrayValue;
            for(const auto &child : child_nodes_) {
                node["CHILDREN"].append(child.NonRecursiveSerialize(false));
            }
        }

        node["POSITION"] = Json::arrayValue;
        for(int dir : position_) {
            node["POSITION"].append(dir);
        }

        return node;
    }

    /**
     * Serialize an entire tree as JSON.
     * @return Recursively-generated JSON merkle tree.
     */
    explicit operator Json::Value() const
    {
        Json::Value node;
        node["HASH"] = std::string(hash_);
        node["MIN_KEY"] = std::string(min_key_);
        node["KEY"] = std::string(max_key_);

        if(IsLeaf()) {
            Json::Value kv_pairs;
            for(const auto &[key, val] : data_) {
                kv_pairs[std::string(key)] = std::string(val);
            }
            node["KV_PAIRS"] = kv_pairs;
        }

        else {
            node["CHILDREN"] = Json::arrayValue;
            for(const auto &child : child_nodes_) {
                node["CHILDREN"].append(Json::Value(child));
            }
        }

        node["POSITION"] = Json::arrayValue;
        for(int dir : position_) {
            node["POSITION"].append(dir);
        }

        return node;
    }

    /**
     * Comparison operator.
     * @param lhs Left hand side.
     * @param rhs Right hand side.
     * @return Do the sides have identical positions and hashes?
     */
    friend bool operator ==(const NodeType &lhs, const NodeType &rhs)
    {
        // Two (sub)trees are equal if they occupy the same range of the key-
        // space (i.e. if their positions are equal), and if they contain ident-
        // ical kv pairs (i.e. their hashes are equal).
        return lhs.position_ == rhs.position_ && lhs.hash_ == rhs.hash_;
    }

private:
    /// Range held by this subtree and its hash.
    ChordKey min_key_, max_key_, hash_;

    /// To traverse from the root node of the tree to this node, go select the
    /// nth child n = position_[depth].
    std::deque<int> position_;

    /// Number of children per node.
    static int num_children_;

    /// List of children of this node. Empty at leaf nodes.
    std::vector<NodeType> child_nodes_;

    /// Key values held at this node. Empty for internal nodes.
    std::map<ChordKey, ValType> data_;
    KvSet leaves_;

    /// Largest key stored in this subtree (makes "Next" much more efficient).
    std::optional<ChordKey> largest_key_;

    /**
     * Turn a leaf node into an internal node.
     */
    void ToInternal()
    {
        CreateChildren();
    }

    /**
     *
     * @param key
     * @return
     */
    [[nodiscard]] unsigned long ChildNum(const ChordKey &key) const
    {
        if(key >= max_key_) {
            return num_children_ - 1;
        }

        if(key < min_key_) {
            return 0;
        }

        mp::cpp_int key_num(key);
        mp::cpp_int key_space = mp::pow(mp::cpp_int(ChordKey::Base()),
                                        ChordKey::Size());
        mp::cpp_bin_float_100 key_len = ChordKey::BinaryLen(),
                              child_id_len = log2(num_children_);
        int shift(key_len - (child_id_len * (GetDepth() + 1)));
        mp::cpp_int shifted_key = (key_num >> shift);
        return (unsigned long) (shifted_key & (num_children_ - 1));
    }

    void Rehash()
    {
        std::string concatenated_keys;
        if(IsLeaf()) {
            if(data_.empty()) {
                hash_ = ChordKey (0);
                return;
            }

            for(const auto &[key, _] : data_) {
                concatenated_keys += std::string(key);
            }
        }
        else {
            for(const auto &node : child_nodes_) {
                concatenated_keys += node.GetHash();
            }

            if(concatenated_keys == std::string(num_children_, '0')) {
                hash_ = ChordKey(0);
                return;
            }
        }

        hash_ = ChordKey(concatenated_keys, false);
    }

    /**
     * Create num_children_ children for this node, subdividing its range among
     * them. Spread kv pairs among these new children.
     */
    void CreateChildren()
    {
        mp::uint256_t key_range(max_key_ - min_key_),
                      last_key = min_key_;
        for(int i = 0; i < num_children_; ++i) {
            mp::uint256_t ub = last_key + (key_range / num_children_);
            std::deque<int> child_pos = position_;
            child_pos.push_back(i);

            child_nodes_.emplace_back(ChordKey(last_key), ChordKey(ub),
                                      child_pos);

            // Note that data_ is a map of ChordKeys and therefore ordered with
            // increasing keys.
            while(data_.begin() != data_.end() &&
                  data_.begin()->first.InBetween(last_key, ub - 1, true))
            {
                child_nodes_.back().data_.insert(*data_.begin());
                data_.erase(data_.begin());
            }
            child_nodes_.back().Rehash();

            last_key = ub;
        }
    }

    /**
     * @return Does this subtree have any data contained within it?
     */
    [[nodiscard]] bool HasData() const
    {
        return hash_ != ChordKey(0);
    }
};

template<typename ValType>
int MerkleTree<ValType>::num_children_ { 8 };

#endif