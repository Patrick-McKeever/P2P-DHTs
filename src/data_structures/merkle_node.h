/**
 * [DEPRECATED] - PROJECT NOW USES MERKLE_TREE.H INSTEAD.
 * merkle_node.h
 *
 * This file aims to implement a compact sparse Merkle tree, a data type
 * described (w/ pseudocode) here: https://eprint.iacr.org/2018/955.pdf
 *
 * As outlined in Josh Cates' 2003 thesis, each peer in a DHash system, in
 * addition to maintaining a database of keys and values, should maintain a
 * database index through which it can easily compare its own database contents
 * with that of other nodes. The Merkle tree, a type of hash tree in which each
 * node is assigned a hash by hashing the concatenation of its children's
 * hashes, offers quick lookups and easy comparison between trees
 * (i.e. if the root nodes have the same hash, the trees are identical).
 * Cates also calls for predictable lookups, a divergence from the standard
 * Merkle tree algorithm. Though he does not use the term, the data structure
 * he refers to is nearly-identical to the compact sparse Merkle tree, in which
 * new keys are inserted based on the "distance" (floor(log2(key1 ^ key2)))
 * between the new key and the left and right branches of any given node.
 *
 *
 * This file should accomplish as follows:
 *      - Implement a class "CSMerkleNode", an implementation of the compact
 *        sparse Merkle node algorithm referenced above;
 *      - Implement a method for key lookups within that class;
 *      - Implement a method for predictable key insertions within that class.
 *
 * The use of this "CSMerkleNode" class as a database index on DHash peers will
 * enable database synchronization.
 */

#ifndef CHORD_FINAL_MERKLE_TREE_H
#define CHORD_FINAL_MERKLE_TREE_H

#include <utility>
#include <cmath>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <json/json.h>
#include "key.h"

namespace mp = boost::multiprecision;

/// Because log2 requires this type, and because this type has only one
/// application in this file, I will refer to the type based on this single
/// application: representing the distance between two keys.
typedef mp::cpp_bin_float_100 KeyDist;

/**
 * Return the distance between two keys.
 *
 * @param key1 First key.
 * @param key2 Second key.
 * @return floor(log_base2(key1 ^ key2))
 */
static KeyDist Distance(const ChordKey &key1, const ChordKey &key2)
{
    mp::uint256_t xor_keys = mp::uint256_t(key1) ^ mp::uint256_t(key2);
    return floor(log2(mp::cpp_bin_float_100(xor_keys)));
}

/**
 * Concatenate hash representations of two keys, hash the result.
 *
 * @param key1 First key.
 * @param key2 Second key.
 * @return Hash of string(first key) + string(second key).
 */
static ChordKey ConcatHash(const ChordKey &key1, const ChordKey &key2)
{
    return ChordKey(std::string(key1) + std::string(key2), false);
}

/**
 * Implementation of a compact sparse Merkle node.
 * @tparam DataType The type of data being stored as values in leaf nodes.
 */
template<typename DataType>
class CSMerkleNode {
public:
    using KvMap = std::map<ChordKey, DataType>;
    using KvPair = std::pair<ChordKey, DataType>;
    using NodeType = CSMerkleNode<DataType>;

    /**
     * Constructor 1. Construct leaf node with given key and value.
     * @param key Key of the new node.
     * @param val Value of the new node.
     */
    explicit CSMerkleNode(ChordKey key, DataType val)
        : key_(std::move(key))
        , value_(std::move(val))
        , hash_(val, false)
    {}

    /**
     * Constructor 2. Construct an interior node given its left and right child.
     * @param left Left child.
     * @param right Right child.
     */
    CSMerkleNode(CSMerkleNode *left, CSMerkleNode *right)
        : left_(left)
        , right_(right)
    {
        if(left != nullptr && right != nullptr) {
            key_ = left->key_ > right->key_ ? left->key_ : right->key_;
            hash_ = ConcatHash(left->hash_, right->hash_);
        }
    }

