#include <gtest/gtest.h>
#include "../src/chord/chord_peer.h"
#include "../src/dhash/dhash_peer.h"
#include "json_reader.h"


/**
 * The local maintenance protocol recursively descends through two nodes'
 * merkle trees to identify discrepancies in the stored keys within a specified
 * range. This allows two nodes to "synchronize" the stored keys within a cert-
 * ain range. After one node "synchronizes" a certain range with another node,
 * we expect that their databases will be identical within that range.
 *
 * Here, we test if our implementation of the synchronize operation fulfills
 * these conditions in simplest case: in which (1) there exists only a single
 * key difference between the two nodes' trees and (2) all keys stored in the
 * nodes' trees are located in the synchronized range.
 */

TEST(DHashSynchronize, AllKeysInRange)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "LocalMaintenanceTest.json");
    Json::Value test_info = test_json["DEPTH_ONE_SINGLE_KEY"];
    std::vector<std::shared_ptr<DHashPeer>> peers;

    // For testing purposes, it's easier to adjust IDA parameters such that the
    // IDA generates three fragments per datum and requiers only 2 fragments to
    // reconstruct the original datum.
    std::function<void(std::shared_ptr<DHashPeer>)> adjust_ida_params =
        [](std::shared_ptr<DHashPeer> peer) { peer->SetIdaParams(3, 2, 257); };

    ChordFromJson(test_info["PEERS"], peers, adjust_ida_params);

    ChordKey key_to_create(test_info["KEY_TO_INSERT"].asString());
    auto val_to_create = test_info["VAL_TO_INSERT"].asString();
    peers[0]->Create(ChordKey(key_to_create), DataBlock(val_to_create));

    AddJsonNodesToChord(test_info["PEERS_TO_JOIN"], peers, adjust_ida_params);

    RemotePeer new_peer = peers.back()->ToRemotePeer();
    peers[0]->Synchronize(new_peer, { peers[0]->min_key_.Get(), peers[0]->id_ });

    EXPECT_EQ(peers.back()->db_.GetIndex(), peers[0]->db_.GetIndex());
}

/**
 * Now, we test the case in which two nodes' merkle trees differ, but the
 * difference is outside the range which must be synchronized. In this case,
 * synchronize should not retrieve the missing key, since it is outside the
 * given range.
 */
TEST(DHashSynchronize, SynchronizeUsesGivenRange)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "LocalMaintenanceTest.json");
    Json::Value test_info = test_json["SYNCHRONIZE_USES_GIVEN_RANGE"];
    std::vector<std::shared_ptr<DHashPeer>> peers;

    std::function<void(std::shared_ptr<DHashPeer>)> adjust_ida_params =
        [](std::shared_ptr<DHashPeer> peer) { peer->SetIdaParams(3, 2, 257); };

    ChordFromJson(test_info["PEERS"], peers, adjust_ida_params);

    ChordKey key_to_insert(test_info["KEY_TO_INSERT"].asString());
    peers[0]->Create(key_to_insert, test_info["VAL_TO_INSERT"].asString());

    AddJsonNodesToChord(test_info["PEERS_TO_JOIN"], peers, adjust_ida_params);

    RemotePeer new_peer = peers.back()->ToRemotePeer();
    ChordKey lower_bound(test_info["SYNCHRONIZE_LOWER_BOUND"].asString()),
             upper_bound(test_info["SYNCHRONIZE_UPPER_BOUND"].asString());
    peers[0]->Synchronize(new_peer, { lower_bound, upper_bound });

    EXPECT_NE(peers.back()->db_.GetIndex(), peers[0]->db_.GetIndex());
}

/**
 * Here, we test that synchronize functions appropriately when forced to iterate
 * through several layers of keys. We create a merkle tree w/ 8+ adjacent on the
 * same leaf, forcing the conversion of that leaf into an internal node. Subseq-
 * uently, we instruct the tree to synchronize with a node containing an empty
 * merkle tree.
 * After this, we expect that both trees contain identical keys within the given
 * range. If this test passes, it indicates that synchronize has been
 * implemented so as to deal with the case where it must synchronize a local
 * tree with a remote tree with a different structure.
 */
