# data/get_wdbc.py
import pandas as pd
import numpy as np

# URL for Wisconsin Diagnostic Breast Cancer (WDBC)
url = "https://archive.ics.uci.edu/ml/machine-learning-databases/breast-cancer-wisconsin/wdbc.data"

# Column names: ID, Diagnosis, 30 features
cols = ['ID', 'Diagnosis'] + [f'feat_{i}' for i in range(1, 31)]

print(f"Downloading {url}...")
try:
    df = pd.read_csv(url, header=None, names=cols)
except Exception as e:
    print(f"Error downloading: {e}")
    exit(1)

# 1. Drop the ID column (irrelevant for ML)
df = df.drop(columns=['ID'])

# 2. Convert Diagnosis 'M' (Malignant) -> 1, 'B' (Benign) -> 0
df['Diagnosis'] = df['Diagnosis'].map({'M': 1, 'B': 0})

# 3. Move 'Diagnosis' (Label) to the END of the dataframe
# Your C++ expects: [features..., label]
label = df.pop('Diagnosis')
df['label'] = label

# 4. Save to CSV without header or index (pure numbers for C++)
output_file = 'wdbc.csv'
df.to_csv(output_file, index=False, header=False)

print(f"Success! Saved {output_file}")
print(f"Total Samples: {len(df)}")
print(f"Features: 30")