    /**
     * Constructor 3. Construct from JSON.
     * @param json_node JSON repr of node.
     */
    CSMerkleNode(const Json::Value &json_node)
        : hash_(json_node["HASH"].asString(), true)
        , key_(json_node["KEY"].asString(), true)
    {
        if(json_node.get("VALUE", NULL) != NULL) {
            value_ = DataType(json_node["VALUE"].asString());
        }

        if(json_node.get("LEFT", NULL) != NULL) {
            left_ = new CSMerkleNode<DataType>(json_node["LEFT"]);
        }

        if(json_node.get("RIGHT", NULL) != NULL) {
            right_ = new CSMerkleNode<DataType>(json_node["RIGHT"]);
        }

        for(const auto &dir : json_node["POSITION"]) {
            position_.push_back(dir.asBool());
        }

        root_ = new CSMerkleNode<DataType>(right_, left_);
    }

    /**
     * Copy constructor. Copy ptr values, not ptrs themselves.
     * @param node Node to copy.
     */
    CSMerkleNode(const CSMerkleNode &node)
        : hash_(node.hash_)
        , key_(node.key_)
        , position_(node.position_)
    {
        if(node.left_ != nullptr) {
            left_ = new CSMerkleNode(*node.left_);
        } if(node.right_ != nullptr) {
            right_ = new CSMerkleNode(*node.right_);
        }
    }

    /**
     * Copy assignment operator.
     * @param rhs Right hand side.
     * @return A version of this node which won't result in ptr errors.
     */
    CSMerkleNode &operator=(const CSMerkleNode &rhs)
    {
        assert(right_ != this);
        assert(left_ != this);

        if(&rhs == this) {
            return *this;
        }

        if(rhs.left_) {
            *left_ = *rhs.left_;
        } else {
            left_ = nullptr;
        }

        if(rhs.right_) {
            *right_ = *rhs.right_;
        } else {
            right_ = nullptr;
        }

        if(rhs.root_) {
            *root_ = *rhs.root_;
        } else {
            root_ = nullptr;
        }

        position_ = rhs.position_;
        hash_ = rhs.hash_;
        key_ = rhs.key_;

        return *this;
    }

    /**
     * Destructor.
     */
    ~CSMerkleNode()
    {
        // Note that root_->left_ points to left_ (same w/ right_), so deleting
        // left_ would trigger a double free.
        delete root_;
    }

    /**
     * Public-facing insert. Insert key-value pair into root.
     * @param key Key to insert.
     * @param val Value to insert.
     */
    void Insert(const ChordKey &key, const DataType &val)
    {
        // If this key has no children, just create new node as child.
        if(root_) {
            root_ = Insert(root_, key, val);
            left_ = root_->left_;
            right_ = root_->right_;
            hash_ = root_->hash_;
            key_ = root_->key_;
        } else {
            root_ = new CSMerkleNode(key, val);
            left_ = root_->left_;
            right_ = root_->right_;
            hash_ = root_->hash_;
            key_ = root_->key_;
        }

        // Correct all nodes' positions.
        root_->FixPositions({});
        FixPositions({});
    }

    /**
     * Find value associated with key.
     * @param key Key to lookup.
     * @return Value associated with it or error if key is not in tree.
     */
    DataType Lookup(const ChordKey &key)
    {
        if(root_) {
            return Lookup(root_, key);
        } else {
            throw std::runtime_error("key does not exist in tree");
        }
    }

    /**
     * Return a map of all kv-pairs in tree with keys between lower_bound and
     * upper_bound.
     * @param lower_bound Lower bound of range.
     * @param upper_bound Upper bound of range.
     * @return Map of kv pairs within range.
     */
    KvMap ReadRange(const ChordKey &lower_bound, const ChordKey &upper_bound)
    {
        if(root_) {
            return ReadRange(root_, lower_bound, upper_bound);
        } else {
            return {};
        }
    }

