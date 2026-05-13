#!/usr/bin/env python3
"""
Export wandb run data to CSV for analysis
"""

import wandb
import pandas as pd
from pathlib import Path

def export_wandb_run(run_path, output_file="wandb_export.csv"):
    """
    Export wandb run history to CSV

    Args:
        run_path: Full run path (e.g., "/nhafey-me/gigalearncpp/runs/l18x6dpy")
        output_file: Output CSV filename
    """
    try:
        # Initialize wandb API
        api = wandb.Api(timeout=60)

        print(f"Fetching run data from: {run_path}")
        run = api.run(run_path)

        # Get history data
        history = run.history()

        # Convert to pandas DataFrame for easier handling
        df = pd.DataFrame(history)

        print(f"\nData shape: {df.shape[0]} rows × {df.shape[1]} columns")
        print(f"\nColumns: {', '.join(df.columns.tolist())}")

        # Save to CSV
        df.to_csv(output_file, index=False)
        print(f"\n✓ Data exported to: {output_file}")

        # Print summary statistics
        print("\nFirst few rows:")
        print(df.head())

        print("\nBasic statistics:")
        print(df.describe())

        return df

    except Exception as e:
        print(f"Error: {e}")
        print("\nMake sure you're authenticated. Run: wandb login")
        return None

if __name__ == "__main__":
    # Your run path
    run_path = "/nhafey-me/gigalearncpp/runs/l18x6dpy"

    # Export the data
    df = export_wandb_run(run_path)