#include "data_fragment.h"

DataFragment::DataFragment(Vector vector, int index, int n, int m, int p)
    : fragment_(std::move(vector))
    , index_(index)
    , n_(n)
    , m_(m)
    , p_(p)
{}

DataFragment::DataFragment(const Json::Value &json_frag)
    : index_(json_frag["INDEX"].asInt())
    , n_(json_frag["N"].asInt())
    , m_(json_frag["M"].asInt())
    , p_(json_frag["P"].asInt())
    , fragment_(ParseFromBase64(json_frag["FRAGMENT"].asString(),
                                ceil(log(p_) / log(64))))
{}

DataFragment::DataFragment(const std::string &encoded_frag)
{
//    encoded_frag.pop_back();
    std::vector<std::string> tm = Split(encoded_frag, ":");
    std::vector<std::string> prefix = Split(tm[0], " ");
    n_ = stoi(prefix[0]);
    m_ = stoi(prefix[1]);
    p_ = stoi(prefix[2]);
    index_ = stoi(prefix[3]);
    for(const auto &frag_el : Split(tm[1], " ")) {
        fragment_.push_back(stoi(frag_el));
    }
}

bool DataFragment::WriteToFile(const char *file_path) const
{
    Json::Value frag_as_json = ToJson();
    Json::FastWriter json_writer;

    std::ofstream output(file_path, std::ofstream::out);
    if(! output) {
        return false;
    }

    output << json_writer.write(frag_as_json);
    output.close();
    return true;
}

[[nodiscard]] Json::Value DataFragment::ToJson() const
{
    Json::Value frag;
    frag["M"] = m_;
    frag["N"] = n_;
    frag["P"] = p_;
    frag["INDEX"] = index_;

    // Since values must be in [0, p), ceil(log64(p)) digits are needed to
    // represent a number in that range.
    int digits_per_val = ceil(log(p_) / log(64));
    frag["FRAGMENT"] = SerializeToBase64(fragment_, digits_per_val);
    return frag;
}

DataFragment::operator Vector() const
{
    return fragment_;
}

DataFragment::operator Json::Value() const
{
    return ToJson();
}

DataFragment::operator std::string() const
{
    std::string fragment_str;
    for(double value : fragment_) {
        fragment_str += std::to_string(value) + " ";
    }
    // Remove trailing space.
    fragment_str.pop_back();

    std::string prefix = std::to_string(m_) + " " + std::to_string(n_) + " " +
                         std::to_string(p_) + " " + std::to_string(index_);
    return prefix + ":" + fragment_str + "\n";
}

bool operator == (const DataFragment &df1, const DataFragment &df2)
{
    return df1.fragment_ == df2.fragment_ && df1.index_ == df2.index_;
}

bool operator < (const DataFragment &df1, const DataFragment &df2)
{
    return df1.index_ < df2.index_;
}

std::string SerializeToBase64(const Vector &frag, int num_digits)
{
    std::string res;
    const auto max_int = (int) pow(64, num_digits);
    for(int val : frag) {
        if(val > max_int) {
            throw std::runtime_error("Cannot encode " + std::to_string(val) +
                                     ", since it exceeds the max value of " +
                                     std::to_string(max_int));
        }

        for(int i = num_digits - 1; i >= 0; --i) {
            res += BASE_64_ALPHABET[val / (int) pow(64, i)];
            val -= (val / (int) pow(64, i)) * (int) pow(64, i);
        }
    }
    return res;
}


Vector ParseFromBase64(const std::string &serialized_frag,
                               int num_digits)
{
    Vector res;
    res.reserve(serialized_frag.length() / num_digits);
    for(int i = 0; i < serialized_frag.length(); i += num_digits) {
        int el = 0;
        for(int j = 0; j < num_digits; ++j) {
            int digit = BASE64_CHAR_TO_INT.at(serialized_frag[i + j]);
            el += digit * (int) pow(64, num_digits - j - 1);
        }
        res.push_back(el);
    }
    return res;
}

std::string SerializeToBytes(const Vector &frag)
{

    std::vector<unsigned char> res;
    res.reserve(frag.size());
    for(int val : frag) {
        res.push_back((char) val);
    }
    return std::string(res.begin(), res.end());
}

Vector ParseFromBytes(const std::string &serialized_frag)
{
    Vector res;
    res.reserve(serialized_frag.length());
    for(unsigned char c : serialized_frag) {
        res.push_back(c);
    }
    return res;
}

StringArr Split(const std::string &s, const std::string &delimiter)
{
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    StringArr res;

    while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
        token = s.substr (pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back (token);
    }

    res.push_back (s.substr (pos_start));
    return res;
}

std::vector<DataFragment> FragsFromMatrix(const Matrix &matrix)
{
    std::vector<DataFragment> frags;
    frags.reserve(matrix.size());
    for(int i = 0; i < matrix.size(); i++) {
        frags.emplace_back(matrix[i], i + 1);
    }
    return frags;
}

DataFragment FragFromFile(const char *file_path)
{
    std::vector<DataFragment> frags;
    Json::Value root;
    Json::Reader reader;

    std::ifstream file(file_path, std::ifstream::binary);
    bool parsingSuccessful = reader.parse(file, root, false);

    if(!parsingSuccessful) {
        throw std::runtime_error("Parsing failed.");
    }

    file.close();
    return DataFragment(root);
}