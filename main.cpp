#include "src/networking/server.h"
#include "src/networking/client.h"
#include <iostream>

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
        delete server_;
    }

    void Run()
    {
        server_->RunInBackground();
    }

private:
    const int value_;
    ServerType *server_;

    static Json::Value add_value(int value, const Json::Value &request)
    {
        Json::Value resp;
        resp["VALUE"] = request["VALUE"].asInt() + value;
        std::cout << "RETURNING " << resp.toStyledString() << std::endl;
        return resp;
    }

    static Json::Value hang()
    {
        while(true) {}
        Json::Value resp;
        return resp;
    }
};

#include <iostream>

int main()
{
    ServerWrapper sw(1, 5000);
    sw.Run();
    std::vector<Client*> clients(6, new Client());


    Json::Value add_one_req, hang_req;
    hang_req["COMMAND"] = "HANG";
    add_one_req["COMMAND"] = "ADD_VAL";
    add_one_req["VALUE"] = 1;

    std::thread t([clients, hang_req] {
        clients.at(0)->MakeRequest("127.0.0.1", 5000, hang_req);
    });

    for(int i = 0; i < 5000; i++)
        for(int j = 1; j < clients.size(); j++) {
            std::cout << i << std::endl;
            std::cout << clients.at(j)
                                 ->MakeRequest("127.0.0.1", 5000, add_one_req)
                                 .toStyledString()
                      << std::endl;
        }
    std::cout << "HERE" << std::endl;
}