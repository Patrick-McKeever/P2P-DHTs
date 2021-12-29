#ifndef CHORD_AND_DHASH_REMOTE_PEER_LIST_H
#define CHORD_AND_DHASH_REMOTE_PEER_LIST_H

#include "../data_structures/thread_safe.h"
#include "remote_peer.h"
#include <json/json.h>
#include <vector>

/**
  * The aim of RemotePeerList is to create an interface for a set of peers (e.g. a
  * successor list). It should maintain a list of peers sorted by key.
  */
class RemotePeerList : public ThreadSafe {
public:
    /**
     * Constructor.
     * @param max_entries Maximum number of entries in the peer list.
     */
    explicit RemotePeerList(int max_entries, ChordKey starting_key);

    /**
     * Constructor 2. Construct from JSON.
     *
     * @param peers_json JSON peer list.
     */
    explicit RemotePeerList(const Json::Value &peers_json);

    RemotePeerList(RemotePeerList &&rhs) noexcept;

    /**
     * Mutator for peers_.
     */
    void Populate(const std::vector<RemotePeer> &peers);

    /**
     * Insert a new peer into the set, sorted by key
     * @param new_peer
     */
    bool Insert(const RemotePeer &new_peer);

    /**
     * Given a contiguous list of peers, return the peer which possesses the
     * key in question.
     * @param key Key to lookup.
     * @param succ Do we want to lookup succ or pred of key? True for succ,
     *             false for pred.
     * @return The peer which succeeds it if it exists in the list, or else
     *         std::nullopt.
     */
    std::optional<RemotePeer> Lookup(const ChordKey &key,
                                     bool succ = true) const;

    std::optional<RemotePeer> LookupLiving(const ChordKey &key) const;

    /**
     * Delete a peer from the list (useful for when a peer fails).
     * @param peer_to_delete Peer which will be deleted from list.
     * @return True if search/deletion completed successfully, false
     *         otherwise.
     */
    void Delete(const RemotePeer &peer_to_delete);

    /**
     * Delete a peer from the list if it contains the specified id.
     * @param id_to_delete The ID to delete from the list.
     */
    void Delete(const ChordKey &id_to_delete);

    /**
     * Erase all elements from the list
     */
    void Erase();

    /**
     * Delete a peer from the list if its information matches that of the given
     * RemotePeer.
     * @param peer RemotePeer to delete.
     * @return Was a peer deleted?
     */
    bool Contains(const RemotePeer &peer);

    /**
     * @return The first peer in the list that is alive.
     */
    RemotePeer FirstLiving() const;

    /**
     * Retrieve nth entry of the peer list.
     *
     * @param n Index of entry to retrieve.
     * @return Nth entry.
     */
    RemotePeer GetNthEntry(int n);

    /**
     * Return index of peer in peers_.
     * @param peer Peer for which we will search.
     * @return -1 if not found, otherwise the index of the peer in the vector.
     */
    int GetIndex(const RemotePeer &peer) const;

    /**
     * Accessor for vector of peers (good for unit testing).
     * Because this is a thread-safe variable with a mutex, we can't really
     * copy it. But, if we want to get a pseudo-copy for whatever reason, we
     * can just read/copy the vector peers_.
     */
    std::vector<RemotePeer> GetEntries();

    /**
     * How many items in list?
     * @return Number of items in RemotePeerList instance
     */
    unsigned long Size();

    /**
     * Convert peer list to JSON.
     * @return Peer list as JSON.
     */
    explicit operator Json::Value();

private:
    int max_entries_;
    ChordKey starting_key_;
    std::vector<RemotePeer> peers_;
};

#endif
