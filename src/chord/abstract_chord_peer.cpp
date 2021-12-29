#include "abstract_chord_peer.h"


/* ----------------------------------------------------------------------------
 * CONSTRUCTOR/DESTRUCTOR: Destructor doesn't actually destroy anything, it's
 *                         just a way for me to print out peer info when run-
 *                         time errors occur and the compiler cleans up. In
 *                         actuality, all member variables that deal with
 *                         raw pointers are already RAII. For that reason, I'm
 *                         not going to define a copy constructor either.
 * -------------------------------------------------------------------------- */

AbstractChordPeer::AbstractChordPeer(std::string ip_addr, unsigned short port,
                                     int num_succs)
    : ip_addr_(std::move(ip_addr))
    , port_(port)
    // Ip addr used to be a const char *. The hashing will produce a diff-
    // erent result for a const char *, because of the null terminator.
    // Since my unit testing assumes certain hashes, I do this workaround
    // to make sure we get the same hashes as before. Not ideal, but eh.
    , id_(ip_addr_ + ":" + std::to_string(port), false)
    , min_key_(id_)
    , finger_table_(id_)
    , num_succs_(num_succs)
    , successors_(num_succs_, id_)
{
    Log("Created peer.");
}

AbstractChordPeer::AbstractChordPeer(AbstractChordPeer &&rhs) noexcept
    : num_succs_(rhs.num_succs_)
    , port_(rhs.port_)
    , ip_addr_(std::move(rhs.ip_addr_))
    , finger_table_(std::move(rhs.finger_table_))
    , predecessor_(std::move(rhs.predecessor_))
    , successors_(std::move(rhs.successors_))
    , min_key_(std::move(rhs.min_key_))
    , id_(rhs.id_)
{}

AbstractChordPeer::~AbstractChordPeer()
{
    // No need for any cleanup. All member vars are already RAII.
    // Just print out some debugging info when destructor is called, for ease
    // of debugging when runtime errors occur.
    Log("FINAL RANGE: " + std::string(min_key_.Get()) + "-"  +std::string(id_));

    if(predecessor_.IsSet()) {
        Log("PREDECESSOR: " + std::string(predecessor_.Get().id_) + " at " +
            predecessor_.Get().ip_addr_ + ":" +
            std::to_string(predecessor_.Get().port_));
    } else {
        Log("PREDECESSOR: NONE");
    }

    for(int i = 0; i < successors_.Size(); ++i) {
        Log("SUCCESSOR " + std::to_string(i) + ": "
            + std::string(successors_.GetNthEntry(i).id_) + " at "
            + successors_.GetNthEntry(i).ip_addr_ + ":"
            + std::to_string(successors_.GetNthEntry(i).port_));
    }

    Log("FINAL FINGER TABLE:\n " + std::string(finger_table_));
}

void AbstractChordPeer::StartChord()
{
    // If this is the first peer in the chord, it will control every key, so...
    min_key_.Set(id_ + 1);
    StartMaintenance();
}


/* ----------------------------------------------------------------------------
 * JOIN/LEAVE/NOTIFY: Implement functions for peers to start a chord, join it,
 *                    leave it gracefully, or simply exit without notification,
 *                    as well as the necessary handlers for these events. Also
 *                    implement the necessary notification functions by which
 *                    new peers alert succs/preds to their presence, as well as
 *                    the handlers handlers of the above functions.
 * -------------------------------------------------------------------------- */

void AbstractChordPeer::Join(const std::string &gateway_ip,
                             unsigned short gateway_port)
{
    Log("Joining chord");

    Json::Value join_req, join_resp;
    join_req["COMMAND"] = "JOIN";
    join_req["NEW_PEER"] = PeerAsJson();

    join_resp = Client::MakeRequest(gateway_ip, gateway_port, join_req);


    predecessor_.Set(RemotePeer(join_resp["PREDECESSOR"]));
    min_key_.Set(predecessor_.Get().id_ + 1);

    PopulateFingerTable(true);

    RemotePeer succ = finger_table_.GetNthEntry(0);
    Notify(succ);

    // This may seem like an arbitrary cutoff (it is), but this will improve
    // lookups in large enough chords.
    if(num_succs_ > 10) {
        for(const auto &pred : GetNPredecessors(id_, num_succs_)) {
            Notify(pred);
        }
        successors_.Populate(GetNSuccessors(id_ + 1, num_succs_));
    }

    // Other peers should update their finger tables to account for us having
    // joined the chord.
    FixOtherFingers(id_);

    StartMaintenance();
}

