#pragma once
#include <vector>
#include <utility>
#include <string>
#include "data.hpp"

// --- LR ---
std::vector<std::vector<double>>
train_lr(const DataSet& ds, int epochs, double lr);

int predict_lr(const std::vector<double>& x,
               const std::vector<std::vector<double>>& W);

void report_accuracy_lr(const std::vector<std::vector<double>>& W,
                        const DataSet& ds, const std::string& label);

// --- MLP ---
std::pair<std::vector<std::vector<double>>, std::vector<std::vector<double>>>
train_mlp(const DataSet& ds, int H, int epochs, double lr);

int predict_mlp(const std::vector<double>& x,
                const std::vector<std::vector<double>>& W1,
                const std::vector<std::vector<double>>& W2);

void report_accuracy_mlp(const std::vector<std::vector<double>>& W1,
                         const std::vector<std::vector<double>>& W2,
                         const DataSet& ds, const std::string& label); 