    /**
     * Update value of key to new_val if it exists in tree.
     * @param key Key whose value will be altered.
     * @param new_val Value to which it will be altered.
     */
    void Update(const ChordKey &key, const DataType &new_val)
    {
        if(root_) {
            root_ = Update(root_, key, new_val);
            left_ = root_->left_;
            right_ = root_->right_;
            hash_ = root_->hash_;
            key_ = root_->key_;
            root_->FixPositions({});
        } else {
            throw std::runtime_error("key does not exist in tree");
        }
    }

    /**
     * Delete key from tree.
     * @param key Key to delete.
     */
    void Delete(const ChordKey &key)
    {
        if(root_) {
            root_ = Delete(root_, key);
            if(root_) {
                left_ = root_->left_;
                right_ = root_->right_;
                hash_ = root_->hash_;
                key_ = root_->key_;
                root_->FixPositions({});
            }
        } else {
            throw std::runtime_error("key does not exist in tree");
        }
    }

    /**
     * Return the next-greatest key after a given key in the merkle tree.
     * @param key Key to lookup in tree and find the next greatest-kv pair.
     * @return Next-greatest key value pair in the tree after the given key.
     */
    std::optional<KvPair> Next(const ChordKey &key)
    {
        if(! root_)
            return std::nullopt;

        if(root_->IsLeaf()) {
            if(key == key_) {
                return std::nullopt;
            }

            KvPair root { key_, root_->value_.value() };
            return root;
        }


        CSMerkleNode *next_key = Next(root_, key);
        if(next_key == nullptr) {
            return std::nullopt;
        }

        KvPair result { next_key->key_, next_key->value_.value() };
        return { result };
    }

    /**
     * @return Is the node in question a leaf?
     */
    [[nodiscard]] bool IsLeaf() const
    {
        return left_ == nullptr && right_ == nullptr;
    }

    /**
     * Determine whether tree contains specified key.
     * @param key Key to find in tree.
     * @return Does tree contain key?
     */
    [[nodiscard]] bool Contains(const ChordKey &key) const
    {
        if(root_) {
            return Contains(root_, key);
        } else {
            return false;
        }
    }

    std::optional<CSMerkleNode> LookupPosition(std::deque<bool> directions)
    {
        if(directions.empty()) {
            if(left_ && right_) {
                return CSMerkleNode(left_, right_);
            } else {
                return std::nullopt;
            }
        }

        CSMerkleNode *next_node = directions.front() ? right_ : left_;

        if(! next_node) {
            return std::nullopt;
        }

        directions.pop_front();
        if(directions.empty()) {
            return *next_node;
        }

        return next_node->LookupPosition(directions);
    }

    /**
     * Does this subtree contain any keys within the given range?
     * @param key_range Pair of keys indicating upper and lower bound.
     * @return Whether or not subtree range overlaps given range.
     */
    bool Overlaps(const std::pair<ChordKey, ChordKey> &key_range) const
    {
        ChordKey lower_bound = key_range.first, upper_bound = key_range.second;

        if(IsLeaf()) {
            return key_.InBetween(lower_bound, upper_bound, true);
        }

        return lower_bound.InBetween(GetMinKey(), key_, true) ||
               upper_bound.InBetween(GetMinKey(), key_, true);
    }

    /**
     * Recurse down tree to find minimum key in it.
     * @return Key of leftmost node in subtree.
     */
    ChordKey GetMinKey() const
    {
        if(IsLeaf()) {
            return key_;
        }
        return left_->GetMinKey();
    }

    /**
     * Accessor for left node.
     * @return Left node if it exists, nullopt otherwise.
     */
    std::optional<CSMerkleNode> GetLeft() const
    {
        if(left_) {
            return *left_;
        }
        return std::nullopt;
    }

    /**
     * Accessor for right node.
     * @return Right node if it exists, nullopt otherwise.
     */
    std::optional<CSMerkleNode> GetRight() const
    {
        if(right_) {
            return *right_;
        }
        return std::nullopt;
    }

