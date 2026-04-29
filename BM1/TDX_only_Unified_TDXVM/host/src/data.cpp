#include "data.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>
#include <cctype>
#include <numeric>

// --- Helpers ---
static std::string trim(const std::string& s) {
    size_t a=0, b=s.size();
    while(a<b && std::isspace((unsigned char)s[a])) ++a;
    while(b>a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

//important to get accuracy
void normalize_data(DataSet& train, DataSet& test) {
    if (train.X.empty()) return;
    int D = train.X[0].size();
    std::vector<double> mu(D, 0.0), sigma(D, 0.0);

    for(const auto& x : train.X) for(int i=0; i<D; ++i) mu[i] += x[i];
    for(int i=0; i<D; ++i) mu[i] /= train.X.size();

    for(const auto& x : train.X) 
        for(int i=0; i<D; ++i) { double d = x[i] - mu[i]; sigma[i] += d*d; }
    
    for(int i=0; i<D; ++i) 
        sigma[i] = std::sqrt(sigma[i] / std::max(1, (int)train.X.size()-1) + 1e-12);

    auto apply = [&](DataSet& ds) {
        for(auto& x : ds.X) for(int i=0; i<D; ++i) x[i] = (x[i] - mu[i]) / sigma[i];
    };
    apply(train);
    apply(test);
}

// --- Parsers ---
// iris, wdbc, mnist
void load_iris_raw(DataSet& ds, const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Error opening " << path << "\n"; return; }
    std::string line;
    while(std::getline(in, line)) {
        if(line.empty()) continue;
        std::stringstream ss(line);
        std::string val; std::vector<double> row;
        while(std::getline(ss, val, ',')) row.push_back(std::stod(val));
        if (row.size() >= 5) {
            ds.Y.push_back((int)row.back());
            row.pop_back();
            ds.X.push_back(row);
        }
    }
}

void load_wdbc_raw(DataSet& ds, const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Error opening " << path << "\n"; return; }
    std::string line;
    while(std::getline(in, line)) {
        if(line.empty()) continue;
        std::stringstream ss(line);
        std::string val;
        std::vector<std::string> tok;
        while(std::getline(ss, val, ',')) tok.push_back(trim(val));

        if(tok.size() < 31) continue;
        int y = 0, start = 0;
        if (tok[1] == "M" || tok[1] == "B") { y = (tok[1] == "M") ? 1 : 0; start = 2; } 
        else if (tok[0] == "M" || tok[0] == "B") { y = (tok[0] == "M") ? 1 : 0; start = 1; }
        
        std::vector<double> x_row;
        for(int i=0; i<30; ++i) x_row.push_back(std::stod(tok[start+i]));
        ds.X.push_back(x_row);
        ds.Y.push_back(y);
    }
}

void load_mnist_raw(DataSet& ds, const std::string& path, int limit) {
    std::ifstream in(path);
    if (!in) { std::cerr << "Error opening " << path << "\n"; return; }
    std::string line;
    int count = 0;
    while(std::getline(in, line)) {
        if(line.empty()) continue;
        if(limit > 0 && count >= limit) break;
        std::stringstream ss(line);
        std::string val; std::vector<double> row;
        int y = -1;
        if(std::getline(ss, val, ',')) y = std::stoi(val);
        while(std::getline(ss, val, ',')) {
            row.push_back(((std::stod(val)/255.0) - 0.5) * 0.125); 
        }
        if(row.size() == 784) {
            ds.X.push_back(row);
            ds.Y.push_back(y);
            count++;
        }
    }
}

// Master Loader
void load_unified_data(const std::string& name, DataSet& train, DataSet& test) {
    // FIXED: Points to the docker volume mount
    std::string prefix = "/app/data_inputs/"; 

    std::cout << "[Data] Loading dataset: " << name << " from " << prefix << "...\n";

    if (name == "iris") {
        DataSet full;
        load_iris_raw(full, prefix + "iris_new.csv");
        if(full.X.empty()) { std::cerr << "Error: Iris data empty.\n"; exit(1); }

        std::mt19937 g(1234);
        std::vector<int> idx(full.X.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::shuffle(idx.begin(), idx.end(), g);
        int n_train = full.X.size() * 0.8;
        for(int i=0; i<n_train; ++i) { train.X.push_back(full.X[idx[i]]); train.Y.push_back(full.Y[idx[i]]); }
        for(size_t i=n_train; i<full.X.size(); ++i) { test.X.push_back(full.X[idx[i]]); test.Y.push_back(full.Y[idx[i]]); }
        normalize_data(train, test);
    } 
    else if (name == "wdbc") {
        DataSet full;
        // Check if you renamed it pima.csv or if it is wdbc.csv
        // Try opening wdbc.csv first, fallback to pima if needed
        std::string wdbc_path = prefix + "wdbc.csv";
        std::ifstream f(wdbc_path);
        if(!f.good()) wdbc_path = prefix + "pima.csv"; 
        
        load_wdbc_raw(full, wdbc_path);
        if(full.X.empty()) { std::cerr << "Error: WDBC data empty.\n"; exit(1); }

        std::mt19937 g(1234);
        std::vector<int> idx(full.X.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::shuffle(idx.begin(), idx.end(), g);
        int n_train = full.X.size() * 0.8;
        for(int i=0; i<n_train; ++i) { train.X.push_back(full.X[idx[i]]); train.Y.push_back(full.Y[idx[i]]); }
        for(size_t i=n_train; i<full.X.size(); ++i) { test.X.push_back(full.X[idx[i]]); test.Y.push_back(full.Y[idx[i]]); }
        normalize_data(train, test);
    }
    else if (name == "mnist") {
        load_mnist_raw(train, prefix + "mnist_train.csv", 5000); 
        load_mnist_raw(test,  prefix + "mnist_test.csv", 48);
        if(train.X.empty()) { std::cerr << "Error: MNIST data empty.\n"; exit(1); }
    }
    else {
        std::cerr << "Unknown dataset: " << name << "\n"; exit(1);
    }
    
    std::cout << "[Data] Ready. Train: " << train.X.size() << ", Test: " << test.X.size() << "\n";
}
