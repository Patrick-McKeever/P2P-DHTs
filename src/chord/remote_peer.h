/**
 * remote_peer.h
 *
 * This file exists to implement several classes:
 *   - RemotePeer, which a locally-run peer will use to represent remote peers.
 *   - ThreadSafeRemotePeer, a wrapper which provides thread-safe access and
 *     mutation of RemotePeer instances.
 */

#ifndef CHORD_FINAL_PEER_REPR_H
#define CHORD_FINAL_PEER_REPR_H

//#include "../data_structures/data_block.h"
 #include "../ida/data_block.h"
#include "../data_structures/key.h"
#include "../networking/client.h"
#include <json/json.h>

/**
 * This class exists to represent peers.
 * Peers will store instances of this class to represent their successors,
 * predecessors, and finger table entries.
*/
class RemotePeer {
public:
    /**
     * Constructor 1. Default constructor.
     */
    RemotePeer();

    /**
     * Constructor 2. Construct RemotePeer from attributes.
     *
     * @param id ID of new RemotePeer.
     * @param min_key Min key in range of keys held by new RemotePeer.
     * @param ip_addr IP Addr on which new RemotePeer is run.
     * @param port Port on which new RemotePeer is run.
     */
    RemotePeer(ChordKey id, ChordKey min_key, std::string ip_addr,
               unsigned short port);

    /**
     * Constructor 3. Construct RemotePeer without set minkey or id.
     *
     * @param ip_addr IP Addr on which new RemotePeer is run.
     * @param port Port on which new RemotePeer is run.
     */
    RemotePeer(std::string ip_addr, unsigned short port);

    /**
     * Constructor 4. Construct RemotePeer from Json::Value containing data members.
     *
     * @param members Json object containing keys:
     *                      - "ID"
     *                      - "MIN_KEY"
     *                      - "MAX_KEY"
     *                      - "IP_ADDR"
     *                      - "PORT"
     */
    explicit RemotePeer(const Json::Value &members);

    /**
     * Send a request to this remote peer, return the response it gives.
     *
     * @param request Request to send to this remote peer.
     * @return Remote peer's response
     */
    [[nodiscard]] Json::Value SendRequest(const Json::Value &request) const;

    /**
     * Is remote peer up and running?
     *
     * @return Whether or not a connection to this remote peer can be
     *         successfully established.
     */
    bool IsAlive() const;

    /**
     * Retrieve the successor of this remote peer.
     * @return This peer's succ.
     */
    RemotePeer GetSucc() const;

    /**
     * Retrieve the predecesspr of this remote peer.
     * @return This peer's pred.
     */
    RemotePeer GetPred() const;

    /**
     * Mechanism for comparing two RemotePeers.
     *
     * @param lhs Left hand RemotePeer.
     * @param rhs Right hand RemotePeer.
     * @return Are all fields the same?
     */
    friend bool operator == (const RemotePeer &lhs,
                             const RemotePeer &rhs);
    /**
     * Comparison mechanism used for implementing maps.
     *
     * @param lhs Left hand RemotePeer.
     * @param rhs Right hand RemotePeer.
     * @return Whose id is larger?
     */
    friend bool operator < (const RemotePeer &lhs, const RemotePeer &rhs);

    /**
     * @return Remote peer as JSON.
     */
    operator Json::Value() const;

    /// ID of peer (hash of its IP and port).
    ChordKey id_;

    /// Minimum key stored by peer.
    ChordKey min_key_;

    /// IP address of peer.
    std::string ip_addr_;

    /// Port on which peer runs.
    unsigned short port_;
};

/**
 * In some cases, peers will need to store mutable representations of peers
 * which will be read and written to by server threads. Thus, it is necessary
 * to implement a thread-safe type of peer which can be read from and written
 * to by multiple threads using read and write locks to ensure safety.
 */
class ThreadSafeRemotePeer : public ThreadSafe {
public:
    /**
     * Default constructor for thread-safe remote peer.
     */
    ThreadSafeRemotePeer();

    /**
     * Construct thread-safe remote peer from a non-thread-safe remote peer.
     * @param peer
     */
    explicit ThreadSafeRemotePeer(const RemotePeer &peer);

    ThreadSafeRemotePeer(ThreadSafeRemotePeer &&rhs) noexcept;

    /**
     * Acquire write lock, set value of peer_.
     * @param peer Value to which peer_ will be set.
     */
    void Set(const RemotePeer &peer);

    /**
     * Acquire read lock, get value of peer_.
     * @return Value of peer_ if it exists.
     */
    RemotePeer Get() const;

    /**
     * Does peer_ have value?
     * @return peer_.has_value()
     */
    bool IsSet();

    /**
     * Reset the value of the optional field.
     */
    void Reset();

private:
    /// Instance of RemotePeer to access/modify.
    std::optional<RemotePeer> peer_;
};

#endif