    /**
     * Accessor for node key.
     * @return key_.
     */
    [[nodiscard]] ChordKey GetKey() const
    {
        return key_;
    }

    /**
     * Accessor for hash of merkle node.
     * @return hash of this node.
     */
    [[nodiscard]] ChordKey GetHash() const
    {
        return hash_;
    }

    /**
     * Accessor for position of this node.
     * @return vector of bools with false indicating left, true indicating
     *         right.
     */
    [[nodiscard]] std::deque<bool> GetPosition() const
    {
        return position_;
    }

    /**
     * Give visually-representative string repr of tree.
     * @return Serialized version of tree.
     */
    [[nodiscard]] std::string ToString() const
    {
        return ToString(0);
    }

    /**
     * Used to exchange nodes in DHash synchronize by listing the only the node
     * and its left and right keys.
     * @param children Should we give only the node or its children as well?
     * @return Node info and its left and right keys.
     */
    [[nodiscard]] Json::Value NonRecursiveSerialize(bool children) const
    {
        Json::Value node;
        node["HASH"] = std::string(hash_);
        node["KEY"] = std::string(key_);
        if(value_.has_value()) {
            node["VALUE"] = std::string(value_.value());
        }

        node["POSITION"] = Json::arrayValue;
        for(bool dir : position_) {
            node["POSITION"].append(dir);
        }

        if(children && left_) {
            node["LEFT"] = left_->NonRecursiveSerialize(false);
        }

        if(children && right_) {
            node["RIGHT"] = right_->NonRecursiveSerialize(false);
        }

        return node;
    }

    /**
     * @return CSMerkleTree as JSON.
     */
    explicit operator Json::Value() const
    {
        Json::Value node;
        node["HASH"] = std::string(hash_);
        node["KEY"] = std::string(key_);
        if(value_.has_value()) {
            node["VALUE"] = value_.value();
        }

        if(left_ != nullptr) {
            node["LEFT"] = Json::Value(left_);
        }

        if(right_ != nullptr) {
            node["RIGHT"] = Json::Value(right_);
        }

        node["POSITION"] = Json::arrayValue;
        for(bool dir : position_) {
            node["POSITION"].append(dir);
        }

        return node;
    }

private:
    /// key_ denotes key of kv pair on leaf node and maximum key of subtree on
    /// non-leaf node.
    /// hash_ denotes hash of value on leaf node and hash of concatenated hashes
    /// of child nodes on non-leaf nodes.
    ChordKey key_, hash_;

    /// value_ of kv pair. Will be empty for non-leaf nodes.
    std::optional<DataType> value_;

    /// Left child, right child, root object of this node.
    CSMerkleNode *left_ = nullptr, *right_ = nullptr, *root_ = nullptr;

    /// What directions must one travel to get from the top of the tree to
    /// here? 0s indicate lefts, 1s indicate rights.
    std::deque<bool> position_;

    /**
     * Insert kv pair as descendant of root.
     * @param root Root to which kv pair will be inserted.
     * @param key Key to insert.
     * @param val Val to insert.
     * @return Modified version of root with kv pair inserted.
     */
    CSMerkleNode *Insert(CSMerkleNode *root, const ChordKey &key,
                         const DataType &val)
    {
        if(root->IsLeaf()) {
            return InsertLeaf(root, key, val);
        }


        if(root->left_->IsLeaf() && root->left_->key_ == key) {
            root->left_ = Insert(root->left_, key, val);
            return new CSMerkleNode(root->left_, root->right_);
        } else if(root->right_->IsLeaf() && root->right_->key_ == key) {
            root->right_ = Insert(root->right_, key, val);
            return new CSMerkleNode(root->left_, root->right_);
        }

        // To ensure predictable logarithmic lookups, we determine where to place
        // a node based on the "distance" (floor(log2(key1 ^ key2))) of the key
        // in question from the left and right keys.
        KeyDist l_dist = Distance(key, root->left_->key_),
                r_dist = Distance(key, root->right_->key_);

        // If the distance between left and right branches are equal, choose the
        // branch w/ the lower hash.
        if(l_dist == r_dist) {
            auto *new_node = new CSMerkleNode(key, val);
            ChordKey min_key = left_->key_ < right_->key_ ? left_->key_ :
                               right_->key_;
            if(key < min_key) {
                return new CSMerkleNode(new_node, root);
            } else {
                return new CSMerkleNode(root, new_node);
            }
        }

        // Otherwise, insert in the branch with the lower distance
        if(l_dist < r_dist) {
            root->left_ = Insert(root->left_, key, val);
        } else {
            root->right_ = Insert(root->right_, key, val);
        }

        return new CSMerkleNode(root->left_, root->right_);
    }

