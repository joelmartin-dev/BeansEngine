import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.stats import norm
import sys, io

file_name = sys.argv[1] + "_processed.csv"

df = pd.read_csv(file_name, header=None, names=["mean", "stdev"])

# Create x-axis range that covers all distributions
x_min = (df["mean"] - 4 * df["stdev"]).min()
x_max = (df["mean"] + 4 * df["stdev"]).max()
x = np.linspace(x_min, x_max, 1000)

# Plot each normal distribution
plt.figure(figsize=(10, 6))

for idx, row in df.iterrows():
    mean = row["mean"]
    stdev = row["stdev"]
    
    # Calculate probability density function
    y = norm.pdf(x, loc=mean, scale=stdev)

    # Plot the distribution
    plt.plot(x, y, label=f"μ={mean:.2f}, σ={stdev:.2f}", alpha=0.7, linewidth=2)

# Customize the plot
plt.xlabel("Microseconds")
plt.ylabel("Probability Density")
plt.title(' '.join(sys.argv[1].split('_')).title() + " Distributions")
plt.legend()
plt.grid(True, alpha=0.3)
plt.tight_layout()

plt.savefig(file_name[:-4] + ".svg", bbox_inches="tight", dpi=100)