import os
import pandas as pd
import numpy as np
import re
from collections import defaultdict
from pathlib import Path
import json

def compute_statistics(dataframe, digits=3):
    if not dataframe.empty:
        return {
            "Average Latency": round(np.mean(dataframe["Latency"]) * 1000, digits),
            "P0 Latency": round(np.min(dataframe["Latency"]) * 1000, digits),
            "P10 Latency": round(np.percentile(dataframe["Latency"], 10) * 1000, digits),
            "P90 Latency": round(np.percentile(dataframe["Latency"], 90) * 1000, digits),
            "P99 Latency": round(np.percentile(dataframe["Latency"], 99) * 1000, digits),
            "StdDev": round(np.std(dataframe["Latency"], ddof=1) * 1000, digits),
        }
    return None


def create_final_dataframe(results):
    df = pd.DataFrame(results)
    return df

def is_cubic_or_prague(filename):
    port_match = re.search(r"192\.168\.1\.2\s(\d{3})", filename)
    if port_match:
        port_number = int(port_match.group(1))
        if 100 <= port_number <= 199:
            return "prague"
        elif 200 <= port_number <= 299:
            return "cubic"
    return None

def process_test_directory(test_dir_path):
    data = {
        "cubic": {"UL": pd.DataFrame(), "DL": pd.DataFrame()},
        "prague": {"UL": pd.DataFrame(), "DL": pd.DataFrame()}
    }

    # Process latency files
    for file in os.listdir(test_dir_path):
        if file.endswith("latency.csv"):
            direction = "UL" if "192.168.1.2" in file.split(" to ")[0] else "DL"
            category = is_cubic_or_prague(file)
            if category:
                try:
                    df = pd.read_csv(os.path.join(test_dir_path, file), encoding='ISO-8859-1')
                    if not data[category][direction].empty:
                        data[category][direction] = pd.concat([data[category][direction], df], ignore_index=True)
                    else:
                        data[category][direction] = df
                except UnicodeDecodeError:
                    print(f"Error decoding file: {file}")

    # Process Bandwidth Calculation
    for direction, summary_file in [("DL", "summary_table_downstream.csv"), ("UL", "summary_table_upstream.csv")]:
        summary_path = os.path.join(test_dir_path, summary_file)
        if os.path.exists(summary_path):
            try:
                summary_df = pd.read_csv(summary_path, encoding='ISO-8859-1')
                # Assume 'Flow ID' column exists and categorization logic is applied here
                for category in ['cubic', 'prague']:
                    # Filter rows based on category using is_cubic_or_prague logic on 'Flow ID'
                    filtered_df = summary_df[summary_df['Flow ID'].apply(lambda x: is_cubic_or_prague(x) == category)]
                    bandwidth = filtered_df['Mean data rate (Mbps)'].sum() if not filtered_df.empty else 0
                    if not data[category][direction].empty:
                        data[category][direction]['Bandwidth (Mbps)'] = bandwidth
                    else:
                        # Create an empty DataFrame with a 'Bandwidth (Mbps)' column if it doesn't exist
                        data[category][direction] = pd.DataFrame({'Bandwidth (Mbps)': [bandwidth]})
            except UnicodeDecodeError as e:
                print(f"Error reading {summary_file}: {e}")

    # Process "CE %" from summary tables
    for direction, summary_file in [("DL", "summary_table_downstream.csv"), ("UL", "summary_table_upstream.csv")]:
        summary_path = os.path.join(test_dir_path, summary_file)
        if os.path.exists(summary_path):
            summary_df = pd.read_csv(summary_path, encoding='ISO-8859-1')
            # Ensure 'Num CE' and 'Num Packets' are numeric and handle potential missing values
            summary_df['Num CE'] = pd.to_numeric(summary_df['Num CE'], errors='coerce').fillna(0)
            summary_df['Num Packets'] = pd.to_numeric(summary_df['Num Packets'], errors='coerce').fillna(1)  # Avoid division by zero
            summary_df['CE %'] = (summary_df['Num CE'] / summary_df['Num Packets']) * 100
            
            for category in ['cubic', 'prague']:
                filtered_df = summary_df[summary_df['Flow ID'].apply(lambda x: is_cubic_or_prague(x) == category)]
                ce_percentage = filtered_df['CE %'].sum() if not filtered_df.empty else 0
                
                if 'CE %' not in data[category][direction].columns:
                    data[category][direction] = data[category][direction].assign(**{'CE %': ce_percentage})
                else:
                    data[category][direction]['CE %'] += ce_percentage

    return data