    /**
     * Take a leaf node, place it and key as children of new node, return
     * new node.
     *
     * @param leaf Leaf which will be sibling of key.
     * @param key ChordKey to insert as sibling of leaf.
     * @param val Val to insert.
     * @return Pointer to the parent of leaf and key.
     */
    static CSMerkleNode *InsertLeaf(CSMerkleNode *leaf, const ChordKey &key,
                                    const DataType &val)
    {
        // Take a leaf node, return a new node (to replace the leaf node),
        // with the leaf node and a new node (constructed from key) as children.
        if(leaf->key_ == key) {
            leaf->value_ = val;
            return leaf;
        }

        auto *new_leaf = new CSMerkleNode(key, val);
        if(key < leaf->key_) {
            return new CSMerkleNode(new_leaf, leaf);
        } else if(key > leaf->key_) {
            return new CSMerkleNode(leaf, new_leaf);
        } else {
            return leaf;
        }
    }

    /**
     * Descend tree recursively according to distance between key and child
     * nodes' keys, and find the value associated with key.
     * @param root (sub)tree from which we will attempt to locate key.
     * @param key Key to lookup.
     * @return Value associated with key.
     */
    DataType Lookup(CSMerkleNode *root, const ChordKey &key) const
    {
        // To see if the three contains a given key, we retrace the steps
        // taken by the insertion algorithm until we hit a leaf. This leaf
        // will be the location of the key if it exists inside the tree.
        if(root->IsLeaf() && root->key_ == key) {
            return root->value_.value();
        }

        if(root->left_->IsLeaf() && root->left_->key_ == key) {
            return root->left_->value_.value();
        }

        if(root->right_->IsLeaf() && root->right_->key_ == key) {
            return root->right_->value_.value();
        }

        KeyDist l_dist = Distance(key, root->left_->key_),
                r_dist = Distance(key, root->right_->key_);

        if(l_dist < r_dist) {
            return Lookup(root->left_, key);
        } else if(r_dist < l_dist) {
            return Lookup(root->right_, key);
        } else {
            throw std::runtime_error("Value not in tree");
        }
    }

    /**
     * Return a map of all kv-pairs in subtree root with keys between specified
     * lower and upper bound.
     * @param root (sub)tree to search.
     * @param lower_bound Lower bound of kv pairs to find.
     * @param upper_bound Upper bound.
     * @return Map of kv pairs in root between lower and upper bound.
     */
    KvMap ReadRange(CSMerkleNode *root, const ChordKey &lower_bound,
                    const ChordKey &upper_bound)
    {
        KvMap results;

        if(root->IsLeaf()) {
            if(root->key_.InBetween(lower_bound, upper_bound, true)) {
                return {{ root->key_, root->value_.value() }};
            } else {
                return {{}};
            }
        }

        // The right node's keys are all greater than the left node's, so, if
        // the max key of the left child is less than the lower_bound, we need
        // to query the left side.
        if(lower_bound <= root->left_->key_) {
            bool left_leaf_in_range = root->left_->key_.InBetween(lower_bound,
                                                                  upper_bound,
                                                                  true);

            if(root->left_->IsLeaf() && left_leaf_in_range) {
                results.insert({ root->left_->key_,
                                 root->left_->value_.value() });
            } else {
                KvMap subtree_results = ReadRange(root->left_, lower_bound,
                                                  upper_bound);
                results.insert(subtree_results.begin(), subtree_results.end());
            }
        }

        // The left node's keys are all less than the right node's keys.
        // This means that we only need to query the right node if the left
        // node's upper bound is less than the query's upper bound.
        if(root->left_->key_ <= upper_bound) {
            bool right_leaf_in_range = root->right_->key_.InBetween(lower_bound,
                                                                    upper_bound,
                                                                    true);

            if(root->right_->IsLeaf() && right_leaf_in_range) {
                results.insert({ root->right_->key_,
                                 root->right_->value_.value() });
            } else {
                KvMap subtree_results = ReadRange(root->right_,
                                                  root->left_->key_,
                                                  upper_bound);
                results.insert(subtree_results.begin(), subtree_results.end());
            }
        }

        return results;
    }

