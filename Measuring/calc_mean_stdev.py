import numpy as np
import pandas as pd
import sys, io

# Read in the single-line entry minus the last character (always comma, reads as NAN)
with open(sys.argv[1] + ".csv") as f:
  processed_values = f.read()[:-1]


# First entry is often missing a character, drop the column
df = pd.read_csv(io.StringIO(processed_values), header=None, index_col=0)
df = df.reset_index(drop=True)

# Get mean and std
mean = df.values.flatten().mean()
stdev = df.values.flatten().std()

# Save mean and std to file (file_name is overwritten constantly)
with open(sys.argv[1] + "_processed.csv", "a") as f:
  f.write(f"{mean},{stdev}\n")
