#include "utils.hpp"
#include <iostream>
int main() {
    if(!file_exists("/data/td_priv.pem")) {
        generate_rsa_keypair(2048);
        std::cout << "[TD-Init] Keys Generated in /data\n";
    }
    return 0;
}
