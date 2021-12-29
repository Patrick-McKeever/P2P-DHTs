#include "dhash_peer.h"
#include <random>
#include <chrono>

using namespace std::literals;

/* ----------------------------------------------------------------------------
 * CONSTRUCTOR/DESTRUCTOR: RAII for server and maintenance thread.
 * -------------------------------------------------------------------------- */

DHashPeer::DHashPeer(std::string ip_addr, int port, int num_replicas)
    : AbstractChordPeer(std::move(ip_addr), port, num_replicas)
    , continue_maintenance_(true)
    , n_(14)
    , m_(10)
    , p_(257)
{
    std::map<std::string, ReqHandler> commands {
            { "JOIN", [this](const Json::Value &req) {
                return JoinHandler(req);
            } },
            { "NOTIFY", [this](const Json::Value &req) {
                return NotifyHandler(req);
            } },
            { "LEAVE", [this](const Json::Value &req) {
                return LeaveHandler(req);
            } },
            { "GET_SUCC", [this](const Json::Value &req) {
                return GetSuccHandler(req);
            } },
            { "GET_PRED", [this](const Json::Value &req) {
                return GetPredHandler(req);
            } },
            { "CREATE_KEY", [this](const Json::Value &req) {
                return CreateKeyHandler(req);
            } },
            { "READ_KEY", [this](const Json::Value &req) {
                return ReadKeyHandler(req);
            } },
            { "READ_RANGE", [this](const Json::Value &req) {
                return ReadRangeHandler(req);
             } },
            { "XCHNG_NODE", [this](const Json::Value &req) {
                return ExchangeNodeHandler(req);
            } },
            { "RECTIFY", [this](const Json::Value &req) {
                return RectifyHandler(req);
            } }
    };

    server_ = std::make_shared<ServerType>(port, 3, commands);
    server_->RunInBackground();
}

DHashPeer::DHashPeer(DHashPeer &&rhs) noexcept
    : AbstractChordPeer(std::move(rhs))
    , server_(std::move(rhs.server_))
    , db_(std::move(rhs.db_))
    , continue_maintenance_(rhs.continue_maintenance_)
    , maintenance_thread_(std::move(rhs.maintenance_thread_))
{}

DHashPeer::~DHashPeer()
{
    std::string key_str = "KEYS: ";

    for(const auto &[key, value] : db_.GetIndex().GetEntries()) {
        key_str += std::string(key) + " ";
    }

    Log(key_str);

    continue_maintenance_ = false;

    if(maintenance_thread_.joinable()) {
        maintenance_thread_.join();
    }
}


/* ----------------------------------------------------------------------------
 * CREATE/READ: Implement member functions to allow creation/reading of key-val
 *              pairs. In a DHash overlay network, fragments are stored on the
 *              14 successors of the key to which they correspond. A create/read
 *              req should first identify these 14 succs then instruct each to
 *              store/send the nth fragment of the key.
 * -------------------------------------------------------------------------- */

void DHashPeer::Create(const std::string &key, const std::string &val)
{
    ChordKey encoded_key(key, false);
    DataBlock encoded_value(val, n_, m_, p_);
    Create(encoded_key, encoded_value);
}


void DHashPeer::Create(const ChordKey &key, const std::string &val)
{
    DataBlock block(val, n_, m_, p_);
    Create(key, block);
}

void DHashPeer::Create(const ChordKey &key, const DataBlock &val)
{
    int num_replicas = 0;
    std::vector<RemotePeer> succ_list = GetNSuccessors(key, n_);

    // A minimum of ten replicas are needed to reconstruct the block.
    if(succ_list.size() < m_) {
        throw std::runtime_error("Insufficient succs in list to complete "
                                 "request.");
    }

    for(int i = 0; i < succ_list.size(); i++) {
        RemotePeer succ = succ_list.at(i);
        Log("Creating " + std::to_string(i) + "th fragment");
        if(succ.id_ == id_) {
            db_.Insert({ key, val.fragments_.at(i) });
            num_replicas++;
        } else if(succ.IsAlive() && CreateKey(key, val.fragments_.at(i), succ)) {
            ++num_replicas;
        }
    }

    // If at least 10 peers successfully stored fragments, then the block can
    // be reconstructed by messaging them.
    if(num_replicas < m_) {
        throw std::runtime_error("Too few succs responded to requests.");
    }
}

