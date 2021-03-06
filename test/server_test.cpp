#include "../src/networking/server.h"
#include "../src/networking/client.h"
#include <gtest/gtest.h>

class ServerWrapper1 {
public:
    using ReqHandler = std::function<Json::Value(const Json::Value &)>;
    using ServerType = Server<ReqHandler>;

    ServerWrapper1(int value, int port)
        : value_(value)
        , port_(port)
    {
        std::map<std::string, ReqHandler> commands = {
                { "ADD_VAL",
                 [this](const Json::Value &req) {
                     return add_value(value_, req);
                 }
                },
                { "HANG",
                  [this](const Json::Value &_) {
                     return hang();
                  }
                },
                { "LONG_REQ",
                  [this](const Json::Value &_) {
                     return long_response();
                 }
                }
        };

        server_ = std::make_shared<ServerType>(port, 3, commands);
    }

    ServerWrapper1(ServerWrapper1 &&rhs) noexcept
        : value_(rhs.value_)
        , port_(rhs.port_)
        , server_(std::move(rhs.server_))
    {
        rhs.moved_ = true;
    }

    // Stopping server in destructor triggers error.
    ~ServerWrapper1()
    {
//        if(! moved_) {
//            std::cout << "HERE" << std::endl;
//            server_->Kill();
//        }
    }

    void Run()
    {
        server_->RunInBackground();
    }

    void Kill()
    {
        server_->Kill();
    }

    void EnableLogging()
    {
        server_->EnableRequestLogging();
    }

    boost::circular_buffer<Json::Value> GetLog() const
    {
        return server_->GetLog();
    }


private:
    const int value_, port_;
    std::shared_ptr<ServerType> server_;
    bool moved_;

    static Json::Value add_value(int value, const Json::Value &request)
    {
        Json::Value resp;
        resp["VALUE"] = request["VALUE"].asInt() + value;
        return resp;
    }

    static Json::Value hang()
    {
        Json::Value resp;
        sleep(5);
        return resp;
    }

    static Json::Value long_response()
    {
        Json::Value resp;
        resp["DATA"] = std::string(16384, '0');
        return resp;
    }
};

class ServerWrapper {
public:
    using ReqHandler = std::function<Json::Value(const Json::Value &)>;
    using ServerType = Server<ReqHandler>;

    ServerWrapper(int value, int port)
            : value_(value)
    {
        std::map<std::string, ReqHandler> commands = {
                { "ADD_VAL",
                        [this](const Json::Value &req) {
                          return add_value(value_, req);
                        }
                },
                { "HANG",
                        [this](const Json::Value &_) {
                          return hang();
                        }
                }
        };

        server_ = new ServerType(port, 3, commands, true);
    }

    ServerWrapper(ServerWrapper &&rhs) noexcept
        : value_(rhs.value_)
        , server_(rhs.server_)
    {
        rhs.moved_ = true;
    }

    ~ServerWrapper()
    {
        if(! moved_)
            server_->HandleStop();
        delete server_;
    }

    void Run()
    {
        server_->RunInBackground();
    }

    void Kill()
    {
        server_->Kill();
    }

    void EnableLogging()
    {
        server_->EnableRequestLogging();
    }

    boost::circular_buffer<Json::Value> GetLog() const
    {
        return server_->GetLog();
    }

private:
    const int value_;
    ServerType *server_;
    bool moved_;

    static Json::Value add_value(int value, const Json::Value &request)
    {
        Json::Value resp;
        resp["VALUE"] = request["VALUE"].asInt() + value;
        return resp;
    }

    static Json::Value hang()
    {
        Json::Value resp;
        sleep(5);
        return resp;
    }
};

TEST(Server, Move)
{
    std::vector<ServerWrapper1> a;
    ServerWrapper1 b(1, 4105);
    b.Run();
    a.push_back(std::move(b));

    a.reserve(10);

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "ADD_VAL";
    add_one_req["VALUE"] = 1;
    add_one_resp = Client::MakeRequest("127.0.0.1", 4105, add_one_req);

    EXPECT_TRUE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["VALUE"].asInt(), 2);
//    b.Kill();
    std::cout << "";
}

#include <shared_mutex>
TEST(Request, Valid)
{
    ServerWrapper sw(1, 4000);
    sw.EnableLogging();
    sw.Run();

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "ADD_VAL";
    add_one_req["VALUE"] = 1;
    add_one_resp = Client::MakeRequest("127.0.0.1", 4000, add_one_req);

    EXPECT_TRUE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["VALUE"].asInt(), 2);

    boost::circular_buffer<Json::Value> log = sw.GetLog();

    std::cout << log[0].toStyledString();
//    sw.Kill();
}

TEST(Request, InvalidCommand)
{
    ServerWrapper sw(1, 4001);
    sw.Run();

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "INVALID_COMMAND";
    add_one_req["VALUE"] = 1;
    add_one_resp = Client::MakeRequest("127.0.0.1", 4001, add_one_req);

    EXPECT_FALSE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["ERRORS"].asString(), "Invalid command.");

//    sw.Kill();
}

TEST(Request, InvalidValue)
{
    ServerWrapper sw(1, 4002);
    sw.Run();

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "ADD_VAL";
    add_one_req["VALUE"] = "INVALID_VALUE";
    add_one_resp = Client::MakeRequest("127.0.0.1", 4002, add_one_req);

    EXPECT_FALSE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["ERRORS"].asString(),
              "Value is not convertible to Int.");

//    sw.Kill();
}

TEST(Client, IsAlive)
{
    ServerWrapper1 sw(1, 4003);
    sw.Run();

    EXPECT_TRUE(Client::IsAlive("127.0.0.1", 4003));
    sw.Kill();
    sleep(1);
    EXPECT_FALSE(Client::IsAlive("127.0.0.1", 4003));
}


TEST(Client, Timeout)
{
    ServerWrapper sw(1, 4004);
    sw.Run();

    Json::Value hang_req;
    hang_req["COMMAND"] = "HANG";
    EXPECT_ANY_THROW(Client::MakeRequest("127.0.0.1", 4004, hang_req));
    sw.Kill();
}

TEST(Request, LongRequest)
{
    ServerWrapper sw(1, 4005);
    sw.Run();

    Json::Value long_req, long_resp;
    long_req["COMMAND"] = "LONG_REQ";
    long_req["DATA"] = std::string(16384, '0');
    long_resp = Client::MakeRequest("127.0.0.1", 4005, long_req);

    EXPECT_TRUE(long_resp["SUCCESS"].asBool());
    EXPECT_EQ(long_resp["DATA"].asString(), std::string(16384, '0'));

    sw.Kill();
}