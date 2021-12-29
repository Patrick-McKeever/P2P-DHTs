/**
 * server.h
 *
 * This file aims to implement the classes necessary to run a server with a set
 * of commands. Other files should be able to:
 *      - Instantiate a server class with a map of strings to handlers, handlers
 *        being static member functions of another class;
 *      - Run the server in a way such that it can received JSON requests with
 *        a "COMMAND" field, lookup the field's value in its commands map, run
 *        the appropriate handler to generate a JSON response, and return that
 *        JSON response to the client. The server should be multithreaded and
 *        able to support multiple clients concurrently.
 *
 * To accomplish this, we will create two template classes, each with two
 * template parameters. The template parameters are:
 *      - RequestHandler : the type of the static member functions that will
 *                         handle requests and produce JSON responses.
 *
 * These template parameters will be used to implement two template classes:
 *      - Session : To handle a single connection with a client.
 *      - Server  : To accept multiple connections with multiple clients and
 *                  create/run new sessions for each of them.
 *
 * Due to undefined behavior of the berkeley sockets API (sys/sockets.h),
 * I have chosen to implement network IO through the boost::asio library.
 **/

#ifndef CHORD_AND_DHASH_SERVER_H
#define CHORD_AND_DHASH_SERVER_H

#include <boost/asio.hpp>
#include <json/json.h>
#include <boost/asio/placeholders.hpp>
#include <boost/thread/thread.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/optional.hpp>
#include <boost/array.hpp>
#include <boost/circular_buffer.hpp>
#include <map>
#include <iostream>
#include <utility>
#include <vector>
#include "../data_structures/thread_safe_queue.h"

using namespace boost::asio;
using namespace boost::asio::ip;
using boost::system::error_code;

/**
 * "Session" class represents a single connection with a client. Inherits from
 * boost::enable_shared_from_this to allow construction of shared_ptr<Session>
 * inside member functions via CRTP (asio is weird with lifetime management).
 * @tparam ReqHandlerType Type of handler function.
 */
template<typename ReqHandlerType>
class Session : public boost::enable_shared_from_this<Session<ReqHandlerType>> {
public:
    /**
     * Constructor.
     * @param context Context to run/stop.
     * @param commands Map of strings to functions which return JSON to send
     *                 to client.(Should this be const ref?)
     */
    explicit Session(io_context &context,
                     std::map<std::string, ReqHandlerType> commands,
                     bool &logging_enabled,
                     std::shared_ptr<ThreadSafeQueue<Json::Value>> queue)
        : commands_(std::move(commands))
        , reader_(Json::CharReaderBuilder().newCharReader())
        , strand_(boost::asio::make_strand(context))
        , socket_(strand_)
        , logging_enabled_(logging_enabled)
        , request_log_(std::move(queue))
    {
        // Send minified JSON, so as to reduce request length.
        writer_["indentation"] = "";
    }

    /**
     * @return Socket attribute (writable from "&").
     */
    tcp::socket& Socket()
    {
        return socket_;
    }

    /**
     * Read request from client, handle (which triggers response-sending).
     */
    void Run()
    {
        auto self(this->shared_from_this());
        async_read(socket_, boost::asio::dynamic_buffer(data_),
            [this, self](error_code ec, std::size_t length) {
                HandleRead(ec, length);
            });
    }

private:
    /// Map of strings (e.g. "GET", "PUT", etc.) to lambdas which take JSON
    /// requests as arguments and return JSON responses.
    std::map<std::string, ReqHandlerType> commands_;
    /// Buffer containing client data.
    std::string data_;
    /// Since async_write returns immediately, reply strings must exist beyond
    /// the scope in which they are called; thus, we will use a member var
    /// as opposed to an in-function variable.
    std::string reply_;
    /// Object to serialize JSON.
    Json::StreamWriterBuilder writer_;
    /// Object to parse JSON.
    const std::unique_ptr<Json::CharReader> reader_;
    /// Strand makes things thread-safe.
    strand<io_context::executor_type> strand_;
    /// Socket from which to read/write.
    tcp::socket socket_;
    /// Is logging enabled?
    bool logging_enabled_;
    /// If logging is enabled, push JSON values to this FIFO queue.
    std::shared_ptr<ThreadSafeQueue<Json::Value>> request_log_;

