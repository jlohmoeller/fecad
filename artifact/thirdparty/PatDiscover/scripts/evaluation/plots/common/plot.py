import typing
import numpy as np
import matplotlib.pyplot as plt

from .load_data import ConfidenceInfo
from .colors import *
from .parameters import mode_show

graph_folder = "graphs"
file_extension = "pdf"

# Figure width (to match LaTeX text width) in inches
text_width_in = 4.8041

default_settings = {
    'patch.linewidth': 1.0,
    'errorbar.capsize': 2,
    'lines.linewidth': 0.5,
    'lines.markeredgewidth': 0.5,
    "hatch.linewidth": 0.1
}

# plt.style.use('seaborn-v0_8-muted')
plt.rcParams.update(default_settings)

"""
-------------------------------------------
---------------- Plotting -----------------
-------------------------------------------
"""

def plot_stacked_bar_data(ax: typing.Any, categories: list[str], subcategories: list[str], data: list[list[ConfidenceInfo]], colors: list[str], offset: float = 0, width=1.0, plot_error=True, hatches=None) -> None:
    n = len(categories)
    index = np.arange(n)

    values = [[item.mean for item in l] for l in data]

    if hatches is None:
        hatches = [' ' for _ in range(len(subcategories))]

    if plot_error:
        bounds = [
            [
                [item.mean - item.lower_bound for item in l],
                [item.upper_bound - item.mean for item in l]
            ]
            for l in data
        ]

    bottom_values = np.zeros(n)
    for i in range(len(subcategories)):
        if plot_error:
            ax.bar(index + offset, values[i], bottom=bottom_values, label=subcategories[i], yerr=bounds[i], color=colors[i], width=width, hatch=hatches[i])
        else:
            ax.bar(index + offset, values[i], bottom=bottom_values, label=subcategories[i], color=colors[i], width=width, hatch=hatches[i])
        
        bottom_values += values[i]

    ax.set_xticks(index)
    ax.set_xticklabels(categories)

def plot_histogram(ax: typing.Any, data_precise: list[float], data_approx: list[float], title: str, x_label: bool, y_label: bool):
    ax.hist(data_precise, bins=30, weights=np.zeros_like(data_precise) + 1.0 / len(data_precise), range=(-0.00025, 0.00025), label="Precise", color=precise_color)
    ax.hist(data_approx, bins=30, weights=np.zeros_like(data_approx) + 1.0 / len(data_approx), range=(-0.00025, 0.00025), label="Approx", color=approx_color)
    ax.set_title(title, pad=2.5)

    ax.set_xticks([-0.0002, 0.000, 0.0002])

    if x_label:
        ax.set_xlabel('Absolute Error')

    if y_label:
        ax.set_ylabel('Frequency')

def output(fig: plt.Figure, filename: str | None = None) -> None:
    if mode_show:
        fig.show()
    else:
        fig.savefig(f"{graph_folder}/{filename}.{file_extension}")