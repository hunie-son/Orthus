import pandas as pd
from sklearn.datasets import fetch_openml
from sklearn.model_selection import train_test_split
import os

print("Downloading MNIST (this may take a minute)...")
# Fetch MNIST (70k samples)
mnist = fetch_openml('mnist_784', version=1, as_frame=True, parser='auto')
X = mnist.data
y = mnist.target.astype(int)

# Normalize now (0-1) so C++ loader is simpler, or keep 0-255. 

# Combine for splitting
df = pd.concat([y, X], axis=1)

print("Splitting data...")
# Standard MNIST split: 60k train, 10k test
train, test = train_test_split(df, test_size=10000, random_state=42, shuffle=True)

# Save to data folder
os.makedirs("data", exist_ok=True)
print("Saving data/mnist_train.csv...")
train.to_csv("data/mnist_train.csv", index=False, header=False) 
print("Saving data/mnist_test.csv...")
test.to_csv("data/mnist_test.csv", index=False, header=False)

print("Done!")
