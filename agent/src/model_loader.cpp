#include "model_loader.hpp"
#include "log.hpp"
#include "json.hpp"
#include <fstream>
#include <stdexcept>
#include <cmath>

using json = nlohmann::json;

Model loadModel(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open model file: " + path);
    }
    json j; f >> j;

    Model m;
    m.features  = j.at("features").get<std::vector<std::string>>();
    m.mean      = j.at("scaler_mean").get<std::vector<double>>();
    m.scale     = j.at("scaler_scale").get<std::vector<double>>();
    m.coef      = j.at("coef").get<std::vector<double>>();
    m.intercept = j.at("intercept").get<double>();
    m.threshold = j.at("decision_threshold").get<double>();

    // sanity checks
    if (m.features.size() != m.mean.size() ||
        m.features.size() != m.scale.size() ||
        m.features.size() != m.coef.size()) {
        throw std::runtime_error("Model dimension mismatch in JSON");
    }

    return m;
}

double inferCheatProbability(const Model& model, const std::vector<double>& x) {
    if (x.size() != model.coef.size()) {
        throw std::runtime_error("Feature vector size does not match model");
    }
    double z = model.intercept;
    for (size_t i = 0; i < model.coef.size(); ++i) {
        double normed = (x[i] - model.mean[i]) / model.scale[i];
        z += model.coef[i] * normed;
    }
    return 1.0 / (1.0 + std::exp(-z)); // sigmoid
}
