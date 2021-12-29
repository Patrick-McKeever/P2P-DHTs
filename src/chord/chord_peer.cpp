#include "chord_peer.h"
#include <chrono>

using namespace std::chrono_literals;


/* ----------------------------------------------------------------------------
 * CONSTRUCTOR/DESTRUCTOR/MOVE: Use RAII for server/thread init/cleanup.
 * -------------------------------------------------------------------------- */

ChordPeer::ChordPeer(std::string ip_addr, unsigned short port, int num_succs)
        : AbstractChordPeer(std::move(ip_addr), port, num_succs)
        , continue_stabilize_(true)
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
            { "RECTIFY", [this](const Json::Value &req) {
              return RectifyHandler(req);
             } }
    };

    server_ = std::make_shared<ServerType>(port, 3, commands);
    server_->RunInBackground();

    // Avoid race condition.
    std::this_thread::sleep_for(10ms);
}

ChordPeer::ChordPeer(ChordPeer &&rhs) noexcept
    : AbstractChordPeer(std::move(rhs))
    , server_(std::move(rhs.server_))
    , db_(std::move(rhs.db_))
    , continue_stabilize_(rhs.continue_stabilize_)
    , stabilize_thread_(std::move(rhs.stabilize_thread_))
{}

ChordPeer::~ChordPeer()
{
    continue_stabilize_ = false;

    // We do the same thing when we leave, but it's necessary to do it
    // here in case a peer does not leave before its destructor is called
    // (e.g. if an error occurs).
    if(stabilize_thread_.joinable()) {
        stabilize_thread_.join();
    }
}

/* ----------------------------------------------------------------------------
 * CREATE/READ: Base chord doesn't allow for mutable data. Only create and read
 *              ops are possible. We also won't worry about replication at this
 *              stage, since Stoica states that this is the responsibility of
 *              the application running chord. We will implement these features
 *              in derived classes (e.g. DHC).
 * -------------------------------------------------------------------------- */

void ChordPeer::Create(const std::string &unhashed, const std::string &val)
{
    // This is the public interface for create op.
    // Dealing with a hashed key is a pain in the ass, so it's easier
    // to let people use strings for lookups and hash them as keys inside
    // the function.
    ChordKey hashed(unhashed, false);
    Create(hashed, val);
}

std::string ChordPeer::Read(const std::string &unhashed)
{
    // See note on create op.
    ChordKey hashed(unhashed, false);
    return Read(hashed);
}

void ChordPeer::Create(const ChordKey &key, const std::string &value)
{
    if(StoredLocally(key)) {
        db_.Insert({ key, value });
        return;
    }

    // Find succ of key, instruct it to store KV pair.
    RemotePeer succ = GetSuccessor(key);
    if(CreateKey(key, value, succ)) {
        return;
    }

    throw std::runtime_error("Remote creation failed");
}

bool ChordPeer::CreateKey(const ChordKey &key, const std::string &val,
                          const RemotePeer &peer)
{
    Json::Value create_req, create_resp;
    create_req["COMMAND"] = "CREATE_KEY";
    create_req["KEY"] = std::string(key);
    create_req["VALUE"] = Json::Value(val);
    create_resp = peer.SendRequest(create_req);
    return create_resp["SUCCESS"].asBool();
}

Json::Value ChordPeer::CreateKeyHandler(const Json::Value &req)
{
    Json::Value create_key_resp;
    ChordKey key(req["KEY"].asString(), true);
    std::string value = req["VALUE"].asString();

    if(StoredLocally(key)) {
        db_.Insert({key, value});
    } else {
        throw std::runtime_error("Key not in range.");
    }

    return create_key_resp;
}

std::string ChordPeer::Read(const ChordKey &key)
{
    if(StoredLocally(key)) {
        return db_.Lookup(key);
    }

    // Find succ of key, instruct it to return KV pair.
    RemotePeer succ = GetSuccessor(key);
    return ReadKey(key, succ);
}

std::string ChordPeer::ReadKey(const ChordKey &key, const RemotePeer &peer)
{
    Json::Value read_req, read_resp;
    read_req["COMMAND"] = "READ_KEY";
    read_req["KEY"] = std::string(key);
    read_resp = peer.SendRequest(read_req);

    if(read_resp["SUCCESS"].asBool()) {
        return read_resp["VALUE"].asString();
    }

    throw std::runtime_error("Key not stored on peer.");
}

