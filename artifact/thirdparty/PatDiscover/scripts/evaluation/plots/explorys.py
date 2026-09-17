import typing
import numpy as np
import matplotlib.pyplot as plt

from common.load_data import AvgCombinedData, ConfidenceInfo, EncryptionType
from common.plot import output, plot_stacked_bar_data, text_width_in
from common.colors import *
from common.parameters import bfv_batch_size, ckks_batch_size


def extract_data(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]], get_item: typing.Callable[[AvgCombinedData], ConfidenceInfo]):
    categories = [100_000, 200_000, 300_000, 400_000, 500_000, 600_000, 674_963]

    data_available_times = []
    hcc_times = []
    complete_times = []

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        if "explorys_data_available" in key[1]:
            data_available_times = list(map(get_item, data[key]))

        if "explorys_hcc" in key[1]:
            hcc_times = list(map(get_item, data[key]))
        
        if "explorys_complete" in key[1]:
            complete_times = list(map(get_item, data[key]))
        
    return categories, data_available_times, hcc_times, complete_times

def extract_error_data(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    data_available_times = []
    hcc_times = []
    complete_times = []

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        if "explorys_data_available" in key[1]:
            data_available_times = list(map(lambda x: x.combinedMetrics.get("Compute Time") / (1000 * 60), data[key]))

        if "explorys_hcc" in key[1]:
            hcc_times = list(map(lambda x: x.combinedMetrics.get("Compute Time") / (1000 * 60), data[key]))
        
        if "explorys_complete" in key[1]:
            complete_times = list(map(lambda x: x.combinedMetrics.get("Compute Time") / (1000 * 60), data[key]))
        
    return data_available_times, hcc_times, complete_times

def plot(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    categories, available_client, hcc_client, complete_client = extract_data(data, lambda x: x.clientCosts.time.get("Query Execution") / (1000 * 60))
    categories, available_server, hcc_server, complete_server = extract_data(data, lambda x: x.serverCosts.time.get("Query Processing") / (1000 * 60))
    available_error, hcc_error, complete_error = extract_error_data(data)

    fig, ax = plt.subplots()
    fig.set_size_inches(text_width_in, 2.0)
    fig.subplots_adjust()


    plot_stacked_bar_data(
        ax=ax,
        categories=categories,
        subcategories=["Available - Server", "Available - Client"],
        data=[available_server, available_client],
        colors=[blend_color(server_color, 0.3), blend_color(client_color, 0.3)],
        offset=-0.25,
        width=0.25,
        plot_error=False,
        hatches=['///////', '///////']
    )

    plot_stacked_bar_data(
        ax=ax,
        categories=categories,
        subcategories=["Diseased - Server", "Diseased - Client"],
        data=[hcc_server, hcc_client],
        colors=[blend_color(server_color, 0.65), blend_color(client_color, 0.65)],
        offset=0.0,
        width=0.25,
        plot_error=False,
        hatches=['\\\\\\\\\\\\\\', '\\\\\\\\\\\\\\']
    )

    plot_stacked_bar_data(
        ax=ax,
        categories=list(map(lambda x: f"{round(x / 1000)}k", categories)),
        subcategories=["Combined - Server", "Combined - Client"],
        data=[complete_server, complete_client],
        colors=[blend_color(server_color, 1.0), blend_color(client_color, 1.0)],
        offset=0.25,
        width=0.25,
        plot_error=False,
        hatches=['xxxxxxx', 'xxxxxxx']
    )

    def get_values(l):
        return [item.mean for item in l]
    
    def get_bounds(l):
        return [[item.mean - item.lower_bound for item in l], [item.upper_bound - item.mean for item in l]]

    idx = np.arange(len(categories))
    ax.errorbar(idx - 0.25, get_values(available_error), yerr=get_bounds(available_error), fmt='none', ecolor='black')
    ax.errorbar(idx + 0.0, get_values(hcc_error), yerr=get_bounds(hcc_error), fmt='none', ecolor='black')
    ax.errorbar(idx + 0.25, get_values(complete_error), yerr=get_bounds(complete_error), fmt='none', ecolor='black')

    ax.set_ylim(0, 10)
    ax.vlines(len(categories) - 1.5, 0, 10, colors='black', linestyles='--')

    ax.set_xlabel("Patient Count [\\#]")
    ax.set_ylabel(r"Runtime [min]")

    fig.tight_layout()
    fig.legend(frameon=True, ncols=3, loc='upper center', bbox_to_anchor=(0.53, 1.175),)

    output(fig, "cohort_query_runtime")
