#include "crypto.hpp"
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <iostream>
#include <sstream>
#include <cstring>

// AES Constructor
AES::AES() : key(32), iv(16) {}

void AES::generate_key_iv() {
    RAND_bytes(key.data(), 32);
    RAND_bytes(iv.data(), 16);
}

void AES::set_key_iv(const ByteVec& kv) {
    if (kv.size() < 48) throw std::runtime_error("set_key_iv: buffer too small");
    std::copy(kv.begin(), kv.begin()+32, key.begin());
    std::copy(kv.begin()+32, kv.begin()+48, iv.begin());
}

ByteVec AES::get_key_iv() const {
    ByteVec kv = key;
    kv.insert(kv.end(), iv.begin(), iv.end());
    return kv;
}

ByteVec AES::encrypt(const std::string& pt) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
    ByteVec ct(pt.size() + 32); // Allow padding space
    int len = 0, outl = 0;
    EVP_EncryptUpdate(ctx, ct.data(), &len, reinterpret_cast<const unsigned char*>(pt.data()), pt.size());
    EVP_EncryptFinal_ex(ctx, ct.data() + len, &outl);
    ct.resize(len + outl);
    EVP_CIPHER_CTX_free(ctx);
    return ct;
}

ByteVec AES::decrypt(const ByteVec& ct) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
    ByteVec pt(ct.size() + 32);
    int len = 0, outl = 0;
    EVP_DecryptUpdate(ctx, pt.data(), &len, ct.data(), ct.size());
    EVP_DecryptFinal_ex(ctx, pt.data() + len, &outl);
    pt.resize(len + outl);
    EVP_CIPHER_CTX_free(ctx);
    return pt;
}

ByteVec rsa_wrap(const std::string& pem, const ByteVec& pt) {
    BIO* bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
    RSA* rsa = PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!rsa) throw std::runtime_error("Invalid RSA public key");
    
    ByteVec ct(RSA_size(rsa));
    int outl = RSA_public_encrypt((int)pt.size(), pt.data(), ct.data(), rsa, RSA_PKCS1_PADDING);
    RSA_free(rsa);
    
    if (outl < 0) throw std::runtime_error("RSA encrypt failed");
    ct.resize(outl);
    return ct;
}

// Dummy fetch since we use shared volumes in this benchmark
std::string fetch_pubkey_from_td() {
    return ""; 
}