Json::Value AbstractChordPeer::JoinHandler(const Json::Value &req)
{
    Json::Value join_resp;
    RemotePeer new_peer(req["NEW_PEER"]);

    // Get predecessor of new peer.
    RemotePeer new_peer_pred = GetPredecessor(new_peer.id_);
    join_resp["PREDECESSOR"] = Json::Value(new_peer_pred);

    // By having gateway node adjust finger table and succ list when peer joins,
    // we reduce the amount of work remaining during stabilize cycles.
    finger_table_.AdjustFingers(new_peer);
    // Note that the peer will only be inserted if it's in the num_succs_
    // succs of this peer.
    successors_.Insert(new_peer);

    return join_resp;
}

void AbstractChordPeer::Notify(const RemotePeer &peer_to_notify)
{
    Json::Value notify_req, notify_resp;
    notify_req["COMMAND"] = "NOTIFY";
    notify_req["NEW_PEER"] = PeerAsJson();
    notify_resp = peer_to_notify.SendRequest(notify_req);

    Json::Value keys_to_absorb = notify_resp["KEYS_TO_ABSORB"];

    AbsorbKeys(keys_to_absorb);
}

Json::Value AbstractChordPeer::NotifyHandler(const Json::Value &req)
{
    Json::Value notify_resp, data_to_transfer;
    RemotePeer new_peer(req["NEW_PEER"]);
    Log("Received notify from " + std::to_string(new_peer.port_));

    if(predecessor_.IsSet() && ! predecessor_.Get().IsAlive()) {
        RemotePeer old_pred = predecessor_.Get();
        // In this case, the new peer is our predecessor
        HandleNotifyFromPred(new_peer);
        HandlePredFailure(old_pred);
        return notify_resp;
    }

    // Update finger table to account for new peer in ring.
    finger_table_.AdjustFingers(new_peer);

    // If new peer is one of our num_succs_ successors, insert it into the succ-
    // essor list.
    successors_.Insert(new_peer);

    // If we don't have a predecessor set, or if the new peer is in between
    // this node and its old predecessor, then the new node is the rightful
    // predecessor.
    bool peer_is_pred = ! predecessor_.IsSet() ||
                        new_peer.id_.InBetween(predecessor_.Get().id_, id_,
                                               false);

    if(peer_is_pred) {
        return HandleNotifyFromPred(new_peer);
    }

    // Useful for the case where one peer has started a chord and a new peer
    // joins. In this case, it will not have been useful to populate the finger
    // table prior ot a second peer joining.
    if(finger_table_.Empty()) {
        PopulateFingerTable(true);
    }

    return notify_resp;
}

void AbstractChordPeer::Leave()
{
    Log("Leaving chord.");
    Json::Value notification_for_succ, notification_for_pred,
            keys_to_transfer, succ_resp, pred_resp;

    // Our predecessor becomes our successor's predecessor.
    notification_for_succ["COMMAND"] = "LEAVE";
    notification_for_succ["LEAVING_ID"] = std::string(id_);
    notification_for_succ["NEW_PRED"] = Json::Value(predecessor_.Get());
    notification_for_succ["NEW_MIN"] = std::string(min_key_.Get());
    notification_for_succ["KEYS_TO_ABSORB"] = KeysAsJson();

    for(const auto &pred : GetNPredecessors(id_, num_succs_)) {
        pred.SendRequest(notification_for_succ);
    }

    // Allow predecessor to update its finger table entries to account for our
    // absence.
    RemotePeer succ = finger_table_.GetNthEntry(0);
    bool succ_condones_leave = true;
    if(succ.IsAlive()) {
        succ_resp = succ.SendRequest(notification_for_succ);
        succ_condones_leave = succ_resp["SUCCESS"].asBool();
    }

    succ.min_key_ = min_key_.Get();

    if(succ_condones_leave) {
        Log("Leaving now.");
        Fail();
    } else {
        throw std::runtime_error("Not ready to leave");
    }
}

