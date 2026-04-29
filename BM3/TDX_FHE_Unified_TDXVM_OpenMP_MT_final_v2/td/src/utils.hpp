#pragma once

#include "openfhe/pke/openfhe.h"
#include "openfhe/pke/cryptocontext-ser.h"
#include "openfhe/pke/key/key-ser.h"

#include <fstream>
#include <thread>
#include <chrono>
#include <stdexcept>

using namespace lbcrypto;

// Block until a file exists
inline void wait_for_file(const std::string& path) {
    for (;;) {
        std::ifstream f(path, std::ios::binary);
        if (f.good()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

inline CryptoContext<DCRTPoly> LoadCC() {
    wait_for_file("/data/cc.json");
    std::ifstream in("/data/cc.json");
    if (!in) throw std::runtime_error("cc.json not found");
    CryptoContext<DCRTPoly> cc;
    if (!Serial::Deserialize(cc, in, SerType::JSON))
        throw std::runtime_error("cc.json deserialize failed");
    return cc;
}

inline PublicKey<DCRTPoly> LoadPK(const CryptoContext<DCRTPoly>& /*cc*/) {
    wait_for_file("/data/pk.bin");
    std::ifstream in("/data/pk.bin", std::ios::binary);
    if (!in) throw std::runtime_error("pk.bin not found");
    PublicKey<DCRTPoly> pk;
    if (!Serial::Deserialize(pk, in, SerType::BINARY))
        throw std::runtime_error("pk.bin deserialize failed");
    return pk;
}

inline PrivateKey<DCRTPoly> LoadSK(const CryptoContext<DCRTPoly>& /*cc*/) {
    wait_for_file("/data/sk.bin");
    std::ifstream in("/data/sk.bin", std::ios::binary);
    if (!in) throw std::runtime_error("sk.bin not found");
    PrivateKey<DCRTPoly> sk;
    if (!Serial::Deserialize(sk, in, SerType::BINARY))
        throw std::runtime_error("sk.bin deserialize failed");
    return sk;
}

