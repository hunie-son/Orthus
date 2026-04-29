#pragma once
#include <openfhe.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lbcrypto;

inline std::vector<double> poly_sigmoid() {
#define SIGMOID_POLY_DEG 3
#if SIGMOID_POLY_DEG == 3
    return {0.5, 0.15012, 0.0, -0.00159305};
#elif SIGMOID_POLY_DEG == 5
    return {0.5, 0.217105019, 0.0, -0.00782319775, 0.0, 0.000118273962};
#elif SIGMOID_POLY_DEG == 7
    return {0.5, 0.235173404, 0.0, -0.0123398426, 0.0, 0.000394263559, 0.0, -0.00000474537849};
#else
# error "SIGMOID_POLY_DEG must be 3, 5, or 7"
#endif
}

inline Ciphertext<DCRTPoly>
eval_poly_on_ct(const CryptoContext<DCRTPoly>& cc,
                const Ciphertext<DCRTPoly>& x,
                const std::vector<double>& a) {
    auto zero = cc->EvalMult(x, 0.0);
    Ciphertext<DCRTPoly> x2 = cc->EvalMult(x, x);
    Ciphertext<DCRTPoly> x3 = cc->EvalMult(x2, x);
    Ciphertext<DCRTPoly> x5, x7;

    if (a.size() > 5 && std::abs(a[5]) > 0.0) x5 = cc->EvalMult(x3, x2);
    if (a.size() > 7 && std::abs(a[7]) > 0.0) { if (!x5) x5 = cc->EvalMult(x3, x2); x7 = cc->EvalMult(x5, x2); }

    auto acc = zero;
    if (a.size() > 0 && std::abs(a[0]) > 0.0)
            acc = cc->EvalAdd(acc, a[0]);
    if (a.size() > 1 && std::abs(a[1]) > 0.0)
            acc = cc->EvalAdd(acc, cc->EvalMult(x,  a[1]));
    if (a.size() > 3 && std::abs(a[3]) > 0.0)
            acc = cc->EvalAdd(acc, cc->EvalMult(x3, a[3]));
    if (a.size() > 5 && std::abs(a[5]) > 0.0)
            acc = cc->EvalAdd(acc, cc->EvalMult(x5, a[5]));
    if (a.size() > 7 && std::abs(a[7]) > 0.0)
            acc = cc->EvalAdd(acc, cc->EvalMult(x7, a[7]));
    return acc;
}