Json::Value AbstractChordPeer::LeaveHandler(const Json::Value &request)
{
    Json::Value leave_resp;
    ChordKey leaving_id(request["LEAVING_ID"].asString(), true);

    // If peer is pred, we must set our predecessor equal to the old one's
    // predecessor and our min key equal to the old pred's predecessor's ID
    // plus one. We must also absorb the keys of our former predecessor.
    if(leaving_id == predecessor_.Get().id_) {
        ChordKey old_pred_id = predecessor_.Get().id_;
        RemotePeer new_pred(request["NEW_PRED"]);
        predecessor_.Set(new_pred);
        ChordKey new_min_key(request["NEW_MIN"].asString(), true);
        min_key_.Set(new_min_key);

        FixOtherFingers(old_pred_id);

        Json::Value keys_to_absorb = request["KEYS_TO_ABSORB"];
        AbsorbKeys(keys_to_absorb);
    }

    successors_.Delete(leaving_id);

    if(successors_.Size() == 0) {
        successors_.Populate(GetNSuccessors(id_ + 1, num_succs_));
    }

    // We should also update our finger tables to note that the successor
    // of the leaving peer has now absorbed that peer's keys.
    RemotePeer new_succ(request["NEW_SUCC"]);
    finger_table_.AdjustFingers(new_succ);
    return leave_resp;
}


/* ----------------------------------------------------------------------------
 * FILE FUNCTIONS: Implement member functions to upload and download files into
 *                 the overlay network.
 * -------------------------------------------------------------------------- */

void AbstractChordPeer::UploadFile(const std::string &file_path)
{
    std::ifstream file(file_path, std::ifstream::binary);
    if(! file) {
        throw std::runtime_error("File failed to open");
    }

    // Find the length of the file
    file.seekg(0, std::ifstream::end);
    std::streampos length = file.tellg();
    file.seekg(0, std::ifstream::beg);

    // Read file into char buffer and then into string
    std::vector<unsigned char> bytes(length);
    file.read((char *)&bytes[0], length);
    std::string file_contents(bytes.begin(), bytes.end());
    std::cout << "File contents are: " << file_contents.substr(0, 5000) << std::endl;
    file.close();

    Log("entering create");
    Create(file_path, file_contents);
}

void AbstractChordPeer::DownloadFile(const std::string &file_name,
                                     const std::string &output_path)
{
    std::string file_contents = Read(file_name);
    Log("File contents are " + file_contents.substr(0, 5000));
    std::ofstream output_file(output_path, std::ofstream::binary);
    if(! output_file) {
        throw std::runtime_error("Failed to open output file");
    }
    Log("Writing to " + output_path);
    output_file << file_contents;
    Log("Written");
    output_file.close();
}


/* ----------------------------------------------------------------------------
 * SUCC/PRED FUNCTIONS: Implement member functions which retrieve successors
 *                      and predecessors of a given key by forwarding them
 *                      around the ring, as well as the relevant handlers.
 * -------------------------------------------------------------------------- */

RemotePeer AbstractChordPeer::GetSuccessor(const ChordKey &key)
{
    // Account for case where key is stored locally.
    if(StoredLocally(key)) {
        return ToRemotePeer();
    }

    Json::Value get_succ_req, json_succ;
    get_succ_req["COMMAND"] = "GET_SUCC";
    get_succ_req["KEY"] = std::string(key);
    json_succ = ForwardRequest(key, get_succ_req);
    return RemotePeer(json_succ);
}

Json::Value AbstractChordPeer::GetSuccHandler(const Json::Value &req)
{
    ChordKey key(req["KEY"].asString(), true);
    RemotePeer succ = GetSuccessor(key);
    return Json::Value(succ);
}

