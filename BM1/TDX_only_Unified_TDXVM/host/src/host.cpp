#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <sys/stat.h>

#include "crypto.hpp"
#include "data.hpp"
#include "model.hpp"

using namespace std;

// Helper: check file existence
inline bool file_exists(const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

// Helper to save matrix to csv
void save_matrix(const string& path, const vector<vector<double>>& W) {
    ofstream out(path);
    for(const auto& row : W) {
        for(size_t i=0; i<row.size(); ++i) {
            out << row[i] << (i+1 < row.size() ? "," : "");
        }
        out << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        cerr << "Usage: ./host <dataset> <model> <mode: encrypt|decrypt>\n";
        return 1;
    }
    string dataset = argv[1];
    string model   = argv[2];
    string mode    = argv[3];

    // --- ENCRYPT PHASE ---
    if (mode == "encrypt") {
        DataSet train, test;
        load_unified_data(dataset, train, test);

        int H = 16, epochs = 100; double lr = 0.1;
        if(dataset == "mnist") { H = 64; epochs = 50;  lr = 0.15; }
        if(dataset == "wdbc")  { H = 16; epochs = 200; lr = 0.05; }
        if(dataset == "iris")  { H = 10; epochs = 200; lr = 0.1;  }

        cout << "[Host] Training " << model << " on " << dataset << "...\n";
        
        int is_mlp = (model == "mlp") ? 1 : 0;

        if (model == "lr") {
            auto W = train_lr(train, epochs, lr);
            report_accuracy_lr(W, train, "Train");
            report_accuracy_lr(W, test, "Test");
            save_matrix("/data/W1.csv", W); // Use W1 slot for LR weights
        } else {
            auto [W1, W2] = train_mlp(train, H, epochs, lr);
            report_accuracy_mlp(W1, W2, train, "Train");
            report_accuracy_mlp(W1, W2, test, "Test");
            save_matrix("/data/W1.csv", W1);
            save_matrix("/data/W2.csv", W2);
        }

        // Save Config for TD
        {
            ofstream conf("/data/config.txt");
            conf << is_mlp << "\n"; // 0=LR, 1=MLP
        }

        // Encryption
        string pub_pem = read_all_str("/data/td_pub.pem");
        AES aes; aes.generate_key_iv();
        
        // Wrap AES Key with RSA
        auto C1 = rsa_wrap(pub_pem, aes.get_key_iv());
        write_bin("/data/C1.bin", C1);

        // Encrypt Test Data (CSV string) -> AES
        stringstream ss;
        for(size_t i=0; i<test.X.size(); ++i) {
            for(size_t j=0; j<test.X[i].size(); ++j) {
                ss << test.X[i][j] << ",";
            }
            ss << test.Y[i] << "\n";
        }
        auto C2 = aes.encrypt(ss.str());
        write_bin("/data/C2.bin", C2);

        // Save AES key locally for decryption later
        write_bin("/tmp/host_aes.key", aes.get_key_iv());
        
        cout << "[Host] Encrypted inputs sent to /data/C1.bin and /data/C2.bin\n";
    }

    // --- DECRYPT PHASE ---
    else if (mode == "decrypt") {
        if (!file_exists("/data/Cres.bin")) {
            cerr << "[Host] Error: No result file found!\n";
            return 1;
        }

        // Restore AES Key
        auto keyiv = read_bin("/tmp/host_aes.key");
        AES aes; aes.set_key_iv(keyiv);

        // Decrypt
        auto cres = read_bin("/data/Cres.bin");
        auto plain = aes.decrypt(cres);
        string result_str(plain.begin(), plain.end());

        cout << "\n>>> BENCHMARK 1 FINAL RESULTS <<<\n";
        
        // Parse CSV to get Accuracy
        stringstream ss(result_str);
        string line;
        while(getline(ss, line)) {
             if (line.find("Accuracy:") != string::npos) cout << line << endl;
             if (line.find("Avg Latency:") != string::npos) cout << line << endl;
        }
        // cout << result_str << endl; // Uncomment to see full CSV
    }

    return 0;
}
