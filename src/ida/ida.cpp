#include "ida.h"

Vector CharsToInts(const std::vector<unsigned char> &v)
{
    Vector res;
    for(unsigned char el : v) {
        res.push_back((int) el);
    }
    return res;
}

Vector StrToInts(const std::string &str)
{
    std::vector<unsigned char> chars;
    std::copy(str.begin(), str.end(), std::back_inserter(chars));
    return CharsToInts(chars);
}

bool AllZeroes(const Vector &v)
{
    for(int el : v) {
        if(el != 0) {
            return false;
        }
    }

    return true;
}

char *ReadFile(const char *file_path)
{
    std::ifstream file_stream(file_path, std::ifstream::binary);
    if(! file_stream) {
        throw std::runtime_error("Error opening file");
    }

    // Get file length, read file contents.
    file_stream.seekg(0, std::ifstream::end);
    int file_len = file_stream.tellg();
    file_stream.seekg(0, std::ifstream::beg);
    char *buffer = new char[file_len + 1];
    file_stream.read(buffer, file_len + 1);

    file_stream.close();
    return buffer;
}

IDA::IDA(int n, int m, int p)
    : n_(n)
    , m_(m)
    , p_(p)
    , encoding_matrix_(ConstructEncodingMatrix(m, n, p))
{
    if(! (n > m && p > n)) {
        throw std::runtime_error("Incorrect parameters.");
    }
}

Matrix IDA::Encode(const Vector &v)
{
    Matrix segments = SplitToSegments(v);
    Matrix fragments;
    for(int i = 0; i < n_; ++i) {
        Vector fragment;
        for(const auto &segment : segments) {
            int prod = InnerProduct(encoding_matrix_[i], segment, p_);
            fragment.push_back(prod);
        }
        fragments.push_back(fragment);
    }

    return fragments;
}

Matrix IDA::EncodePlaintext(const std::string &str)
{
    return Encode(StrToInts(str));
}

Matrix IDA::EncodeFile(const char *file_path)
{
    std::ifstream file_stream(file_path, std::ifstream::binary);
    if(! file_stream) {
        throw std::runtime_error("Error opening file");
    }

    std::vector<unsigned char> file_contents;

    if(! file_stream.eof() && ! file_stream.fail())
    {
        // Find the length of the file, allocate appropriately sized buffer
        file_stream.seekg(0, std::ifstream::end);
        std::streampos length = file_stream.tellg();
        file_stream.seekg(0, std::ifstream::beg);
        file_contents.resize(length);

        file_stream.seekg(0, std::ios_base::beg);
        file_stream.read((char *) &file_contents[0], length);
    }

    file_stream.close();
    return Encode(CharsToInts(file_contents));
}

void IDA::EncodeToFiles(const char *in_file,
                        const std::vector<std::string> &out_files)
{
    if(out_files.size() != n_) {
        throw std::runtime_error("Number of outfiles should be " +
                                 std::to_string(n_));
    }

    Matrix encoded_file = EncodeFile(in_file);
    std::vector<DataFragment> frags = FragsFromMatrix(encoded_file);
    for(int i = 0; i < out_files.size(); ++i) {
        frags[i].WriteToFile(out_files[i].c_str());
    }
}

Vector IDA::Decode(const Matrix &encoded, const Vector &frag_indices)
{
    if(encoded.size() < m_) {
        throw std::runtime_error(std::to_string(m_) + " frags are required"
                                                      " to decode.");
    }

    Vector first_m_frags(frag_indices.begin(), frag_indices.begin() + m_);
    Matrix inv_encoding_matrix = VandermondeInverse(first_m_frags, p_),
            output_matrix = MatrixProduct(inv_encoding_matrix, encoded, p_),
            original_segments;

    int num_cols = output_matrix[0].size(),
        num_rows = output_matrix.size();

    for(int i = 0; i < num_cols; ++i) {
        Vector col;
        for(int j = 0; j < num_rows; ++j) {
            col.push_back(output_matrix[j][i]);
        }
        original_segments.push_back(col);
    }

    // Sometimes, reconstructed block will have several zeroed-out rows
    // at the back. These are useless and should be removed.
    while(AllZeroes(original_segments.back())) {
        original_segments.pop_back();
    }

    // Likewise, the final non-zero row of the block may have some tailing
    // zeroes. Remove those as well. It would be akin to having several
    // null-terminating characters, one after the other.
    while(original_segments.back().back() == 0) {
        original_segments.back().pop_back();
    }

    Vector original;
    for(const auto &segment : original_segments) {
        original.insert(original.end(), segment.begin(), segment.end());
    }

    return original;
}

Vector IDA::Decode(const std::vector<DataFragment> &frags)
{
    Matrix encoded;
    Vector frag_indices;

    for(const auto &frag : frags) {
        encoded.push_back(frag.fragment_);
        frag_indices.push_back(frag.index_);
    }

    return Decode(encoded, frag_indices);
}

Matrix IDA::SplitToSegments(const Vector &v)
{
    Matrix segments;
    for(int i = 0; i < v.size(); i += m_) {
        Vector segment(m_, 0);
        int end_pos = (i + m_) > (v.size()) ? (v.size()) : (i + m_);
        // The end pos should either be the last element in the array or
        // m positions ahead of start_pos, whichever comes first.
        std::copy(v.begin()+i, v.begin()+end_pos,  segment.begin());
        segments.push_back(segment);
    }

    return segments;
}