// We changed this definition. Is this causing the hanging?
std::vector<RemotePeer> AbstractChordPeer::GetNSuccessors(const ChordKey &key,
                                                          int n)
{
    Log("Getting n succs");
    std::vector<RemotePeer> successors_list;
    std::set<ChordKey> succ_ids;
    ChordKey previous_peer_id = key - 1;

    for(int i = 0; i < n; i++) {
        RemotePeer ith_succ = GetSuccessor(previous_peer_id + 1);

        // Imagine if this method were called with n=5 in a chord comprised
        // of only 2 peers. In this case, it would not make sense to return
        // a vector alternating between the same two peers until it reaches
        // 5 entries, so, when we "loop back around" to the first key, it's
        // time to break and return a 2-entry vector.
        bool already_in_list = succ_ids.find(ith_succ.id_) != succ_ids.end();
        if(already_in_list) {
            break;
        }

        successors_list.push_back(ith_succ);
        succ_ids.insert(ith_succ.id_);
        previous_peer_id = ith_succ.id_;
    }

    Log("Got n succs");
    return successors_list;
}

RemotePeer AbstractChordPeer::GetPredecessor(const ChordKey &key)
{
    // If this is the only peer in the chord, then this is the pred.
    if(! predecessor_.IsSet()) {
        return ToRemotePeer();
    }

    // If we are the key's succ, then our pred is the key's pred.
    if(StoredLocally(key)) {
        return predecessor_.Get();
    }

    // Successor list lookup can often be quicker than finger table lookup,
    // so this is a simple optimization.
    std::optional<RemotePeer> succ_of_key = successors_.Lookup(key);
    if(succ_of_key.has_value()) {
        RemotePeer pred_of_succ = succ_of_key->GetPred();
        // If the key is between the successor and its predecessor.
        if(key.InBetween(pred_of_succ.id_, succ_of_key->id_, true)) {
            return pred_of_succ;
        }
    }

    // Even though we're looking for a pred, we forward it to the key's succ,
    // who in turn gives its pred.
    Json::Value pred_req, json_pred;
    pred_req["COMMAND"] = "GET_PRED";
    pred_req["KEY"] = std::string(key);
    json_pred = ForwardRequest(key, pred_req);

    if(json_pred["SUCCESS"].asBool()) {
        return RemotePeer(json_pred);
    }

    throw std::runtime_error("Lookup failed w/ error: " +
                             json_pred["ERRORS"].asString());
}

Json::Value AbstractChordPeer::GetPredHandler(const Json::Value &req)
{
    ChordKey key(req["KEY"].asString(), true);
    RemotePeer pred = GetPredecessor(key);
    return Json::Value(pred);
}

std::vector<RemotePeer> AbstractChordPeer::GetNPredecessors(const ChordKey &key,
                                                            int n)
{
    std::vector<RemotePeer> pred_list;
    ChordKey previous_peer_id = key;

    for(int i = 0; i < n; i++) {
        RemotePeer ith_succ = GetPredecessor(previous_peer_id - 1);
        pred_list.push_back(ith_succ);

        // For reasoning here, see comment on the GetNSuccessors member.
        if(previous_peer_id == key && i != 0) {
            break;
        }

        previous_peer_id = ith_succ.id_;
    }
    return pred_list;
}

/*-----------------------------------------------------------------------------
 * MAINTENANCE: Here, we implement functions necessary to maintain the correct-
 *              ness of the overlay network, in this case Stabilize, which
 *              periodically checks the predecessor of this node's successor
 *              and determines whether or not it ought to be our successor,
 *              and PopulateFingerTable, which initializes/updates our finger
 *              table.
 *----------------------------------------------------------------------------*/

void AbstractChordPeer::Stabilize()
{
    Log("Running stabilize.");

    if(! predecessor_.Get().IsAlive()) {
        HandlePredFailure(predecessor_.Get());
    }

    // If successors list is empty, initialize w/ GetNSuccessors.
    if(successors_.Size() == 0) {
        successors_.Populate(GetNSuccessors(id_ + 1, num_succs_));
        PopulateFingerTable(false);
        return;
    }

    RemotePeer immediate_succ = successors_.GetNthEntry(0);

    while(! immediate_succ.IsAlive()) {
        successors_.Delete(immediate_succ);
        immediate_succ = successors_.GetNthEntry(0);
    }

    RemotePeer pred_of_succ = immediate_succ.GetPred();


    // In this case, we are the correct predecessor (or at least more correct)
    // of our immediate successor.
    bool incorrect_succ = id_.InBetween(pred_of_succ.id_, immediate_succ.id_,
                                        true);


    // If we are in between our succ's listed pred and the succ, or if our
    // succ's listed pred is down, we should be the succ's pred.
    if(incorrect_succ || ! pred_of_succ.IsAlive()) {
        Log("Notifying " + std::to_string(immediate_succ.port_));
        Notify(immediate_succ);
    }


    Log("Updating succ list");
    UpdateSuccList();
    Log("Finished updating succs");
    Log("Populating FT");
    PopulateFingerTable(false);
    Log("Finished updating FT");
}

