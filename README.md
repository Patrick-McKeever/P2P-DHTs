#P2P DHTs

##Theory

This library contains basic C++ implementations of the Chord and DHash protocols. The following section will detail the
theory underlying these protocols.
###The Chord Protocol
In the construction of peer-to-peer (P2P) distributed hash tables (DHT), one faces an inherent tradeoff between the efficiency of lookups and of routing tables. Larger routing tables facilitate quicker lookups of keys (i.e. fewer hops from one node to the node storing the relevant key), though maintaining these tables can become infeasible in systems containing large amounts of nodes. Likewise, smaller hash tables necessitate more hops for lookups.

The chord protocol achieves a desirable balance between lookup speed and routing table size, allowing for `O(log(n))` lookups with routing tables ("finger tables") consisting of only a number of entries commensurate to the number of digits in a node's binary identifier (Stoica et al, p. 9). In the chord protocol, nodes are positioned around a "chord ring" based on a binary identifier derived from a hash of their IP address (and port). (Key, Value) pairs are similarly hashed based on the key and placed within the chord ring, such that each node controls keys within the range `(PREDECESSOR_ID, NODE_ID]` - a technique known as "consistent hashing" (Karger et al).
![Consistent Hashing, (Stoica et al, p. 3)](docs/imgs/consistentHashing.png?raw=true "Consistent Hashing")

