#include <openfhe.h>
#include <openfhe/pke/cryptocontext-ser.h>
#include <openfhe/pke/ciphertext-ser.h>
#include <openfhe/pke/key/key-ser.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <omp.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <atomic>

#include <unistd.h> // getpid

#include "utils.hpp"

using namespace lbcrypto;
using std::cout;
using std::endl;

static std::mutex g_net_mtx;
static std::atomic<uint64_t> g_req_ctr{0};

static std::string get_env(const char* var, const char* def) {
    const char* val = std::getenv(var);
    return val ? std::string(val) : std::string(def);
}

static int run_cmd(const std::string& cmd) {
    int rc = std::system(cmd.c_str());
    if (rc != 0) return rc;
    return 0;
}

static void check_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) throw std::runtime_error("Missing file: " + path);
}

static std::vector<std::vector<double>> load_matrix_csv(const std::string& path) {
    std::vector<std::vector<double>> W;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
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

// === CHANGED ===
// Read stride, batchcap, and nsamples from pack_meta.txt.
// nsamples is required so the last partial batch is timed correctly.
static bool read_pack_meta(size_t& stride, size_t& batchcap, size_t& nsamples) {
    std::ifstream meta("/data/pack_meta.txt");
    if (!meta.good()) return false;

    std::string k;
    long long v;
    while (meta >> k >> v) {
        if (k == "stride") stride = (size_t)v;
        else if (k == "batchcap") batchcap = (size_t)v;
        else if (k == "nsamples") nsamples = (size_t)v;
    }
    return true;
}

// Network serialized TDX refresh to avoid req/res collisions and /data bloat
static void td_refresh(Ciphertext<DCRTPoly>& ct) {
    if (!ct) throw std::runtime_error("td_refresh got nullptr ciphertext");

    uint64_t ctr = g_req_ctr.fetch_add(1);
    int pid = (int)getpid();

    std::string f_req = "/data/req_" + std::to_string(pid) + "_" + std::to_string(ctr) + ".bin";
    std::string f_res = "/data/res_" + std::to_string(pid) + "_" + std::to_string(ctr) + ".bin";

    {
        std::ofstream out(f_req, std::ios::binary);
        Serial::Serialize(ct, out, SerType::BINARY);
    }

    std::string ip   = get_env("TD_HOST", "host.docker.internal");
    std::string port = get_env("TD_PORT", "10022");
    std::string user = get_env("TD_USER", "root");
    std::string pass = get_env("TD_PASS", "123456");
    std::string target = user + "@" + ip;

    std::string ssh_base = "sshpass -p " + pass + " ssh -p " + port +
                           " -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -q ";
    std::string scp_base = "sshpass -p " + pass + " scp -P " + port +
                           " -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -q ";

    std::lock_guard<std::mutex> lock(g_net_mtx);

    // upload req
    {
        std::string cmd = scp_base + f_req + " " + target + ":" + f_req;
        if (run_cmd(cmd) != 0) {
            std::remove(f_req.c_str());
            throw std::runtime_error("td_refresh scp upload failed");
        }
    }

    // exec refresh
    {
        std::string remote_exec =
            "export LD_LIBRARY_PATH=/data/lib:$LD_LIBRARY_PATH; "
            "/data/td-bootstrap " + f_req + " " + f_res;
        std::string cmd = ssh_base + target + " \"" + remote_exec + "\"";
        if (run_cmd(cmd) != 0) {
            run_cmd(ssh_base + target + " \"rm -f " + f_req + " " + f_res + "\"");
            std::remove(f_req.c_str());
            throw std::runtime_error("td_refresh remote exec failed");
        }
    }

    // download res
    {
        std::string cmd = scp_base + target + ":" + f_res + " " + f_res;
        if (run_cmd(cmd) != 0) {
            run_cmd(ssh_base + target + " \"rm -f " + f_req + " " + f_res + "\"");
            std::remove(f_req.c_str());
            throw std::runtime_error("td_refresh scp download failed");
        }
    }

    // cleanup on guest
    run_cmd(ssh_base + target + " \"rm -f " + f_req + " " + f_res + "\"");

    // load refreshed ct
    {
        std::ifstream in(f_res, std::ios::binary);
        Serial::Deserialize(ct, in, SerType::BINARY);
    }

    std::remove(f_req.c_str());
    std::remove(f_res.c_str());
}

int main() {
    try {
        cout << "[Host] Phase 2: Host2 Inference (TDX Assisted)...\n";

        // Load cc + keys
        CryptoContext<DCRTPoly> cc;
        check_file("/data/keys/cc.json");
        { std::ifstream f("/data/keys/cc.json"); Serial::Deserialize(cc, f, SerType::JSON); }

        PublicKey<DCRTPoly> pk;
        check_file("/data/keys/pk.bin");
        { std::ifstream f("/data/keys/pk.bin", std::ios::binary); Serial::Deserialize(pk, f, SerType::BINARY); }

        check_file("/data/keys/evm.bin");
        check_file("/data/keys/eva.bin");
        { std::ifstream f("/data/keys/evm.bin", std::ios::binary); cc->DeserializeEvalMultKey(f, SerType::BINARY); }
        { std::ifstream f("/data/keys/eva.bin", std::ios::binary); cc->DeserializeEvalAutomorphismKey(f, SerType::BINARY); }

        // Warm up rotation keys that we actually use
        cout << "[Host2] Warming up rotation keys... ";
        auto warm_pt = cc->MakeCKKSPackedPlaintext(std::vector<double>{0.0});
        auto warm_ct = cc->Encrypt(pk, warm_pt);
        for (int32_t s = 1; s <= 512; s <<= 1) {
            try { cc->EvalAtIndex(warm_ct, s); } catch (...) {}
        }
        cout << "Done.\n";

        cout << "[Host2] Keys Loaded Successfully.\n";

        // === CHANGED ===
        // Load packing meta including nsamples.
        size_t STRIDE = 0, BATCH_CAP = 0, N_SAMPLES = 0;
        if (!read_pack_meta(STRIDE, BATCH_CAP, N_SAMPLES)) {
            throw std::runtime_error("Failed to read /data/pack_meta.txt");
        }
        if (STRIDE == 0 || BATCH_CAP == 0 || N_SAMPLES == 0) {
            throw std::runtime_error("Invalid pack_meta.txt: stride/batchcap/nsamples");
        }

        // Determine mode and load weights
        bool is_mlp = std::ifstream("/data/W1.csv").good();
        std::vector<std::vector<double>> W1, W2, W_LR;

        int D1 = 0;
        int H  = 0;
        int C  = 0;

        if (is_mlp) {
            W1 = load_matrix_csv("/data/W1.csv");
            W2 = load_matrix_csv("/data/W2.csv");
            if (W1.empty() || W2.empty()) throw std::runtime_error("Empty W1/W2");
            H  = (int)W1.size();
            C  = (int)W2.size();
            D1 = (int)W1[0].size(); // includes bias
            if ((int)W2[0].size() != H + 1) throw std::runtime_error("W2 width must be H+1");
            cout << "[Host2] Mode: MLP (H=" << H << ")\n";
        } else {
            W_LR = load_matrix_csv("/data/W.csv");
            if (W_LR.empty()) throw std::runtime_error("Empty W");
            C  = (int)W_LR.size();
            D1 = (int)W_LR[0].size(); // includes bias
            cout << "[Host2] Mode: LR\n";
        }

        const uint32_t slots = cc->GetRingDimension() / 2;

        // Masks
        std::vector<double> ms(slots, 0.0);
        for (size_t i = 0; i < slots; i += STRIDE) ms[i] = 1.0;
        auto maskStart = cc->MakeCKKSPackedPlaintext(ms);

        std::vector<double> md(slots, 0.0);
        for (size_t i = 0; i < slots; i += STRIDE) {
            for (int d = 0; d < D1; d++) md[i + d] = 1.0; // D1 includes bias at offset 0
        }
        auto maskD1 = cc->MakeCKKSPackedPlaintext(md);

        // Bias plaintexts for output layer in MLP
        std::vector<Plaintext> outBiasPt;
        if (is_mlp) {
            outBiasPt.resize(C);
            for (int c = 0; c < C; ++c) {
                std::vector<double> pb(slots, 0.0);
                for (size_t i = 0; i < slots; i += STRIDE) pb[i] = W2[c][0]; // bias weight
                outBiasPt[c] = cc->MakeCKKSPackedPlaintext(pb);
            }
        }

        // Timing + IO
        std::ifstream ifs_ct("/data/C1.bin", std::ios::binary);
        std::ofstream ofs_out("/data/C2.bin", std::ios::binary | std::ios::trunc);

        int threads = 1;
        if (const char* env_t = std::getenv("OMP_NUM_THREADS")) threads = std::stoi(env_t);
        omp_set_num_threads(threads);
        cout << "[Host2] Running with " << threads << " threads (Network Serialized).\n";

        auto sigmoid_coeffs = poly_sigmoid();

        size_t global_idx = 0;
        double total_batch_ms = 0.0;
        size_t total_samples_processed = 0;

        while (ifs_ct.peek() != EOF) {
            // === CHANGED ===
            // Stop once we have processed all real samples.
            if (global_idx >= N_SAMPLES) break;

            // === CHANGED ===
            // Compute the REAL number of samples in this batch.
            size_t remaining = N_SAMPLES - global_idx;
            size_t b = std::min(BATCH_CAP, remaining);

            Ciphertext<DCRTPoly> c_x;
            Serial::Deserialize(c_x, ifs_ct, SerType::BINARY);
            if (!c_x) throw std::runtime_error("Failed to read input ciphertext");

            auto t0 = std::chrono::high_resolution_clock::now();

            if (is_mlp) {
                std::vector<Ciphertext<DCRTPoly>> H_ct((size_t)H);
                std::vector<std::string> herr((size_t)H);

                #pragma omp parallel for
                for (int j = 0; j < H; ++j) {
                    try {
                        // Elementwise multiply by weights
                        std::vector<double> wp(slots, 0.0);
                        for (size_t base = 0; base < slots; base += STRIDE) {
                            for (int d = 0; d < D1; d++) wp[base + d] = W1[j][d];
                        }
                        auto pt_w = cc->MakeCKKSPackedPlaintext(wp);

                        auto c_mul = cc->EvalMult(c_x, pt_w);
                        td_refresh(c_mul);

                        // Keep only the D1 window
                        auto x = cc->EvalMult(c_mul, maskD1);

                        // Sum within the D1 window
                        for (int32_t s = 1; s < D1; s <<= 1) {
                            x = cc->EvalAdd(x, cc->EvalAtIndex(x, s));
                        }

                        // Keep result only at the start slot
                        auto c_dot = cc->EvalMult(x, maskStart);
                        td_refresh(c_dot);

                        // Activation approximation
                        auto c_act = eval_poly_on_ct(cc, c_dot, sigmoid_coeffs);
                        td_refresh(c_act);

                        // Ensure only start slots remain
                        H_ct[(size_t)j] = cc->EvalMult(c_act, maskStart);
                    } catch (const std::exception& e) {
                        herr[(size_t)j] = e.what();
                    } catch (...) {
                        herr[(size_t)j] = "unknown";
                    }
                }

                for (int j = 0; j < H; ++j) {
                    if (!herr[(size_t)j].empty()) {
                        throw std::runtime_error("Hidden thread error j=" + std::to_string(j) + " msg=" + herr[(size_t)j]);
                    }
                    if (!H_ct[(size_t)j]) {
                        throw std::runtime_error("Hidden ciphertext is nullptr. MLP hidden j=" + std::to_string(j));
                    }
                }

                // Output layer
                for (int c = 0; c < C; ++c) {
                    auto acc = cc->EvalAdd(cc->EvalMult(H_ct[0], W2[c][1]), outBiasPt[c]);
                    for (int j = 1; j < H; ++j) {
                        acc = cc->EvalAdd(acc, cc->EvalMult(H_ct[(size_t)j], W2[c][j + 1]));
                    }
                    Serial::Serialize(acc, ofs_out, SerType::BINARY);
                }
            } else {
                // LR path
                std::vector<Ciphertext<DCRTPoly>> LR_ct((size_t)C);

                #pragma omp parallel for
                for (int c = 0; c < C; ++c) {
                    std::vector<double> wp(slots, 0.0);
                    for (size_t base = 0; base < slots; base += STRIDE) {
                        for (int d = 0; d < D1; d++) wp[base + d] = W_LR[c][d];
                    }
                    auto pt_w = cc->MakeCKKSPackedPlaintext(wp);

                    auto c_mul = cc->EvalMult(c_x, pt_w);
                    td_refresh(c_mul);

                    auto x = cc->EvalMult(c_mul, maskD1);
                    for (int32_t s = 1; s < D1; s <<= 1) {
                        x = cc->EvalAdd(x, cc->EvalAtIndex(x, s));
                    }

                    auto c_logit = cc->EvalMult(x, maskStart);
                    td_refresh(c_logit);

                    LR_ct[(size_t)c] = c_logit;
                }

                for (int c = 0; c < C; ++c) {
                    if (!LR_ct[(size_t)c]) throw std::runtime_error("LR logits nullptr c=" + std::to_string(c));
                    Serial::Serialize(LR_ct[(size_t)c], ofs_out, SerType::BINARY);
                }
            }

            auto t1 = std::chrono::high_resolution_clock::now();
            double batch_ms =
                (double)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            total_batch_ms += batch_ms;

            // === CHANGED ===
            // Divide by the REAL batch size, not the fixed BATCH_CAP.
            double per_sample_ms = batch_ms / (double)b;

            // === CHANGED ===
            // Print only real samples in this batch.
            for (size_t i = 0; i < b; i++) {
                cout << "[Host2] sample #" << global_idx
                     << " time=" << std::fixed << std::setprecision(4)
                     << per_sample_ms << " ms\n";
                global_idx++;
            }

            total_samples_processed += b;
        }

        // === CHANGED ===
        // Average over the REAL number of processed samples.
        double avg_ms = (total_samples_processed > 0)
            ? (total_batch_ms / (double)total_samples_processed)
            : 0.0;
        double avg_s = avg_ms / 1000.0;

        cout << "\n>>> BENCHMARK 3 FINAL RESULTS <<<\n";
        cout << "Avg Latency (ms/sample): " << std::fixed << std::setprecision(4) << avg_ms << "\n";
        cout << "Avg Latency (s/sample):  " << std::fixed << std::setprecision(6) << avg_s << "\n";
        cout << "[Host2] Total " << (long long)total_batch_ms << " ms\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Host2 error: " << e.what() << endl;
        return 1;
    }
}