    /**
     * Update value of key given the subtree in which it exists.
     * @param root Subtree to search for key.
     * @param key Key whose value will be altered.
     * @param new_val Value to be associated with key.
     * @return Altered subtree.
     */
    CSMerkleNode *Update(CSMerkleNode *root, const ChordKey &key,
                         const DataType &new_val)
    {
        // We've been through this already. Descend the tree while minimizing
        // distance between key and node, update the leaf node if it exists.
        if(root->IsLeaf()) {
            if(root_->key_ == key) {
                return new CSMerkleNode<DataType>(key, new_val);
            } else {
                return root;
            }
        }

        if(root->left_->IsLeaf() && root->left_->key_ == key) {
            root->left_ = Update(root->left_, key, new_val);
            return new CSMerkleNode(root->left_, root->right_);
        }
        if(root->right_->IsLeaf() && root_->right_->key_ == key) {
            root->right_ = Update(root->right_, key, new_val);
            return new CSMerkleNode(root->left_, root->right_);
        }

        KeyDist l_dist = Distance(key, root->left_->key_),
                r_dist = Distance(key, root->right_->key_);

        if(l_dist == r_dist) {
            return root;
        } else if(l_dist < r_dist) {
            root->left_ = Update(root->left_, key, new_val);
        } else {
            root->right_ = Update(root->right_, key, new_val);
        }

        return new CSMerkleNode(root->left_, root->right_);
    }

    /**
     * Delete key from root, reorder remaining leaves, return modified root.
     *
     * @param root The node from which key will be deleted.
     * @param key ChordKey to delete.
     * @return New node with key deleted and other leaves reorganized.
     */
    CSMerkleNode *Delete(CSMerkleNode *root, const ChordKey &key)
    {
        // Deletes work essentially the same as insertion.
        if(root->IsLeaf()) {
            if(root_->key_ == key) {
                return nullptr;
            } else {
                return root;
            }
        }

        // If we need to delete left node, the right node will simply replace
        // its parent and vice versa.
        if(root->left_->IsLeaf() && root->left_->key_ == key) {
            return root->right_;
        }

        if(root->right_->IsLeaf() && root_->right_->key_ == key) {
            return root->left_;
        }

        KeyDist l_dist = Distance(key, root->left_->key_),
                r_dist = Distance(key, root->right_->key_);

        // In this case, the requested key doesn't exist in the tree.
        if(l_dist == r_dist) {
            return root;
        } else if(l_dist < r_dist) {
            root->left_ = Delete(root->left_, key);
        } else {
            root->right_ = Delete(root->right_, key);
        }

        return new CSMerkleNode(root->left_, root->right_);
    }

