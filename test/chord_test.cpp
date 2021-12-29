#include <gtest/gtest.h>
#include "json_reader.h"
#include "../src/chord/chord_peer.h"
#include <filesystem>

/**
 * In this test, we test whether AbstractChordPeer::GetSuccessor correctly
 * returns the successor of a key when the successor of that key is the node
 * calling the function. If a peer owns a key, it should return its own
 * information when trying to find the successor of that key. Moreover, it
 * should do this before checking any other information.
 *
 * We test the latter condition by inserting a RemotePeer which claims to
 * own the whole keyspace (including segments which the tested node registers
 * as being within its own keyspace) into the peer's successor list. The method
 * should return the node being tested rather than its successor.
 */
TEST(ChordGetSucc, LocalKey)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetSuccTest.json");
    Json::Value test_info = tests_json["GET_SUCC_OF_LOCAL_KEY"];

    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());

    peer.min_key_.Set(ChordKey(test_info["PEER"]["MIN_KEY"].asString(), true));
    peer.successors_.Insert(RemotePeer(test_info["PEER"]["SUCCESSOR"]));

    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString(), true);
    RemotePeer succ = peer.GetSuccessor(key_to_lookup);

    EXPECT_EQ(succ.id_, peer.id_);
}

/**
 * After the AbstractChordPeer::GetSuccessor method determines that a key is not
 * held locally, it should first query its finger table. Here, we create a simple
 * 2-node chord and erase node 1's successor list and its predecessor field.
 * This ensures that it must rely exclusively on its finger table to lookup
 * a remote key. We then ask it to do precisely that, and test whether the
 * finger table correctly returns node 2.
 */
TEST(ChordGetSucc, FromFingerTable)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetSuccTest.json");
    Json::Value test_info = tests_json["GET_SUCC_FROM_FINGER_TABLE"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    peers[0]->successors_.Erase();
    peers[0]->predecessor_.Reset();
    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString());

    // NOTE: When testing, prefer converting a key to a string and comparing
    //       it with a string from the test's JSON file. This means that, when
    //       the two don't match, GTest will output both as strings rather than
    //       hex (the representation of the type in memory).
    EXPECT_EQ(std::string(peers[0]->GetSuccessor(key_to_lookup).id_),
              test_info["EXPECTED_SUCC_ID"].asString());
}

/**
 * In the case that a node's finger table lookup erroneously returns that same
 * node, this is likely the result of a predecessor having joined and absorbed
 * a chunk of the original node's keyspace. In this case, GetSuccessor should
 * return the node's predecessor.
 */
TEST(ChordGetSucc, FromPredecessor)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetSuccTest.json");
    Json::Value test_info = tests_json["GET_SUCC_FROM_PREDECESSOR"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    // We will replace all entries in peer 0's finger table with copies of
    // itself (listed as controlling the entire keyspace) using the
    // "AdjustFingers" method.
    peers[0]->finger_table_.AdjustFingers(RemotePeer(peers[0]->id_,
                                                     peers[0]->id_ + 1,
                                                     peers[0]->ip_addr_,
                                                     peers[0]->port_));

    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString());
    EXPECT_EQ(peers[0]->GetSuccessor(key_to_lookup).id_,
              peers[0]->GetPredecessor().id_);
}

/**
 * In the case where a peer calls AbstractChordPeer::GetSuccessor and
 * (1) it does not own the key, (2) its finger table lookup returns itself,
 * (3) its predecessor is dead, and (4) a successor list lookup fails/returns a
 * dead peer, AbstractChordPeer::GetSuccessor should throw an error. If none
 * of the aforementioned conditions are met, a livelock will ensue, with peers
 * perpetually forwarding the request back and forth to one another. As a result,
 * we need this method to fail under those conditions.
 */
TEST(ChordGetSucc, Failing)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetSuccTest.json");
    Json::Value test_info = tests_json["GET_SUCC_FAILING"];
    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());

    RemotePeer succ(test_info["PEER"]["SUCCESSOR"]);

    // Ensure that peer's predecessor and successor fields point to a non-living
    // peer.
    peer.predecessor_.Set(succ);
    peer.successors_.Insert(succ);
    // This will assign succ as the successor of all fingers with lower bounds
    // in succ's stated range. Since succ's stated range encompassses the entire
    // keyspace, this will replace all fingers with succ.
    peer.finger_table_.AdjustFingers(succ);

    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString());
    EXPECT_ANY_THROW(peer.GetSuccessor(key_to_lookup));
}