void AbstractChordPeer::UpdateSuccList()
{
    std::vector<RemotePeer> old_peer_list = successors_.GetEntries();

    ChordKey previous_succ_id = id_;

    // Otherwise, iterate through the old list and find any new peers between
    // old entries.
    for(auto &nth_entry : old_peer_list) {
        RemotePeer last_entry = nth_entry;

        // Find any new peers between nth entry of successor list and n - 1th
        // entry.
        while(true) {
            // Contacts dead entries.
            RemotePeer pred_of_last_entry;
            try {
                pred_of_last_entry = last_entry.GetPred();
            } catch(const std::runtime_error &err) {
                break;
            }

            // In this case, we've found all the new nodes between our nth (old)
            // succ and our n-1th (old) succ, by getting each pred of the nth succ
            // and each pred of that node until we reach the (n-1th) succ.
            if(pred_of_last_entry.id_ == previous_succ_id ||
               pred_of_last_entry.id_ == id_) {
                break;
            }

            if(pred_of_last_entry.IsAlive()) {
                successors_.Insert(pred_of_last_entry);
            }

            last_entry = pred_of_last_entry;
        }

        previous_succ_id = nth_entry.id_;
    }

    // If successor list is still too small, get successors of final entry
    // to populate the remaining segment.
    if(successors_.Size() < num_succs_) {
        int size = successors_.Size(), discrepancy = num_succs_ - size;

        RemotePeer last_succ = successors_.GetNthEntry(size - 1);
        std::vector<RemotePeer> succs = GetNSuccessors(last_succ.id_ + 1,
                                                       discrepancy);

        for(const RemotePeer &peer : succs) {
            if(peer.id_ != id_) {
                successors_.Insert(peer);
            }
        }
    }
}

void AbstractChordPeer::PopulateFingerTable(bool initialize)
{
    Log("Populating ft");
    for(int i = 0; i < finger_table_.num_entries_; ++i) {
        std::pair<ChordKey, ChordKey> entry_range = finger_table_.GetNthRange(i);
        Json::Value succ_req;
        succ_req["COMMAND"] = "GET_SUCC";
        succ_req["KEY"] = std::string(entry_range.first);

        if(initialize) {
            // If key is local, then entry should just point to ourselves.
            if(StoredLocally(entry_range.first)) {
                finger_table_.AddFinger(ChordFingerTable::FingerType {
                        entry_range.first,
                        entry_range.second,
                        ToRemotePeer()
                });
            }

            else {
                // The closest preceding node that we know of for any entry
                // entry is the previous entry, so we should query it.
                // For i = 0, no previous entry exists, so we'll just ask the
                // predecessor.
                RemotePeer peer_to_query = i == 0 ?
                                           predecessor_.Get() :
                                           finger_table_.GetNthEntry(i - 1);
                Json::Value succ_resp = peer_to_query.SendRequest(succ_req);
                finger_table_.AddFinger(ChordFingerTable::FingerType {
                        entry_range.first,
                        entry_range.second,
                        RemotePeer(succ_resp)
                });
            }
        }

        else {
            if(i == 0) {
                finger_table_.EditNthFinger(i, GetSuccessor(entry_range.first));
            } else {
                // If i is not 0, then the path closest preceding node we know
                // of for this entry is the peer in the last entry, so query it.
                RemotePeer peer_to_query = finger_table_.GetNthEntry(i - 1);
                Json::Value succ_resp = peer_to_query.SendRequest(succ_req);
                finger_table_.EditNthFinger(i, RemotePeer(succ_resp));
            }
        }
    }
    Log("Ended ft pop");
}