    /**
     * Return leaf node containing the next-greatest kv pair after a key in the
     * subtree root.
     * @param root Subtree to search for the next-greatest key after key.
     * @param key The key before the key that will be returned.
     * @return Leaf node containing next-greatest key value pair in root after
     *         key in question if it exists, else nullptr.
     */
    CSMerkleNode *Next(CSMerkleNode *root, const ChordKey &key) const
    {
        if(root->IsLeaf()) {
            return nullptr;
        }

        // If left node is key, then its right sibling is necessarily the next
        // greatest.
        if(root->left_->IsLeaf() && root->left_->key_ == key) {
            return root->right_;
        }

        // If the rightmost node in the subtree contains the key, then there
        // is no greater key in the subtree.
        if(root->right_->IsLeaf() && root->right_->key_ == key) {
            return nullptr;
        }

        // If the key is the max key in the left subtree, then the next greatest
        // key is in the right subtree.
        if(root->left_->key_ <= key) {
            return Next(root->right_, key);
        }

        // Otherwise, it will be in the left subtree.
        return Next(root->left_, key);
    }

    /**
     * Private "Contains". Test if root contains key.
     *
     * @param root The node whose successors will be checked.
     * @param key The key to check for.
     * @return Is key a descendant of root?
     */
    bool Contains(CSMerkleNode *root, const ChordKey &key) const
    {
        // To see if the three contains a given key, we retrace the steps
        // taken by the insertion algorithm until we hit a leaf. This leaf
        // will be the location of the key if it exists inside the tree.
        if(root->IsLeaf()) {
            return root->key_ == key;
        }

        if(root->left_->IsLeaf() && root->left_->key_ == key) {
            return true;
        }

        if(root->right_->IsLeaf() && root->right_->key_ == key) {
            return true;
        }

        KeyDist l_dist = Distance(key, root->left_->key_),
                r_dist = Distance(key, root->right_->key_);

        if(l_dist < r_dist) {
            return Contains(root->left_, key);
        } else if(r_dist < l_dist) {
            return Contains(root->right_, key);
        } else {
            return false;
        }
    }

    /**
     * Navigate through this subtree, recursively assign all nodes their correct
     * positions.
     * @param dirs The set of directions that will be assigned to this node.
     *             A packed bit array in which 0s indicate lefts and 1s indicate
     *             right which allow us to navigate from the top of the tree
     *             to the bottom.
     */
    void FixPositions(const std::deque<bool> &dirs) {
        position_ = dirs;
        if(left_) {
            std::deque<bool> l_dirs = dirs;
            l_dirs.push_back(0);
            left_->FixPositions(l_dirs);
        }

        if(right_) {
            std::deque<bool> r_dirs = dirs;
            r_dirs.push_back(1);
            right_->FixPositions(r_dirs);
        }
    }

    /**
     * Private string converter. Convert node to string.
     *
     * @param level Level of recursion at which function was called (0 for root
     *              node).
     * @return Recurisvely-generated string representing node and its children,
     *         in form:
     *              "HASH: [HASH]"
     *              "KEY: [KEY]"
     *              "VALUE: [VALUE]"
     *              "LEFT: {"
     *              "   [LEFT_CHILD_STRING]"
     *              "}"
     *              "RIGHT: {"
     *              "   [RIGHT_CHILD_STRING]"
     *              "}"
     */
    [[nodiscard]] std::string ToString(int level) const
    {
        // Primarily for debugging.
        std::string res, tabs(level, '\t');
        res += tabs + "HASH: " + std::string(hash_) + "\n";
        res += tabs + "KEY: " + std::string(key_);

        if(value_.has_value()) {
            res += "\n" + tabs + "VALUE: " + std::string(value_.value());
        }

        if(! position_.empty()) {
            res += "\n" + tabs + "POSITION:";
            for(bool dir : position_) {
                res += " " + std::to_string(dir);
            }
        }

        if(left_) {
            res += "\n" + tabs + "LEFT: {\n" + left_->ToString(level + 1) +
                   "\n" + tabs + "}";
        } if(right_) {
            res += "\n" + tabs + "RIGHT: {\n" + right_->ToString(level + 1) +
                   "\n" + tabs + "}";
        }

        return res;
    }
};

#endif