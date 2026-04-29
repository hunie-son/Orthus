#pragma once
#include <vector>
#include <string>

struct DataSet {
    std::vector<std::vector<double>> X;
    std::vector<int> Y;
};

void load_unified_data(const std::string& dataset_name, DataSet& train, DataSet& test);