/**
 * When attempting to find the predecessor of a locally stored key, a node
 * should return its own predecessor. (We assume that this field will always
 * be up-to-date, since the join method entails notification of a peer's
 * immediate successor.
 */
TEST(ChordGetPred, LocalKey)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetPredTest.json");
    Json::Value test_info = tests_json["GET_PRED_OF_LOCAL_KEY"];

    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());

    peer.min_key_.Set(ChordKey(test_info["PEER"]["MIN_KEY"].asString()));
    peer.predecessor_.Set(RemotePeer(test_info["PEER"]["PRED"]));

    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString());
    EXPECT_EQ(peer.GetPredecessor(key_to_lookup).id_,
              peer.predecessor_.Get().id_);
}

/**
 * When looking up the predecessor of a key, it is optimal to first estimate
 * the location of the key in a node's successor list. In a low-churn chord or
 * a high churn chord with many nodes, we assume that only a few new nodes have
 * joined between these successors without the querying node's knowledge; thus,
 * lookup of predecessors in the successor list should return after 1 request
 * in the case where a listed node owns the key and the list is up-to-date and
 * after a small amount of hops when the list is slightly out-of-date.
 *
 * In this test, we remove a node's finger table and require that it look up
 * a key's predecessor based solely on its successor list. Does it produce the
 * correct result?
 */
TEST(ChordGetPred, FromSuccList)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetPredTest.json");
    Json::Value test_info = tests_json["GET_PRED_IN_SUCC_LIST"];

    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);
    for(const auto &json_peer : test_info["PEERS"][0]["SUCCS"]) {
        peers[0]->successors_.Insert(RemotePeer(json_peer));
    }

    // We will replace all entries in peer 0's finger table with copies of
    // itself (listed as controlling the entire keyspace) using the
    // "AdjustFingers" method.
    peers[0]->finger_table_.AdjustFingers(RemotePeer(peers[0]->id_,
                                                     peers[0]->id_ + 1,
                                                     peers[0]->ip_addr_,
                                                     peers[0]->port_));

    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString());
    EXPECT_EQ(std::string(peers[0]->GetPredecessor(key_to_lookup).id_),
              test_info["EXPECTED_PRED_ID"].asString());
}

/**
 * In the case where the successor list is not viable, predecessor lookups
 * should consult the finger table and forward the request in the standard way.
 * To test that the implementation fulfills this specification, we delete a
 * node's successor list, requiring that it use only its finger table to lookup
 * a key's predecessor. Does it produce the correct result?
 */
TEST(ChordGetPred, FromFingerTable)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetPredTest.json");
    Json::Value test_info = tests_json["GET_PRED_FROM_FINGER_TABLE"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    peers[0]->successors_.Erase();
    peers[0]->predecessor_.Reset();
    ChordKey key_to_lookup(test_info["KEY_TO_LOOKUP"].asString());
    EXPECT_EQ(std::string(peers[0]->GetPredecessor(key_to_lookup).id_),
              test_info["EXPECTED_PRED_ID"].asString());
}

/**
 * In the case where a node's successor list is empty or filled with dead peers,
 * its predecessor is dead, and its finger table nodes cannot be contacted, the
 * request is impossible. AbstractChordPeer::GetPredecessor should simply throw
 * an exception.
 */
TEST(ChordGetPred, Failing)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "GetPredTest.json");
    Json::Value test_info = tests_json["GET_PRED_FAILING"];
    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());
    RemotePeer dead_peer(peer.id_, peer.id_ + 1, peer.ip_addr_, peer.port_ + 1);
    peer.predecessor_.Set(dead_peer);
    peer.finger_table_.AdjustFingers(dead_peer);
    EXPECT_ANY_THROW(peer.GetPredecessor(ChordKey("0")));
}

/**
 * When a peer receives a notification from a newly-joined predecessor, it must
 * do three things: (1) set the new node as its predecessor; (2) set its minkey
 * as the ID of the predecessor plus one, so as to forfeit a segment of its
 * keyspace to the new node; and (3) return a response which provides to the
 * predecessor the keys within the segment of keyspace absorbed by the
 * predecessor node.
 *
 * This test simulates a notification request from a node's predecessor and
 * tests that its response (generated by AbstractChordPeer::NotifyHandler)
 * fulfills the above 3 conditions.
 */