def process_results(root_dir):
    all_results = []

    test_run_dirs = [
        d for d in os.listdir(root_dir) if os.path.isdir(os.path.join(root_dir, d))
    ]

    for test_dir in test_run_dirs:
        test_dir_path = os.path.join(root_dir, test_dir)
        data = process_test_directory(test_dir_path)

        # Initialize a template for the results dictionary for this test directory
        test_results_template = {"Label": test_dir}

        for direction in ["UL", "DL"]:
            # Initialize default values
            default_stats = {"Average Latency": None, "P10 Latency": None, "P90 Latency": None, "P99 Latency": None, "StdDev Latency": None}

            # Compute statistics for cubic and prague if data is available
            if not data["cubic"][direction].empty and "Latency" in data["cubic"][direction].columns:
                stats_cubic = compute_statistics(data["cubic"][direction],digits=0)
            else:
                stats_cubic = default_stats
            bandwidth_cubic = round(data["cubic"][direction]["Bandwidth (Mbps)"].mean()) if "Bandwidth (Mbps)" in data["cubic"][direction].columns else 0
            ce_cubic = data["cubic"][direction]["CE %"].mean() if "CE %" in data["cubic"][direction].columns else 0

            if not data["prague"][direction].empty and "Latency" in data["prague"][direction].columns:
                stats_prague = compute_statistics(data["prague"][direction],digits=0)
            else:
                stats_prague = default_stats
            bandwidth_prague = round(data["prague"][direction]["Bandwidth (Mbps)"].mean()) if "Bandwidth (Mbps)" in data["prague"][direction].columns else 0
            ce_prague = data["prague"][direction]["CE %"].mean() if "CE %" in data["prague"][direction].columns else 0

            # Merge cubic and prague stats into the results template
            for key, value in stats_cubic.items():
                test_results_template[f"{key} {direction} Cubic"] = value
            test_results_template[f"Average Bandwidth {direction} Cubic (Mbps)"] = bandwidth_cubic
            test_results_template[f"CE % {direction} Cubic"] = ce_cubic

            for key, value in stats_prague.items():
                test_results_template[f"{key} {direction} Prague"] = value
            test_results_template[f"Average Bandwidth {direction} Prague (Mbps)"] = bandwidth_prague
            test_results_template[f"CE % {direction} Prague"] = ce_prague

        all_results.append(test_results_template)


    df_results = pd.DataFrame(all_results)
    results_csv_path = os.path.join(root_dir, "processed_results.csv")
    df_results.to_csv(results_csv_path, index=False)

    print(f"Combined results saved to {os.path.join(root_dir, 'processed_results.csv')}")


def extract_testcase_identifiers(row):
    identifiers = row['Test Case'].split('-')
    identifier_dict = {}
    for identifier in identifiers:
        letters, num = identifier[:2], int(identifier[2:])
        identifier_dict["x" + letters] = num
    return pd.Series(identifier_dict)



