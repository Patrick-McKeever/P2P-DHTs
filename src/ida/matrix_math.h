#ifndef CHORD_AND_DHASH_MATRIX_MATH_H
#define CHORD_AND_DHASH_MATRIX_MATH_H

#include <vector>
#include <iostream>
#include <map>
#include <stdexcept>
#include <cmath>

using Vector = std::vector<int>;
using Matrix = std::vector<std::vector<int>>;

/**
 * Stylized prints for vectors/matrices.
 */
void Print(const Vector &v);
void Print(const Matrix &m);

/**
 * Python-style modulo operator. Same as C-style modulo for positives, but,
 * which negatives, will produce a positive value.
 *
 * @param lhs Dividend
 * @param rhs Modulus
 * @return (lhs % rhs + rhs) % rhs
 */
int Modulo(int lhs, int rhs);

/**
 * Inner product of a and b, with all ops mod prime.
 *
 * @param lhs Multiplicand
 * @param rhs Multiplier
 * @param prime Prime number used for modulo.
 * @return Sum of products of each entry in lhs and rhs modulo prime.
 */
int InnerProduct(const Vector &lhs, const Vector &rhs, int prime);

/**
 * Multiply two matrices, but every operation is done modulo some prime number.
 *
 * @param lhs Matrix 1.
 * @param rhs Matrix 2.
 * @param prime Prime which will be used for modulos.
 * @return (lhs * rhs) % prime. In python, np.matmul(lhs, rhs) % prime
 */
Matrix MatrixProduct(const Matrix &lhs, const Matrix &rhs, int prime);

/**
 * Transpose operation, in which m[i][j] becomes m[j][i] for every value i and
 * j in the given matrix.
 *
 * @param m Matrix to transpose.
 * @return Transposed version of m.
 */
Matrix Transpose(const Matrix &m);

/**
 * Return n^-1 mod p.
 *
 * @param n Integer.
 * @param p Prime number.
 * @return n^-1 mod p
 */
int ModInverse(int n, int p);

/**
 * Create the matrix used to encode vectors in the IDA.
 *
 * @param m Min num fragments needed to decode an encoded vector.
 * @param n Number of fragments produced by encoding a vector.
 * @param p Prime number.
 * @return Matrix where row i = { i^0, i^1, i^2...i^(m-1) }
 */
Matrix ConstructEncodingMatrix(int m, int n, int p);

/**
 * Elementary symmetrical transformation of a vector.
 * @param v Vector to transform.
 * @param m Minimum number of fragments needed to reproduce original vector
 *          after encoding.
 * @return List in which element i is the sum of products of i distinct elements
 *         of the given vector.
 * Note: Should this be done modulo p? Why is p parameter unused in original
 * repo?
 */
Vector ElementarySymmetricTransform(const Vector &v, int m);

/**
 * Compute the inverse of a vandermonde matrix, with all ops modulo p.
 *
 * @param basis A flattened vandermonde matrix.
 * @param p A prime number.
 * @return The inverse of the vandermonde matrix modulo p.
 */
Matrix VandermondeInverse(const Vector &basis, int p);

#endif