TEST(ChordNotify, FromPred)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "NotifyTest.json");
    Json::Value test_info = tests_json["NOTIFY_FROM_PRED"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value kvs = test_info["KEYS_TO_STORE"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        peers[0]->Create(ChordKey(it.key().asString()), it->asString());
    }

    Json::Value notify_resp = peers[0]->NotifyHandler(test_info["JSON_REQ"]);

    RemotePeer new_pred(test_info["JSON_REQ"]["NEW_PEER"]);
    EXPECT_EQ(peers[0]->min_key_.Get(), new_pred.id_ + 1);
    EXPECT_EQ(peers[0]->predecessor_.Get().id_, new_pred.id_);
    EXPECT_EQ(notify_resp["KEYS_TO_ABSORB"], test_info["KVS_TO_XFER"]);
}

/**
 * When a peer receives a notification from a successor, it ought to do 2
 * things: (1) insert the successor into its successor list (if appropriate);
 * and (2) modify its finger table to account for the successor.
 *
 * This test simulates a notification request from a node's newly-joined imm-
 * ediate successor and verifies that AbstractChordPeer::NotifyHandler does
 * these two tasks. We create a single-node chord and simulate a notify request
 * specifying a new peer which claims to occupy the entire keyspace; the
 * original (tested) peer should alter all entries in its finger table to the
 * new node and should insert the new node into its successor list.
 */
TEST(ChordNotify, FromSucc)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "NotifyTest.json");
    Json::Value test_info = tests_json["NOTIFY_FROM_SUCC"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    peers[0]->NotifyHandler(test_info["JSON_REQ"]);
    RemotePeer new_peer(test_info["JSON_REQ"]["NEW_PEER"]);
    EXPECT_EQ(peers[0]->successors_.GetNthEntry(0), new_peer);

    for(int i = 0; i < peers[0]->finger_table_.num_entries_; ++i) {
        RemotePeer ith_succ = peers[0]->finger_table_.GetNthEntry(i);
        EXPECT_EQ(ith_succ, new_peer);
    }
}

/**
 * In the notification unit tests, we've examined 2 of 3 cases: (1) the case
 * where the notifying node is a predecessor of the notified node; (2) the case
 * where the notifying node is a successor.
 *
 * The third case occurs when the notifying node has no significance to the
 * notified node at all. This can occur when nodes attempt to rectify a network
 * partition or adjust to the failure of a node.
 *
 * In this case, the notified node should neither update its successor list
 * nor its predecessor field. To test this, we create a 4-node chord and
 * simulate a new node's notification. This node will have no significance to the
 * tested peer (the first peer in the list), so this peer should adjust neither
 * its predecessor field nor its successor values.
 */
TEST(ChordNotify, FromIrrelevantNode)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "NotifyTest.json");
    Json::Value test_info = tests_json["NOTIFY_FROM_IRRELEVANT_NODE"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    peers[0]->NotifyHandler(test_info["JSON_REQ"]);
    RemotePeer non_notable_peer(test_info["JSON_REQ"]["NEW_PEER"]);
    EXPECT_NE(peers[0]->predecessor_.Get().id_, non_notable_peer.id_);
    EXPECT_FALSE(peers[0]->successors_.Contains(non_notable_peer));
}

/**
 * During stabilize, a node should check if its immediate successor is dead and
 * replace it with the successor of that successor. Here, we kill a node's
 * successor, instruct the node to stabilize, and check that it has an
 * up-to-date successor field after the operation.
 */
TEST(ChordStabilize, StabilizeChecksSucc)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "StabilizeTest.json");
    Json::Value test_info = tests_json["CHECKS_SUCCS"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    for(Json::ArrayIndex i = 0; i < peers.size(); ++i) {
        if(test_info["PEERS"][i]["KILL"].asBool()) {
            peers[i]->Fail();
        }
    }

    peers[0]->Stabilize();
    EXPECT_EQ(std::string(peers[0]->successors_.GetNthEntry(0).id_),
              test_info["EXPECTED_SUCC_ID"].asString());
}

