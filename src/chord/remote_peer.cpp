#include "remote_peer.h"

RemotePeer::RemotePeer()
    : id_("0", true)
    , min_key_("0", true)
{}

RemotePeer::RemotePeer(ChordKey id, ChordKey min_key, std::string ip_addr,
                       unsigned short port)
    : id_(std::move(id))
    , min_key_(std::move(min_key))
    , ip_addr_(std::move(ip_addr))
    , port_(port)
{}

RemotePeer::RemotePeer(std::string ip_addr, unsigned short port)
    : ip_addr_(std::move(ip_addr))
    , port_(port)
{}

RemotePeer::RemotePeer(const Json::Value &members)
    : id_(members["ID"].asString(), true)
    , min_key_(members["MIN_KEY"].asString(), true)
    , ip_addr_(members["IP_ADDR"].asString())
    , port_(members["PORT"].asInt())
{}

Json::Value RemotePeer::SendRequest(const Json::Value &request) const
{
    if(IsAlive()) {
        Json::Value resp = Client::MakeRequest(ip_addr_, port_, request);
        if(resp["SUCCESS"].asBool()) {
            return resp;
        }
        throw std::runtime_error("Failed request: " + resp.toStyledString());
    }

    else {
        throw std::runtime_error("Peer is down.");
    }
}

bool RemotePeer::IsAlive() const
{
    return Client::IsAlive(ip_addr_, port_);
}

RemotePeer RemotePeer::GetSucc() const
{
    // To find the successor of a remote peer, we can simply ask it for the
    // successor of its own ID plus one.
    Json::Value succ_req, succ_resp;
    succ_req["COMMAND"] = "GET_SUCC";
    succ_req["KEY"] = std::string(id_ + 1);
    succ_resp = SendRequest(succ_req);
    return RemotePeer(succ_resp);
}

RemotePeer RemotePeer::GetPred() const
{
    // To find the predecessor of a remote peer, we can simply ask it for the
    // predecessor of its own ID.
    Json::Value pred_req, pred_resp;
    pred_req["COMMAND"] = "GET_PRED";
    pred_req["KEY"] = std::string(id_);
    pred_resp = SendRequest(pred_req);
    return RemotePeer(pred_resp);
}

bool operator == (const RemotePeer &lhs, const RemotePeer &rhs)
{
    return (lhs.ip_addr_ == rhs.ip_addr_ &&
            lhs.id_      == rhs.id_      &&
            lhs.min_key_ == rhs.min_key_ &&
            lhs.port_    == rhs.port_);
}

bool operator < (const RemotePeer &lhs, const RemotePeer &rhs)
{
    return lhs.id_ < rhs.id_;
}

RemotePeer::operator Json::Value() const
{
    Json::Value peer_json;
    peer_json["IP_ADDR"] = ip_addr_;
    peer_json["PORT"] = port_;
    peer_json["ID"] = std::string(id_);
    peer_json["MIN_KEY"] = std::string(min_key_);
    return peer_json;
}


ThreadSafeRemotePeer::ThreadSafeRemotePeer() = default;

ThreadSafeRemotePeer::ThreadSafeRemotePeer(const RemotePeer &peer)
    : peer_(peer)
{}

ThreadSafeRemotePeer::ThreadSafeRemotePeer(ThreadSafeRemotePeer &&rhs) noexcept
{
    WriteLock rhs_lock(rhs.mutex_);
    peer_ = std::move(rhs.peer_);
}

void ThreadSafeRemotePeer::Set(const RemotePeer &peer)
{
    WriteLock lock(mutex_);
    peer_ = peer;
}

RemotePeer ThreadSafeRemotePeer::Get() const
{
    ReadLock lock(mutex_);
    if(peer_.has_value()) {
        return peer_.value();
    }

    throw std::runtime_error("Peer does not have value.");
}

bool ThreadSafeRemotePeer::IsSet()
{
    return peer_.has_value();
}

void ThreadSafeRemotePeer::Reset()
{
    peer_.reset();
}