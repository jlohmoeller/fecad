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

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        if "fib_4_availability" in key[1]:
            data_available_times = list(map(get_item, data[key]))
        
    return categories, data_available_times

def extract_error_data(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    data_available_times = []

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        if "fib_4_availability" in key[1]:
            data_available_times = list(map(lambda x: x.combinedMetrics.get("Compute Time") / (1000 * 60), data[key]))
        
    return data_available_times

def plot(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    categories, available_client = extract_data(data, lambda x: x.clientCosts.time.get("Query Execution") / (1000 * 60))
    categories, available_server = extract_data(data, lambda x: x.serverCosts.time.get("Query Processing") / (1000 * 60))
    available_error = extract_error_data(data)

    fig, ax = plt.subplots()
    fig.set_size_inches(text_width_in, 2.0)
    fig.subplots_adjust()


    plot_stacked_bar_data(
        ax=ax,
        categories=list(map(lambda x: f"{round(x / 1000)}k", categories)),
        subcategories=["Server", "Client"],
        data=[available_server, available_client],
        colors=[server_color, client_color],
        width=0.65,
        plot_error=False,
        # hatches=['///////', '///////']
    )

    def get_values(l):
        return [item.mean for item in l]
    
    def get_bounds(l):
        return [[item.mean - item.lower_bound for item in l], [item.upper_bound - item.mean for item in l]]

    idx = np.arange(len(categories))
    ax.errorbar(idx, get_values(available_error), yerr=get_bounds(available_error), fmt='none', ecolor='black')

    ax.set_ylim(0, 1.2)
    ax.vlines(len(categories) - 1.5, 0, 1.2, linestyles='--', colors='black')

    ax.set_xlabel("Patient Count [\\#]")
    ax.set_ylabel(r"Runtime [min]")

    fig.tight_layout()
    legend = fig.legend(frameon=True, ncols=2, loc='upper center', bbox_to_anchor=(0.53, 1.1), edgecolor='black')
    legend.get_frame().set_linewidth(0.75)


    output(fig, "fib4_query_runtime")
