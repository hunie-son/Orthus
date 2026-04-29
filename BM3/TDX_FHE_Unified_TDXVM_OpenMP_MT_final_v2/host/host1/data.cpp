#include "data.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <cmath>
#include <cctype>
#include <numeric>

static std::string trim(const std::string& s) {
    size_t a=0, b=s.size();
    while(a<b && std::isspace((unsigned char)s[a])) ++a;
    while(b>a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

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

void load_iris_raw(DataSet& ds, const std::string& path) {
    std::ifstream in(path);
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
    std::string line;
    int count = 0;
    while(std::getline(in, line)) {
        if(line.empty()) continue;
        if(limit > 0 && count >= limit) break;
        std::stringstream ss(line);
        std::string val;
        std::vector<double> row;
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

void load_unified_data(const std::string& name, DataSet& train, DataSet& test) {
    std::cout << "[Data] Loading dataset: " << name << "...\n";
    if (name == "iris") {
        DataSet full;
        load_iris_raw(full, "../data/iris_new.csv");
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
        load_wdbc_raw(full, "../data/wdbc.csv");
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
        load_mnist_raw(train, "../data/mnist_train.csv", 5000); 
        load_mnist_raw(test,  "../data/mnist_test.csv", 48);
    }
    else {
        std::cerr << "Unknown dataset: " << name << "\n"; exit(1);
    }
    
    std::ofstream out_y("/data/test_labels_unified.txt");
    for(int y : test.Y) out_y << y << "\n";
    std::cout << "[Data] Ready. Train: " << train.X.size() << ", Test: " << test.X.size() << "\n";
}
