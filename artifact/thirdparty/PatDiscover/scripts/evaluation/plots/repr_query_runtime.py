import typing
import matplotlib.pyplot as plt
import numpy as np

from common.load_data import AvgCombinedData, EncryptionType
from common.plot import output, plot_stacked_bar_data, text_width_in
from common.colors import *


def e2e_data_extraction(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]],
                        pred: typing.Callable[[tuple[str, str, EncryptionType]], bool]):
    categories = []
    query_client = []
    query_server = []
    combined_data = []

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        if not pred(key):
            continue

        query_client = list(map(lambda x: x.clientCosts.time["Query Execution"] / (1000 * 60), data[key]))
        query_server = list(map(lambda x: x.serverCosts.time["Combined Query"] / (1000 * 60), data[key]))
        combined_data = list(map(lambda x: x.combinedMetrics["Compute Time"] / (1000 * 60), data[key]))

        categories = list(map(lambda x: x.patientCount, data[key]))

    return categories, query_client, query_server, combined_data

def plot(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    subcategories_query = ["Discovery Server", "Client"]

    fig, (ax1, ax2) = plt.subplots(1, 2)
    fig.set_size_inches(text_width_in, 2.0)
    # fig.subplots_adjust(left=0.1, right=0.99, bottom=0.2, top=0.8, wspace=0.3)

    categories, query_client, query_server, processing_time = e2e_data_extraction(data=data, pred=lambda x: "default_precise.json" in x[1])
    values_query = [query_server, query_client]

    def get_values(l):
        return [item.mean for item in l]
    
    def get_bounds(l):
        return [[item.mean - item.lower_bound for item in l], [item.upper_bound - item.mean for item in l]]

    idx = np.arange(len(categories))
    plot_stacked_bar_data(
        ax=ax1,
        categories=list(map(lambda x: f"{round(x / 1000)}k", categories)),
        subcategories=subcategories_query,
        data=values_query,
        colors=[server_color, client_color],
        width=0.65,
        plot_error=False,
    )
    ax1.errorbar(idx, get_values(processing_time), yerr=get_bounds(processing_time), fmt='none', ecolor='black')
    
    ax1.set_xlabel("Patient Count [\\#]")
    ax1.set_ylabel(r"Runtime [min]")
    ax1.set_title("Precise")

    categories, query_client, query_server, processing_time = e2e_data_extraction(data=data, pred=lambda x: "default_approx.json" in x[1])
    values_query = [query_server, query_client]

    plot_stacked_bar_data(
        ax=ax2,
        categories=list(map(lambda x: f"{round(x / 1000)}k", categories)),
        subcategories=subcategories_query,
        data=values_query,
        colors=[server_color, client_color],
        width=0.65,
        plot_error=False,
    )
    ax2.errorbar(idx, get_values(processing_time), yerr=get_bounds(processing_time), fmt='none', ecolor='black')

    ax2.set_xlabel("Patient Count [\\#]")
    ax2.set_ylabel(r"Runtime [min]")
    ax2.set_title("Approximate")

    handles, _ = ax1.get_legend_handles_labels()

    fig.legend(handles, subcategories_query, frameon=True, loc='upper center', bbox_to_anchor=(0.53, 1.1), ncols=2)
    fig.tight_layout()

    output(fig, "repr_query_runtime")
