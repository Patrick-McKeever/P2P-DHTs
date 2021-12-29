#include "matrix_math.h"

void Print(const Vector &v)
{
    std::cout << "[";
    for(const auto &cell : v) {
        std::cout << cell << ", ";
    }
    std::cout << "]";
}

void Print(const Matrix &m) {
    std::cout << "[";
    for(const auto &row : m) {
        Print(row);
        std::cout << ",";
    }
    std::cout << "]" << std::endl;
}

int Modulo(int lhs, int rhs)
{
    return (lhs % rhs + rhs) % rhs;
}

int InnerProduct(const Vector &lhs, const Vector &rhs, int prime)
{
    unsigned long min_len = lhs.size() <= rhs.size() ? lhs.size() : rhs.size();
    int sum = 0;
    for(int i = 0; i < min_len; ++i)
        sum += lhs[i] * rhs[i];
    return Modulo(sum, prime);
}

Matrix MatrixProduct(const Matrix &lhs, const Matrix &rhs, int prime)
{
    Matrix result;
    unsigned long lhs_rows = lhs.size(),
            lhs_cols = lhs[0].size(),
            rhs_cols = rhs[0].size();

    for(int i = 0; i < lhs_rows; ++i) {
        Vector row;
        for (int j = 0; j < rhs_cols; ++j) {
            int cell = 0;
            for (int k = 0; k < lhs_cols; ++k) {
                cell = Modulo(cell + lhs[i][k] * rhs[k][j], prime);
            }
            row.push_back(cell);
        }
        result.push_back(row);
    }

    return result;
}

Matrix Transpose(const Matrix &m)
{
    Matrix result(m.size(), Vector(m.size(), 0));
    for(int i = 0; i < m.size(); ++i)
        for(int j = 0; j < m.size(); ++j)
            result[i][j] = m[j][i];
    return result;
}

int ModInverse(int n, int p)
{
    int t = 0, new_t = 1;
    int r = p, new_r = n;

    while(new_r) {
        int quotient = r / new_r;
        int temp = t;
        t = new_t;
        new_t = temp - quotient * new_t;
        temp = r;
        r = new_r;
        new_r = temp - quotient * new_r;
    }

    if(r > 1)
        throw std::runtime_error("N is not invertible");
    if(t < 0)
        t += p;
    return t;
}

Matrix ConstructEncodingMatrix(int m, int n, int p)
{
    Matrix encoding_matrix;
    for(int a = 1; a <= n; ++a) {
        Vector row;
        int elt = 1;
        for(int i = 0; i < m; ++i) {
            row.push_back(elt);
            elt = Modulo(elt * a, p);
        }
        encoding_matrix.push_back(row);
    }
    return encoding_matrix;
}

Vector ElementarySymmetricTransform(const Vector &v, int m)
{
    Matrix el(m + 1, Vector(v.size() + 1, 0));
    for(int i = 1; i <= v.size(); ++i)
        el[1][i] = el[1][i-1] + v[i-1];
    for(int i = 2; i <= m; ++i)
        for(int j = i; j <= v.size(); ++j)
            el[i][j] = el[i-1][j-1] * v[j-1] + el[i][j-1];

    Vector result;
    for(int i = 0; i <= m; ++i)
        result.push_back(el[i].back());
    return result;
}

Matrix VandermondeInverse(const Vector &basis, int p)
{
    int m = basis.size();

    Vector el = ElementarySymmetricTransform(basis, m), denominators;

    for(int i = 0; i < m; ++i) {
        int prod = 1, elt = basis[i];
        for(int j = 0; j < m; ++j) {
            if(j != i) {
                prod = Modulo(prod * (elt - basis[j]), p);
            }
        }
        denominators.push_back(prod);
    }

    Matrix numerators;
    for(int i = 0; i < m; ++i) {
        Vector row = {1};
        int elt = basis[i], sign = -1;
        for(int j = 1; j < m; ++j) {
            int cell = Modulo(Modulo(row.back() * elt, p) + sign * el[j], p);
            row.push_back(cell);
            sign *= -1;
        }
        std::reverse(row.begin(), row.end());
        numerators.push_back(row);
    }

    std::map<int, int> inverses;
    Matrix result;
    for(int i = 0; i < m; ++i) {
        int denom = denominators[i], inv;
        bool denom_in_inverse = inverses.find(denom) != inverses.end();
        if(denom_in_inverse) {
            inv = inverses[denom];
        } else {
            inv = ModInverse(denom, p);
            inverses[denom] = inv; /* Causes segfault? */
        }

        Vector row;
        for(int num : numerators[i]) {
            row.push_back(Modulo(num * inv, p));
        }

        result.push_back(row);
    }

    return Transpose(result);
}