/**
 * During stabilization, if a peer notices that its immediate successor is dead,
 * it should notify the successor of its former successor, so that the successor
 * of the dead node can set the stabilizing node as its new predecessor. We
 * here create a 4-node chord, kill 2 nodes, instruct a node to stabilize,
 * and test that, in doing so, its new successor has set the stabilizing node
 * as its predecessor. This is central to repairs of network partitions.
 */
TEST(ChordStabilize, StabilizeNotifiesSuccWithDeadPred)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "StabilizeTest.json");
    Json::Value test_info = tests_json["NOTIFIES_SUCC_WITH_DEAD_PRED"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    for(Json::ArrayIndex i = 0; i < peers.size(); ++i) {
        if(test_info["PEERS"][i]["KILL"].asBool()) {
            peers[i]->Fail();
        }
    }

    int stabilize_ind = test_info["STABILIZE_IND"].asInt(),
        tested_ind    = test_info["TESTED_IND"].asInt();
    peers[stabilize_ind]->Stabilize();

    EXPECT_EQ(std::string(peers[tested_ind]->predecessor_.Get().id_),
              test_info["EXPECTED_PRED_ID"].asString());
}

/**
 * When calling AbstractChordPeer::UpdateSuccList, a node should identify
 * any new nodes "between" the existing entries in its successor list. It does
 * so by checking that each node's successor in the list is identical to the
 * previous entry in the list and, if not, adding the new predecessor as a new
 * intermediate item in the list. Here, we test that this method works correctly
 * in the case where only one node exists in the gaps between nodes.
 *
 * We begin by setting up a chord then allowing new nodes to join shortly there-
 * after. These nodes will not be initially recorded in the original nodes'
 * successor lists, but, after a call to UpdateSuccList, they should be included
 * in the calling node's successor list.
 */
TEST(ChordUpdateSuccList, SingleNewNodesBetweenSuccs)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "UpdateSuccTest.json");
    Json::Value test_info = tests_json["SINGLE_NODE_BETWEEN_SUCCS"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value joining_peers = test_info["JOINING_PEERS"];
    AddJsonNodesToChord(joining_peers, peers);
    peers[0]->UpdateSuccList();

    Json::Value expected_succs = test_info["EXPECTED_SUCCS"];
    for(int i = 0; i < peers[0]->successors_.Size(); ++i) {
        ChordKey expected_succ_id(expected_succs[i]["ID"].asString());
        EXPECT_EQ(peers[0]->successors_.GetNthEntry(i).id_, expected_succ_id);
    }
}

/**
 * This test tests the same function as the above test
 * (AbstractChordPeer::UpdateSuccList) but assesses the case in which multiple
 * new nodes exist in between successor list entries.
 */
TEST(ChordUpdateSuccList, MultipleNewNodesBetweenSuccs)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "UpdateSuccTest.json");
    Json::Value test_info = tests_json["MULTIPLE_NODES_BETWEEN_SUCCS"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value joining_peers = test_info["JOINING_PEERS"];
    AddJsonNodesToChord(joining_peers, peers);
    peers[0]->UpdateSuccList();

    Json::Value expected_succs = test_info["EXPECTED_SUCCS"];
    for(int i = 0; i < peers[0]->successors_.Size(); ++i) {
        ChordKey expected_succ_id(expected_succs[i]["ID"].asString());
        EXPECT_EQ(peers[0]->successors_.GetNthEntry(i).id_, expected_succ_id);
    }
}

/**
 * In the case where, after expansion by getting the predecessors of existing
 * entries produces a list with less than the specified length,
 * AbstractChordPeer::UpdateSuccList should get the successors of the final
 * entry in the list until (1) it reaches the peer calling the function (i.e.
 * it loops around the chord fully or (2) the list reaches the correct size.
 *
 * Here, we simulate a case in which this "clockwise expansion" is necessary
 * and assess whether the method correctly fills in the tested peer's successor
 * list.
 */
TEST(ChordUpdateSuccList, ClockwiseExpansionNeeded)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "UpdateSuccTest.json");
    Json::Value test_info = tests_json["CLOCKWISE_EXPANSION_NEEDED"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value joining_peers = test_info["JOINING_PEERS"];
    AddJsonNodesToChord(joining_peers, peers);
    peers[0]->UpdateSuccList();

    Json::Value expected_succs = test_info["EXPECTED_SUCCS"];
    for(int i = 0; i < peers[0]->successors_.Size(); ++i) {
        ChordKey expected_succ_id(expected_succs[i]["ID"].asString());
        EXPECT_EQ(peers[0]->successors_.GetNthEntry(i).id_, expected_succ_id);
    }
}

