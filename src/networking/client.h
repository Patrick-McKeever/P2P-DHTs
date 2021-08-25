#ifndef CLIENT_H_CHORD_FINAL
#define CLIENT_H_CHORD_FINAL

/**
 * client.h
 *
 * This file aims to implement a simple client class which uses boost::asio to
 * communicate with a server in JSON requests. This client class should be able
 * to:
 *      - Send JSON requests to a given IP/port combo and return JSON responses.
 *      - Determine whether or not a server is running on a given IP/port combo.
 */

#include <json/json.h>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

using boost::asio::ip::tcp;
using boost::system::error_code;
typedef boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>
        rcv_timeout_option;

class Client {
public:
    /**
     * Constructor, initializes JSON parser and serializer.
     */
    Client();

    /**
     * Send JSON request to server, return JSON response from server.
     *
     * @param ip_addr IP addr of server.
     * @param port Port of server.
     * @param request Request to send to server.
     * @return Response from server to our request..
     */
    Json::Value MakeRequest(const std::string &ip_addr, unsigned short port,
                            const Json::Value &request);

    /**
     * Is a server running and accepting connections on ip_addr:port?
     *
     * @param ip_addr IP address to message.
     * @param port Port to message.
     * @return Whether or not this IP/port combo accepts our requests.
     */
    static bool IsAlive(const std::string &ip_addr, unsigned short port);

private:
    /// Reads JSON.
    const std::unique_ptr<Json::CharReader> reader_;
    /// Writes JSON.
    Json::StreamWriterBuilder writer_;
};

#endif