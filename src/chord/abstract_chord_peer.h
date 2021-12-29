/**
 * abstract_chord_peer.h
 *
 * Many P2P DHT protocols rely on a simplified version of the chord protocol
 * for lookup services. Protocols like DHash (see: Josh Cates' thesis) and DHC
 * (see: Muthitachaharoen and Morris' paper "Atomic Mutable Data in a
 * Distributed Hash Table") rely on the chord protocol to place peers in a log-
 * ical ring, maintain finger tables for lookups, enable lookups of successors/
 * predecessors, and implement a stabilize protocol to keep finger tables and
 * predecessor/successor pointers up to date.
 *
 * This file aims to implement a class - AbstractChordPeer - which will act as a
 * base class for these protocols. It cannot implement the full functionality of
 * the chord protocol, since this would require various members that should not
 * be inherited by protocols like DHash. (The full chord protocol will be imp-
 * lemented in the ChordPeer class, which also inherits from AbstractChordPeer).
 *
 * Consequently, AbstractChordPeer will implement only a subset of chord functions.
 * It should contain member functions to issue and handle the following RPCs:
 *      - Successor lookups;
 *      - Predecessor lookups.
 *
 * AbstractChordPeer should also maintain member variables that will be relevant for
 * chord-derived protocols, including:
 *      - A predecessor pointer;
 *      - A finger table;
 *      - A field denoting the minimum and maximum keys held within the peer
 *        (min_key_ and id_);
 *      - Fields giving the IP address and port on which the peer is run.
 *
 * All data members should be thread-safe. This will be achieved primarily by
 * use of thread-safe types for any mutable member variables
 * (e.g. ThreadSafeRemotePeer for AbstractChordPeer::predecessor_).
 *
 * For information on how chord works, see Ian Stoica's original paper
 * (https://pdos.csail.mit.edu/papers/chord:sigcomm01/chord_sigcomm.pdf).
 */

#ifndef CHORD_AND_DHASH_ABSTRACT_CHORD_PEER_H
#define CHORD_AND_DHASH_ABSTRACT_CHORD_PEER_H

#include "../data_structures/database.h"
#include "../data_structures/finger_table.h"
#include "../data_structures/key.h"
#include "../data_structures/thread_safe.h"
#include "remote_peer_list.h"
#include <boost/thread/mutex.hpp>
#include <fstream>
#include <json/json.h>
#include <string>
#include <utility>

using ChordFingerTable = FingerTable<RemotePeer>;

/**
 * Implement base chord functionality to be inherited by ChordPeer, DHashPeer,
 * and DHCPeer classes.
 *
 * Should be abstract class such that join/leave/notify calls and handlers are
 * mandated to be implemented in derived class.
 */
class AbstractChordPeer {
public:
    /// Getters. Not even gonna bother commenting.
    std::string GetIpAddr() const;
    unsigned short GetPort() const;
    ChordKey GetId();
    ChordKey GetMinKey();
    ChordFingerTable GetFingerTable();
    RemotePeer GetPredecessor();
    std::vector<RemotePeer> GetSuccessors();

    /**
     * Start a chord.
     */
    void StartChord();

    /**
     * Join the chord via a gateway node.
     *
     * @param gateway_ip IP address of gateway node.
     * @param gateway_port Port of gateway peer.
     */
    void Join(const std::string &gateway_ip, unsigned short gateway_port);

    /**
     * Leave chord overlay network.
     * Will send leave notifications to pred(s) and succ(s).
     */
    void Leave();

    /**
     * Create a key-value pair and store it on the chord.
     * @param unhashed Unhashed key.
     * @param val Value associated with said key.
     */
    virtual void Create(const std::string &unhashed, const std::string &val) = 0;

    /**
     * Read the value of a key on the chord.
     * @param unhashed Unhashed key.
     * @return Value of key if it exists or throw an error if it does not.
     */
    virtual std::string Read(const std::string &unhashed) = 0;

    void UploadFile(const std::string &file_path);
    void DownloadFile(const std::string &file_name,
                      const std::string &output_path);

protected:
    /**
     * Construct chord base peer running at specified IP addr and port.
     *
     * @param ip_addr IP addr on which chord base peer will run.
     * @param port Port on which chord base peer will run.
     */
    AbstractChordPeer(std::string ip_addr, unsigned short port, int num_succs);

    AbstractChordPeer(const AbstractChordPeer &rhs) = delete;

    AbstractChordPeer(AbstractChordPeer &&rhs) noexcept;

    /**
     * Destructor. Since there's nothing to destroy, just print out information
     * of peer (useful for debugging purposes).
     */
    virtual ~AbstractChordPeer();

    /**
     * Pure virtual handler for join requests by other nodes seeking to join
     * chord overlay network using this node as a gateway node.
     *
     * @param req JSON request sent by joining node.
     * @return JSON response indicating node's predecessor.
     */
    Json::Value JoinHandler(const Json::Value &req);

    /**
     * Start the stabilize/maintenance/whatever thread and detach it.
     */
    virtual void StartMaintenance() = 0;

    /**
     * Pure virtual handler for leave requests by other nodes.
     *
     * @param req Request sent by our pred or succ.
     * @return Response indicating our consent for pred/succ to leave.
     */
    Json::Value LeaveHandler(const Json::Value &req);

    /**
     * Return list of kv pairs as json.
     */
    virtual Json::Value KeysAsJson() = 0;

    virtual void Fail() = 0;

    /**
     * Pure virtual function to notify another peer that this peer has entered
     * the chord.
     *
     * @param peer_to_notify Peer to which a notification will be sent.
     */
    void Notify(const RemotePeer &peer_to_notify);

