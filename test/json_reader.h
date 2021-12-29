/**
 * json_reader.h
 *
 * Since we read unit test info from JSON files, we can significantly expedite
 * the testing process by creating some functions to help out with reading files
 * and doing test setup from them.
 */

#ifndef CHORD_AND_DHASH_JSON_READER_H
#define CHORD_AND_DHASH_JSON_READER_H

#include "json/json.h"
#include <filesystem>
#include <functional>
#include <iostream>

/**
 * Given a file path relative to the program file (e.g. the CPP from which this
 * function is called), return a path relative to the executable from which
 * the program is running.
 *
 * @param path Path relative to program file (*.cpp, *.h).
 * @return Path relative to executable.
 */
std::filesystem::path RelativePath(const std::string &path);

/**
 * Given a file path relative to a code file, transform it into a path relative
 * to the executable.
 *
 * @param file_path Path to JSON file relative to program file (*.cpp, *.h)
 * @return Parsed JSON from within file.
 */
Json::Value JsonFromFile(const std::string &file_path);

/**
 * Given a JSON representation of peers, populate the given vector with the
 * peers specified in the JSON, and start a chord from these peers.
 *
 * @tparam PeerType The type of peer to be used (e.g. ChordPeer, DHashPeer).
 *                  Should have a constructor taking an ip_addr (str), then
 *                  a port (unsigned short), then the length of the successor
 *                  list (int).
 * @param peers The JSON representation of peers, a list of JSON objects
 *              specifying a port, an ip addr, and the number of successors
 *              for each element.
 * @param modifier A lambda to modify each element after its construction.
 * @param chord The vector to populate with peers in the JSON.
 */
template<typename PeerType>
void ChordFromJson(const Json::Value &peers,
                   std::vector<std::shared_ptr<PeerType>> &chord,
                   const std::function<void(std::shared_ptr<PeerType>)>
                        &modifier = [](std::shared_ptr<PeerType>) {})
{
    for(const auto &peer: peers) {
        std::string ip_addr = peer["IP"].asString();
        unsigned short port = peer["PORT"].asInt();
        int num_succs       = peer["NUM_SUCCS"].asInt();
        auto new_peer = std::make_shared<PeerType>(ip_addr, port, num_succs);
        modifier(new_peer);
        chord.push_back(new_peer);
    }

    chord[0]->StartChord();
    for(int i = 1; i < peers.size(); ++i) {
        chord[i]->Join(chord[0]->GetIpAddr(), chord[0]->GetPort());
    }
}

/**
 * Given a JSON representation of a set of peers and a chord of nodes (stored
 * in a vector of shared ptrs), have the new nodes join the chord.
 *
 * @tparam PeerType The type of peer in the chord.
 * @param joining_peers JSON-encoded representations of peers to join the chord.
 * @param modifier A lambda to modify each element after its construction.
 * @param chord The pre-existing chord of nodes.
 */
template<typename PeerType>
void AddJsonNodesToChord(const Json::Value &joining_peers,
                         std::vector<std::shared_ptr<PeerType>> &chord,
                         const std::function<void(std::shared_ptr<PeerType>)>
                         &modifier = [](std::shared_ptr<PeerType>) {})
{
    for(const auto &joining_peer : joining_peers) {
        std::string ip = joining_peer["IP"].asString();
        unsigned short port = joining_peer["PORT"].asInt();
        int num_succs = joining_peer["NUM_SUCCS"].asInt();
        auto new_peer = std::make_shared<PeerType>(ip, port, num_succs);
        modifier(new_peer);
        chord.push_back(new_peer);

        // Since we always test the first node in a chord, it's good to have
        // new nodes join the second. This prevents the first node from learning
        // about new nodes purely because it was randomly chosen as the gateway.
        // If we put 0 here in the update succ tests, for e.g., we would have
        // no way to verify that the first node was learning about new nodes
        // from the UpdateSuccList method as opposed to its use as a gateway.
        chord.back()->Join(chord[1]->GetIpAddr(), chord[1]->GetPort());
    }
}

#endif