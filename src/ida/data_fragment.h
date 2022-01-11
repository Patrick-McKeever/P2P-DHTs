#ifndef CHORD_AND_DHASH_DATA_FRAGMENT_H
#define CHORD_AND_DHASH_DATA_FRAGMENT_H

#include <string>
#include <json/json.h>
#include <fstream>
#include "matrix_math.h"

/**
 * The IDA will produce a 2D matrix of ints. This matrix can be reconstructed
 * in full from only a handful of the rows in that matrix, so long as the index
 * of those rows in the matrix are known.
 * This data structure exists to represent a single row. It should:
 *      - Hold the vector of doubles corresponding to a single row.
 *      - Hold the index of said row.
 *      - Be able to be serialized into a string.
 */
class DataFragment {
public:
    /**
     * Default constructor.
     */
    DataFragment() = default;

    /**
     * Construct from vector of doubles and index.
     *
     * @param vector One row of matrix from IDA::Encode.
     * @param index Index of row in said matrix.
     */
    DataFragment(Vector vector, int index, int n = 14, int m = 10, int p = 257);

    /**
     * Constructor 2. Construct a data fragment from a JSON-encoded fragment.
     * @param json_frag Json value specifying m, n, p, index of fragment, and
     *                  the fragment's vector of integer values.
     */
    explicit DataFragment(const Json::Value &json_frag);

    explicit DataFragment(const std::string &encoded_frag);

    /**
     * Write a JSON-encoded version of the fragment to the given file.
     *
     * @param file_path The file to which the fragment will be written.
     * @return True on success, false on failure.
     */
    bool WriteToFile(const char *file_path) const;

    /**
     * Produce a JSON-encoded version of the fragment.
     * @return JSON-encoded version of fragment specifying n, m, p, index, and
     *         the vector of integers associated with it.
     */
    [[nodiscard]] Json::Value ToJson() const;

    /**
     * Convert to a vector of doubles (i.e. return fragment_).
     * @return fragment_
     */
    explicit operator Vector() const;

    /**
     * Cast to JSON
     * @return JSON version of fragment.
     */
    explicit operator Json::Value() const;

    /**
     * Return human-readable string representing fragment. Useful for debugging.
     * @return string of form "[INDEX]:[VAL_1] [VAL_2]...[VAL_N]\n"
     */
    explicit operator std::string() const;

    /**
     * Comparison operator.
     *
     * @param df1 Lefthand.
     * @param df2 Righthand.
     * @return Equality of left and right hands.
     */
    friend bool operator == (const DataFragment &df1, const DataFragment &df2);

    /**
     * Less than operator (for implementation of sets/maps of DataFragments).
     * @param df1 Lefthand.
     * @param df2 Righthand.
     * @return Is df1's index less than df2's?
     *         (Note: this is useless for practical comparison purposes,
     *          but it allows for insertion into sets and maps.)
     */
    friend bool operator < (const DataFragment &df1, const DataFragment &df2);

    /// Index of the fragment. (e.g. IDA produced 14 fragments, this is nth).
    int index_, m_, n_, p_;

    /// A vector of doubles representing a row from a matrix given by
    /// IDA::Encode.
    Vector fragment_;
};

using StringArr = std::vector<std::string>;

static const char BASE_64_ALPHABET[65] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const std::map<char, int> BASE64_CHAR_TO_INT {
    { 'A', 0 },  { 'B', 1 },  { 'C', 2 },  { 'D', 3 },  { 'E', 4 },  { 'F', 5 },
    { 'G', 6 },  { 'H', 7 },  { 'I', 8 },  { 'J', 9 },  { 'K', 10 }, { 'L', 11 },
    { 'M', 12 }, { 'N', 13 }, { 'O', 14 }, { 'P', 15 }, { 'Q', 16 }, { 'R', 17 },
    { 'S', 18 }, { 'T', 19 }, { 'U', 20 }, { 'V', 21 }, { 'W', 22 }, { 'X', 23 },
    { 'Y', 24 }, { 'Z', 25 }, { 'a', 26 }, { 'b', 27 }, { 'c', 28 }, { 'd', 29 },
    { 'e', 30 }, { 'f', 31 }, { 'g', 32 }, { 'h', 33 }, { 'i', 34 }, { 'j', 35 },
    { 'k', 36 }, { 'l', 37 }, { 'm', 38 }, { 'n', 39 }, { 'o', 40 }, { 'p', 41 },
    { 'q', 42 }, { 'r', 43 }, { 's', 44 }, { 't', 45 }, { 'u', 46 }, { 'v', 47 },
    { 'w', 48 }, { 'x', 49 }, { 'y', 50 }, { 'z', 51 }, { '0', 52 }, { '1', 53 },
    { '2', 54 }, { '3', 55 }, { '4', 56 }, { '5', 57 }, { '6', 58 }, { '7', 59 },
    { '8', 60 }, { '9', 61 }, { '+', 62 }, { '/', 63 }
};

std::string SerializeToBase64(const Vector &frag, int num_digits = 2);
Vector ParseFromBase64(const std::string &serialized_frag,
                               int num_digits = 2);

/**
 * Create a string from a vector of ints, with each character of the string
 * being a char with an ASCII value equivalent to the corresponding element
 * in the int array.
 * @param frag Array of ints.
 * @return String of corresponding chars.
 */
std::string SerializeToBytes(const Vector &frag);

/**
 * Given a string, produce a vector in which each element gives the ASCII code
 * of the corresponding character in the string.
 * @param serialized_frag A serialized data fragment.
 * @return The parsed data fragment.
 */
Vector ParseFromBytes(const std::string &serialized_frag);

/**
 * Split a string into a vector of substrings at every occurrence of a delimiter.
 * @param s The string to be split.
 * @param delimiter The delimiter which, when encountered in the string, will
 *                  cause the string to be split at that point.
 * @return A vector of substrings taken from the original string at every
 *         instance of the supplied delimiter.
 */
StringArr Split(const std::string &s, const std::string &delimiter);

/**
 * Given a matrix of length n generated by an IDA with parameters n, m, and p,
 * return a vector of data fragments which store the vectors and indices of each
 * individual row.
 * @param matrix Matrix generated by IDA encoding.
 * @return Vector of DataFragments, one for each row of given matrix.
 */
std::vector<DataFragment> FragsFromMatrix(const Matrix &matrix);

/**
 * Construct a DataFragment from a file where it has been stored.
 * @param file_path A file containing a JSON-encoded version of a data fragment.
 * @return A DataFragment constructed from the info in the file.
 */
DataFragment FragFromFile(const char *file_path);

#endif