def merge_input_with_results(root_dir):
    input_df = pd.read_csv("config.csv")  # Contains "Test Case"

    results_df = pd.read_csv(os.path.join(root_dir, "processed_results.csv"))

    # Match the test case label from directory names so they line up
    results_df["Test Case Match"] = results_df["Label"].apply(lambda x: x.split("_")[0])

    merged_df = pd.merge(
        results_df,
        input_df,
        left_on="Test Case Match",
        right_on="Test Case",
        how="left",
    )

    # Add columns for the different testcase identifiers
    merged_df = pd.concat([merged_df, merged_df.apply(extract_testcase_identifiers, axis=1)], axis=1)
   

    # Generate the 'link' column
    merged_df['link'] = merged_df['Label'].apply(lambda label: f"link:./{os.path.join(label)}[{label}]/")
    merged_df['Label'] = merged_df['link']

    # Sort by Test Case id
    merged_df=merged_df.sort_values(by='Test Case')

    # Reorder columns to place 'Label', 'Test Case', input_df columns, then the rest
    input_df_columns = [col for col in input_df.columns if col != 'Test Case']  # Avoid duplicating 'Test Case'
    final_columns = ['Label', 'Test Case'] + input_df_columns + \
                    [col for col in merged_df.columns if col not in ['Label', 'Test Case'] + input_df_columns + ['Test Case Match']]

    # Add 'link' as the last column
    final_columns = [col for col in final_columns if col != 'link'] + ['link']

    merged_df = merged_df[final_columns]

    # Drop temporary columns
    # columns_to_drop = ['Test Case Match', 'MS', 'AP', 'TC', 'TS', 'LS']
    # merged_df.drop(columns_to_drop, errors='ignore', axis=1, inplace=True)

    final_csv_path = os.path.join(root_dir, "results.csv")
    merged_df.to_csv(final_csv_path, index=False)

    print(f"Final merged results saved to {final_csv_path}")

    return Path(final_csv_path)


def hide_columns(root_dir, hidden_columns):
        
    df = pd.read_csv(os.path.join(root_dir, "results.csv"))
    df=df.sort_values(by='Test Case')

    # Specify columns intended to be dropped
    intended_columns_to_drop = ['Test Case Match', 'xMS', 'xAP', 'xTC', 'xTS', 'xLS', 'xTS',
                                'calc_ABW_DL_Prague_Mbps', 'calc_ABW_DL_Cubic_Mbps', 
                                'calc_P99_Latency_DL_Prague', 'calc_P99_Latency_DL_Cubic'] + hidden_columns
    columns_to_drop = [col for col in intended_columns_to_drop if col in df.columns]

    # Drop the specified columns from the DataFrame
    df.drop(columns=columns_to_drop, inplace=True)

    # Save the cleaned DataFrame to "detailed_results.csv"
    # Used by exporter
    detailed_csv_path = os.path.join(root_dir, "detailed_results.csv")
    df.to_csv(detailed_csv_path, index=False)
    print(f"Post Processed Final merged results saved to {detailed_csv_path}")

    return Path(detailed_csv_path)


def post_process(root_dir):
        
    df = pd.read_csv(os.path.join(root_dir, "results.csv"))
    df=df.sort_values(by='Test Case')

    df['Log Rate Ratio'] = np.nan
    df['Latency Benefit'] = np.nan

    # Initialize additional columns for tracking used values
    df['calc_ABW_DL_Prague_Mbps'] = np.nan
    df['calc_ABW_DL_Cubic_Mbps'] = np.nan
    df['calc_P99_Latency_DL_Prague'] = np.nan
    df['calc_P99_Latency_DL_Cubic'] = np.nan

    # the following code assumes that TC1 is cubic only, TC2 is prague only, TC3+ are cubic+prague equal numbers of flows
    # it also assumes that sorting by Test Case (above) succeeds in lining up the appropriate TC1 and TC2 instances for the ".index" lines below.

    # Calculate Log Rate Ratios
    cubic_rates=df[(df.xTC==1)|(df.xTC > 2)]['Average Bandwidth DL Cubic (Mbps)']
    prague_rates=df[df.xTC >= 2]['Average Bandwidth DL Prague (Mbps)']
    cubic_rates.index = prague_rates.index
    rate_ratios=prague_rates/cubic_rates
    log_rate_ratios = round(np.log10(rate_ratios), 3)
    df['Log Rate Ratio']=log_rate_ratios
    df['calc_ABW_DL_Prague_Mbps'] = prague_rates
    df['calc_ABW_DL_Cubic_Mbps'] = cubic_rates
 
    # Calculate Latency Benefits
    wanDelays = df[df.xTC >= 2]['wanLinkDelay'].str.extract(r'"(\d+)ms"').astype(int).squeeze() # get the wanLinkDelay as an integer in ms
    cubic_P99s=df[(df.xTC==1)|(df.xTC > 2)]['P99 Latency DL Cubic']
    prague_P99s=df[df.xTC >= 2]['P99 Latency DL Prague']
    cubic_P99s.index = prague_P99s.index
    latency_benefits=round(cubic_P99s - prague_P99s,3)
    df['Latency Benefit']=latency_benefits
    df['calc_P99_Latency_DL_Prague'] = prague_P99s - wanDelays
    df['calc_P99_Latency_DL_Cubic'] = cubic_P99s - wanDelays
 
 
    # Temporary detailed results with calculation information used.
    # Used by process_summmary_csv
    calc_csv_path = os.path.join(root_dir, "calc_detailed_results.csv")
    df.to_csv(calc_csv_path, index=False)
    print(f"Intermediary calculated metrics saved to {calc_csv_path}")


