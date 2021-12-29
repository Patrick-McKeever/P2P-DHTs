#include "../src/data_structures/merkle_tree.h"
#include "../src/data_structures/merkle_node.h"
#include <gtest/gtest.h>

TEST(MerkleNode, CopyAssignment)
{
    CSMerkleNode<std::string> a(nullptr, nullptr);

    if(true) {
        CSMerkleNode<std::string> b(nullptr, nullptr);
        for(int i = 0; i < 10; ++i) {
            std::string key_str(32, '0' + i);
            ChordKey key_to_insert(key_str, true);
            b.Insert(key_to_insert, std::string("asdf"));
            for(int j = 0; j < 32; ++j) {
                a.Insert(key_to_insert + j, std::string(key_to_insert + j));
            }
        }
        a = b;
    }

    std::cout << a.ToString() << std::endl;
}

TEST(MerkleTree, Insert)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> kvs;
    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        for(int j = 0; j < 32; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });
            kvs.insert({ key_to_insert + j, std::string(key_to_insert + j) });
        }
    }

    for(const auto &[k, v] : kvs) {
        EXPECT_EQ(tree.Lookup(k), v);
        EXPECT_TRUE(tree.Contains(k));
    }
}

TEST(MerkleTree, ReadRange)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> no_mod_res, with_mod_res;
    ChordKey lb("22222222222222222222222222222222", true),
             ub("44444444444444444444444444444444", true);

    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        for(int j = 0; j < 32; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });

            if((key_to_insert + j).InBetween(lb, ub, true))
                no_mod_res.insert({ key_to_insert + j,
                                    std::string(key_to_insert + j) });

            if((key_to_insert + j).InBetween(ub, lb, true))
                with_mod_res.insert({ key_to_insert + j,
                                      std::string(key_to_insert + j) });
        }
    }

    EXPECT_EQ(tree.ReadRange(lb, ub), no_mod_res);
    EXPECT_EQ(tree.ReadRange(ub, lb), with_mod_res);
}

TEST(MerkleTree, Next)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> results;
    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        // Exceeding the number of children per row allows us to text if our
        // methods work after having converted leaf nodes to internal ones.
        for(int j = 0; j < 17; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });
            results.insert({ key_to_insert + j, std::string(key_to_insert + j) });
        }
    }

    std::pair<ChordKey, std::string> first_key = *results.begin();
    for(auto it = results.begin(); it != (--results.end());) {
        std::optional<MerkleTree<std::string>::KvPair> next = tree.Next(it->first);
        EXPECT_EQ(next->first, (++it)->first);
    }

    // Calling next on final key in tree should return the first key in the tree
    EXPECT_EQ(tree.Next((--results.end())->first)->first,
              results.begin()->first);
}

TEST(MerkleTree, Update)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> results;
    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        // Exceeding the number of children per row allows us to text if our
        // methods work after having converted leaf nodes to internal ones.
        for(int j = 0; j < 17; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });
            results.insert({ key_to_insert + j, std::string(key_to_insert + j) });
        }
    }

    ChordKey hash_before = tree.GetHash();

    for(const auto &[k, v] : results) {
        std::string updated_val = v + "_updated";
        tree.Update({ k, updated_val });
        results.at(k) = updated_val;
    }

    ChordKey hash_after = tree.GetHash();
    EXPECT_NE(hash_before, hash_after);

    for(const auto &[k, updated_v] : results)
        EXPECT_EQ(tree.Lookup(k), updated_v);
}

TEST(MerkleTree, Delete)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> results;
    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        // Exceeding the number of children per row allows us to text if our
        // methods work after having converted leaf nodes to internal ones.
        for(int j = 0; j < 17; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });
            results.insert({ key_to_insert + j, std::string(key_to_insert + j) });
        }
    }

    for(int i = 0; i < 40; ++i) {
        ChordKey key_to_delete = (++results.begin())->first;
        tree.Delete(key_to_delete);
        EXPECT_ANY_THROW(tree.Lookup(key_to_delete));
        results.erase(key_to_delete);
    }
}

TEST(MerkleTree, Json)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> results;
    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        // Exceeding the number of children per row allows us to text if our
        // methods work after having converted leaf nodes to internal ones.
        for(int j = 0; j < 17; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });
            results.insert({ key_to_insert + j, std::string(key_to_insert + j) });
        }
    }

    Json::Value to_json(tree);
    MerkleTree<std::string> from_json(to_json);
    EXPECT_EQ(from_json, tree);

    // This is necessary to test that the from-JSON constructor properly
    // recursed and copied all leaf nodes and associated keys.
    for(const auto &[k, v] : results)
        EXPECT_EQ(from_json.Lookup(k), v);
}

TEST(MerkleTree, GetEntries)
{
    MerkleTree<std::string> tree;
    std::map<ChordKey, std::string> results;
    for(int i = 0; i < 10; ++i) {
        std::string key_str(32, '0' + i);
        ChordKey key_to_insert(key_str, true);
        // Exceeding the number of children per row allows us to text if our
        // methods work after having converted leaf nodes to internal ones.
        for(int j = 0; j < 17; ++j) {
            tree.Insert({ key_to_insert + j, std::string(key_to_insert + j) });
            results.insert({ key_to_insert + j, std::string(key_to_insert + j) });
        }
    }

    EXPECT_EQ(tree.GetEntries(), results);
    std::cout << Json::Value(tree).toStyledString() << std::endl;
}

TEST(MerkleTree, Insert12)
{
    MerkleTree<std::string> tree;
    tree.Insert({ ChordKey("asdfs", false), "asdf" });
}