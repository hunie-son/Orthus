#include <iostream>
#include "utils.hpp"

int main() {
    // generate keys
    if (!file_exists("td_priv.pem")) {
        generate_rsa_keypair(2048);
        std::cout<<"td: keypair generated"<<std::endl;
        return 0;
    }
    
    try {
        // Load incoming ciphertexts
        //auto C1 = read_bin("C1.bin");
        //auto C2 = read_bin("C2.bin");

	auto C1 = read_bin("/data/C1.bin");
	auto C2 = read_bin("/data/C2.bin");

        // Unwrap AES key/iv
        AES aes;
        auto keyiv = rsa_unwrap(C1);
        aes.set_key_iv(keyiv);

        // Decrypt payload, run inference, re-encrypt
        auto raw   = aes.decrypt(C2);
        auto result_csv = infer_csv(raw);
        auto Cres  = aes.encrypt(result_csv);
        //write_bin("Cres.bin", Cres);
        write_bin("/data/Cres.bin", Cres);
	
	std::cout<<"td: wrote Cres.bin"<<std::endl;
    } catch (const std::exception& e) {
        std::cerr<<"td error: "<<e.what()<<std::endl;
        return 1;
    }
    return 0;
}