TEST(DHashSynchronize, HighDepth)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "LocalMaintenanceTest.json");
    Json::Value test_info = test_json["HIGH_DEPTH"];
    std::vector<std::shared_ptr<DHashPeer>> peers;
    std::function<void(std::shared_ptr<DHashPeer>)> adjust_ida_params =
        [](std::shared_ptr<DHashPeer> peer) { peer->SetIdaParams(3, 2, 257); };
    ChordFromJson(test_info["PEERS"], peers, adjust_ida_params);

    Json::Value kvs = test_info["KEYS_TO_INSERT"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        peers[0]->Create(ChordKey(it.key().asString()), it->asString());
    }

    AddJsonNodesToChord(test_info["PEERS_TO_JOIN"], peers, adjust_ida_params);
    RemotePeer new_peer = peers.back()->ToRemotePeer();
    ChordKey lower_bound(test_info["SYNCHRONIZE_LOWER_BOUND"].asString()),
             upper_bound(test_info["SYNCHRONIZE_UPPER_BOUND"].asString());
    peers[0]->Synchronize(new_peer, { lower_bound, upper_bound });
    EXPECT_EQ(peers.back()->db_.GetIndex(), peers[0]->db_.GetIndex());
}

/**
 * Global maintenance should move misplaced keys to their correct successor.
 * A node should periodically run global maintenance by iterating through the
 * keys in its database, verifying that it is within the n successors which
 * ought to hold that key, and, if they are not, transferring the keys to one
 * of the n successors before deleting them from its own DB.
 *
 * We simulate a simple condition in which we insert several keys into the
 * database of a peer where they do not belong. After running global maintenance,
 * we attempt to verify that the keys have been returned to a correct successor.
 */
TEST(DHashGlobalMaintenance, MisplacedKeys)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "GlobalMaintenanceTest.json");
    Json::Value test_info = test_json["MISPLACED_KEYS"];
    std::vector<std::shared_ptr<DHashPeer>> peers;

    // Only the immediate peer following the key should possess it.
    std::function<void(std::shared_ptr<DHashPeer>)> adjust_ida_params =
        [](std::shared_ptr<DHashPeer> peer) { peer->SetIdaParams(2, 1, 257); };
    ChordFromJson(test_info["PEERS"], peers, adjust_ida_params);

    int tested_ind = test_info["TESTED_IND"].asInt();

    Json::Value kvs = test_info["KEYS_TO_INSERT"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        ChordKey key_to_insert(it.key().asString());
        DataBlock block(it->asString(), 2, 1, 257);
        DataFragment frag_to_insert = block.fragments_[0];
        peers[tested_ind]->db_.Insert({ key_to_insert, frag_to_insert });
    }

    peers[tested_ind]->RunGlobalMaintenance();

    EXPECT_EQ(peers[0]->db_.GetIndex().GetHash(),
              ChordKey(test_info["EXPECTED_TESTED_HASH"].asString()));
}

/**
 * Here we test the exchange node function. When called, the function should
 * message the specified peer with a node from the calling peer's merkle tree.
 * The messaged peer should return the equivalently positioned node in its
 * merkle tree.
 */
TEST(DHashExchangeNode, ExistingNode)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "ExchangeNodeTest.json");
    Json::Value test_info = test_json["EXISTING_NODE"];
    std::vector<std::shared_ptr<DHashPeer>> peers;
    std::function<void(std::shared_ptr<DHashPeer>)> adjust_ida_params =
        [](std::shared_ptr<DHashPeer> peer) { peer->SetIdaParams(3, 2, 257); };
    ChordFromJson(test_info["PEERS"], peers, adjust_ida_params);

    DHashPeer::DbEntry entry = peers[0]->ExchangeNode(peers[1]->ToRemotePeer(),
                                                      peers[0]->db_.GetIndex(),
                                                      { peers[0]->id_ + 1,
                                                        peers[0]->id_ });
    EXPECT_EQ(entry, peers[1]->db_.GetIndex());
}

/**
 * Here, we test that ExchangeNode correctly throws an error when we attempt to
 * fetch the equivalently positioned node in a remote peer's merkle tree but no
 * such node exists (due to differing dimensions of the trees).
 *
 * We do this by creating 9 leaf keys in a single subtree of a peer's merkle
 * node, forcing it to branch off into new subtrees within that subtree.
 * Following this, the structure of the peer's merkle tree and other peers'
 * merkle trees will be different. We then ask the original node to exchange
 * a newly created merkle tree node with another peer, which does not have
 * an equivalently positioned node. This should throw an error.
 */
