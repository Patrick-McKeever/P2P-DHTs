#include "data_block.h"
#include "ida.h"

DataBlock::DataBlock(const std::string &input, int n, int m, int p)
    : n_(n)
    , m_(m)
    , p_(p)
    , original_(StrToInts(input))
    , ida_(n, m, p)
{
    Matrix frags = ida_.Encode(original_);
    for(int i = 0; i < frags.size(); ++i) {
        fragments_.emplace_back(frags[i], i + 1);
    }
}

DataBlock::DataBlock(const Json::Value &json_block)
    : n_(json_block["N"].asInt())
    , m_(json_block["M"].asInt())
    , p_(json_block["P"].asInt())
    , ida_(n_, m_, p_)
{
    Vector frag_indices;
    for(const auto &frag : json_block["FRAGMENTS"]) {
        fragments_.emplace_back(frag);
    }
    original_ = ida_.Decode(fragments_);
}

DataBlock::DataBlock(const std::vector<DataFragment> &fragments, int n, int m,
                     int p)
    : n_(n)
    , m_(m)
    , p_(p)
    , ida_(n_, m_, p_)
{
    // Create fragments and indices.
    std::vector<int> frag_indices;
    Matrix frag_matrix;
    for(const DataFragment &fragment : fragments) {
        frag_indices.push_back(fragment.index_);
        frag_matrix.push_back(fragment.fragment_);
    }

    // This may seem redundant. Why decode original and then re-encode it?
    // The answer is because the IDA::Decode method requires only a fraction
    // of the total fragments produced from encoding (in this case, only 10
    // of the 14 fragments produced from encoding are needed to decode.)
    // As a result, we cannot simply pass a list of fragments created from
    // frag_indices and frag_matrix to "FragsFromMatrix", we must instead
    // re-generate all 14 fragments, in case less than 14 were passed to us.
    original_ = ida_.Decode(frag_matrix, frag_indices);
    fragments_ = FragsFromMatrix(ida_.Encode(original_));
}

DataBlock::operator Json::Value() const
{
    Json::Value json_block;
    json_block["N"] = n_;
    json_block["M"] = m_;
    json_block["P"] = p_;
    json_block["FRAGMENTS"] = Json::arrayValue;
    for(const auto &frag : fragments_) {
        json_block["FRAGMENTS"].append(Json::Value(frag));
    }
    return json_block;
}

DataBlock::operator std::string() const
{
    std::string res;
    for(const DataFragment &fragment : fragments_) {
        res += std::string(fragment);
    }

    // Remove final "\n".
    res.pop_back();
    return res;
}

[[nodiscard]] std::string DataBlock::Decode() const
{
    std::string res;

    // "original_" is a double vector consisting of UTF codes, possibly
    // with padding.
    for(const auto &char_code : original_) {
        res += char(char_code);
    }

    // 0 codes are used to pad end of buffer
    while(char(res.back()) == 0) {
        res.pop_back();
    }

    return res;
}

bool operator == (const DataBlock &db1, const DataBlock &db2)
{
    return db1.original_ == db2.original_ && db1.fragments_ == db2.fragments_;
}