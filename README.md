# Anti-Cheat Detection Agent

**Hybrid Rule + Machine Learning System for Real-Time Gameplay Anomaly Detection**
*(Developed by Janie Shin)*

---

## 📖 Overview

This project implements a **hybrid anti-cheat detection system** that combines **statistical anomaly rules** with a **machine-learning model** to identify suspicious gameplay behavior from player telemetry.

It simulates a lightweight in-game monitoring agent that detects irregular motion patterns, latency anomalies, or injected actions — mimicking the internal structure of professional game security systems like **Riot Vanguard**, **Valve VACNet**, or **Blizzard Warden**.

---

## ⚙️ System Architecture

```
               ┌───────────────────────────┐
               │      Game Harness          │
               │ (simulated player data)    │
               └────────────┬───────────────┘
                            │ telemetry.csv
                            ▼
               ┌───────────────────────────┐
               │        Agent Core          │
               │  ├── Rule-Based Detector   │
               │  └── ML-Based Detector     │
               │        (LogReg model)      │
               └────────────┬───────────────┘
                            │ eval.csv
                            ▼
               ┌───────────────────────────┐
               │     Jupyter Analytics      │
               │ ROC/PR curves, threshold   │
               │ sweeps, feature weights    │
               └───────────────────────────┘
```

**Components:**

* **`game_harness/`** – Generates synthetic gameplay telemetry (`data/telemetry.csv`).
* **`agent/`** – Reads telemetry, runs detection (rules + ML), and outputs `data/eval.csv`.
* **`models/`** – Stores JSON-exported logistic regression models.
* **`notebooks/`** – Python notebooks for training, visualization, and threshold tuning.
* **`common/`** – Shared logging utilities and JSON parser.

---

## 🚀 Features

| Feature                          | Description                                                                                              |
| -------------------------------- | -------------------------------------------------------------------------------------------------------- |
| **Rule-Based Anomaly Detection** | Detects abnormal speeds using mean ± 3σ, median ± MAD, and hard caps.                                    |
| **Machine Learning Detector**    | Logistic regression trained on synthetic telemetry; exported as JSON and loaded into C++.                |
| **Hybrid Gating**                | Alerts only when both ML and rules agree — reducing false positives.                                     |
| **Evaluation Logging**           | Writes detailed evaluation (`tick`, `speed`, `ml_prob`, `rule_alert`, `cheat_flag`) for ROC/PR analysis. |
| **Visualization Toolkit**        | Jupyter notebooks to inspect coefficients, plot ROC/PR curves, and tune thresholds.                      |
| **Cross-Platform Build**         | Compiles on macOS/Linux using CMake; Windows stubs provided for extension.                               |

---

## 🧠 Why It Matters

This project demonstrates how **AI-assisted security agents** operate in real-time environments:

* **Telemetry ingestion** → continuous behavior monitoring.
* **Anomaly detection** → fast, interpretable thresholds for outlier behavior.
* **ML inference** → adaptive models that learn typical vs. abnormal play.
* **Hybrid fusion** → the balance between precision (few false alerts) and recall (few misses).

It reflects the design principles used in modern **Endpoint Detection and Response (EDR)** tools, **esports anti-cheat**, and **cloud anomaly systems**.

---

## 🧩 Build & Run

### **Prerequisites**

* macOS / Linux (tested on macOS 15)
* CMake ≥ 3.16
* C++17 compiler (Clang / g++)
* Python ≥ 3.10 with packages:
  `numpy pandas scikit-learn matplotlib jupyter`

### **Setup**

```bash
git clone https://github.com/yourusername/anti-cheat.git
cd anti-cheat
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

### **Build**

```bash
cmake -S . -B build
cmake --build build
```

### **Run**

```bash
# Generate gameplay telemetry
./build/game_harness/game_harness

# Run rule-based detection only
./build/agent/agent --quiet --eval=data/eval_rules.csv

