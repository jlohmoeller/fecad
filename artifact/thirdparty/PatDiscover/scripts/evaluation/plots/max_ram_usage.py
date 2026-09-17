import numpy as np
import matplotlib.pyplot as plt

from common.load_data import AvgCombinedData, EncryptionType
from common.plot import output, text_width_in
from common.colors import *


def prepare_data(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    boolean_data = []
    enum_precise_data = []
    enum_approx_data = []
    continuous_precise_data = []
    continuous_approx_data = []
    distance_precise_data = []
    distance_approx_data = []

    for key in data:
        if key[2] != EncryptionType.CIPHERTEXT_REAL:
            continue

        client_ram = data[key][0].clientCosts.maxRamUsage / (1024 * (1000 ** 2))
        server_ram = data[key][0].serverCosts.maxRamUsage / (1024 * (1000 ** 2))
        patient_storage = data[key][0].serverCosts.storageSize / (1000 ** 2)

        if "boolean_only" in key[1]:
            boolean_data = [server_ram, client_ram, patient_storage]
        elif "enum_precise_only.json" in key[1]:
            enum_precise_data = [server_ram, client_ram, patient_storage]
        elif "enum_approx_only.json" in key[1]:
            enum_approx_data = [server_ram, client_ram, patient_storage]
        elif "continuous_precise_only.json" in key[1]:
            continuous_precise_data = [server_ram, client_ram, patient_storage]
        elif "continuous_approx_only.json" in key[1]:
            continuous_approx_data = [server_ram, client_ram, patient_storage]
        elif "distance_precise_only.json" in key[1]:
            distance_precise_data = [server_ram, client_ram, patient_storage]
        elif "distance_approx_only.json" in key[1]:
            distance_approx_data = [server_ram, client_ram, patient_storage]

    all_data = [boolean_data, enum_precise_data, enum_approx_data, continuous_precise_data,
                continuous_approx_data, distance_precise_data, distance_approx_data]

    values = [[item[i].mean for item in all_data] for i in range(2)]
    bounds = [[(item[i].mean - item[i].lower_bound, item[i].upper_bound - item[i].mean) for item in all_data] for i in range(2)]
    storage_values = [item[2].mean for item in all_data]
    storage_bounds = [(item[2].mean - item[2].lower_bound, item[2].upper_bound - item[2].mean) for item in all_data]

    return values, bounds, storage_values, storage_bounds

def plot(data: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]):
    fig, ax = plt.subplots()
    fig.set_size_inches(text_width_in, 1.3)
    fig.subplots_adjust(left=0.085, right=0.885, bottom=0.1875, top=0.85)

    storage_ax = ax.twinx()

    colors = [server_color, client_color]
    labels = ["Discovery Server", "Client"]
    categories = ["Bool", "Enum\nPrecise", "Enum\nApprox.", "Range\nPrecise", "Range\nApprox.", "Distance\nPrecise", "Distance\nApprox."]

    index = np.arange(len(categories))
    bar_width = 0.25

    values, bounds, storage_values, storage_bounds = prepare_data(data)

    for i in range(2):
        ax.bar(index - bar_width / 2 + i * bar_width, values[i], bar_width, yerr=bounds[i], label=labels[i], color=colors[i])

    storage_ax.scatter(index, storage_values, marker="x", label="Storage", color=attribute_size_color, s=10, linewidth=1)
    storage_ax.set_ylabel("Disk Storage [MB]")

    ax.set_xticks(index)
    ax.set_xticklabels(categories)
    ax.set_ylabel("Maximum Memory [GB]")

    handles, _ = ax.get_legend_handles_labels()
    storage_handles, _ = storage_ax.get_legend_handles_labels()

    ax_pos_min = ax.get_position().xmin
    ax_pos_max = ax.get_position().xmax

    fig.legend([*handles, *storage_handles], [*labels, "Attribute Size"], loc='upper center', bbox_to_anchor=(ax_pos_min + (ax_pos_max - ax_pos_min) / 2, 1), ncols=3)

    output(fig, "max_ram_usage")
