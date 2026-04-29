#pragma once
#include <openfhe.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lbcrypto;

// Sigmoid polynomial coeffs (degree 5 by default)
inline std::vector<double> poly_sigmoid() {
#ifndef SIGMOID_POLY_DEG
#define SIGMOID_POLY_DEG 7 // increased to 7 for MNIST but input needs to be tiny
#endif
#if SIGMOID_POLY_DEG == 3
    //return {0.5, 0.180601755, 0.0, -0.00309176627};
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

inline Ciphertext<DCRTPoly>
tree_sum_slots(const CryptoContext<DCRTPoly>& cc,
               Ciphertext<DCRTPoly> ct,
               std::size_t count) {
    if (count == 0) return ct;
    std::size_t step = 1;
    while (step < count) {
        auto rot = cc->EvalAtIndex(ct, static_cast<int>(step));
        ct = cc->EvalAdd(ct, rot);
        step <<= 1;
    }
    return ct;
}

inline std::vector<std::vector<double>> load_W1(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("load_W1: open failed: " + path);
    std::vector<std::vector<double>> W1;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line); 
	std::string tok; std::vector<double> row;
        
	while (std::getline(ss, tok, ',')) 
		if (!tok.empty()) row.push_back(std::stod(tok));
        if (!row.empty()) W1.push_back(std::move(row));
    }
    return W1;
}

inline std::vector<std::vector<double>> load_W2(const std::string& path) {
    std::ifstream in(path);
    if (!in) 
	    throw std::runtime_error("load_W2: open failed: " + path);
    
    std::vector<std::vector<double>> W2;
    std::string line;
    
    while (std::getline(in, line)) {
        std::istringstream ss(line); 
	std::string tok; 
	std::vector<double> row;
        
	while (std::getline(ss, tok, ',')) 
		if (!tok.empty()) row.push_back(std::stod(tok));
        if (!row.empty()) W2.push_back(std::move(row));
    }
    return W2;
}

