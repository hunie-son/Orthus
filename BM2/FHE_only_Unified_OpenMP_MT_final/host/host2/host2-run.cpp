#include <openfhe.h>
#include <openfhe/pke/cryptocontext-ser.h>
#include <openfhe/pke/ciphertext-ser.h>
#include <openfhe/pke/key/key-ser.h>
#include <fstream>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <omp.h>
#include "utils.hpp"

using namespace lbcrypto;
using std::cout;
using std::endl;

// --- Helpers ---
static CryptoContext<DCRTPoly> load_cc(const std::string& path = "/data/keys/cc.json") {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("load_cc failed");
    CryptoContext<DCRTPoly> cc;
    Serial::Deserialize(cc, f, SerType::JSON);
    return cc;
}

static PublicKey<DCRTPoly> load_pk(const std::string& path = "/data/keys/pk.bin") {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("load_pk failed");
    PublicKey<DCRTPoly> pk;
    Serial::Deserialize(pk, f, SerType::BINARY);
    return pk;
}

static size_t nextPow2_u(size_t x) {
    size_t p = 1;
    while (p < x) p <<= 1;
    return p;
}

static Plaintext make_mask_block_start(const CryptoContext<DCRTPoly>& cc,
                                       size_t slots, size_t stride) {
    std::vector<double> m(slots, 0.0);
    for (size_t i = 0; i < slots; i += stride) m[i] = 1.0;
    return cc->MakeCKKSPackedPlaintext(m);
}

static Plaintext make_mask_first_k_in_block(const CryptoContext<DCRTPoly>& cc,
                                            size_t slots, size_t stride, size_t k) {
    std::vector<double> m(slots, 0.0);
    for (size_t base = 0; base < slots; base += stride) {
        for (size_t i = 0; i < k; ++i) m[base + i] = 1.0;
    }
    return cc->MakeCKKSPackedPlaintext(m);
}

static Ciphertext<DCRTPoly> sum_first_k_in_blocks(const CryptoContext<DCRTPoly>& cc,
                                                  const Ciphertext<DCRTPoly>& ct,
                                                  size_t k,
                                                  size_t stride,
                                                  const Plaintext& maskFirstK,
                                                  const Plaintext& maskStart) {
    auto x = cc->EvalMult(ct, maskFirstK);
    size_t p2 = nextPow2_u(k);
    for (size_t shift = 1; shift < p2; shift <<= 1) {
        auto r = cc->EvalAtIndex(x, (int32_t)shift);
        x = cc->EvalAdd(x, r);
    }
    x = cc->EvalMult(x, maskStart);
    return x;
}

// === CHANGED ===
// Read stride, batchcap, and nsamples so Host2 can compute the REAL last batch size.
static bool read_pack_meta(size_t& stride, size_t& batchcap, size_t& nsamples) {
    std::ifstream in("/data/pack_meta.txt");
    if (!in) return false;

    std::string k;
    long long v;
    while (in >> k >> v) {
        if (k == "stride") stride = (size_t)v;
        else if (k == "batchcap") batchcap = (size_t)v;
        else if (k == "nsamples") nsamples = (size_t)v;
    }
    return true;
}

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

inline std::vector<std::vector<double>> load_matrix(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("load_matrix failed " + path);

    std::vector<std::vector<double>> W;
    std::string line;

    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string tok;
        std::vector<double> row;

        while (std::getline(ss, tok, ',')) {
            if (!tok.empty()) row.push_back(std::stod(tok));
        }
        if (!row.empty()) W.push_back(std::move(row));
    }
    return W;
}

