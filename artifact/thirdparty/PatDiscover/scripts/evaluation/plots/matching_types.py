import typing
import numpy as np
import matplotlib.pyplot as plt

from common.load_data import AvgCombinedData, ConfidenceInfo, EncryptionType
from common.plot import output, text_width_in
from common.colors import *
from common.parameters import bfv_batch_size, ckks_batch_size

def amortize_with_batch_size_list(d: list[ConfidenceInfo], batch_size: int):
    return [x / batch_size for x in d]

def prepare_data(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]], get_item: typing.Callable[[AvgCombinedData], ConfidenceInfo]):
    boolean_data: ConfidenceInfo = ConfidenceInfo(0, 0)
    precise_enum_data: ConfidenceInfo = ConfidenceInfo(0, 0)
    approx_enum_data: ConfidenceInfo = ConfidenceInfo(0, 0)
    precise_continuous_data: ConfidenceInfo = ConfidenceInfo(0, 0)
    approx_continuous_data: ConfidenceInfo = ConfidenceInfo(0, 0)
    precise_distance_data: ConfidenceInfo = ConfidenceInfo(0, 0)
    approx_distance_data: ConfidenceInfo = ConfidenceInfo(0, 0)

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        query_time: ConfidenceInfo = list(map(get_item, data[key]))[0]

        if "boolean_only.json" in key[1]:
            boolean_data = query_time / bfv_batch_size
        elif "enum_precise_only.json" in key[1]:
            precise_enum_data = query_time / bfv_batch_size
        elif "enum_approx_only.json" in key[1]:
            approx_enum_data = query_time / ckks_batch_size
        elif "continuous_precise_only.json" in key[1]:
            precise_continuous_data = query_time / bfv_batch_size
        elif "continuous_approx_only.json" in key[1]:
            approx_continuous_data = query_time / ckks_batch_size
        elif "distance_precise_only.json" in key[1]:
            precise_distance_data = query_time / bfv_batch_size
        elif "distance_approx_only.json" in key[1]:
            approx_distance_data = query_time / ckks_batch_size

    all_timings: list[ConfidenceInfo] = [boolean_data, precise_enum_data, approx_enum_data, precise_continuous_data,
                                         approx_continuous_data, precise_distance_data, approx_distance_data]
    values = [x.mean for x in all_timings]
    bounds = [(x.mean - x.lower_bound, x.upper_bound - x.mean) for x in all_timings]

    return values, bounds

def plot(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    categories = ["Bool", "Enum", "Range", "Distance"]
    index = np.arange(2 * len(categories) - 1)

    fig, (ax1, ax2) = plt.subplots(nrows = 1, ncols=2)
    fig.set_size_inches(text_width_in, 1.3)
    fig.subplots_adjust(left=0.11, right=0.99, bottom=0.13, top=0.78, wspace=0.3)

    client_vals, client_bounds = prepare_data(data, lambda x: x.clientCosts.time.get("Query Execution"))
    idx = np.concatenate([np.array([index[0]]), index[1::2]])
    ax1.bar(idx, 1000 * np.array([client_vals[0]] + client_vals[1::2]), yerr=1000 * np.array([client_bounds[0]] + client_bounds[1::2]), label="Precise", color=precise_color)
    ax1.bar(index[2::2], 1000 * np.array(client_vals[2::2]), yerr=1000 * np.array(client_bounds[2::2]), label="Approximate", color=approx_color)
    ax1.set_xticks([0, 1.5, 3.5, 5.5])
    ax1.set_xticklabels(categories)
    ax1.set_ylabel("Runtime [μs]")
    ax1.set_title("Client Query Time", pad=2.5)

    server_vals, server_bounds = prepare_data(data, lambda x: x.serverCosts.time.get("Query Processing"))
    ax2.bar(idx, [server_vals[0]] + server_vals[1::2], yerr=[server_bounds[0]] + server_bounds[1::2], label="Precise", color=precise_color)
    ax2.bar(index[2::2], server_vals[2::2], yerr=server_bounds[2::2], label="Approximate", color=approx_color)
    ax2.set_xticks([0, 1.5, 3.5, 5.5])
    ax2.set_xticklabels(categories)
    ax2.set_ylabel("Runtime [ms]")
    ax2.set_title("Server Query Time", pad=2.5)

    handles, _ = ax1.get_legend_handles_labels()
    ax1_pos = ax1.get_position().xmin

    fig.legend(handles, ["Precise", "Approximate"], loc='upper center', bbox_to_anchor=(0.5 + (ax1_pos / 2) - 0.01 / 2, 1), ncols=2)

    output(fig, "matching_types")