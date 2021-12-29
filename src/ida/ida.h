#ifndef CHORD_AND_DHASH_IDA_H
#define CHORD_AND_DHASH_IDA_H

#include "matrix_math.h"
#include "data_fragment.h"
#include <string>
#include <memory>
#include <fstream>
#include <type_traits>
#include <vector>
#include <json/json.h>
#include <numeric>

/**
 * Convert vector of characters into vector of ints, w/ each element as
 * corresponding char's ASCII code.
 * @param v Vector of chars.
 * @return Equivalent vector of ints.
 */
Vector CharsToInts(const std::vector<unsigned char> &v);

/**
 * Take a string, create a vector containing the ASCII codes of each char.
 * @param str String to turn into int vector.
 * @return Int vector.
 */
Vector StrToInts(const std::string &str);

/**
 * Does a vector consist exclusively of 0s?
 * @param v Vector of ints.
 * @return Is every single el in vector a 0?
 */
bool AllZeroes(const Vector &v);

/**
 * Read the contents of a file.
 * @param file_path Absolute path to file (maybe rework to allow relative).
 * @return Contents of the file as a char *.
 */
char *ReadFile(const char *file_path);

class IDA {
public:
    /**
     * Constructor to instantiate IDA which encodes data into n fragments,
     * requiring m to reconstruct the original datum, and uses prime p for
     * encoding purposes.
     * @param n Number of fragments to generate per datum.
     * @param m Necessary amount of fragments to reconstruct original datum.
     * @param p Prime used for encoding purposes.
     */
    IDA(int n, int m, int p);

    /**
     * Encode a vector of integers as a matrix, with each row of the matrix
     * being a fragment.
     * @param v Vector of integers to encode.
     * @return Encoded fragments, represented as matrix.
     */
    Matrix Encode(const Vector &v);

    /**
     * Encode a string as a matrix.
     * @param str String to encode.
     * @return A matrix representing fragments of the encoded str.
     */
    Matrix EncodePlaintext(const std::string &str);

    /**
     * Given a file path, encode the contents of the file and return the
     * resultant matrix.
     * @param file_path Absolute path to file to encode.
     * @return Encoded file.
     */
    Matrix EncodeFile(const char *file_path);

    /**
     * Take a file, encode it, store each resulting fragment in a file
     * given by the out_files vector.
     * @param in_file Path to file to encode.
     * @param out_files Paths to files in which to store fragments.
     */
    void EncodeToFiles(const char *in_file,
                       const std::vector<std::string> &out_files);

    /**
     * Given a matrix of encoded fragments and a corresponding vector of their
     * indices, decode the fragments into the original vector.
     * @param encoded Encoded fragments.
     * @param frag_indices The indices of those fragments, such that
     *                     frag_indices[n] gives the index of the encoded[n].
     * @return The decoded vector.
     */
    Vector Decode(const Matrix &encoded, const Vector &frag_indices);

    /**
     * Decode a vector of DataFragments.
     * @param frags A vector of DataFragments (i.e. objects w/ both index and
     *              vector.
     * @return Decoded vector.
     */
    Vector Decode(const std::vector<DataFragment> &frags);

private:
    /// Paramters of IDA; IDA will produce n fragments but require only n to
    /// decode any datum. It will use some prime number p for purposes of
    /// encoding.
    int n_, m_, p_;
    /// Matrix used for encoding purposes. Don't worry about it.
    Matrix encoding_matrix_;

    /**
     * Take a flat vector, split it into rows of length m. If the length of the
     * vector is not evenly divisible by 10, the remaining elements should be
     * padded with 0s.
     * @param v A vector of ints.
     * @return The vector split into a matrix with rows of length m.
     */
    Matrix SplitToSegments(const Vector &v);
};

#endif