/**
 * Lastly, we will test the case in which a node's successor list is up-to-date.
 * In this case, AbstractChordPeer::UpdateSuccList should change nothing.
 */
TEST(ChordUpdateSuccList, NoChanges)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "UpdateSuccTest.json");
    Json::Value test_info = tests_json["NO_CHANGES_NEEDED"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value joining_peers = test_info["JOINING_PEERS"];
    AddJsonNodesToChord(joining_peers, peers);
    peers[0]->UpdateSuccList();

    Json::Value expected_succs = test_info["EXPECTED_SUCCS"];
    for(int i = 0; i < peers[0]->successors_.Size(); ++i) {
        ChordKey expected_succ_id(expected_succs[i]["ID"].asString());
        EXPECT_EQ(peers[0]->successors_.GetNthEntry(i).id_, expected_succ_id);
    }
}

/**
 * When a node leaves, the leaving should instruct its immediate successor to
 * update its predecessor field to the leaving node's predecessor.
 */
TEST(ChordLeave, LeaveUpdatesPred)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "LeaveTest.json");
    Json::Value test_info = tests_json["LEAVE_UPDATES_PRED"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    int leaving = test_info["LEAVE_INDEX"].asInt(),
        tested  = test_info["TEST_INDEX"].asInt();
    peers[leaving]->Leave();
    EXPECT_EQ(std::string(peers[tested]->predecessor_.Get().id_),
              test_info["EXPECTED_PRED_ID"].asString());
}

/**
 * Likewise, when leaving, the leaving node should instruct its immediate succ
 * to update its minimum key to the leaving node's predecessor's id + 1.
 */
TEST(ChordLeave, LeaveUpdatesMinKey)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "LeaveTest.json");
    Json::Value test_info = tests_json["LEAVE_UPDATES_MINKEY"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    int leaving = test_info["LEAVE_INDEX"].asInt(),
        tested  = test_info["TEST_INDEX"].asInt();
    peers[leaving]->Leave();
    EXPECT_EQ(std::string(peers[tested]->GetMinKey()),
              test_info["EXPECTED_MINKEY"].asString());
}

/**
 * When a node leaves, it should transfer all of its keys to its successor,
 * who will absorb its keyspace.
 *
 * Here, we create a simple chord, create keys belonging to a node, instruct
 * that node to leave, and test that its predecessor has received all of the
 * leaving node's keys.
 */
TEST(ChordLeave, LeaveTransfersKeys)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "LeaveTest.json");
    Json::Value test_info = tests_json["LEAVE_TRANSFERS_KEYS"];
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value kvs = test_info["KVS_TO_TRANSFER"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        peers[0]->Create(ChordKey(it.key().asString()), it->asString());
    }

    int leaving = test_info["LEAVE_INDEX"].asInt(),
            tested  = test_info["TEST_INDEX"].asInt();
    peers[leaving]->Leave();

    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        ChordKey key(it.key().asString());
        ASSERT_TRUE(peers[tested]->db_.Contains(key));
        EXPECT_EQ(peers[tested]->db_.Lookup(key), it->asString());
    }
}

/**
 * When a chord node receives a "CREATE_KEY" RPC, it should create the given
 * key and value in its database, provided that the key is within the keyspace
 * it owns.
 */
TEST(ChordCreateKey, Valid)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "CreateKeyTest.json");
    Json::Value test_info = tests_json["VALID"];
    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());
    // Node is alone in chord and therefore owns entire keyspace.
    peer.StartChord();

    peer.CreateKeyHandler(test_info["JSON_REQ"]);
    ChordKey key(test_info["EXPECTED_KEY"].asString());
    ASSERT_TRUE(peer.db_.Contains(key));
    EXPECT_EQ(peer.db_.Lookup(key), test_info["EXPECTED_VAL"].asString());
}

/**
 * In the case where the "CREATE_KEY" RPC specifies a key which is not owned by
 * the RPC's recipient, the recipient should throw an error.
 */
