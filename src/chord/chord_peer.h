/**
 * chord_peer.h
 *
 * This file seeks to implement a class, ChordPeer, inherits from AbstractChordPeer,
 * and implements a complete version of the chord protocol. It should support
 * methods to issue and handle the following RPCs:
 *  - Join requests;
 *  - Notifications of newly-joined nodes;
 *  - Graceful leave requests;
 *  - Put (Create) operations; and
 *  - Get (Read) operations.
 *
 * This class should also maintain a multi-threaded server to respond to RPCs
 * from other peers, and a thread to periodically stabilize.
 */

#ifndef CHORD_AND_DHASH_CHORD_PEER_H
#define CHORD_AND_DHASH_CHORD_PEER_H

#include "../networking/server.h"
#include "abstract_chord_peer.h"
#include <chrono>
#include <gtest/gtest_prod.h>

/**
 * Implementation of full chord protocol.
 */
class ChordPeer : public AbstractChordPeer {
public:
    /**
     * Construct chord peer, begin running server.
     *
     * @param ip_addr IP addr on which to run server.
     * @param port Port on which to run server.
     */
    ChordPeer(std::string ip_addr, unsigned short port, int num_succs);

    /**
     * No copy construction allowed.
     */
    ChordPeer(const ChordPeer &rhs) = delete;

    /**
     * Move constructor. Used to put ChordPeer in vector.
     * @param rhs ChordPeer to be moved.
     */
    ChordPeer(ChordPeer &&rhs) noexcept;

    /**
     * RAII.
     */
    ~ChordPeer();

    /**
     * Stop the server, but provide no notification to any other nodes, forcing
     * them to correct the network. Exists exclusively for unit testing.
     */
    void Fail() override;

    /**
     * Create a key-value pair and store it on the chord.
     * @param unhashed Unhashed key.
     * @param val Value associated with said key.
     */
    void Create(const std::string &unhashed, const std::string &val) override;

    /**
     * Read the value of a key on the chord.
     * @param unhashed Unhashed key.
     * @return Value of key if it exists or throw an error if it does not.
     */
    std::string Read(const std::string &unhashed) override;

    /// Maps keys to strings.
    TextDb db_;

protected:
    using ReqHandler = std::function<Json::Value(const Json::Value &)>;
    using ServerType = Server<ReqHandler>;

    /**
     * Given our new predecessor, set it as our predecessor, transfer relevant
     * keys.
     *
     * @param new_pred our new predecessor.
     * @return Json with keys that now belong to predecessor.
     */
    Json::Value HandleNotifyFromPred(const RemotePeer &new_pred) override;

    /**
     * @return Keys in the database as a JSON map.
     */
    Json::Value KeysAsJson() override;

    /**
     * Start the stabilize loop.
     */
    void StartMaintenance() override;

    /**
     * Instruct the successor of key to store key-value pair.
     *
     * @param key Key to store.
     * @param value Value with which it is associated.
     */
    void Create(const ChordKey &key, const std::string &value);

    /**
     * Instruct another chord peer to store a key-value pair.
     *
     * @param key The key for said peer to store.
     * @param val The value of the KV pair.
     * @param peer The remote DHash peer to store the KV pair.
     */
    bool CreateKey(const ChordKey &key, const std::string &val,
                   const RemotePeer &peer);

    /**
     * When instructed by a remote peer to hold a key, insert it in our database
     * and return a response indicating success/failure.
     *
     * @param req Request indicating kv-pair to store in our db.
     * @return Success if key is in range, failure otherwise.
     */
    Json::Value CreateKeyHandler(const Json::Value &req);

    /**
     * Determine successor of key, read its value.
     *
     * @param key Key whose value ought to be returned.
     * @return Value with which key is associated.
     */
    std::string Read(const ChordKey &key);

    /**
     * Instruct another chord peer to return the value corresponding to a key.
     *
     * @param key The key for said peer to store.
     * @param peer The peer storing the KV pair
     * @return Value with which key is associated.
     */
    std::string ReadKey(const ChordKey &key, const RemotePeer &peer);

    /**
     * When instructed by a remote peer to return the value of a given key,
     * return its value in a JSON response if it exists in our database,
     * otherwise throw an error.
     *
     * @param req Request indicating key to lookup.
     * @return JSON response indicating value associated with key, if it
     *         exists, otherwise throw an error.
     */
    Json::Value ReadKeyHandler(const Json::Value &req);

    /**
     * Run chord stabilize algorithm at intervals of five seconds in a loop
     * while continue_stabilize_ is set to true.
     */
    void StabilizeLoop();

    Json::Value ForwardRequest(const ChordKey &key, const Json::Value &request)
                override;


    void HandlePredFailure(const RemotePeer &old_pred) override;

    /**
     * When given a JSON map of key-value pairs by a remote peer, read this map
     * and insert its entries into our DB (useful for joins and leaves).
     * @param kv_pairs KV pairs to insert into DB.
     */
    void AbsorbKeys(const Json::Value &kv_pairs) override;

    /// Thread which performs the stabilize operation every 5 seconds as
    /// outlined by Stoica.
    std::thread stabilize_thread_;

    /// Server to respond to queries from other nodes.
    std::shared_ptr<Server<ReqHandler>> server_;

    /// Stabilize thread will run while this is true.
    bool continue_stabilize_;

private:
    FRIEND_TEST(ChordGetSucc, LocalKey);
    FRIEND_TEST(ChordGetSucc, FromFingerTable);
    FRIEND_TEST(ChordGetSucc, FromPredecessor);
    FRIEND_TEST(ChordGetSucc, Failing);
    FRIEND_TEST(ChordGetPred, LocalKey);
    FRIEND_TEST(ChordGetPred, FromSuccList);
    FRIEND_TEST(ChordGetPred, FromFingerTable);
    FRIEND_TEST(ChordGetPred, Failing);
    FRIEND_TEST(ChordNotify, FromPred);
    FRIEND_TEST(ChordNotify, FromSucc);
    FRIEND_TEST(ChordNotify, FromIrrelevantNode);
    FRIEND_TEST(ChordStabilize, StabilizeChecksSucc);
    FRIEND_TEST(ChordStabilize, StabilizeNotifiesSuccWithDeadPred);
    FRIEND_TEST(ChordUpdateSuccList, SingleNewNodesBetweenSuccs);
    FRIEND_TEST(ChordUpdateSuccList, MultipleNewNodesBetweenSuccs);
    FRIEND_TEST(ChordUpdateSuccList, ClockwiseExpansionNeeded);
    FRIEND_TEST(ChordUpdateSuccList, NoChanges);
    FRIEND_TEST(AbstractChordPeer, FixOtherFingers);
    FRIEND_TEST(ChordLeave, LeaveUpdatesPred);
    FRIEND_TEST(ChordLeave, LeaveUpdatesMinKey);
    FRIEND_TEST(ChordLeave, LeaveFixesFingers);
    FRIEND_TEST(ChordLeave, LeaveTransfersKeys);
    FRIEND_TEST(ChordCreateKey, Valid);
    FRIEND_TEST(ChordCreateKey, NonLocalKey);
    FRIEND_TEST(ChordReadKey, Valid);
    FRIEND_TEST(ChordReadKey, NonExistentKey);
};

#endif