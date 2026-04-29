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
    { std::ofstream f("/data/sk.bin", std::ios::binary);      Serial::Serialize(sk, f, SerType::BINARY); }
    { std::ofstream f("/data/keys/evm.bin", std::ios::binary); cc->SerializeEvalMultKey(f, SerType::BINARY); }
    { std::ofstream f("/data/keys/eva.bin", std::ios::binary); cc->SerializeEvalAutomorphismKey(f, SerType::BINARY); }
}

int main(int argc, char** argv) {
    std::cout << "[Host] Phase 1: Training, KeyGen (Slow) & Encryption...\n";
    std::cout << "[Host1] !!! DELETING OLD KEYS TO PREVENT STALE CONFIG !!!\n";
    std::system("rm -rf /data/keys /data/*.bin /data/*.json /data/pack_meta.txt");

    std::string dataset = (argc > 1) ? argv[1] : "mnist";
    std::string model   = (argc > 2) ? argv[2] : "mlp";

    std::cout << "[Data] Loading dataset: " << dataset << "...\n";
    DataSet train, test;
    load_unified_data(dataset, train, test);
    std::cout << "[Data] Ready. Train: " << train.X.size() << ", Test: " << test.X.size() << "\n";

    std::cout << "[Host] Training " << model << " on " << dataset << "...\n";

    int H = 16, epochs = 100;
    double lr = 0.1;
    if (dataset == "mnist") { H = 64; epochs = 50;  lr = 0.15; }
    if (dataset == "wdbc")  { H = 16; epochs = 200; lr = 0.1;  }
    if (dataset == "iris")  { H = 10; epochs = 200; lr = 0.1;  }

    int input_dim_pack = 0;

    // =========================
    // Phase 1: Train and save weights
    // =========================
    if (model == "lr") {
        auto W = train_lr(train, epochs, lr);

        std::cout << "--------------------------------------------------\n";
        report_accuracy_lr(W, train, "Train");
        report_accuracy_lr(W, test,  "Test");
        std::cout << "--------------------------------------------------\n";

        double scale = 0.5;
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
    else { // MLP
        auto [W1, W2] = train_mlp(train, H, epochs, lr);

        std::cout << "--------------------------------------------------\n";
        report_accuracy_mlp(W1, W2, train, "Train");
        report_accuracy_mlp(W1, W2, test,  "Test");
        std::cout << "--------------------------------------------------\n";

        double scale = 1.0;
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
            int H1 = W2[0].size();
            for (size_t c = 0; c < W2.size(); ++c) {
                for (int j = 0; j < H1; ++j) {
                    f2 << W2[c][j] << (j + 1 < H1 ? "," : "");
                }
                f2 << "\n";
            }
        }
    }

    // =========================
    // FHE setup
    // =========================
    CCParams<CryptoContextCKKSRNS> params;
    params.SetSecretKeyDist(UNIFORM_TERNARY);
    params.SetSecurityLevel(HEStd_NotSet);
    params.SetRingDim(65536);
    params.SetScalingModSize(59);
    params.SetFirstModSize(60);

    std::vector<uint32_t> levelBudget = {8, 8};
    uint32_t approxBootstrapDepth = FHECKKSRNS::GetBootstrapDepth(levelBudget, UNIFORM_TERNARY);

    params.SetMultiplicativeDepth(approxBootstrapDepth + 10);
    params.SetScalingTechnique(FLEXIBLEAUTO);

    std::cout << "[Host1] Generating Context...\n";
    std::cout << "   - Ring: 65536\n";
    std::cout << "   - ModSize: 59\n";
    std::cout << "   - Level Budget: {8, 8}\n";
    std::cout << "   - Total Depth: " << params.GetMultiplicativeDepth() << "\n";

    auto cc = GenCryptoContext(params);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    cc->Enable(FHE);

    std::cout << "[Host1] Generating Keys...\n";
    auto kp = cc->KeyGen();
    cc->EvalMultKeyGen(kp.secretKey);

    std::cout << "[Host1] Generating Rotation Keys (1..4096)...\n";
    std::vector<int32_t> rots;
    for (int32_t s = 1; s <= 4096; s <<= 1) rots.push_back(s);
    cc->EvalRotateKeyGen(kp.secretKey, rots);

    std::cout << "[Host1] Generating Bootstrapping Keys (This is slow)...\n";
    cc->EvalBootstrapSetup(levelBudget);
    cc->EvalBootstrapKeyGen(kp.secretKey, 65536 / 2);

    std::cout << "[Host1] Serializing Keys...\n";
    save_keys(cc, kp.publicKey, kp.secretKey);

    // =========================
    // Packing setup
    // =========================
    const uint32_t slots = 65536 / 2;
    const size_t maxLen  = (size_t)input_dim_pack;
    const size_t STRIDE  = 2 * nextPow2_u(maxLen);

    // === CHANGED ===
    // Use the SAME batch policy as BM3 so BM2 and BM3 compare fairly.
    size_t BATCH_CAP = 100;
    if (const char* e = std::getenv("BATCH_CAP")) {
        try { BATCH_CAP = std::stoul(e); } catch (...) {}
    }

    size_t max_p = slots / STRIDE;
    if (BATCH_CAP > max_p) BATCH_CAP = max_p;

    // === CHANGED ===
    // Process the FULL encrypted test set, not only 4 samples.
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
    // Save nsamples so Host2 can correctly handle the FINAL partial batch.
    {
        std::ofstream meta("/data/pack_meta.txt");
        meta << "stride "   << STRIDE   << "\n";
        meta << "batchcap " << BATCH_CAP << "\n";
        meta << "nsamples " << n        << "\n";
    }

    std::ofstream ofs("/data/C1.bin", std::ios::binary);

    std::cout << "[Host1] Encrypting " << n << " samples...\n";

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

    std::cout << "[Host] Encrypted inputs sent to /data/C1.bin\n";
    return 0;
}