bool DHashPeer::CreateKey(const ChordKey &key, const DataFragment &val,
                          const RemotePeer &peer)
{
    Json::Value create_req, create_resp;
    create_req["COMMAND"] = "CREATE_KEY";
    create_req["KEY"] = std::string(key);
    create_req["VALUE"] = Json::Value(val);
    create_resp = peer.SendRequest(create_req);
    return create_resp["SUCCESS"].asBool();
}

Json::Value DHashPeer::CreateKeyHandler(const Json::Value &req)
{
    Log("Received CK request");
    Json::Value create_resp;
    ChordKey key(req["KEY"].asString(), true);
    DataFragment val(req["VALUE"]);

    if(db_.Contains(key)) {
        throw std::runtime_error("Key already exists in db.");
    }

    db_.Insert({ key, val });
    Log("Completed CK request");
    return create_resp;
}

std::string DHashPeer::Read(const std::string &key)
{
    ChordKey encoded_key(key, false);
    DataBlock encoded_value = Read(encoded_key);
    return encoded_value.Decode();
}

DataBlock DHashPeer::Read(const ChordKey &key)
{
    std::vector<RemotePeer> succ_list = GetNSuccessors(key, num_succs_);
    std::set<DataFragment> fragments;

    int i = 0;
    for(auto &succ : succ_list) {
        Log("Getting " + std::to_string(++i) + "th frag");
        if(fragments.size() == m_) {
            break;
        }

        if(succ.id_ == id_ && db_.Contains(key)) {
            fragments.insert(db_.Lookup(key));
        } else {
            try {
                fragments.insert(ReadKey(key, succ));
            }
            // If the key is not stored on the frag we message, ReadFrag
            // will throw an error. The solution is simply to message the
            // next frag.
            catch(const std::exception &err) {
                continue;
            }
        }
    }

    // A minimum of ten fragments are needed to reconstruct a data block.
    if(fragments.size() < m_) {
        throw std::runtime_error("Less than " + std::to_string(m_) +
                                 " distinct frags.");
    }

    std::vector<DataFragment> frag_vec(fragments.begin(), fragments.end());
    return DataBlock(frag_vec, n_, m_, p_);
}

DataFragment DHashPeer::ReadKey(const ChordKey &key, const RemotePeer &peer)
{
    Json::Value read_req, read_resp;
    read_req["COMMAND"] = "READ_KEY";
    read_req["KEY"] = std::string(key);
    read_resp = peer.SendRequest(read_req);
    return DataFragment(read_resp["VALUE"]);
}

Json::Value DHashPeer::ReadKeyHandler(const Json::Value &req)
{
    Json::Value read_resp;
    ChordKey key(req["KEY"].asString(), true);

    // Note that FragDb::Lookup will throw an error if the key is not found,
    // as it should.
    Log("1");
    read_resp["VALUE"] = Json::Value(db_.Lookup(key));
    Log("2");
    return read_resp;
}

DHashPeer::KvMap DHashPeer::ReadRange(const RemotePeer &succ,
                                      const KeyRange &key_range)
{
    Json::Value read_range_req, read_range_resp;
    read_range_req["COMMAND"] = "READ_RANGE";
    read_range_req["LOWER_BOUND"] = std::string(key_range.first);
    read_range_req["UPPER_BOUND"] = std::string(key_range.second);
    read_range_resp = succ.SendRequest(read_range_req);

    KvMap ret_val;
    for(const auto &kv_pair : read_range_resp["KV_PAIRS"]) {
        ret_val.insert({ ChordKey(kv_pair["KEY"].asString(), true),
                         DataFragment(kv_pair["VAL"]) });
    }
    return ret_val;
}

