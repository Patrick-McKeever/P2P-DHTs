#include <gtest/gtest.h>
#include "../src/networking/server.h"
#include "../src/networking/client.h"

class ServerWrapper {
public:
    typedef std::function<Json::Value(const Json::Value &)> ReqHandler;
    using ServerType = Server<ReqHandler>;

    ServerWrapper(int value, int port)
            : value_(value)
    {
        std::map<std::string, ReqHandler> commands = {
                { "ADD_VAL",  [this](const Json::Value &req) {
                  return add_value(value_, req); } },
                { "HANG", [this](const Json::Value &_) { return hang(); }}
        };

        server_ = new ServerType(port, 3, commands);
    }

    ~ServerWrapper()
    {
        server_->HandleStop();
    }

    void Run()
    {
        server_->RunInBackground();
    }

    void Kill()
    {
        server_->Kill();
    }

private:
    const int value_;
    ServerType *server_;

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


TEST(Request, Valid)
{
    ServerWrapper sw(1, 5000);
    sw.Run();
    Client client;

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "ADD_VAL";
    add_one_req["VALUE"] = 1;
    add_one_resp = client.MakeRequest("127.0.0.1", 5000, add_one_req);

    EXPECT_TRUE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["VALUE"].asInt(), 2);

    sw.Kill();
}

TEST(Request, InvalidCommand)
{
    ServerWrapper sw(1, 5001);
    sw.Run();
    Client client;

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "INVALID_COMMAND";
    add_one_req["VALUE"] = 1;
    add_one_resp = client.MakeRequest("127.0.0.1", 5001, add_one_req);

    EXPECT_FALSE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["ERRORS"].asString(), "Invalid command.");

    sw.Kill();
}

TEST(Request, InvalidValue)
{
    ServerWrapper sw(1, 5002);
    sw.Run();
    Client client;

    Json::Value add_one_req, add_one_resp;
    add_one_req["COMMAND"] = "ADD_VAL";
    add_one_req["VALUE"] = "INVALID_VALUE";
    add_one_resp = client.MakeRequest("127.0.0.1", 5002, add_one_req);

    EXPECT_FALSE(add_one_resp["SUCCESS"].asBool());
    EXPECT_EQ(add_one_resp["ERRORS"].asString(),
              "Value is not convertible to Int.");

    sw.Kill();
}

TEST(Client, IsAlive)
{
    ServerWrapper sw(1, 5003);
    sw.Run();

    EXPECT_TRUE(Client::IsAlive("127.0.0.1", 5003));
    sw.Kill();
    sleep(1);
    EXPECT_FALSE(Client::IsAlive("127.0.0.1", 5003));
}