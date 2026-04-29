#include "model.hpp"
#include <cmath>
#include <random>
#include <iostream>
#include <algorithm>
#include <numeric>

// Standard Sigmoid (Exact Math)
static double sigmoid(double z) { return 1.0 / (1.0 + std::exp(-z)); }

static std::vector<double> softmax(const std::vector<double>& z) {
    std::vector<double> out(z.size());
    double m = -1e300;
    for (double val : z) if (val > m) m = val;
    double sum = 0.0;
    for (size_t i = 0; i < z.size(); ++i) { out[i] = std::exp(z[i] - m); sum += out[i]; }
    for (size_t i = 0; i < z.size(); ++i) out[i] /= sum;
    return out;
}

// === LR ===
std::vector<std::vector<double>> train_lr(const DataSet& ds, int epochs, double lr) {
    int N = ds.X.size(); if (N==0) return {};
    int D = ds.X[0].size();
    int C = 0; for(int y : ds.Y) if(y > C) C = y; C += 1;
    int D1 = D + 1;
    auto W = std::vector<std::vector<double>>(C, std::vector<double>(D1));
    std::mt19937 rng(1234);
    std::normal_distribution<double> dist(0.0, 0.01);
    for (auto& r : W) for (auto& v : r) v = dist(rng);
    std::vector<int> idx(N); std::iota(idx.begin(), idx.end(), 0);
    double decay = 0.0001;

    for (int ep = 0; ep < epochs; ++ep) {
        std::shuffle(idx.begin(), idx.end(), rng);
        for (int i : idx) {
            std::vector<double> x_aug(D1); x_aug[0] = 1.0;
            for (int d = 0; d < D; ++d) x_aug[d+1] = ds.X[i][d];
            std::vector<double> z(C, 0.0);
            for (int c = 0; c < C; ++c)
                for (int d = 0; d < D1; ++d) z[c] += W[c][d] * x_aug[d];
            std::vector<double> preds = softmax(z);
            for (int c = 0; c < C; ++c) {
                double err = preds[c] - (c == ds.Y[i] ? 1.0 : 0.0);
                for (int d = 0; d < D1; ++d) W[c][d] -= lr * (err * x_aug[d] + decay * W[c][d]);
            }
        }
    }
    return W;
}

int predict_lr(const std::vector<double>& x, const std::vector<std::vector<double>>& W) {
    int D = x.size(), C = W.size();
    std::vector<double> x_aug(D+1); x_aug[0]=1.0;
    for(int d=0; d<D; ++d) x_aug[d+1]=x[d];
    int best_c = 0; double best_val = -1e300;
    for (int c = 0; c < C; ++c) {
        double dot = 0.0;
        for (int d = 0; d < D+1; ++d) dot += W[c][d] * x_aug[d];
        if (dot > best_val) { best_val = dot; best_c = c; }
    }
    return best_c;
}

void report_accuracy_lr(const std::vector<std::vector<double>>& W, const DataSet& ds, const std::string& label) {
    int correct = 0;
    for (int i = 0; i < (int)ds.X.size(); ++i) if (predict_lr(ds.X[i], W) == ds.Y[i]) ++correct;
    std::cout << "[LR] " << label << " acc: " << 100.0 * correct / ds.X.size() << "%\n";
}

// === MLP ===
std::pair<std::vector<std::vector<double>>, std::vector<std::vector<double>>>
train_mlp(const DataSet& ds, int H, int epochs, double lr) {
    int N = ds.X.size(); if(N==0) return {};
    int D = ds.X[0].size();
    int C = 0; for(int y : ds.Y) if(y > C) C = y; C += 1;
    int D1 = D + 1, H1 = H + 1;
    auto W1 = std::vector<std::vector<double>>(H, std::vector<double>(D1));
    auto W2 = std::vector<std::vector<double>>(C, std::vector<double>(H1));
    std::mt19937 rng(1234); std::normal_distribution<double> dist(0.0, 0.1);
    for (auto& r : W1) for (auto& v : r) v = dist(rng);
    for (auto& r : W2) for (auto& v : r) v = dist(rng);
    std::vector<int> idx(N); std::iota(idx.begin(), idx.end(), 0);
    double decay = 0.0005;

    for (int ep = 0; ep < epochs; ++ep) {
        std::shuffle(idx.begin(), idx.end(), rng);
        for (int i : idx) {
            std::vector<double> x_aug(D1); x_aug[0] = 1.0;
            for (int d = 0; d < D; ++d) x_aug[d+1] = ds.X[i][d];
            std::vector<double> h(H), h_aug(H1); h_aug[0] = 1.0;
            for (int j = 0; j < H; ++j) {
                double z = 0.0;
                for (int d = 0; d < D1; ++d) z += W1[j][d] * x_aug[d];
                h[j] = sigmoid(z); h_aug[j+1] = h[j];
            }
            std::vector<double> s(C); double m = -1e300;
            for (int c = 0; c < C; ++c) {
                double t = 0.0;
                for (int j = 0; j < H1; ++j) t += W2[c][j] * h_aug[j];
                s[c] = t; m = std::max(m, t);
            }
            double sum = 0.0;
            for (int c = 0; c < C; ++c) { s[c] = std::exp(s[c] - m); sum += s[c]; }
            for (int c = 0; c < C; ++c) s[c] /= sum;

            std::vector<double> delta2(C);
            for (int c = 0; c < C; ++c) delta2[c] = s[c] - (c == ds.Y[i] ? 1.0 : 0.0);

            std::vector<double> delta1(H);
            for (int j = 0; j < H; ++j) {
                double grad = 0.0;
                for (int c = 0; c < C; ++c) grad += W2[c][j+1] * delta2[c];
                delta1[j] = h[j] * (1 - h[j]) * grad;
            }

            for (int c = 0; c < C; ++c) for (int j = 0; j < H1; ++j) W2[c][j] -= lr * (delta2[c] * h_aug[j] + decay * W2[c][j]);
            for (int j = 0; j < H; ++j) for (int d = 0; d < D1; ++d) W1[j][d] -= lr * (delta1[j] * x_aug[d] + decay * W1[j][d]);
        }
    }
    return {W1, W2};
}

int predict_mlp(const std::vector<double>& x,
                const std::vector<std::vector<double>>& W1,
                const std::vector<std::vector<double>>& W2) {
    int D = x.size(), H = W1.size(), C = W2.size();
    std::vector<double> x_aug(D+1); x_aug[0]=1.0;
    for(int d=0; d<D; ++d) x_aug[d+1]=x[d];
    std::vector<double> h(H), h_aug(H+1); h_aug[0]=1.0;
    for(int j=0; j<H; ++j) {
        double z=0.0;
        for(int d=0; d<D+1; ++d) z+=W1[j][d]*x_aug[d];
        h[j]=1.0/(1.0+std::exp(-z)); h_aug[j+1]=h[j];
    }
    double best=-1e300; int pred=0;
    for(int c=0; c<C; ++c) {
        double s=0.0;
        for(int j=0; j<H+1; ++j) s+=W2[c][j]*h_aug[j];
        if(s>best) { best=s; pred=c; }
    }
    return pred;
}

void report_accuracy_mlp(const std::vector<std::vector<double>>& W1,
                         const std::vector<std::vector<double>>& W2,
                         const DataSet& ds, const std::string& label) {
    int correct=0;
    for(int i=0; i<(int)ds.X.size(); ++i) if(predict_mlp(ds.X[i], W1, W2)==ds.Y[i]) ++correct;
    std::cout << "[MLP] " << label << " acc: " << 100.0*correct/ds.X.size() << "%\n";
}
