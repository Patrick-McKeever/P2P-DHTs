#ifndef CHORD_AND_DHASH_SERVER_H
#define CHORD_AND_DHASH_SERVER_H

#include <boost/asio.hpp>
#include <json/json.h>
#include <boost/asio/placeholders.hpp>
#include <boost/thread/thread.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>
#include <map>
#include <iostream>
#include <vector>

using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

template<typename ReqHandlerType>
class Session : public boost::enable_shared_from_this<Session<ReqHandlerType>> {
public:
    explicit Session(io_context &context,
                     std::map<std::string, ReqHandlerType> commands)
        : commands_(commands)
        , reader_((new Json::CharReaderBuilder)->newCharReader())
        , strand_(boost::asio::make_strand(context))
        , socket_(strand_)
    {}

    tcp::socket& Socket()
    {
        return socket_;
    }

    void Run()
    {
        auto self(this->shared_from_this());
        socket_.async_read_some(buffer(data_),
            [this, self](error_code ec, std::size_t length) {
                HandleRead(ec, length);
            });
    }

private:
    std::map<std::string, ReqHandlerType> commands_;
    boost::array<char, 4096> data_;
    std::string reply_;
    Json::StreamWriterBuilder writer_;
    const std::unique_ptr<Json::CharReader> reader_;
    strand<io_context::executor_type> strand_;
    tcp::socket socket_;

    void HandleRead(error_code ec, std::size_t bytes_xfrd)
    {
        if(! ec) {
            JSONCPP_STRING parse_err;
            Json::Value json_req, json_resp;
            std::string client_req_str(data_.data(), data_.data() + bytes_xfrd);

            if (reader_->parse(client_req_str.c_str(),
                               client_req_str.c_str() + client_req_str.length(),
                               &json_req, &parse_err))
            {
                try {
                    // Get JSON response.
                    json_resp = ProcessRequest(json_req);
                    json_resp["SUCCESS"] = true;
                } catch (const std::exception &ex) {
                    // If json parsing failed.
                    json_resp["SUCCESS"] = false;
                    json_resp["ERRORS"] = std::string(ex.what());
                }
            } else {
                // If json parsing failed.
                json_resp["SUCCESS"] = false;
                json_resp["ERRORS"] = std::string(parse_err);
            }

            reply_ = Json::writeString(writer_, json_resp);
            auto self(this->shared_from_this());
            async_write(socket_, buffer(reply_),
                [this, self](error_code ec, std::size_t bytes_xfrd) {
                    HandleWrite(ec);
                });
        }
    }

    void HandleWrite(const error_code &ec)
    {
        if(! ec) {
            error_code ignored_ec;
            socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
        }
    }

    /**
     * Lookup command specified in request in commands map. Call it,
     * and return its response.
     *
     * @param request Request issued by client.
     * @return Response to request.
     */
    Json::Value ProcessRequest(Json::Value request)
    {
        Json::Value response;
        std::string command = request["COMMAND"].asString();

        // If command is not valid, give a response with an error.
        if(commands_.find(command) == commands_.end())
            throw std::runtime_error("Invalid command.");

            // Otherwise, run the relevant handler.
        else {
            ReqHandlerType handler = commands_.at(command);
            response = handler(request);
        }
        return response;
    }
};

template<typename ReqHandlerType>
class Server {
public:
    using ServerInstantiation = Server<ReqHandlerType>;
    using SessionPtr = boost::shared_ptr<Session<ReqHandlerType>>;
    using ThreadPtr = boost::shared_ptr<boost::thread>;

    Server(const int port, const int num_threads,
           std::map<std::string, ReqHandlerType> commands)
        : port_(port)
        , num_threads_(num_threads)
        , commands_(commands)
        , signals_(io_context_)
        , acceptor_(io_context_)
        , new_session_()
    {
        signals_.add(SIGINT);
        signals_.add(SIGTERM);
        signals_.add(SIGQUIT);

        boost::asio::ip::tcp::resolver resolver(io_context_);
        boost::asio::ip::tcp::endpoint endpoint =
                *resolver.resolve(tcp::endpoint(tcp::v4(), port)).begin();
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        StartAccept();
    }

    void Run()
    {
        std::vector<ThreadPtr> threads;
        for(int i = 0; i < num_threads_; i++) {
            ThreadPtr new_thread(new boost::thread([ObjectPtr = &io_context_] {
                ObjectPtr->run();
            }));
            threads.push_back(new_thread);
        }

        for(auto & thread : threads)
            thread->join();
    }

    void RunInBackground()
    {
        if(! t_.joinable()) {
            t_ = std::thread([this] {
                Run();
            });
        }
    }

    void StartAccept()
    {
        new_session_.reset(new Session(io_context_, commands_));
        acceptor_.async_accept(new_session_->Socket(),
            [this](error_code ec) {
                HandleAccept(ec);
            });
    }

    void HandleAccept(const error_code &ec)
    {
        if(! ec)
            new_session_->Run();
        StartAccept();
    }

    void Kill()
    {
        post(io_context_, [this] {
          std::cout << "CLOSING" << std::endl;
          acceptor_.close(); // causes .cancel() as well
        });
    }

    void HandleStop()
    {
        io_context_.stop();
    }

private:
    const int port_, num_threads_;
    std::map<std::string, ReqHandlerType> commands_;
    std::thread t_;

    io_context io_context_;
    signal_set signals_;
    tcp::acceptor acceptor_;
    SessionPtr new_session_;
};

#endif