TEST(ChordCreateKey, NonLocalKey)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "CreateKeyTest.json");
    Json::Value test_info = tests_json["VALID"];
    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());
    peer.StartChord();

    // Ensure that peer occupies no keyspace.
    peer.min_key_.Set(peer.id_);

    EXPECT_ANY_THROW(peer.CreateKeyHandler(test_info["JSON_REQ"]));
}

/**
 * A "READ_KEY" RPC should return the specified KV pair in the recipient node's
 * database, provided that the key is inside the recipient node's database.
 */
TEST(ChordReadKey, Valid)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "ReadKeyTest.json");
    Json::Value test_info = tests_json["VALID"];
    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());
    peer.StartChord();

    peer.CreateKeyHandler(test_info["CREATE_REQ"]);
    ChordKey key(test_info["EXPECTED_KEY"].asString());

    // In order for the read req to work, the create req must have worked.
    // Let's assert that this is the case and terminate otherwise.
    ASSERT_TRUE(peer.db_.Contains(key));
    ASSERT_EQ(peer.db_.Lookup(key), test_info["EXPECTED_VAL"].asString());

    EXPECT_EQ(peer.ReadKeyHandler(test_info["READ_REQ"])["VALUE"].asString(),
              test_info["EXPECTED_VAL"].asString());
}

/**
 * In the case where a "READ_KEY" RPC specifies a key not contained in the rec-
 * ipient's database, the recipient should throw an error.
 */
TEST(ChordReadKey, NonExistentKey)
{
    Json::Value tests_json = JsonFromFile("test_json/chord_tests/"
                                          "ReadKeyTest.json");
    Json::Value test_info = tests_json["NON_EXISTENT_KEY"];
    ChordPeer peer(test_info["PEER"]["IP"].asString(),
                   test_info["PEER"]["PORT"].asInt(),
                   test_info["PEER"]["NUM_SUCCS"].asInt());
    peer.StartChord();

    EXPECT_ANY_THROW(peer.ReadKeyHandler(test_info["READ_REQ"]));
}

/**
 * In this test, we simulate a simple 6-node chord. We assess whether, after
 * node joins, nodes can successfully identify their predecessors, minimum
 * keys, and have transferred keys to the relevant peers.
 */
TEST(ChordIntegration, Join)
{
    Json::Value test_info = JsonFromFile("test_json/chord_tests/"
                                         "ChordIntegrationJoinTest.json");

    // We use a vector of shared ptr for lifetime reasons. We only want the
    // destructor of each object called once, since the destructor contains
    // logging info.
    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    Json::Value kvs = test_info["KV_PAIRS"];
    for(Json::Value::const_iterator it = kvs.begin(); it != kvs.end(); ++it) {
        std::string key(it.key().asString()), val(it->asString());
        peers[0]->Create(key, val);
    }

    for(int i = 0; i < peers.size(); ++i) {
        Json::Value peer_json = test_info["PEERS"][i];

        auto expected_pred_id = std::string(peers[i]->GetPredecessor().id_),
             actual_pred_id   = peer_json["EXPECTED_PREDECESSOR_ID"].asString();
        EXPECT_EQ(expected_pred_id, actual_pred_id);

        Json::Value expected_kvs = peer_json["EXPECTED_KV_PAIRS"];
        Json::Value::const_iterator it;
        for(it = expected_kvs.begin(); it != expected_kvs.end(); ++it) {
            ChordKey expected_key(it.key().asString());
            std::string expected_val = it->asString();

            bool key_in_db = peers[i]->db_.Contains(expected_key);
            EXPECT_TRUE(key_in_db);

            if(key_in_db) {
                EXPECT_EQ(peers[i]->db_.Lookup(expected_key), expected_val);
            }
        }
    }
}

/**
 * In this test, we test create and read operations in a static 6-node chord.
 * The desired condition is one in which any node can insert data on any other
 * node and any node in the chord can subsequently read that data.
 * In order to test this condition, we simply simulate the insertion of a large
 * number of data into the chord from each node and attempt to read this data
 * from each node. With a large enough dataset, we can ensure that each node
 * contains at least some data, meaning that we will test the ability of each
 * node to read inserted data from each other node.
 */