    /**
     * Pure virtual function handler to respond to notification from a new peer
     * in the network.
     *
     * @param req Notification request sent by new peer in chord.
     * @return Response indicating that we've observed their request.
     */
    Json::Value NotifyHandler(const Json::Value &req);

    /**
     * Query the chord overlay network, determine which peer succeeds a given
     * key.
     *
     * @param key Key whose successor should be found.
     * @return The peer which succeeds the key.
     */
    RemotePeer GetSuccessor(const ChordKey &key);

    /**
     * Respond to request intended to determine successor of key.
     *
     * @param req Req specifiying key whose successor ought to be found.
     * @return JSON response indicating the successor of the key.
     */
    Json::Value GetSuccHandler(const Json::Value &req);

    /**
     * Issue GetSuccessor calls such that we determine the N successors of a
     * key.
     *
     * @param key Key whose n successors will be found.
     * @param n Number of successors to find.
     * @return Vector of successors of key.
     */
    std::vector<RemotePeer> GetNSuccessors(const ChordKey &key, int n);

    /**
     * Return the predecessor of a key.
     *
     * @param key Key whose predecessor will be found.
     * @return The predecessor of the key in question.
     */
    RemotePeer GetPredecessor(const ChordKey &key);

    /**
     * Respond to request by remote peer to find successor of key.
     *
     * @param req Request to find predecessor of key.
     * @return JSON response indicating predecessor of key.
     */
    Json::Value GetPredHandler(const Json::Value &req);

    /**
     * Issue GetPredecessors in a way such that we return the N predecessors of
     * a given key.
     *
     * @param key Key whose predecessors will be found.
     * @param n Number predecessors to find.
     * @return N predecessors of key as a vector.
     */
    std::vector<RemotePeer> GetNPredecessors(const ChordKey &key, int n);

    /**
     * Find predecessor of successor, determine whether a new node has joined
     * between the us and our successor but failed to alert us.
     * Update finger table.
     */
    void Stabilize();

    /**
     * Called inside stabilize. Given an already-populated succ list, update
     * its entries.
     */
    void UpdateSuccList();

    /**
     * Lookup the successors of all keys in the finger table and fill in its
     * entries.
     *
     * @param initialize Has finger table been populated before, or is it empty?
     */
    void PopulateFingerTable(bool initialize);

    /**
     * Upon joining, notify predecessors of starting_key - 2^i for i = 0...m of
     * our existence so that they may update their finger tables.
     *
     * @param starting_key The key whose predecessors will have outdated finger-
     *                     table entries and be notified. When we join, this
     *                     will be our id. When we replace a failed predecessor,
     *                     this will be the predecessor's ID.
     */
    void FixOtherFingers(const ChordKey &starting_key);

    /**
     * Produces JSON response for notify response sent by pred.
     * @param new_peer Predecessor which has sent a notification.
     * @return Response giving keys in range [min_key, new_peer_id] to
     *         predecessor.
     */
    virtual Json::Value HandleNotifyFromPred(const RemotePeer &new_pred) = 0;

    /**
     * When given a JSON map of key-value pairs by a remote peer, read this map
     * and insert its entries into our DB (useful for joins and leaves).
     * @param kv_pairs KV pairs to insert into DB.
     */
    virtual void AbsorbKeys(const Json::Value &kv_pairs) = 0;

    /**
     * If we detect that our predecessor has failed, then this will be called
     * in order to fix other nodes' finger tables, update our minimum key, and
     * adjust our finger table.
     */
    virtual void HandlePredFailure(const RemotePeer &old_pred) = 0;

    /**
     * When a peer fails, tell its predecessors to delete the node from their
     * finger tables and successor lists.
     * @param failed_peer The peer which has failed.
     */
    void Rectify(const RemotePeer &failed_peer);

    /**
     * Given a rectify request, remove failed node from finger table/succ list.
     * @param req Request indicating failed node.
     * @return Response indicating successful completion of req.
     */
    Json::Value RectifyHandler(const Json::Value &req);

    /**
     * Is key stored on this peer?
     * I.e. Is this peer one of the NUM_REPLICAS successors of the key?
     *
     * @param key ChordKey to look for.
     * @return Is this->min_key_ <= key <= this->id_?
    */
    bool StoredLocally(const ChordKey &key);

    /**
     * When given a request pertaining to a certain key, forward said request to
     * the corresponding peer in the finger table.
     *
     * @param request Request to forward
     * @param key The key to which the request corresponds, which
     *            will be queried in the finger table.
     * @return The response given by the relevant peer.
     */
    virtual Json::Value ForwardRequest(const ChordKey &key,
                                       const Json::Value &request) = 0;

    /**
     * Convert this peer to a representation of a RemotePeer (in order to send
     * this peer's info to other peers).
     *
     * @return Remote peer with attributes of this locally-run peer.
     */
    RemotePeer ToRemotePeer();

    /**
     * Convert the information of this peer to JSON.
     *
     * @return JSON representation of this peer.
     */
    Json::Value PeerAsJson();

    /**
     * Output str to stdout with a preface indicating the ID and IP/port of this
     * peer.
     *
     * @param str Str to write to stdout.
     */
    void Log(const std::string &str);


    /// IP addr on which peer runs.
    std::string ip_addr_;

    /// Port on which peer runs, number of successors that a given peer will
    /// maintain.
    const int port_, num_succs_;

    /// Identifier of peer which will determine its placement in a logical ring.
    const ChordKey id_;

    /// Finger table which facilitates lookups throughout the network by
    /// maintaining list of successors of keys which increase by powers of 2,
    /// starting at the ID of this peer.
    ChordFingerTable finger_table_;

    /// Predecessor of this peer.
    ThreadSafeRemotePeer predecessor_;

    /// List of successors.
    RemotePeerList successors_;

    /// Minimum key held by this peer.
    ThreadSafeChordKey min_key_;
};


#endif