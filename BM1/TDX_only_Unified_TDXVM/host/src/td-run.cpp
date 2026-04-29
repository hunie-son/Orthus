#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <chrono>
#include <iomanip>
#include "utils.hpp"

using namespace std;

// Activation
static double sigmoid(double z) { return 1.0 / (1.0 + std::exp(-z)); }

// Matrix-Vector Multiplication
vector<double> mat_vec_mul(const vector<vector<double>>& W, const vector<double>& x) {
    if (W.empty()) return {};
    size_t rows = W.size();
    size_t cols = W[0].size();
    vector<double> res(rows, 0.0);
    // W[row][0] is bias. x matches W columns 1..end
    for(size_t i=0; i<rows; ++i) {
        double sum = W[i][0]; 
        for(size_t j=1; j<cols; ++j) {
            if(j-1 < x.size()) sum += W[i][j] * x[j-1];
        }
        res[i] = sum;
    }
    return res;
}

// Load Weights
vector<vector<double>> load_matrix(const string& path) {
    ifstream in(path);
    vector<vector<double>> mat;
    string line;
    while(getline(in, line)) {
        stringstream ss(line);
        string val; vector<double> row;
        while(getline(ss, val, ',')) row.push_back(stod(val));
        mat.push_back(row);
    }
    return mat;
}

int main() {
    try {
        // --- 1. Load & Decrypt Data ---
        if(!file_exists("/data/C1.bin")) return 1;
        auto C1 = read_bin("/data/C1.bin");
        auto C2 = read_bin("/data/C2.bin");
        
        AES aes;
        auto keyiv = rsa_unwrap(C1);
        aes.set_key_iv(keyiv);

        auto raw_data = aes.decrypt(C2);
        string csv_content(raw_data.begin(), raw_data.end());

        // --- 2. Load Model ---
        int is_mlp = 0;
        { ifstream cnf("/data/config.txt"); cnf >> is_mlp; }
        
        vector<vector<double>> W1 = load_matrix("/data/W1.csv");
        vector<vector<double>> W2;
        if (is_mlp) W2 = load_matrix("/data/W2.csv");

        cout << "[TD] Model Loaded. Type: " << (is_mlp ? "MLP" : "LR") << endl;

        // --- 3. Inference Loop ---
        stringstream ss(csv_content);
        string line;
        stringstream out_ss;
        out_ss << "sample_id,predicted,actual,latency_us\n";

        int total = 0, correct = 0;
        double total_us = 0.0; // Changed to double for precision
        int row_idx = 0;

        // For console printing limit
        int print_limit = 30;

        while(getline(ss, line)) {
            if(line.empty()) continue;
            stringstream ls(line);
            string val; vector<double> x;
            while(getline(ls, val, ',')) x.push_back(stod(val));
            
            int actual = (int)x.back();
            x.pop_back();

            // === TIMER START (Nanoseconds) ===
            auto t0 = chrono::high_resolution_clock::now();
            int pred = 0;

            if (is_mlp) {
                // MLP Forward
                auto h = mat_vec_mul(W1, x);
                for(auto& v : h) v = sigmoid(v);
                auto out = mat_vec_mul(W2, h);
                
                double max_v = -1e9;
                for(size_t i=0; i<out.size(); ++i) if(out[i] > max_v) { max_v = out[i]; pred = i; }
            } else {
                // LR Forward
                auto out = mat_vec_mul(W1, x);
                double max_v = -1e9;
                for(size_t i=0; i<out.size(); ++i) if(out[i] > max_v) { max_v = out[i]; pred = i; }
            }
            // === TIMER END ===
            auto t1 = chrono::high_resolution_clock::now();
            
            // Calculate duration in microseconds (floating point)
            double duration_us = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count() / 1000.0;
            total_us += duration_us;

            if (pred == actual) correct++;
            
            // Print details to console (Like Benchmark 3)
            if (row_idx < print_limit) {
                cout << "[TD] sample #" << row_idx << " time=" << fixed << setprecision(3) << duration_us << " us" << endl;
            }

            out_ss << row_idx << "," << pred << "," << actual << "," << duration_us << "\n";
            row_idx++;
            total++;
        }

        // --- 4. Summary & Encrypt ---
        double avg_lat = (total > 0) ? (total_us / total) : 0.0;
        
        out_ss << "\nSUMMARY:\n";
        out_ss << "Accuracy: " << (100.0 * correct / total) << "%\n";
        out_ss << "Avg Latency: " << fixed << setprecision(4) << avg_lat << " us/sample\n";

        cout << "[TD] Processed " << total << " samples." << endl;
        cout << "[TD] Total Time: " << (total_us/1000.0) << " ms" << endl;

        auto cres = aes.encrypt(out_ss.str());
        write_bin("/data/Cres.bin", cres);
        cout << "[TD] Result encrypted and saved.\n";

    } catch (const exception& e) {
        cerr << "[TD Error] " << e.what() << endl;
        return 1;
    }
    return 0;
}
