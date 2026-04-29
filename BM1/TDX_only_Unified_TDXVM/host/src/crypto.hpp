#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <iterator>

using ByteVec = std::vector<unsigned char>;

inline ByteVec read_bin(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("read_bin: failed to open " + path);
    return ByteVec(std::istreambuf_iterator<char>(in), {});
}

inline void write_bin(const std::string& path, const ByteVec& d) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("write_bin: failed to open " + path);
    out.write(reinterpret_cast<const char*>(d.data()), d.size());
}

inline std::string read_all_str(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("failed to open " + path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string fetch_pubkey_from_td();
ByteVec rsa_wrap(const std::string& pem, const ByteVec& pt);

struct AES {
    ByteVec key, iv;
    AES();
    void generate_key_iv();
    void set_key_iv(const ByteVec& kv);
    ByteVec encrypt(const std::string& pt);
    ByteVec decrypt(const ByteVec& ct);
    ByteVec get_key_iv() const;
};
