#include "client.h"
#include <iostream>

/**
 * Split a string into a vector of substrings based on delimiter.
 *
 * @param s String to split.
 * @param delimiter The substring which delimits the substrings in the result.
 * @return A vector of substrings.
 */
static std::vector<std::string> Split(const std::string &s,
                                      const std::string &delimiter)
{
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;

    while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

/**
 * A poor connection can result in extraneous characters being appended to
 * the end of messages. Remove those characters if they exist.
 *
 * @param serializedJson Serialized JSON message received over a network
 *                       connection.
 * @return Serialized JSON without extraneous characters after final bracket.
 */
static std::string SanitizeJson(const std::string &serializedJson)
{
    std::vector<std::string> split_by_bracket = Split(serializedJson, "}");
    if(!split_by_bracket.back().empty())
        split_by_bracket.pop_back();

    std::string res_str;
    for(const auto &sub_str : split_by_bracket)
        res_str += sub_str + "}";

    return res_str;
}

Client::Client()
        : reader_((new Json::CharReaderBuilder)->newCharReader())
{}

Json::Value Client::MakeRequest(const std::string &ip_addr, unsigned short port,
                                const Json::Value &request)
{
    boost::asio::io_context io_context;

    std::string serialized_req = Json::writeString(writer_, request);
    tcp::socket s(io_context);

    tcp::resolver resolver(io_context);
    try {
        s.connect({boost::asio::ip::address::from_string(ip_addr), port});
    } catch(const std::exception &err) {
        throw std::exception();
    }
    boost::asio::write(s, boost::asio::buffer(serialized_req));
    s.shutdown(tcp::socket::shutdown_send);

    error_code ec;
    char reply[2048];

    size_t reply_length = boost::asio::read(s, boost::asio::buffer(reply),
                                            ec);

    Json::Value json_resp;
    JSONCPP_STRING parse_err;
    std::string resp_str(reply);
    std::string sanitized_resp = SanitizeJson(resp_str);

    Json::CharReader *reader = Json::CharReaderBuilder().newCharReader();
    bool success = reader->parse(sanitized_resp.c_str(),
                                 sanitized_resp.c_str() + sanitized_resp.length(),
                                 &json_resp, &parse_err);
    delete reader;
    if (success)
        return json_resp;

    throw std::runtime_error("Error parsing response.");
}

bool Client::IsAlive(const std::string &ip_addr, unsigned short port)
{
    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    try {
        s.connect({boost::asio::ip::address::from_string(ip_addr), port});
    } catch(const boost::wrapexcept<boost::system::system_error> &err) {
        s.close();
        return false;
    }

    s.close();
    return true;
}