// TD refresh (OpenFHE): decrypt -> re-encode -> re-encrypt using keys in /data
#include <openfhe.h>
#include <openfhe/pke/cryptocontext-ser.h>
#include <openfhe/pke/ciphertext-ser.h>
#include <openfhe/pke/key/key-ser.h>
#include <openfhe/pke/scheme/ckksrns/ckksrns-ser.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lbcrypto;
using namespace std;

static bool fileExists(const char* p) { ifstream f(p, ios::binary); return (bool)f; }

int main(int argc, char** argv) {
    const char* inPath  = (argc >= 2) ? argv[1] : "/data/C_boot_req.bin";
    const char* outPath = (argc >= 3) ? argv[2] : "/data/C_boot_resp.bin";
    const char* ccPath  = "/data/cc.json";
    const char* pkPath  = "/data/pk.bin";
    const char* skPath  = "/data/sk.bin";

    if (!fileExists(inPath)) { 
	    cerr << "TD: missing input " << inPath << "\n"; return 1; 
    }
    
    if (!fileExists(ccPath) || !fileExists(pkPath) || !fileExists(skPath)) {
        cerr << "TD: missing cc.json/pk.bin/sk.bin\n"; return 1;
    }

    try {
        // Load context and keys 
        CryptoContext<DCRTPoly> cc; { 
		ifstream f(ccPath); 
		Serial::Deserialize(cc, f, SerType::JSON); 
	}
        
	PublicKey<DCRTPoly> pk;     
	{ 
		ifstream f(pkPath, ios::binary); 
		Serial::Deserialize(pk, f, SerType::BINARY); 
	}
        
	PrivateKey<DCRTPoly> sk;    
	{ 
		ifstream f(skPath, ios::binary); 
		Serial::Deserialize(sk, f, SerType::BINARY); 
	}

        cc->Enable(PKE);
        cc->Enable(KEYSWITCH);
        cc->Enable(LEVELEDSHE);

        // Load incoming ciphertext 
        Ciphertext<DCRTPoly> cin;   
	{ 
		ifstream fi(inPath, ios::binary); 
		Serial::Deserialize(cin, fi, SerType::BINARY); 
	}

        // Decrypt to plaintext 
        Plaintext pt;
        cc->Decrypt(sk, cin, &pt);

        // IMPORTANT: Re-encode from raw values to a *fresh* plaintext at top level.
        // Using the decrypted Plaintext object directly for Encrypt can trigger a tower/level mismatch.
        std::vector<std::complex<double>> valsC = pt->GetCKKSPackedValue();
        std::vector<double> vals(valsC.size());
        
	for (size_t i = 0; i < valsC.size(); ++i) 
		vals[i] = valsC[i].real();

        Plaintext pFresh = cc->MakeCKKSPackedPlaintext(vals);

        // Re-encrypt with public key 
        auto cout_ct = cc->Encrypt(pk, pFresh);

        // Save refreshed ciphertext 
        ofstream fo(outPath, ios::binary);
        Serial::Serialize(cout_ct, fo, SerType::BINARY);
        return 0;

    } catch (const std::exception& e) {
        cerr << "TD exception: " << e.what() << "\n"; return 1;

    } catch (...) {
        cerr << "TD exception: unknown\n"; return 1;
    }
}