TEST(ChordIntegration, CreateAndRead)
{
    Json::Value test_info = JsonFromFile("test_json/chord_tests/"
                                         "ChordIntegration"
                                         "CreateAndReadTest.json");

    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    for(int i = 0; i < 100; i += peers.size()) {
        for(int j = 0; j < peers.size(); ++j) {
            peers[j]->Create(std::to_string(i + j), std::to_string(i + j));
        }
    }

    for(int i = 0; i < 100; ++i) {
        for(const auto &peer : peers) {
            EXPECT_EQ(peer->Read(std::to_string(i)), std::to_string(i));
        }
    }
}

/**
 * Stabilize updates nodes' successor pointers. This test will seek to determine
 * whether, after 1 stabilize cycle, each node's successor list is up-to-date.
 * Once again, we are worried about conditions in a static 6-node chord.
 */
TEST(ChordIntegration, Stabilize)
{
    Json::Value test_info = JsonFromFile("test_json/chord_tests/"
                                         "ChordIntegrationStabilizeTest.json");

    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    // Allow time for 1 stabilize cycle.
    sleep(6);

    for(Json::Value::ArrayIndex i = 0; i != test_info["PEERS"].size(); ++i) {
        Json::Value peer = test_info["PEERS"][i],
                    expected_succs = peer["EXPECTED_SUCCS"];

        for(Json::Value::ArrayIndex j = 0; j < expected_succs.size(); ++j) {;
            EXPECT_EQ(std::string(peers[i]->GetSuccessors()[j].id_),
                      expected_succs[j].asString());
        }
    }
}

/**
 * This test will seek to assess whether or not our implementation of graceful
 * leaves is correct. More specifically, this test will seek to set up a 6-node
 * chord and have all but one node leave. If leaves successfully transfer keys
 * and update predecessor pointers, then all keys created throughout the
 * lifetime of the chord will exist on the
 */
TEST(ChordIntegration, GracefulLeave)
{
    Json::Value test_info = JsonFromFile("test_json/chord_tests/"
                                         "ChordIntegration"
                                         "GracefulLeaveTest.json");

    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    for(int i = 0; i < 100; ++i) {
        peers[i % peers.size()]->Create("key" + std::to_string(i),
                                        "value" + std::to_string(i));
    }

    for(int i = 0; i < (peers.size() - 1); ++i) {
        peers[i]->Leave();
    }

    for(int i = 0; i < 100; ++i) {
        EXPECT_EQ(peers[peers.size() - 1]->Read("key" + std::to_string(i)),
                  "value" + std::to_string(i));
    }
}

/**
 * In this final test, we determine the capacity of a chord to repair itself
 * via periodic stabilization following the loss of a large portion of its
 * members. We establish a simple six-node chord, simulate the failure of
 * two nodes, and wait three stabilize cycles. After this point, we test
 * whether predecessors, minimum keys, and successor lists have been altered
 * to account for the failure of nodes.
 */
TEST(ChordIntegration, NodeFailure)
{
    Json::Value test_info = JsonFromFile("test_json/chord_tests/"
                                         "ChordIntegration"
                                         "NodeFailureTest.json");

    std::vector<std::shared_ptr<ChordPeer>> peers;
    ChordFromJson(test_info["PEERS"], peers);

    peers[0]->Fail();
    peers[1]->Fail();

    sleep(40);

    for(Json::Value::ArrayIndex i = 2; i < test_info["PEERS"].size(); ++i) {
        Json::Value peer_json = test_info["PEERS"][i];

        // We expect here that the node, after failure, has correctly identified
        // its minimum key, meaning that it has taken responsibility for the
        // keyspace left behind by a failed node.
        EXPECT_EQ(std::string(peers[i]->GetMinKey()),
                  peer_json["EXPECTED_MINKEY"].asString());

        // We expect here that the node has correctly identified its
        // predecessor, enabling correct lookups in the future.
        EXPECT_EQ(std::string(peers[i]->GetPredecessor().id_),
                  peer_json["EXPECTED_PREDECESSOR_ID"].asString());

        // We expect here that the
        Json::Value expected_succs = peer_json["EXPECTED_SUCCS"];
        for(Json::Value::ArrayIndex j = 0; j < 3; ++j) {
            EXPECT_EQ(std::string(peers[i]->GetSuccessors()[j].id_),
                      expected_succs[j].asString());
        }
    }
}