TEST(DHashExchangeNode, NonExistentNode)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "ExchangeNodeTest.json");
    Json::Value test_info = test_json["NON_EXISTENT_NODE"];
    std::vector<std::shared_ptr<DHashPeer>> peers;
    std::function<void(std::shared_ptr<DHashPeer>)> adjust_ida_params =
        [](std::shared_ptr<DHashPeer> peer) { peer->SetIdaParams(3, 2, 257); };
    ChordFromJson(test_info["PEERS"], peers, adjust_ida_params);

    Json::Value kvs = test_info["KEYS_TO_INSERT"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        peers[0]->db_.Insert({ ChordKey(it.key().asString()) ,
                               DataBlock(it->asString()).fragments_[0] });
    }

    DHashPeer::DbEntry entry = peers[0]->db_.GetIndex().GetNthChild(0);
    EXPECT_ANY_THROW(peers[0]->ExchangeNode(peers[1]->ToRemotePeer(),
                                            entry,
                                            { peers[0]->id_ + 1,
                                              peers[0]->id_ }));
}

/**
 * This is a fairly simple test. We just want to create a key and ensure that
 * it is visible throughout the chord.
 */
TEST(DHashIntegration, CreateAndRead)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "DHashIntegration"
                                         "CreateAndReadTest.json");
    std::vector<std::shared_ptr<DHashPeer>> peers;
    ChordFromJson(test_json["PEERS"], peers);

    peers[0]->Create(test_json["KEY"].asString(), test_json["VAL"].asString());
    for(const auto &peer : peers) {
        EXPECT_EQ(peer->Read(test_json["KEY"].asString()),
                  test_json["VAL"].asString());
    }
}

/**
 * Here, we test if the overlay network can repair itself after the voluntary
 * exit of several nodes. Since DHash requires 10 of 14 successors to
 * reconstruct a given key, it should be able to tolerate the loss of 4
 * successors in an 18-node chord while still being able to read the
 * keys inserted.
 */
TEST(DHashIntegration, MaintenanceAfterLeave)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "DHashIntegration"
                                         "MaintenanceAfterLeaveTest.json");
    std::vector<std::shared_ptr<DHashPeer>> peers;
    ChordFromJson(test_json["PEERS"], peers);

    Json::Value kvs = test_json["KV_PAIRS"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        peers[0]->Create(it.key().asString(), it->asString());
    }

    for(const auto &index : test_json["LEAVING_INDICES"]) {
        peers[index.asInt()]->Leave();
    }

    sleep(20);

    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        for(const auto &index : test_json["REMAINING_INDICES"]) {
            EXPECT_EQ(peers[index.asInt()]->Read(it.key().asString()),
                      it->asString());
        }
    }
}

/**
 * Likewise, the overlay network should be able to tolerate the failure of 4
 * nodes in an 18 node chord.
 */
TEST(DHashIntegration, MaitenanceAfterFail)
{
    Json::Value test_json = JsonFromFile("test_json/dhash_tests/"
                                         "DHashIntegration"
                                         "MaintenanceAfterFailTest.json");
    std::vector<std::shared_ptr<DHashPeer>> peers;
    ChordFromJson(test_json["PEERS"], peers);

    Json::Value kvs = test_json["KV_PAIRS"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        peers[0]->Create(it.key().asString(), it->asString());
    }

    for(const auto &index : test_json["FAILING_INDICES"]) {
        peers[index.asInt()]->Fail();
    }

    sleep(20);

    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        for(const auto &index : test_json["REMAINING_INDICES"]) {
            EXPECT_EQ(peers[index.asInt()]->Read(it.key().asString()),
                      it->asString());
        }
    }
}

TEST(Idk, ReadFile)
{
    std::ifstream is("/home/patrick/Music/pilgrims_chorus.webm",
                     std::ifstream::binary);

    // Find the length of the file
    is.seekg(0, std::ifstream::end);
    std::streampos length = is.tellg();
    is.seekg(0, std::ifstream::beg);

    // Create a vector to read it into
    std::vector<unsigned char> bytes(length);

    // Actually read data
    is.read((char *)&bytes[0], length);
    std::string a(bytes.begin(), bytes.end());

    // Close the file explicitly, since we're finished with it
    is.close();

    std::ofstream out("outfile.webm", std::ofstream::binary);
    out << a;
    out.close();
}