void AbstractChordPeer::FixOtherFingers(const ChordKey &starting_key)
{
    // Affected nodes should update their finger tables to account for this
    // node's entry. The predecessors of id_ - 2^i for i = 1...m will be
    // affected.
    std::optional<RemotePeer> former_peer;
    for(int i = 1; i <= ChordKey::BinaryLen(); ++i) {
        ChordKey decrease_interval(mp::pow(mp::uint256_t(2), i - 1));
        RemotePeer p = GetPredecessor(starting_key - decrease_interval);

        // No need to notify the same peer twice.
        if(former_peer.has_value() && former_peer.value() == p) {
            continue;
        }

        Log("Sending notification to pred of " +
            std::string(starting_key - decrease_interval) +
            ", which is: " + std::to_string(p.port_));
        former_peer = p;

        // In this case, we've reached the part of the chord owned by us.
        // Since this is a contiguous space, we can simply stop notifying here.
        if(p.id_ == id_) {
            break;
        }

        if(p.IsAlive()) {
            Notify(p);
        }
    }
}

void AbstractChordPeer::Rectify(const RemotePeer &failed_peer)
{
    // Have to ensure that rectify is not being called erroneously.
    if(failed_peer.IsAlive()) {
        return;
    }

    Log("Rectifying failure of " + std::to_string(failed_peer.port_));
    Json::Value rectify_req;
    rectify_req["COMMAND"] = "RECTIFY";
    rectify_req["FAILED_NODE"] = Json::Value(failed_peer);
    rectify_req["ORIGINATOR"] = PeerAsJson();

    std::optional<RemotePeer> former_peer;
    for(int i = 1; i <= ChordKey::BinaryLen(); ++i) {
        ChordKey decrease_interval(mp::pow(mp::uint256_t(2), i - 1));
        RemotePeer p = GetPredecessor(failed_peer.id_ - decrease_interval);

        // No need to notify the same peer twice.
        if(former_peer.has_value() && former_peer.value() == p) {
            continue;
        }

        former_peer = p;

        // In this case, we've reached the part of the chord owned by us.
        // Since this is a contiguous space, we can simply stop notifying here.
        if(p.id_ == id_) {
            break;
        }

        if(p.IsAlive()) {
            p.SendRequest(rectify_req);
        }
    }
}

Json::Value AbstractChordPeer::RectifyHandler(const Json::Value &req)
{
    Json::Value resp;
    RemotePeer originator(req["ORIGINATOR"]);
    if(originator.id_ == id_) {
        return resp;
    }

    RemotePeer failed_node(req["FAILED_NODE"]);
    successors_.Delete(failed_node);
    finger_table_.ReplaceDeadPeer(failed_node, originator);

    Notify(originator);
    return resp;
}

/*-----------------------------------------------------------------------------
 * MISCELLANEOUS: Anything else.
 *----------------------------------------------------------------------------*/

RemotePeer AbstractChordPeer::ToRemotePeer()
{
    return RemotePeer(id_, min_key_.Get(), ip_addr_, port_);
}

Json::Value AbstractChordPeer::PeerAsJson()
{
    return Json::Value(ToRemotePeer());
}

void AbstractChordPeer::Log(const std::string &str)
{
    std::cout << "[" << std::string(id_) << "@" << ip_addr_ << ":"
              << port_ << "] " << str << std::endl;
}

bool AbstractChordPeer::StoredLocally(const ChordKey &key)
{
    // This node holds keys in range [min_key_, id_], so, if a key is
    // clockwise-between them, this node should hold it (assuming it exists).
    return key.InBetween(min_key_.Get(), id_, true);
}

/*-----------------------------------------------------------------------------
 * ACCESSORS: For unit testing.
 *----------------------------------------------------------------------------*/

std::string AbstractChordPeer::GetIpAddr() const
{
    return ip_addr_;
}

unsigned short AbstractChordPeer::GetPort() const
{
    return port_;
}

ChordKey AbstractChordPeer::GetId()
{
    return id_;
}

ChordKey AbstractChordPeer::GetMinKey()
{
    return min_key_.Get();
}

ChordFingerTable AbstractChordPeer::GetFingerTable()
{
    return finger_table_;
}

RemotePeer AbstractChordPeer::GetPredecessor()
{
    return predecessor_.Get();
}

std::vector<RemotePeer> AbstractChordPeer::GetSuccessors()
{
    return successors_.GetEntries();
}