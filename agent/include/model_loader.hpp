#pragma once
#include <vector>
#include <string>

struct Model {
    std::vector<std::string> features;
    std::vector<double> mean;
    std::vector<double> scale;
    std::vector<double> coef;
    double intercept{};
    double threshold{};
};

Model loadModel(const std::string& path);
double inferCheatProbability(const Model& model, const std::vector<double>& x);
