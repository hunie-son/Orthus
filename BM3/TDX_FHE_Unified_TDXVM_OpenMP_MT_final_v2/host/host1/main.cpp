#include <iostream>
#include <fstream>
#include <string>
#include <openfhe.h>
#include <openfhe/pke/cryptocontext-ser.h>
#include <openfhe/pke/key/key-ser.h>
#include <openfhe/pke/scheme/ckksrns/ckksrns-ser.h>
#include <algorithm>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <cstdlib>

#include "data.hpp"
#include "model.hpp"

using namespace lbcrypto;

void print_stats(const std::string& name, const std::vector<std::vector<double>>& W) {
    double min_val = 1e300, max_val = -1e300, avg_abs = 0.0;
    size_t count = 0;
    for (const auto& row : W) {
        for (double v : row) {
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
            avg_abs += std::abs(v);
            count++;
        }
    }
    if (count > 0) avg_abs /= count;
    std::cout << "[Debug] " << name
              << " -> Min: " << min_val
              << " Max: " << max_val
              << " AvgAbs: " << avg_abs << "\n";
}

static size_t nextPow2_u(size_t x) {
    size_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

static void save_keys(const CryptoContext<DCRTPoly>& cc,
                      const PublicKey<DCRTPoly>& pk,
                      const PrivateKey<DCRTPoly>& sk) {
    std::filesystem::create_directories("/data/keys");
    { std::ofstream f("/data/keys/cc.json", std::ios::binary); Serial::Serialize(cc, f, SerType::JSON); }
    { std::ofstream f("/data/keys/pk.bin", std::ios::binary); Serial::Serialize(pk, f, SerType::BINARY); }
    { std::ofstream f("/data/keys/sk.bin", std::ios::binary); Serial::Serialize(sk, f, SerType::BINARY); }
    { std::ofstream f("/data/keys/evm.bin", std::ios::binary); cc->SerializeEvalMultKey(f, SerType::BINARY); }
    { std::ofstream f("/data/keys/eva.bin", std::ios::binary); cc->SerializeEvalAutomorphismKey(f, SerType::BINARY); }
}

int main(int argc, char** argv) {
    std::string dataset = (argc > 1) ? argv[1] : "mnist";
    std::string model   = (argc > 2) ? argv[2] : "mlp";

    std::cout << "[Host] Phase 1: Training & Encryption (TDX Benchmark)...\n";
    std::cout << "[Host1] !!! DELETING OLD KEYS !!!\n";
    std::system("rm -rf /data/keys /data/*.bin /data/*.json /data/pack_meta.txt");

    std::cout << "[Data] Loading dataset: " << dataset << "...\n";
    DataSet train, test;
    load_unified_data(dataset, train, test);
    std::cout << "[Data] Ready. Train: " << train.X.size() << ", Test: " << test.X.size() << "\n";

    int H = 64, epochs = 50;
    double lr = 0.15;
    if (dataset == "iris") { H = 10; epochs = 200; lr = 0.1; }
    if (dataset == "wdbc") { H = 16; epochs = 200; lr = 0.1; }

    int input_dim_pack = 0;
    std::cout << "Training " << model << " (" << H << " hidden) on " << dataset << "...\n";

    if (model == "lr") {
        auto W = train_lr(train, epochs, lr);
        report_accuracy_lr(W, train, "Train");
        report_accuracy_lr(W, test,  "Test");

        double scale = 0.5;
        std::cout << "\n--- Weight Statistics ---\n";
        print_stats("W", W);
        std::cout << "[Info] Applying Scale Factor: " << scale << "\n";

        for (auto& row : W) for (auto& val : row) val *= scale;

        input_dim_pack = W[0].size();
        std::ofstream fW("/data/W.csv");
        for (int c = 0; c < (int)W.size(); ++c) {
            for (int d = 0; d < input_dim_pack; ++d) {
                fW << W[c][d] << (d + 1 < input_dim_pack ? "," : "");
            }
            fW << "\n";
        }
    }
    else {
        auto [W1, W2] = train_mlp(train, H, epochs, lr);
        report_accuracy_mlp(W1, W2, train, "Train");
        report_accuracy_mlp(W1, W2, test,  "Test");

        std::cout << "\n--- Weight Statistics (Native) ---\n";
        print_stats("W1", W1);
        print_stats("W2", W2);

        double scale = 1.0;
        std::cout << "[Info] Applying Scale Factor: " << scale << "\n";

        for (auto& row : W1) for (auto& val : row) val *= scale;
        for (auto& row : W2) for (auto& val : row) val *= scale;

        input_dim_pack = W1[0].size();
        {
            std::ofstream f1("/data/W1.csv");
            for (size_t j = 0; j < W1.size(); ++j) {
                for (int d = 0; d < input_dim_pack; ++d) {
                    f1 << W1[j][d] << (d + 1 < input_dim_pack ? "," : "");
                }
                f1 << "\n";
            }
        }
        {
            std::ofstream f2("/data/W2.csv");
            for (size_t c = 0; c < W2.size(); ++c) {
                for (int j = 0; j < (int)W2[0].size(); ++j) {
                    f2 << W2[c][j] << (j + 1 < (int)W2[0].size() ? "," : "");
                }
                f2 << "\n";
            }
        }
    }

    CCParams<CryptoContextCKKSRNS> params;
    params.SetSecurityLevel(HEStd_128_classic);
    params.SetRingDim(65536);
    params.SetScalingModSize(59);
    params.SetMultiplicativeDepth(10);
    params.SetScalingTechnique(FLEXIBLEAUTO);

    auto cc = GenCryptoContext(params);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);

    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    std::vector<int32_t> rots;
    for (int32_t s = 1; s <= 4096; s <<= 1) rots.push_back(s);
    cc->EvalRotateKeyGen(kp.secretKey, rots);

    save_keys(cc, kp.publicKey, kp.secretKey);

    const uint32_t slots = cc->GetRingDimension() / 2;
    const size_t maxLen  = (size_t)input_dim_pack;
    const size_t STRIDE  = 2 * nextPow2_u(maxLen);

    size_t BATCH_CAP = 100;
    if (const char* e = std::getenv("BATCH_CAP")) {
        try { BATCH_CAP = std::stoi(e); } catch (...) {}
    }

    size_t max_p = slots / STRIDE;
    if (BATCH_CAP > max_p) BATCH_CAP = max_p;

    size_t n = test.X.size();

    std::cout << "\n================ PACKING INFO ================\n";
    std::cout << " Dataset:       " << dataset << " (" << model << ")\n";
    std::cout << " Total Slots:   " << slots << "\n";
    std::cout << " Input Dim (D): " << (input_dim_pack - 1) << " (+1 Bias)\n";
    std::cout << " Stride Size:   " << STRIDE << " slots per sample\n";
    std::cout << " Packing Limit: " << BATCH_CAP << " samples per ciphertext\n";
    std::cout << " Test Samples:  " << n << "\n";
    std::cout << "==============================================\n\n";

    // === CHANGED ===
    // Save nsamples so BM3 Host2 can also handle the FINAL partial batch correctly.
    {
        std::ofstream meta("/data/pack_meta.txt");
        meta << "stride "   << STRIDE    << "\n";
        meta << "batchcap " << BATCH_CAP << "\n";
        meta << "nsamples " << n         << "\n";
    }

    std::ofstream ofs("/data/C1.bin", std::ios::binary);
    std::cout << "Host1 Setup Complete. Wrote /data files.\n";

    for (size_t base = 0; base < n; base += BATCH_CAP) {
        size_t b = std::min(BATCH_CAP, n - base);
        std::vector<double> packed(slots, 0.0);

        for (size_t s = 0; s < b; ++s) {
            size_t off = s * STRIDE;
            packed[off] = 1.0;

            const auto& x_raw = test.X[base + s];
            for (int i = 0; i < (int)x_raw.size(); ++i) {
                packed[off + 1 + i] = x_raw[i];
            }
        }

        auto pt = cc->MakeCKKSPackedPlaintext(packed);
        auto ct = cc->Encrypt(kp.publicKey, pt);
        Serial::Serialize(ct, ofs, SerType::BINARY);
    }

    return 0;
}
