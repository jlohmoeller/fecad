import typing
import matplotlib.pyplot as plt

from common.load_data import AvgCombinedData, EncryptionType
from common.plot import output, plot_stacked_bar_data, text_width_in
from common.colors import *


def extract_communication_data(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]],
                               pred: typing.Callable[[tuple[str, str, EncryptionType]], bool]):
    categories = []
    keys_cost = []
    pd_cost = []
    query_cost = []
    query_res_cost = []

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        if pred(key):
            keys_cost = list(map(lambda x: x.taCosts.communication["Context Initialization"] / 1e9, data[key]))
            pd_cost = list(map(lambda x: x.clientCosts.communication["Data Upload"] / 1e9, data[key]))
            query_cost = list(map(lambda x: x.clientCosts.communication["Query Upload"] / 1e9, data[key]))
            query_res_cost = list(map(lambda x: x.serverCosts.communication["Query Results"] / 1e9, data[key]))
            categories = list(map(lambda x: x.patientCount, data[key]))

            break

    return categories, keys_cost, pd_cost, query_cost, query_res_cost


def plot(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    fig, (ax1, ax2) = plt.subplots(nrows = 1, ncols = 2)
    fig.set_size_inches(text_width_in, 2.0)
    # fig.subplots_adjust(left=0.105, right=0.99, bottom=0.2, top=0.8, wspace=0.3)

    subcategories = ["Patient Attributes", "Query", "Query Results"]
    categories, keys_cost, pd_cost, query_cost, query_res_cost = extract_communication_data(data, lambda x: "default_precise.json" in x[0])
    values = [pd_cost, query_cost, query_res_cost]

    plot_stacked_bar_data(
        ax=ax1,
        categories=list(map(lambda x: f"{round(x / 1000)}k", categories)),
        subcategories=subcategories,
        data=values,
        colors=[patient_attribute_color, query_color, result_color],
        width=0.65
    )
    ax1.set_xlabel("Patient Count [\\#]")
    ax1.set_ylabel("Transferred Data [GB]")
    ax1.set_title("Precise")


    categories, keys_cost, pd_cost, query_cost, query_res_cost = extract_communication_data(data, lambda x: "default_approx.json" in x[0])
    values = [pd_cost, query_cost, query_res_cost]
    plot_stacked_bar_data(
        ax=ax2,
        categories=list(map(lambda x: f"{round(x / 1000)}k", categories)),
        subcategories=subcategories,
        data=values,
        colors=[patient_attribute_color, query_color, result_color],
        width=0.65
    )
    ax2.set_xlabel("Patient Count [\\#]")
    ax2.set_ylabel(r"Transferred Data [GB]")
    ax2.set_title("Approximate")

    handles, _ = ax1.get_legend_handles_labels()

    fig.legend(handles, subcategories, frameon=True, loc='upper center', bbox_to_anchor=(0.53, 1.1), ncols=3)
    fig.tight_layout()

    output(fig, "communication_costs")