Json::Value DHashPeer::ReadRangeHandler(const Json::Value &request)
{
    Json::Value read_range_resp, key_frag_pairs, key_frag_pair;
    ChordKey lower_bound(request["LOWER_BOUND"].asString(), true),
             upper_bound(request["UPPER_BOUND"].asString(), true);


    for(const auto &[key, val] : db_.ReadRange(lower_bound, upper_bound)) {
        key_frag_pair["KEY"] = std::string(key);
        key_frag_pair["VAL"] = Json::Value(val);
        key_frag_pairs.append(key_frag_pair);
    }

    read_range_resp["KV_PAIRS"] = key_frag_pairs;
    Log("Received read range " + std::string(lower_bound) + "-" +
        std::string(upper_bound));
    return read_range_resp;
}


/* ----------------------------------------------------------------------------
 * MAINTENANCE FUNCTIONS: Implement member functions which ensure that lookups,
 *                        fragment locations, etc are correct. This includes
 *                        stabilization of the finger table and successors list,
 *                        as well as global and local maintenance of fragments.
 *                        Lastly, implement rectify function and its handler to
 *                        cope with node failure.
 * -------------------------------------------------------------------------- */

void DHashPeer::StartMaintenance()
{
    maintenance_thread_ = std::thread([this]() { MaintenanceLoop(); });
//    maintenance_thread_.detach();
}

void DHashPeer::MaintenanceLoop()
{
    auto timestamp = std::chrono::high_resolution_clock::now();
    while(continue_maintenance_) {
        try {
            // This method of sleeping has several appealing properties. Since we
            // check to see if 5 seconds have elapsed every 10ms and otherwise
            // continue, this means that, when the destructor is called, it will
            // terminate within 10ms. This means we don't have to detach threads.
            auto now = std::chrono::high_resolution_clock::now();
            if (now - timestamp < 5s) {
                std::this_thread::sleep_for(10ms);
                continue;
            }

            Stabilize();
            RunGlobalMaintenance();
            RunLocalMaintenance();
            timestamp = std::chrono::high_resolution_clock::now();
        } catch(const std::exception &ex) {
            if(! continue_maintenance_)
                break;
            Log("Continuing");
            timestamp = std::chrono::high_resolution_clock::now();
            continue;
        }
    }
}

void DHashPeer::RunGlobalMaintenance()
{
    Log("running global maintenance");
    ChordKey current_key = id_, previous_key, starting_key;

    if(db_.Next(id_).has_value()) {
        starting_key = db_.Next(id_)->first;
    }

    bool first_iter = true;
    while(db_.Next(current_key).has_value()) {
        KvPair next = db_.Next(current_key).value();

        bool first_iteration = current_key == id_;
        // Next key will always be starting key on first iteration, so we have
        // to prevent the loop from breaking immediately.
        bool loop_around = next.first.InBetween(id_, starting_key, true);
        if(loop_around && ! first_iter) {
            break;
        }
        first_iter = false;

        // If this peer's id is contained within the n_ successors of the key
        // in question, then it should possess the key.
        Log("1");
        std::vector<RemotePeer> succs = GetNSuccessors(next.first, n_);
        bool key_is_misplaced = true;
        for(int i = 0; i < succs.size(); ++i) {
            if(succs.at(i).id_ == id_) {
                key_is_misplaced = false;
            }
        }

        if(key_is_misplaced) {
            for(auto &succ : succs) {
                KvMap resp = ReadRange(succ, { next.first, succs.at(0).id_ }),
                      keys_in_range = db_.ReadRange(next.first, succs.at(0).id_);

                for(const auto &[key, frag] : keys_in_range) {
                    if(resp.find(key) == resp.end()) {
                        CreateKey(key, frag, succ);
                        db_.Delete(key);
                    }
                }
            }
        }

        current_key = succs.at(0).id_;
        previous_key = next.first;
    }
    Log("Global maintenance over");
}