In order to assist efficient lookups, nodes maintain "finger tables", which maintain the successor node of all keys `([NODE_ID] + 2^(i - 1)) mod 2^m` for all values of `i = 0...m`. (Recall that `m` denotes the number of digits in nodes' binary identifiers.)
![Finger Tables, (Stoica et al, p. 5)](docs/imgs/fingerTable.png?raw=true "Finger Tables")

When a new node seeks to join the chord, it contacts a "gateway node". The gateway node subsequently computes the ID of the new node (a hash of its IP and potentially the port it used to initiate contact) and identifies its successor within the chord ring. The new node now simply inserts itself between its successor and its successor's predecessor, notifying the successor to update the value of its predecessor to the new node. The new node subsequently absorbs all keys greater than the ID of its successor's predecessor and less-than-or-equal-to its own ID. At this point, nodes are instructed to update predecessors and finger tables. Similarly, when a node wishes to leave the chord, its keys are absorbed by its successor, and its successor and predecessor must update their predecessor, successor, and finger table values accordingly.
![Join and Leave Procedures, (Stoica et al, p. 6)](docs/imgs/joinAndLeave.png?raw=true "Join and Leave Procedures")

Lastly, nodes must periodically run a "stabilize" protocol, in which a node n asks its successor for the value of its predecessor. If n is not the predecessor, as it would expect to be, then n must inform the successor's predecessor of its existence and update its finger table (Stoica et al).

Later literature on the topic has examined the issue of fault tolerance in the chord protocol. Research by Pamela
Zave has proven that nodes can repair the network in the event of a network partition, provided that they maintain
a list of their n successors. Doing so allows nodes to "fill in the gaps" between successors, weeding out the unreachable
ones, in a process called "rectifying" (Zave).

###The DHash Protocol

Stoica et al's original specification of the chord protocol suggested no method to replicate data, instead leaving the task to 
the application implementing the chord protocol. In 2003, Josh Cates suggested the DHash protocol to do precisely this.

DHash relies on Chord to facilitate lookups of keys, placement of nodes, and so forth. However, it differs from the Chord 
protocol in one notable way. Rather than assigning a single document to the immediate successor of that document's key (as in Chord),
DHash instead encodes documents through an information dispersal algorithm (IDA). This IDA will encode a document and produce
`n` distinct fragments, of which only `m` are necessary to reconstruct the original, such that `m < n` (Rabin). In DHash,
the `n` successors of a given key each store a distinct fragment from that key. Since `n > m`, the network can afford to 
lose `n - m` nodes before a document becomes unreachable.

DHash relies on 2 periodic maintenance protocols to ensure that data fragments are properly placed:
- In the local maintenance protocol, a node ensures that each of its successors possesses a fragment for the keys in its range. For each 
  of its `n` successors, the node will recursively descend through a Merkle tree representing the logical ring's keyspace
  and determine any differences between its successor and its own databases within the node's immediate range. If these exist,
  the node running the local maintenance protocol will supply fragments of the relevant keys to the successor in question.
- In the global maintenance protocol, a node iterates through the keys in its database. For each key, the node determines 
  whether or not it ought to hold the key in its database - i.e. whether or not the node running the protocol is one of the `n`
  successors of the key in question. If it is not, the node will send the key to one of those successors and delete it from
  its own database (Cates).
  
##Implementation
This library implements the Chord protocol as outlined by Stoica et al, with the modifications suggested by Zave to allow for
rectification in the event of network partitions. As such, this implementation can tolerate the arbitrary loss of peers
while maintaining consistent lookups throughout the network. The class `ChordPeer` provides an implementation of the Chord Protocol,
including the ability to insert blocks of data into the overlay network. (Note that `ChordPeer` does not replicate these data blcoks;
for fault tolerance with regards to data, you must use `DHashPeer` instead.)

My implementation of the DHash protocol relies on Rabin's information dispersal algorithm, with default parameters n = 14, m = 10,
and p = 256. This is sufficient to allow encoding of most file formats. The `DHashPeer` class provides an implementation
of the DHash protocol.

##Interface
**NOTE:** More complete documentation can be found in the `docs` directory, but I thought it would be prudent to provide an 
explanation of the most pertinent parts of the interface in the `README`. This constitutes only a small portion of the 
overall library, however.
###AbstractChordPeer
The following public methods appear within `AbstractChordPeer`, the base class of `ChordPeer` and `DHashPeer`.
  
**Joining and Leaving the Overlay Network:**
- `void AbstractChordPeer::StartChord()`: Create a chord with this peer as the initial node.
- `void AbstractChordPeer::Join(const std::string &gateway_ip, unsigned short gateway_port)`: Join the chord through 
  a *gateway node*, an existing peer within the overlay network identified through the `gateway_ip` and `gateway_port`
  parameters.
- `void AbstractChordPeer::Leave()`: Leave the overlay network while informing all relevant peers and transferring all
  relevant keys.
  
**Storing/Reading Keys and Values from the Overlay Network:**
- `void AbstractChordPeer::Create(const std::string &unhashed, const std::string &val)`: Create a key-value pair and 
  store it within the overlay network.
- `std::string AbstractChordPeer::Read(const std::string &unhashed)`: Return the value associated with the key 
  `unhashed`, if it is stored within the overlay network. Otherwise throw an error. 
- `void UploadFile(const std::string &file_path)`: Read the contents of the given (absolute) path `file_path`, encode it 
  (if necessary, as it is with DHash), and store its contents within the overlay network.
- `void DownloadFile(const std::string &file_name, const std::string &output_path)`: If a file with the name `file_name` 
  has been uploaded into the overlay network, retrieve its contents, decode it, and save those contents to the absolute 
  path `output_path`. If this file does not exist in the network, throw an error.
  
**Positional Lookups:**
- `RemotePeer GetSuccessor(const std::string &unhashed_key)`: Return the peer possessing the lowest identifier in the 
  overlay network which exceeds the SHA-1 hash of `unhashed_key`.
- `std::vector<RemotePeer> GetNSuccessors(const std::string &unhashed_key, int n)`: Return the list of peers possessing 
  the `n` lowest identifiers in the  overlay network which are higher than the SHA-1 hash of `unhashed_key`.
- `RemotePeer GetPredecessor(const std::string &unhashed_key)`: Return the peer possessing the highest identifier in 
  the overlay network which is lower than the SHA-1 hash of `unhashed_key`.
- `std::vector<RemotePeer> GetNPredecessors(const std::string &unhashed_key, int n)`: Return the list of peers 
  possessing the `n` highest identifiers in the overlay network which are lower than the SHA-1 hash of `unhashed_key`.
  
###Chord

- `ChordPeer::ChordPeer(std::string ip_addr, unsigned short port, int num_succs)`: Construct a `ChordPeer` running a 
server on `ip_addr`:`port`, which maintains a successor list of `num_succs`. `ChordPeer` relies on RAII for its server
and maintenance thread, so construction of a `ChordPeer` will begin running its server and a thread to
periodically run the stabilize protocol.
###DHash

- `DHashPeer(std::string ip_addr, int port, int num_replicas)`: Construct a `DHashPeer` running a
  server on `ip_addr`:`port`, which maintains a successor list of `num_succs`. `DHashPeer` relies on RAII for its server
  and maintenance thread, so construction of a `DHashPeer` will begin running its server and a thread to
  periodically run the global maintenance, local maintenance, and stabilize protocols.
- `std::tuple<int, int, int> GetIdaParams() const`: Get the parameters of the peer's information dispersal algorithm in
  the form of a tuple `(n, m, p)`, where `n` denotes the total number of fragments generated by the IDA for any given
  data block, `m` the minimum number of fragments necessary to reconstruct the original block, and `p` the prime value
  used for modulus operations.
- `void SetIdaParams(int n, int m, int p)`: Set the IDA parameters, where `n` denotes the total number of fragments generated by the IDA for any given
  data block, `m` the minimum number of fragments necessary to reconstruct the original block, and `p` the prime value
  used for modulus operations.
##Installation
To use this library in a CMake project, insert the following into your project's `CMakeLists.txt`:
```cmake
include(FetchContent)

FetchContent_Declare(
      p2p_dhts
      URL https://github.com/Patrick-McKeever/P2P-DHTs.git
)

FetchContent_MakeAvailable(p2p_dhts)

target_link_libraries([YOUR TARGET HERE] dhts)
```
##Sources
- Cates, Josh. “Robust and efficient data management for a distributed hash table.” (2003).
- Karger, David, et al. “Consistent Hashing and Random Trees: Distributed Caching Protocols for Relieving Hot Spots on the World Wide Web.” Proceedings of the Twenty-Ninth Annual ACM Symposium on Theory of Computing, Association for Computing Machinery, 1997, pp. 654–63. ACM Digital Library, doi:10.1145/258533.258660.
- Rabin M.O. (1990) The Information Dispersal Algorithm and its Applications. In: Capocelli R.M. (eds) Sequences. Springer, New York, NY. https://doi.org/10.1007/978-1-4612-3352-7_32
- Stoica, Ion & Morris, Robert & Karger, David & Kaashoek, M. & Balakrishnan, Hari. (2001). Chord: A Scalable Peer-to-Peer Lookup Service for Internet Applications. ACM SIGCOMM Computer Communication Review, vol. 31. 31. 10.1145/964723.383071.
- Zave, Pamela. "How to make Chord correct (using a stable base)." CoRR, abs/1502.06461 (2015).
