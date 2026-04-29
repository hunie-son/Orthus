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

static bool read_pack_meta(size_t& stride, size_t& batchcap) {
    std::ifstream in("/data/pack_meta.txt");
    if (!in) return false;
    std::string k; long long v;
    while (in >> k >> v) { if (k == "stride") stride = (size_t)v; if (k == "batchcap") batchcap = (size_t)v; }
    return true;
}

std::vector<int> load_test_labels(const char* path) {
    std::vector<int> Y;
    std::ifstream in(path);
    int y; while(in >> y) Y.push_back(y);
    return Y;
}

static int count_lines(const char* path) {
    ifstream in(path); int n = 0; string s;
    while (getline(in, s)) if (!s.empty()) ++n;
    return n;
}

static bool file_exists(const char* p) { ifstream f(p); return f.good(); }

int main() {
    try {
        CryptoContext<DCRTPoly> cc;
        { ifstream f("/data/keys/cc.json", ios::binary); Serial::Deserialize(cc, f, SerType::JSON); }
        PrivateKey<DCRTPoly> sk;
        { ifstream f("/data/sk.bin", ios::binary); Serial::Deserialize(sk, f, SerType::BINARY); }
        
        cc->Enable(PKE); cc->Enable(KEYSWITCH); cc->Enable(LEVELEDSHE);

        std::vector<int> Y_true = load_test_labels("/data/test_labels_unified.txt");
        
        int C = 0;
        if (file_exists("/data/W1.csv")) C = count_lines("/data/W2.csv"); // MLP
        else C = count_lines("/data/W.csv"); // LR

        size_t STRIDE=1, BATCH_CAP=1;
        read_pack_meta(STRIDE, BATCH_CAP);

        std::ifstream in("/data/C2.bin", ios::binary);
        std::ofstream out("/data/preds.txt");

        size_t correct = 0, sample_idx = 0;
        out << "sample,true,pred\n";

        while (sample_idx < Y_true.size()) {
            if (in.peek() == EOF) break;

            Ciphertext<DCRTPoly> ct;
            // FIX: Removed 'if (!...)' check because Deserialize returns void
            Serial::Deserialize(ct, in, SerType::BINARY);
            
            if (in.fail()) break; // Check stream state instead

            Plaintext pt;
            cc->Decrypt(sk, ct, &pt);
            auto packed = pt->GetRealPackedValue();

            // Handle Batch
            size_t remaining = Y_true.size() - sample_idx;
            size_t b = std::min(BATCH_CAP, remaining);

            // Since Host2 writes one ciphertext per batch (containing C packed scores),
            // we just need to read the stride offsets.
            // (Assumes MLP Host2 loop output order)
            
            // NOTE: If Host2 writes separate ciphertexts for LR classes, this needs adjustment.
            // But for MLP with vector<Ciphertext> H_ct, the output logic usually combines them 
            // or writes them sequentially.
            // In the Host2 code provided, it writes 'acc' once per class 'c'.
            // So we actually need to read C ciphertexts per batch for MLP.
            
            vector<vector<double>> batch_scores(C);
            batch_scores[0] = packed; // Class 0 is what we just read

            // Read the remaining C-1 classes
            for(int c=1; c<C; ++c) {
                Ciphertext<DCRTPoly> ct_next;
                Serial::Deserialize(ct_next, in, SerType::BINARY);
                Plaintext pt_next;
                cc->Decrypt(sk, ct_next, &pt_next);
                batch_scores[c] = pt_next->GetRealPackedValue();
            }

            for (size_t i = 0; i < b; ++i) {
                vector<double> scores(C);
                size_t slot = i * STRIDE;
                for(int c=0; c<C; ++c) {
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

        double acc = (sample_idx > 0) ? 100.0 * correct / sample_idx : 0.0;
        cout << "Processed " << sample_idx << " samples. Accuracy = " << acc << "%\n";
        return 0;
    } catch (const std::exception& e) {
        cerr << "Decrypt Error: " << e.what() << endl;
        return 1;
    }
}