void DHashPeer::RunLocalMaintenance()
{
    Log("Running local maintenance");
    if(db_.Size() == 0) {
        Log("Size is 0.");
        return;
    }

    for(int i = 0; i < successors_.Size(); ++i) {
        if(successors_.GetNthEntry(i).id_ != id_) {
            Synchronize(successors_.GetNthEntry(i), { min_key_.Get(), id_ });
        }
    }

    Log("Local maintenance over");
}

void DHashPeer::RetrieveMissing(const ChordKey &key)
{
    Log("Retrieving " + std::string(key));
    DataBlock block = Read(key);
    std::vector<DataFragment> random_els;
    std::sample(block.fragments_.begin(),
                block.fragments_.end(),
                std::back_inserter(random_els),
                1, std::mt19937{std::random_device{}()});
    DataFragment random_frag = random_els.at(0);
    db_.Insert({ key, random_frag });
    Log("Retrieved");
}

void DHashPeer::Synchronize(const RemotePeer &succ, const KeyRange &key_range)
{
    SynchronizeHelper(succ, key_range, db_.GetIndex());
}

void DHashPeer::SynchronizeHelper(const RemotePeer &succ,
                                  const KeyRange &key_range,
                                  const MerkleTree<DataFragment> &local_node)
{
    ChordKey lower_bound = key_range.first, upper_bound = key_range.second;
    DbEntry remote_node = ExchangeNode(succ, local_node, key_range);
    CompareNodes(remote_node, local_node, succ, key_range);

    if(! remote_node.IsLeaf() && ! local_node.IsLeaf()) {
        for(int i = 0; i < MerkleTree<std::string>::GetNumChildren(); ++i) {
            bool needs_sync = NeedsSync(remote_node.GetNthChild(i),
                                        local_node.GetNthChild(i),
                                        key_range);
            if(needs_sync) {
                SynchronizeHelper(succ, key_range, local_node.GetNthChild(i));
            }
        }
    }
}

bool DHashPeer::NeedsSync(const DbEntry &remote_node, const DbEntry &local_node,
                          const KeyRange &key_range)
{
    bool hashes_match = local_node.GetHash() == remote_node.GetHash(),
         overlap = /* local_node.Overlaps(key_range) */ true;

    return overlap && !hashes_match;
}


void DHashPeer::CompareNodes(const DbEntry &remote_node,
                             const DbEntry &local_node,
                             const RemotePeer &succ,
                             const KeyRange &key_range)
{
    if(remote_node.IsLeaf()) {
        for(const auto &[k, _] : remote_node.GetEntries()) {
            if(IsMissing(k, key_range)) {
                RetrieveMissing(k);
            }
        }
    }

    // Deal with the case in which trees have different dimensions for the
    // relevant range (e.g. we can't compare children nodes of local or remote
    // node, because equivalently-positioned children only exist on one).
    // In this case, a node should simply request all keys the synchronizing
    // node possesses within the given range and insert them into its tree.
    else if(local_node.IsLeaf()) {
        KvMap succ_kvs = ReadRange(succ, local_node.GetRange());

        for(const auto &[k, _] : succ_kvs) {
            RetrieveMissing(k);
        }
    }
}

bool DHashPeer::IsMissing(const ChordKey &k, const KeyRange &key_range)
{
    return k.InBetween(key_range.first, key_range.second, true) &&
           ! db_.Contains(k);
}

DHashPeer::DbEntry DHashPeer::ExchangeNode(const RemotePeer &succ,
                                           const DbEntry &node,
                                           const KeyRange &key_range)
{
    Json::Value exchange_req;
    exchange_req["COMMAND"] = "XCHNG_NODE";

    exchange_req["NODE"] = node.NonRecursiveSerialize(true);
    exchange_req["REQUESTER"] = ToRemotePeer();
    exchange_req["LOWER_BOUND"] = std::string(key_range.first);
    exchange_req["UPPER_BOUND"] = std::string(key_range.second);

    Json::Value resp = succ.SendRequest(exchange_req);

    return DbEntry(resp);
}