def process_summary_csv(rootResultsdir):
    # Load the configuration from campaigns.json
    script_dir = os.path.dirname(__file__)
    with open(os.path.join(script_dir, 'campaigns.json'), 'r') as config_file:
        campaigns_config = json.load(config_file)

    df = pd.read_csv(os.path.join(rootResultsdir, "calc_detailed_results.csv"))

    # Use subset of csv
    data_subset = df[['Test Case', 'wanLinkDelay', 'channelWidth', 'Log Rate Ratio', 'Latency Benefit',
                      'calc_ABW_DL_Prague_Mbps', 'calc_ABW_DL_Cubic_Mbps', 
                      'calc_P99_Latency_DL_Prague', 'calc_P99_Latency_DL_Cubic']].copy()

    # Re-add columns for the different testcase identifiers
    data_subset = pd.concat([data_subset, data_subset.apply(extract_testcase_identifiers, axis=1)], axis=1)

    ## Create a TS x TC table for each case of the other testcase identifiers
    row_labels_map = campaigns_config['TS']
    row_identifier='xTS'
    column_labels_map = campaigns_config['TC']
    column_identifier='xTC'

    # get a list of the other identifier tags
    identifier_list = [col for col in data_subset.columns if col.startswith('x') and col not in [row_identifier, column_identifier]]
    
    # Filter out TC1 as a special case, since its data is in the corresponding TC2 row
    valid_data_subset = data_subset[(data_subset['xTC'] > 1) & data_subset['Log Rate Ratio'].notna() & data_subset['Latency Benefit'].notna()]

    # Figure out the dimensions of each table and set the default table to be all zeros
    num_rows = valid_data_subset[row_identifier].unique().size
    num_columns = valid_data_subset[column_identifier].unique().size
    output_data_extended = defaultdict(lambda: np.zeros((num_rows, num_columns), dtype=object))

    for _, row in valid_data_subset.iterrows():
        table_index = tuple(row[col] for col in identifier_list)
        row_index, col_index = row[row_identifier] - 1, row[column_identifier] - 2

        # Extended cell content
        extended_cell_content = (f"{row['Log Rate Ratio']:+.1f} "
                         f"[a: {row['calc_ABW_DL_Prague_Mbps']:.0f}M, b: {row['calc_ABW_DL_Cubic_Mbps']:.0f}M] +\n"
                         f"{row['Latency Benefit']:.0f}ms "
                         f"[a: {row['calc_P99_Latency_DL_Prague']:.0f}ms, b: {row['calc_P99_Latency_DL_Cubic']:.0f}ms]")
        output_data_extended[table_index][row_index, col_index] = extended_cell_content

    # Generate original and extended CSV files
    output_files = []
    for table_index, extended_matrix in output_data_extended.items():

        file_name_base = ""
        for i,identifier in enumerate(identifier_list):
            name_component = campaigns_config[identifier[1:]][str(table_index[i])]
            file_name_base += name_component + "_"
        extended_file_name = file_name_base[:-1] + ".csv"

        extended_full_path = os.path.join(rootResultsdir, extended_file_name)

        # Save extended summary CSV
        df_extended_output = pd.DataFrame(extended_matrix, columns=[column_labels_map[str(i + 2)] for i in range(extended_matrix.shape[1])])
        df_extended_output.index = [row_labels_map[str(i + 1)] for i in range(num_rows)]
        df_extended_output.index.name = ""
        df_extended_output.to_csv(extended_full_path)
        output_files.append(Path(extended_full_path))

        print(f"Extended summary csv saved to {extended_full_path}")

    return output_files

