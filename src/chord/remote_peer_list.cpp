#include "remote_peer_list.h"
#include <utility>

RemotePeerList::RemotePeerList(int max_entries, ChordKey starting_key)
    : max_entries_(max_entries)
    , starting_key_(std::move(starting_key))
{}

RemotePeerList::RemotePeerList(const Json::Value &peers_json)
    : max_entries_(peers_json["MAX_ENTRIES"].asInt())
    , starting_key_(peers_json["STARTING_KEY"].asString(), true)
{
    for(const auto &peer : peers_json["PEERS"]) {
        peers_.emplace_back(peer);
    }
}

RemotePeerList::RemotePeerList(RemotePeerList &&rhs) noexcept
{
    WriteLock rhs_lock(rhs.mutex_);
    max_entries_ = std::move(rhs.max_entries_);
    starting_key_ = std::move(rhs.starting_key_);
    peers_ = std::move(rhs.peers_);
}

void RemotePeerList::Populate(const std::vector<RemotePeer> &peers)
{
    peers_ = peers;
}

bool RemotePeerList::Insert(const RemotePeer &new_peer)
{
    WriteLock lock(mutex_);
    if(new_peer.port_ == 0) {
        throw std::runtime_error("Corrupted JSON");
    }
    // In case you're wondering whether we could use std::set instead of a
    // vector, we can't. The sorting requires comparison of each element
    // to both left and right entries in each prospective position, since it
    // uses clockwise in-between. As a result, we need to use a vector and
    // implement our own insert.
    if(peers_.empty()) {
        peers_.push_back(new_peer);
        return true;
    }

    // Since we'll be comparing successors to the entries and ids before them,
    // we need to initialize these variables here.
    ChordKey previous_key = starting_key_;
    // Does new peer belong on successors list?
    bool new_peer_is_succ = false;
    // Position of the new peer in the vector (if necessary); needed for insert.
    typename std::vector<RemotePeer>::iterator new_peer_position;

    for(auto it = peers_.begin(); it != peers_.end(); ++it) {
        if(new_peer.id_ == it->id_) {
            return false;
        }

        if(new_peer.id_.InBetween(previous_key, it->id_, true)) {
            new_peer_position = it;
            new_peer_is_succ = true;
            break;
        }
        previous_key = it->id_;
    }

    if(new_peer_is_succ) {
        peers_.insert(new_peer_position, new_peer);

        if(peers_.size() > max_entries_) {
            peers_.pop_back();
        }

        return true;
    }

    if(peers_.size() < max_entries_) {
        peers_.push_back(new_peer);
        return true;
    }

    return false;
}

std::optional<RemotePeer> RemotePeerList::Lookup(const ChordKey &key,
                                           bool succ) const
{
    ReadLock lock(mutex_);
    ChordKey previous_id = starting_key_;
    for(int i = 0; i < peers_.size(); ++i) {
        if(key.InBetween(previous_id, peers_.at(i).id_, true)) {
            if(succ) {
                return peers_.at(i);
            }

            // If we want to get the pred of the key, we must return the
            // previous entry.
            else if(i != 0) {
                return peers_.at(i - 1);
            } else {
                return std::nullopt;
            }
        }

        previous_id = peers_.at(i).id_;
    }

    return std::nullopt;
}

std::optional<RemotePeer>
RemotePeerList::LookupLiving(const ChordKey &key) const
{
    ReadLock lock(mutex_);
    std::optional<RemotePeer> succ = Lookup(key);
    if(succ.has_value()) {
        if(succ->IsAlive()) {
            return succ;
        }

        int succ_ind = GetIndex(succ.value());
        for(int i = succ_ind; i % peers_.size() < succ_ind; ++i) {
            RemotePeer peer = peers_.at(i % peers_.size());
            if(peer.IsAlive()) {
                return peer;
            }
        }
    }

    return std::nullopt;
}

void RemotePeerList::Delete(const RemotePeer &peer_to_delete)
{
    WriteLock lock(mutex_);
    Delete(peer_to_delete.id_);
}

void RemotePeerList::Delete(const ChordKey &id_to_delete)
{
    std::vector<RemotePeer>::iterator it;
    for(it = peers_.begin(); it != peers_.end(); ++it) {
        if(it->id_ == id_to_delete) {
            break;
        }
    }

    if(it != peers_.end()) {
        peers_.erase(it);
    }
}

void RemotePeerList::Erase()
{
    peers_.clear();
}

bool RemotePeerList::Contains(const RemotePeer &peer)
{
    for(const auto &p : peers_) {
        if(p.id_ == peer.id_) {
            return true;
        }
    }

    return false;
}

RemotePeer RemotePeerList::GetNthEntry(int n)
{
    ReadLock lock(mutex_);
    auto it = peers_.begin();
    std::advance(it, n);
    return *it;
}

RemotePeer RemotePeerList::FirstLiving() const
{
    for(const auto &p : peers_) {
        if(p.IsAlive()) {
            return p;
        }
    }
    throw std::runtime_error("No living peers");
}

int RemotePeerList::GetIndex(const RemotePeer &peer) const
{
    for(int i = 0; i < peers_.size(); ++i) {
        if(peers_.at(i).id_ == peer.id_) {
            return i;
        }
    }

    return -1;
}

unsigned long RemotePeerList::Size()
{
    ReadLock lock(mutex_);
    return peers_.size();
}

std::vector<RemotePeer> RemotePeerList::GetEntries()
{
    ReadLock lock(mutex_);
    return peers_;
}

RemotePeerList::operator Json::Value()
{
    ReadLock lock(mutex_);
    Json::Value peers_json;
    peers_json["MAX_ENTRIES"] = max_entries_;
    peers_json["STARTING_KEY"] = std::string(starting_key_);

    for(const auto &peer : peers_) {
        peers_json["PEERS"].append(Json::Value(peer));
    }

    return peers_json;
}
