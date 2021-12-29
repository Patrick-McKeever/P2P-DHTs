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
    if(!split_by_bracket.back().empty()) {
        split_by_bracket.pop_back();
    }

    std::string res_str;
    for(const auto &sub_str : split_by_bracket) {
        res_str += sub_str + "}";
    }

    return res_str;
}

Json::Value Client::MakeRequest(const std::string &ip_addr, unsigned short port,
                                const Json::Value &request)
{
    boost::asio::io_context io;
    Json::StreamWriterBuilder writer_;
    // Send minified JSON.
    writer_["indentation"] = "";
    std::string serialized_req = Json::writeString(writer_, request);

    tcp::socket s(io);

    // connect, send
    s.connect({boost::asio::ip::address::from_string(ip_addr), port});
    boost::asio::write(s, boost::asio::buffer(serialized_req));
    s.shutdown(tcp::socket::shutdown_send);

    // read for max 5s
    boost::asio::steady_timer timer(io, std::chrono::seconds(5));
    timer.async_wait([&](error_code ec) { s.cancel(); });

    std::string reply_buf, json_data;
    error_code  reply_ec;
    async_read(s, boost::asio::dynamic_buffer(reply_buf),
               [&](error_code ec, size_t) { timer.cancel(); reply_ec = ec; });

    io.run();

    if (!reply_ec || reply_ec == boost::asio::error::eof) {
        Json::Value json_resp;
        JSONCPP_STRING parse_err;
        std::string sanitized_resp = SanitizeJson(reply_buf);

        Json::CharReader *reader = Json::CharReaderBuilder().newCharReader();
        bool success = reader->parse(sanitized_resp.c_str(),
                                     sanitized_resp.c_str() + sanitized_resp.length(),
                                     &json_resp, &parse_err);
        delete reader;
        if (success) {
            return json_resp;
        }

        throw std::runtime_error("Error parsing response.");
    } else {
        throw boost::system::system_error(reply_ec);
    }
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