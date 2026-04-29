#include <openfhe.h>
#include <openfhe/pke/cryptocontext-ser.h>
#include <openfhe/pke/ciphertext-ser.h>
#include <openfhe/pke/key/key-ser.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

using namespace lbcrypto;
using namespace std;

static int count_lines(const char* path) {
    ifstream in(path); int n = 0; string s;
    while (getline(in, s)) if (!s.empty()) ++n;
    return n;
}

static bool file_exists(const char* p) { ifstream f(p); return f.good(); }

std::vector<int> load_test_labels(const char* path) {
    std::vector<int> Y;
    std::ifstream in(path);
    int y; while(in >> y) Y.push_back(y);
    return Y;
}

static bool read_pack_meta(size_t& stride, size_t& batchcap) {
    std::ifstream in("/data/pack_meta.txt");
    if (!in) return false;
    std::string k; long long v;
    while (in >> k >> v) { if (k == "stride") stride = (size_t)v; if (k == "batchcap") batchcap = (size_t)v; }
    return true;
}

int main() {
    CryptoContext<DCRTPoly> cc;
    // >>> FIX: Load from /data/keys/ <<<
    { ifstream f("/data/keys/cc.json"); Serial::Deserialize(cc, f, SerType::JSON); }
    
    PrivateKey<DCRTPoly> sk;
    // >>> FIX: Load from /data/keys/ <<<
    { ifstream f("/data/keys/sk.bin", ios::binary); Serial::Deserialize(sk, f, SerType::BINARY); }
    
    cc->Enable(PKE); cc->Enable(KEYSWITCH); cc->Enable(LEVELEDSHE);

    // ... (Rest of file remains unchanged) ...
    std::vector<int> Y_true = load_test_labels("/data/test_labels_unified.txt");
    if(Y_true.empty()) { cerr << "Error: No labels found.\n"; return 1; }

    int C = 0;
    if (file_exists("/data/W1.csv")) {
        C = count_lines("/data/W2.csv"); 
    } else {
        C = count_lines("/data/W.csv");  
    }

    size_t STRIDE=1, BATCH_CAP=1;
    read_pack_meta(STRIDE, BATCH_CAP);

    std::ifstream in("/data/C2.bin", ios::binary);
    std::ofstream out("/data/preds.txt");

    size_t correct = 0, sample_idx = 0;
    out << "sample,true,pred\n";

    while (sample_idx < Y_true.size()) {
        if (in.peek() == EOF) break;

        vector<vector<double>> batch_scores(C);
        for (int c = 0; c < C; ++c) {
            Ciphertext<DCRTPoly> ct;
            Serial::Deserialize(ct, in, SerType::BINARY);
            Plaintext pt;
            cc->Decrypt(sk, ct, &pt);
            batch_scores[c] = pt->GetRealPackedValue();
        }

        size_t remaining = Y_true.size() - sample_idx;
        size_t b = std::min(BATCH_CAP, remaining);

        for (size_t i = 0; i < b; ++i) {
            vector<double> scores(C);
            size_t slot = i * STRIDE;
            for (int c = 0; c < C; ++c) {
                if (slot < batch_scores[c].size()) scores[c] = batch_scores[c][slot];
                else scores[c] = -9999.0;
            }
            int pred = std::max_element(scores.begin(), scores.end()) - scores.begin();
            int truth = Y_true[sample_idx];
            if (pred == truth) correct++;
            out << sample_idx << "," << truth << "," << pred << "\n";
            sample_idx++;
        }
    }

    double acc = 100.0 * correct / sample_idx;
    cout << "Processed " << sample_idx << " samples. Accuracy = " << acc << "%\n";
    return 0;
}