# Run hybrid (rule + ML)
AC_HYBRID=1 ./build/agent/agent --quiet --eval=data/eval_hybrid.csv
```

Results are written to:

```
data/telemetry.csv
data/eval_rules.csv
data/eval_hybrid.csv
```

---

## 📊 Analysis & Visualization

Run the notebook:

```bash
cd notebooks
jupyter notebook eval_visuals.ipynb
```

You’ll see:

* **Coefficient ranking** (most influential features)
* **ROC / PR curves** for rule, ML, and hybrid detectors
* **Threshold sweeps** to tune decision boundary
* **Timeline plots** of ML probabilities and cheat ticks

---

## 🧮 Model Export (Python → C++)

Training in `ai_baseline.ipynb`:

```python
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
import json, time

logreg = Pipeline([
    ("scaler", StandardScaler()),
    ("clf", LogisticRegression(max_iter=1000, class_weight="balanced", random_state=42))
])
logreg.fit(X_train, y_train)

scaler = logreg.named_steps["scaler"]
clf = logreg.named_steps["clf"]

export = {
    "schema_version": 1,
    "type": "logistic_regression",
    "features": list(X_train.columns),
    "scaler_mean": scaler.mean_.tolist(),
    "scaler_scale": scaler.scale_.tolist(),
    "coef": clf.coef_[0].tolist(),
    "intercept": float(clf.intercept_[0]),
    "decision_threshold": 0.85
}

os.makedirs("../models", exist_ok=True)
with open(f"../models/logreg_export.json","w") as f:
    json.dump(export, f, indent=2)
```

This JSON is directly consumed by the C++ agent.

---

## 📈 Example Output

```
[I] Agent starting | platform=macos | build=Oct 14 2025 21:53:20
[I] Thresholds: mean=1.773 std=2.108 thr3σ=8.095 | median=1.228 MAD=0.527 thrRobust=3.571 | abs=6.00
[I] Eval: TP=9 FP=0 FN=2 TN=89 | precision=1.00 recall=0.82
[I] Loaded ML model from 'models/logreg_export.json' with 6 features, threshold=0.85
[I] AI detected 32 potential cheats
[I] AI Eval: TP=11 FP=21 FN=0 TN=68 | precision=0.34 recall=1.00
[I] Wrote 100 eval rows to data/eval.csv
```

---

## 📘 Evaluation Report (Summary)

| Detector      | Precision | Recall | F1   | Notes                          |
| ------------- | --------- | ------ | ---- | ------------------------------ |
| Rules         | 1.00      | 0.82   | 0.90 | Conservative, few false alarms |
| ML (thr=0.85) | 0.34      | 1.00   | 0.50 | Aggressive, detects all cheats |
| Hybrid        | 1.00      | 0.82   | 0.90 | Best balance                   |

Threshold tuned via ROC/PR analysis.

---

## 🧠 Concepts Behind the Project

| Concept                             | Description                                                                  |
| ----------------------------------- | ---------------------------------------------------------------------------- |
| **3σ Rule**                         | Detects statistical outliers assuming normal distribution.                   |
| **Median Absolute Deviation (MAD)** | Robust outlier detection tolerant to noise.                                  |
| **Logistic Regression**             | Probabilistic binary classifier modeling log-odds of cheating.               |
| **Hybrid Detection**                | Combines interpretable rules with adaptive ML for reliability.               |
| **ROC/PR Analysis**                 | Evaluates precision-recall tradeoffs under threshold sweeps.                 |
| **Telemetry Ingestion**             | Streaming numerical features (speed, accel, ping, etc.) like real game data. |

---

## 🧭 Next Steps (Future Work)

* Add **tree-based models** (Random Forest, XGBoost).
* Extend telemetry with **mouse/keyboard input**, **network jitter**, etc.
* Integrate **real-time dashboard** (Flask + Plotly or Grafana).
* Push alerts to **AWS CloudWatch** or **Slack webhook**.
* Explore **online retraining** to adapt to new cheat patterns.

---

## 📄 License

MIT License — open for educational and research use.

---

## 🧑‍💻 Author

**Janie Shin**
*B.S. Computer Science & Mathematics, Vanderbilt University*
Cybersecurity & AI Research • Women in Computing • Gen.G ChallengHER
📧 [Your Email Here] | 💼 [LinkedIn Profile URL]

---
