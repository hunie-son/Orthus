import csv
import matplotlib.pyplot as plt
import numpy as np

#CSV_FILE = "spy_counts.csv"
#CSV_FILE = "result_norand.csv"
CSV_FILE = "result.csv"



#NUMBER_OF_ENCRYPTIONS = 10000.0  # matches your C code
NUMBER_OF_ENCRYPTIONS = 10000.0  # matches your C code


offsets = []
pt_labels = []
rows = []

with open(CSV_FILE, newline="") as f:
    reader = csv.reader(f)

    header = None
    for row in reader:
        if not row:
            continue

        # Skip the log line
        if row[0].startswith("[spy]"):
            continue

        # Header line
        if row[0] == "probe_offset":
            header = row
            pt_labels = header[1:]
            continue

        # Data line: first column should be hex like "0x000000"
        if row[0].startswith("0x"):
            offsets.append(int(row[0], 16))
            rows.append([int(x) for x in row[1:]])

# Convert to array of miss rates
data = np.array(rows, dtype=float) / NUMBER_OF_ENCRYPTIONS

plt.figure(figsize=(8, 5))
plt.imshow(data, aspect="auto", interpolation="nearest",cmap="viridis_r")
plt.colorbar(label="miss rate")

# Y axis: cache line offsets
yticks = list(range(len(offsets)))
ytick_labels = [hex(o) for o in offsets]
plt.yticks(yticks, ytick_labels)

# X axis: pt_0, pt_16, ...
xticks = list(range(len(pt_labels)))
plt.xticks(xticks, pt_labels, rotation=45, ha="right")

plt.xlabel("plaintext first byte group")
plt.ylabel("probe cache line offset from AES_TTABLE_OFFSET")
plt.title("AES T table Flush Flush miss rates")

plt.tight_layout()
plt.savefig("spy_heatmap.png")
# no plt.show() needed on server