int main() {
    try {
        cout << "[Host] Phase 2: Host2 Inference (Pure FHE)...\n";

        auto cc = load_cc();
        auto pk = load_pk();

        { std::ifstream f("/data/keys/evm.bin", std::ios::binary); cc->DeserializeEvalMultKey(f, SerType::BINARY); }
        { std::ifstream f("/data/keys/eva.bin", std::ios::binary); cc->DeserializeEvalAutomorphismKey(f, SerType::BINARY); }

        std::vector<uint32_t> levelBudget = {8, 8};
        cc->EvalBootstrapSetup(levelBudget);

        bool is_mlp = file_exists("/data/W1.csv");
        std::vector<std::vector<double>> W_LR, W1, W2;
        int D1 = 0;

        if (is_mlp) {
            W1 = load_matrix("/data/W1.csv");
            W2 = load_matrix("/data/W2.csv");
            D1 = W1[0].size();
            cout << "[Host2] Model Loaded. Type: MLP (H=" << W1.size() << ")\n";
        } else {
            W_LR = load_matrix("/data/W.csv");
            D1 = W_LR[0].size();
            cout << "[Host2] Model Loaded. Type: LR\n";
        }

        const uint32_t slots = cc->GetRingDimension() / 2;

        // === CHANGED ===
        size_t STRIDE = 0, BATCH_CAP = 0, N_SAMPLES = 0;
        if (!read_pack_meta(STRIDE, BATCH_CAP, N_SAMPLES)) {
            throw std::runtime_error("Failed to read /data/pack_meta.txt");
        }
        if (BATCH_CAP == 0) BATCH_CAP = 1;
        if (N_SAMPLES == 0) throw std::runtime_error("Invalid nsamples in /data/pack_meta.txt");

        Plaintext maskStart = make_mask_block_start(cc, slots, STRIDE);
        Plaintext maskD1    = make_mask_first_k_in_block(cc, slots, STRIDE, (size_t)D1);
        auto coeffs = poly_sigmoid();

        std::ifstream ifs_ct("/data/C1.bin", std::ios::binary);
        std::ofstream ofs_out("/data/C2.bin", std::ios::binary | std::ios::trunc);

        using clock = std::chrono::high_resolution_clock;

        size_t global_sample_idx = 0;
        double total_ms = 0.0;
        size_t total_samples_processed = 0;

        int max_threads = omp_get_max_threads();
        omp_set_num_threads(max_threads);
        cout << "[Host2] Running Real FHE with " << max_threads << " threads (Parallel Mode).\n";

        while (true) {
            if (ifs_ct.peek() == EOF) break;
            if (global_sample_idx >= N_SAMPLES) break;

            auto b0 = clock::now();

            Ciphertext<DCRTPoly> c_x;
            Serial::Deserialize(c_x, ifs_ct, SerType::BINARY);

            double batch_bootstrap_ms = 0.0;

            if (is_mlp) {
                std::vector<Ciphertext<DCRTPoly>> H_ct(W1.size());

                #pragma omp parallel for reduction(+:batch_bootstrap_ms)
                for (size_t j = 0; j < W1.size(); ++j) {
                    try {
                        std::vector<double> wpack(slots, 0.0);
                        for (size_t base = 0; base < slots; base += STRIDE) {
                            for (int d = 0; d < D1; ++d) {
                                wpack[base + d] = W1[j][d];
                            }
                        }

                        auto c_mul = cc->EvalMult(c_x, cc->MakeCKKSPackedPlaintext(wpack));
                        auto c_dot = sum_first_k_in_blocks(cc, c_mul, D1, STRIDE, maskD1, maskStart);

                        auto t1 = clock::now();
                        c_dot = cc->EvalBootstrap(c_dot);
                        auto t2 = clock::now();
                        batch_bootstrap_ms += std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

                        auto c_act = eval_poly_on_ct(cc, c_dot, coeffs);
                        c_act = cc->EvalMult(c_act, maskStart);
                        H_ct[j] = c_act;
                    } catch (const std::exception& e) {
                        #pragma omp critical
                        std::cerr << "Thread Error: " << e.what() << endl;
                    }
                }

                for (size_t c = 0; c < W2.size(); ++c) {
                    auto acc = cc->EvalMult(H_ct[0], 0.0);
                    if (W2[c][0] != 0.0) acc = cc->EvalAdd(acc, W2[c][0]);

                    for (size_t j = 1; j < W2[0].size(); ++j) {
                        if (W2[c][j] == 0.0) continue;
                        acc = cc->EvalAdd(acc, cc->EvalMult(H_ct[j - 1], W2[c][j]));
                    }

                    acc = cc->EvalMult(acc, maskStart);
                    Serial::Serialize(acc, ofs_out, SerType::BINARY);
                }
            }
            else {
                std::vector<Ciphertext<DCRTPoly>> LR_ct(W_LR.size());

                #pragma omp parallel for reduction(+:batch_bootstrap_ms)
                for (size_t c = 0; c < W_LR.size(); ++c) {
                    try {
                        std::vector<double> wpack(slots, 0.0);
                        for (size_t base = 0; base < slots; base += STRIDE) {
                            for (int d = 0; d < D1; ++d) {
                                wpack[base + d] = W_LR[c][d];
                            }
                        }

                        auto c_mul = cc->EvalMult(c_x, cc->MakeCKKSPackedPlaintext(wpack));
                        auto c_logit = sum_first_k_in_blocks(cc, c_mul, D1, STRIDE, maskD1, maskStart);

                        auto t1 = clock::now();
                        c_logit = cc->EvalBootstrap(c_logit);
                        auto t2 = clock::now();
                        batch_bootstrap_ms += std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

                        c_logit = cc->EvalMult(c_logit, maskStart);
                        LR_ct[c] = c_logit;
                    } catch (const std::exception& e) {
                        #pragma omp critical
                        std::cerr << "Thread Error: " << e.what() << endl;
                    }
                }

                for (size_t c = 0; c < W_LR.size(); ++c) {
                    Serial::Serialize(LR_ct[c], ofs_out, SerType::BINARY);
                }
            }

            auto b1 = clock::now();
            double dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(b1 - b0).count();
            total_ms += dur_ms;

            // === CHANGED ===
            // Use the REAL number of samples in this batch, especially for the FINAL partial batch.
            size_t remaining = N_SAMPLES - global_sample_idx;
            size_t b = std::min(BATCH_CAP, remaining);

            double time_per_sample = dur_ms / (double)b;
            for (size_t i = 0; i < b; ++i) {
                cout << "[Host2] sample #" << (global_sample_idx + i)
                     << " time=" << time_per_sample << " ms\n";
            }

            global_sample_idx += b;
            total_samples_processed += b;
        }

        double avg_latency_ms = (total_samples_processed > 0)
            ? (total_ms / (double)total_samples_processed)
            : 0.0;

        double avg_latency_s = avg_latency_ms / 1000.0;

        cout << "\n>>> BENCHMARK 2 FINAL RESULTS (BOOTSTRAPPED) <<<\n";
        cout << "Avg Latency (ms/sample): " << std::fixed << std::setprecision(4) << avg_latency_ms << "\n";
        cout << "Avg Latency (s/sample):  " << std::fixed << std::setprecision(6) << avg_latency_s  << "\n";
        cout << "[Host2] Total " << (long long)total_ms << " ms\n";

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Host2 error: " << e.what() << endl;
        return 1;
    }
}