Json::Value DHashPeer::ExchangeNodeHandler(const Json::Value &request)
{
    DbEntry remote_node(request["NODE"]);
    std::deque<int> dirs = remote_node.GetPosition();
    std::optional<DbEntry> local_node = db_.GetIndex().LookupByPosition(dirs);

    RemotePeer requesting_node(request["REQUESTER"]);
    KeyRange key_range = { ChordKey(request["LOWER_BOUND"].asString(), true),
                           ChordKey(request["UPPER_BOUND"].asString(), true) };

    Log("Comparing nodes");
    CompareNodes(remote_node, local_node.value(), requesting_node, key_range);
    Log("Nodes compared");

    return local_node->NonRecursiveSerialize(true);
}

/* ----------------------------------------------------------------------------
 * MISC: Anything else. Mostly implementing pure virtual methods from the base
 *       class.
 * -------------------------------------------------------------------------- */

std::tuple<int, int, int> DHashPeer::GetIdaParams() const
{
    return std::make_tuple(n_, m_, p_);
}

void DHashPeer::SetIdaParams(int n, int m, int p)
{
    n_ = n;
    m_ = m;
    p_ = p;
}

Json::Value DHashPeer::ForwardRequest(const ChordKey &key,
                                      const Json::Value &request)
{
    // Get closest preceding node of key in finger table, forward request
    // to it.
    RemotePeer key_succ = finger_table_.Lookup(key);

    // If the finger table points to us, then it most likely belongs to our
    // predecessor, who absorbed a share of our keys when it joined.
    if(key_succ.id_ == id_ && predecessor_.Get().IsAlive()) {
        key_succ = predecessor_.Get();
    }

        // If, for whatever reason, the successor selected so far is not alive,
        // then we need to select another one, preferably from our successors list
        // but possibly just by defaulting to our predecessor.
    else if(! key_succ.IsAlive()) {
        std::optional<RemotePeer> succ_lookup = successors_.LookupLiving(key);

        if(succ_lookup.has_value()) {
            key_succ = succ_lookup.value();
        } else if(successors_.GetNthEntry(0).IsAlive()) {
            key_succ = successors_.GetNthEntry(0);
        } else {
            throw std::runtime_error("Lookup failed");
        }
    }

    return key_succ.SendRequest(request);
}

Json::Value DHashPeer::HandleNotifyFromPred(const RemotePeer &new_pred)
{
    Json::Value notify_resp;

    // Update any finger tables which should now point to new peer.
    finger_table_.AdjustFingers(new_pred);
    predecessor_.Set(new_pred);
    min_key_.Set(predecessor_.Get().id_ + 1);

    if(successors_.Size() == 0) {
        Log("2");
        successors_.Populate(GetNSuccessors(id_ + 1, num_succs_));
    }

    return notify_resp;
}

void DHashPeer::Fail()
{
    Log("Stopping server/stabilize loop now");
    if(server_->IsAlive()) {
        server_->Kill();
    }
    continue_maintenance_ = false;
}

void DHashPeer::AbsorbKeys(const Json::Value &kv_pairs)
{
    // This is a pure virtual function which gets called in the join handler
    // of the base class. It makes sense in ChordPeer and DHC, but, since keys
    // don't get transferred with joins in DHash, we just leave the method empty
    // here.
}

Json::Value DHashPeer::KeysAsJson()
{
    // Same as in last one. We don't actually transfer keys on joins/leaves,
    // so there's no use implementing this method.
    Json::Value _;
    return _;
}


void DHashPeer::HandlePredFailure(const RemotePeer &old_pred)
{
    // Adjust our finger table to account for that fact.
    finger_table_.AdjustFingers(ToRemotePeer());
    Rectify(predecessor_.Get());
}