Json::Value ChordPeer::ReadKeyHandler(const Json::Value &req)
{
    Json::Value read_key_resp;
    ChordKey key(req["KEY"].asString(), true);

    // Lookup method will automatically throw error if key does not exist,
    // and this will be handled by server code so as to generate a response
    // indicating that the request was unsuccessful. Hence, we do not need
    // to check if the key exists in the db before looking it up.
    if(StoredLocally(key)) {
        read_key_resp["VALUE"] = db_.Lookup(key);
    } else {
        throw std::runtime_error("Key not stored locally.");
    }

    return read_key_resp;
}


/* ----------------------------------------------------------------------------
 * MISC: Anything else. Mostly implementing pure virtual methods from the base
 *       class.
 * -------------------------------------------------------------------------- */

Json::Value ChordPeer::ForwardRequest(const ChordKey &key,
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
        std::optional<RemotePeer> succ_lookup = successors_.Lookup(key);
        if(succ_lookup.has_value() && succ_lookup->IsAlive()) {
            key_succ = succ_lookup.value();
        } else {
            throw std::runtime_error("Lookup failed");
        }
    }

    return key_succ.SendRequest(request);
}

void ChordPeer::StabilizeLoop()
{
    auto timestamp = std::chrono::high_resolution_clock::now();
    while(continue_stabilize_) {
        try {
            auto now = std::chrono::high_resolution_clock::now();
            if (now - timestamp < 5s) {
                std::this_thread::sleep_for(10ms);
                continue;
            }
            Stabilize();
            timestamp = std::chrono::high_resolution_clock::now();
        } catch(const std::exception &ex) {
            // In this case, we've become isolated from the chord. This can
            // occur for one of two reasons:
            //    (1) We've voluntarily left the chord, in which case continue,
            //        will force us to evaluate  continue_stabilize_ again,
            //        leading us to see that it is now false and stop;
            //    (2) We've become isolated from the chord through some type
            //        of network partition. In this case, we will also continue,
            //        find continue_stabilize_ to be true, and attempt stabiliz-
            //        ation again in an attempt to rejoin the network.
            Log("CAUGHT " + std::string(ex.what()) + " - CONTINUING");
            timestamp = std::chrono::high_resolution_clock::now();
            continue;
        }
    }
}

void ChordPeer::AbsorbKeys(const Json::Value &kv_pairs)
{
    // In case where one peer is sending a JSON map of KV pairs to another,
    // this function will be called on that JSON map to insert the KV pairs
    // into the recipient's DB.
    for(Json::Value::const_iterator itr = kv_pairs.begin();
        itr != kv_pairs.end(); ++itr)
    {
        ChordKey key(itr.key().asString(), true);
        std::string val = (*itr).asString();
        db_.Insert({ key, val });
    }
}

Json::Value ChordPeer::HandleNotifyFromPred(const RemotePeer &new_pred)
{
    // This function returns a response for a new peer that is a predecessor.
    Json::Value notify_resp, data_to_transfer;

    // Any keys between our old predecessor's id (exclusive) and
    // the new node's (i.e. our new predecessor's) id (inclusive) now
    // belong to the new node.
    TextDb::KeyValMap keys_to_transfer = db_.ReadRange(min_key_.Get(),
                                                       new_pred.id_);

    for(const auto &[key, val] : keys_to_transfer) {
        data_to_transfer[std::string(key)] = val;
        db_.Delete(key);
    }

    // Update any finger tables which should now point to new peer.
    finger_table_.AdjustFingers(new_pred);
    predecessor_.Set(new_pred);
    min_key_.Set(predecessor_.Get().id_ + 1);

    notify_resp["KEYS_TO_ABSORB"] = data_to_transfer;

    return notify_resp;
}


void ChordPeer::HandlePredFailure(const RemotePeer &old_pred)
{
    // Adjust our finger table to account for that fact.
    finger_table_.AdjustFingers(ToRemotePeer());
    // Inform other nodes that we are replacing the failed predecessor and that
    // they ought to update their FTs.
//    Rectify(predecessor_.Get());
    Rectify(old_pred);
}

void ChordPeer::Fail()
{
    Log("Stopping server/stabilize loop now");
    if(server_->IsAlive()) {
        server_->Kill();
    }
    continue_stabilize_ = false;
}

Json::Value ChordPeer::KeysAsJson()
{
    Json::Value keys_to_transfer;
    for(const auto &[key, value] : db_.GetIndex().GetEntries()) {
        keys_to_transfer[std::string(key)] = value;
    }

    return keys_to_transfer;
}

void ChordPeer::StartMaintenance()
{
    stabilize_thread_ = std::thread([this] { StabilizeLoop(); });
    stabilize_thread_.detach();
}