    /**
     * Having read into "data_" from a socket, handle the client's request.
     * @param ec General error code.
     * @param bytes_xfrd Number of bytes read from socket.
     */
    void HandleRead(error_code ec, std::size_t bytes_xfrd)
    {
        // EC of 2 isn't really an "error", per se. Just means that the client
        // closed the connection. The data is still available, and we can still
        // work with it.
        if(ec && ec.value() != 2) {
            std::cerr << ec.message() << std::endl;
            return;
        }

        JSONCPP_STRING parse_err;
        Json::Value json_req, json_resp;
        // Changed
        std::string client_req_str(data_);

        if (reader_->parse(client_req_str.c_str(),
                           client_req_str.c_str() + client_req_str.length(),
                           &json_req, &parse_err))
        {
            // If logging is enabled, log this request inside our queue.
            if(logging_enabled_) {
                request_log_->PushBack(json_req);
            }

            try {
                // Get JSON response.
                json_resp = ProcessRequest(json_req);
                json_resp["SUCCESS"] = true;
            } catch (const std::exception &ex) {
                // If ProcessRequest threw an error.
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

    /**
     * Shutdown socket after having written to socket.
     * @param ec Has an error occurred during write?
     */
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
        if(commands_.find(command) == commands_.end()) {
            throw std::runtime_error("Invalid command.");
        }

            // Otherwise, run the relevant handler.
        else {
            ReqHandlerType handler = commands_.at(command);
            response = handler(request);
        }
        return response;
    }
};

/**
 * "Server" class represents a multi-threaded server.
 * @tparam ReqHandlerType Type of handler function.
 */
template<typename ReqHandlerType>
class Server {
public:
    using ServerInstantiation = Server<ReqHandlerType>;
    using SessionPtr = boost::shared_ptr<Session<ReqHandlerType>>;
    using ThreadPtr = boost::shared_ptr<boost::thread>;

    /**
     * Constructor.
     * @param port Port on which to run server.
     * @param num_threads Number worker threads to run.
     * @param commands Map of strings to functions which return JSON to send
     *                 to client.
     */
    Server(const int port, const int num_threads,
           std::map<std::string, ReqHandlerType> commands,
           bool logging_enabled = false)
        : port_(port)
        , num_threads_(num_threads)
        , commands_(commands)
        , signals_(io_context_)
        , acceptor_(io_context_)
        , new_session_()
        , is_alive_(true)
        , logging_enabled_(logging_enabled)
        , request_log_(std::make_shared<ThreadSafeQueue<Json::Value>>(32))
    {
        /// Adding these signals allows threads to shut down gracefully when
        /// the process running the server terminates.
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

    Server(const Server &rhs) = delete;

    Server(Server &&rhs) noexcept
        : io_context_(std::move(rhs.io_context_))
        , port_(std::move(rhs.port_))
        , num_threads_(std::move(rhs.num_threads_))
        , commands_(std::move(rhs.commands_))
        , signals_(io_context_)
        , acceptor_(std::move(rhs.acceptor_))
        , is_alive_(std::move(rhs.is_alive_))
        , t_(std::move(rhs.t_))
        , new_session_(std::move(rhs.new_session_))
        , logging_enabled_(rhs.logging_enabled_)
        , request_log_(std::move(rhs.request_log_))
    {
        /// Adding these signals allows threads to shut down gracefully when
        /// the process running the server terminates.
        signals_.add(SIGINT);
        signals_.add(SIGTERM);
        signals_.add(SIGQUIT);
    }

    ~Server()
    {
        io_context_.stop();
        if(t_.joinable()) {
            t_.join();
        }
    }

    /**
     * Start/run worker threads, join upon completion.
     */
    void Run()
    {
        std::vector<ThreadPtr> threads;
        for(int i = 0; i < num_threads_; i++) {
            ThreadPtr new_thread(new boost::thread([ObjectPtr = &io_context_] {
                ObjectPtr->run();
            }));
            threads.push_back(new_thread);
        }

        for(auto & thread : threads) {
            thread->join();
        }
    }

    /**
     * Run the server as a background thread.
     */
    void RunInBackground()
    {
        is_alive_ = true;
        if(! t_.joinable()) {
            t_ = std::thread([this] {
                Run();
            });
        }
    }

    /**
     * Start accepting client connections, make a new session for each one.
     */
    void StartAccept()
    {
        new_session_.reset(
            new Session(io_context_, commands_, logging_enabled_, request_log_),
            [](Session<ReqHandlerType> *t) {
                delete t;
            });

        acceptor_.async_accept(new_session_->Socket(),
            [this](error_code ec) {
                HandleAccept(ec);
            });
    }

    /**
     * Run the session after it has been set up.
     * @param ec Has an error occurred during async_accept?
     */
    void HandleAccept(const error_code &ec)
    {
        if(! ec) {
            new_session_->Run();
        }
        StartAccept();
    }

    /**
     * Stop the server.
     */
    void Kill()
    {
        post(io_context_, [this] {
          acceptor_.close(); // causes .cancel() as well
        });

        is_alive_ = false;
    }

    /**
     * Instruct sessions to input JSON requests into ThreadSafeQueue reference
     * passed to Session constructor.
     */
    void EnableRequestLogging()
    {
        logging_enabled_ = true;
    }

    /**
     * Tell sessions to stop entering JSON requests into ThreadSafeQueue ref.
     */
    void DisableRequestLogging()
    {
        logging_enabled_ = false;
    }

    /**
     * Has server been killed? (Note: The server may still be running at this
     * point, due to the asynchronous nature of io_contexts. All this tells
     * us is whether server->Kill() has been called already).
     * @return is_alive_.
     */
    bool IsAlive()
    {
        return is_alive_;
    }

    /**
     * Stop the session (bound to SIGTERM to allow graceful exits).
     */
    void HandleStop()
    {
        io_context_.stop();
    }

    [[nodiscard]] boost::circular_buffer<Json::Value> GetLog() const
    {
        return request_log_->GetBuffer();
    }

private:
    /// Port on which server runs & number of worker threads.
    const int port_, num_threads_;
    /// If this is set to true, the server will log all requests in a FIFO
    /// queue of a certain length.
    bool logging_enabled_;
    /// If logging is enabled, we will push requests received from clients to
    /// this queue.
    std::shared_ptr<ThreadSafeQueue<Json::Value>> request_log_;
    /// Map of strings (e.g. "GET", PUT") to the functions which handle the
    /// corresponding requests. These functions should accept JSON requests as
    /// an argument and generate JSON responses.
    std::map<std::string, ReqHandlerType> commands_;
    /// Background thread on which server runs.
    std::thread t_;
    /// IO context on which server runs.
    io_context io_context_;
    /// We're gonna bind SIGTERM and SIGABRT to HandleStop.
    signal_set signals_;
    /// Acceptor for new client requests.
    tcp::acceptor acceptor_;
    /// New client session.
    SessionPtr new_session_;
    /// Has server been killed yet?
    bool is_alive_;
};

#endif
