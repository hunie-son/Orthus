#pragma once
#include <vector>
#include <string>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <sys/stat.h>

using ByteVec = std::vector<unsigned char>;

inline bool file_exists(const std::string& p) {
    struct stat st; return stat(p.c_str(), &st) == 0;
}

inline void generate_rsa_keypair(int bits) {
    RSA* rsa = RSA_new();
    BIGNUM* e = BN_new(); BN_set_word(e, RSA_F4);
    RSA_generate_key_ex(rsa, bits, e, nullptr);
    BN_free(e);
    BIO* bio = BIO_new_file("/data/td_priv.pem", "w");
    PEM_write_bio_RSAPrivateKey(bio, rsa, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(bio);
    bio = BIO_new_file("/data/td_pub.pem", "w");
    PEM_write_bio_RSA_PUBKEY(bio, rsa);
    BIO_free(bio);
    RSA_free(rsa);
}

inline ByteVec read_bin(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("read failed: " + path);
    return ByteVec(std::istreambuf_iterator<char>(in), {});
}
inline void write_bin(const std::string& path, const ByteVec& d) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("write failed: " + path);
    out.write(reinterpret_cast<const char*>(d.data()), d.size());
}

inline ByteVec rsa_unwrap(const ByteVec& C1) {
    BIO* bio = BIO_new_file("/data/td_priv.pem", "r");
    if (!bio) throw std::runtime_error("no priv key");
    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio,nullptr,nullptr,nullptr);
    BIO_free(bio);
    ByteVec keyiv(RSA_size(rsa));
    int len = RSA_private_decrypt(C1.size(), C1.data(), keyiv.data(), rsa, RSA_PKCS1_PADDING);
    if(len < 0) throw std::runtime_error("unwrap failed");
    keyiv.resize(len);
    return keyiv;
}

struct AES {
    ByteVec key, iv;
    AES() : key(32), iv(16) {}
    void generate_key_iv() { RAND_bytes(key.data(),32); RAND_bytes(iv.data(),16); }
    void set_key_iv(const ByteVec& kv) {
        if(kv.size()<48) throw std::runtime_error("kv size");
        std::copy(kv.begin(), kv.begin()+32, key.begin());
        std::copy(kv.begin()+32, kv.end(), iv.begin());
    }
    ByteVec get_key_iv() const {
        ByteVec kv = key; kv.insert(kv.end(), iv.begin(), iv.end()); return kv;
    }
    ByteVec encrypt(const std::string& pt) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
        ByteVec ct(pt.size()+32); int len, out;
        EVP_EncryptUpdate(ctx, ct.data(), &len, (unsigned char*)pt.data(), pt.size());
        EVP_EncryptFinal_ex(ctx, ct.data()+len, &out);
        ct.resize(len+out); EVP_CIPHER_CTX_free(ctx); return ct;
    }
    ByteVec decrypt(const ByteVec& ct) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
        ByteVec pt(ct.size()+32); int len, out;
        EVP_DecryptUpdate(ctx, pt.data(), &len, ct.data(), ct.size());
        EVP_DecryptFinal_ex(ctx, pt.data()+len, &out);
        pt.resize(len+out); EVP_CIPHER_CTX_free(ctx); return pt;
    }
};
