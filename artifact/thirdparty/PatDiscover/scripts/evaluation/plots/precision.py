import matplotlib.pyplot as plt

from common.load_data import CombinedData, EncryptionType, extract_query_results
from common.plot import plot_histogram
from common.plot import output, text_width_in
from common.latex_export import register_variable


def plot(data: list[CombinedData]):
    enum_precise = []
    enum_approx = []
    continuous_precise = []
    continuous_approx = []
    distance_precise = []
    distance_approx = []

    enum_approx_max_error = 0
    continuous_approx_max_error = 0
    distance_approx_max_error = 0
    repr_approx_max_error = 0

    grouped: dict[tuple[str, str, int, str], list[CombinedData]] = dict()
    for item in data:
        if item.encryptionType == EncryptionType.CIPHERTEXT_TOY:
            continue

        key = (item.attributeConfig, item.query, item.patientCount, item.dataHash)
        if key not in grouped:
            grouped[key] = []

        if item.encryptionType == EncryptionType.CIPHERTEXT_REAL:
            grouped[key].insert(0, item)
        elif item.encryptionType == EncryptionType.PLAINTEXT:
            grouped[key].insert(1, item)

    for key in grouped:
        t = []
        if "enum_precise_only.json" in key[1]:
            t = ["EnumPrecise"]
        elif "enum_approx_only.json" in key[1]:
            t = ["EnumApprox"]
        elif "continuous_precise_only.json" in key[1]:
            t = ["ContinuousPrecise"]
        elif "continuous_approx_only.json" in key[1]:
            t = ["ContinuousApprox"]
        elif "distance_precise_only.json" in key[1]:
            t = ["DistancePrecise"]
        elif "distance_approx_only.json" in key[1]:
            t = ["DistanceApprox"]
        elif "default_approx.json" in key[1]:
            t = ["EnumApprox", "ContinuousApprox", "DistanceApprox"]

        if len(t) == 0:
            continue

        if len(grouped[key]) != 2:
            raise RuntimeError()

        first_data = extract_query_results(grouped[key][0].runId, t)
        second_data = extract_query_results(grouped[key][1].runId, t)

        difference = []
        for pat_id in first_data:
            new_diffs = [v1 - v2 for v1, v2 in zip(first_data[pat_id], second_data[pat_id])]
            difference += new_diffs

            if not "default_approx.json" in key[1]:
                for item in first_data[pat_id]:
                    if abs(item) > 0.0022 and abs(1.0 - item) > 0.0022:
                        print(f"ERROR (first data): {item}")
                
                for item in second_data[pat_id]:
                    if abs(item) > 0.0022 and abs(1.0 - item) > 0.0022:
                        print(f"ERROR (second data): {item}")

        max_diff = max(map(lambda x: abs(x), difference))
        if "enum_precise_only.json" in key[1]:
            print(f"EnumPrecise: {max_diff}")
            enum_precise += difference
        elif "enum_approx_only.json" in key[1]:
            print(f"EnumApprox: {max_diff}")
            enum_approx += difference
            enum_approx_max_error = max(max_diff, enum_approx_max_error)
        elif "continuous_precise_only.json" in key[1]:
            print(f"RangePrecise: {max_diff}")
            continuous_precise += difference
        elif "continuous_approx_only.json" in key[1]:
            print(f"RangeApprox: {max_diff}")
            continuous_approx += difference
            continuous_approx_max_error = max(max_diff, continuous_approx_max_error)
        elif "distance_precise_only.json" in key[1]:
            print(f"DistancePrecise: {max_diff}")
            distance_precise += difference
        elif "distance_approx_only.json" in key[1]:
            print(f"DistanceApprox: {max_diff}")
            distance_approx += difference
            distance_approx_max_error = max(max_diff, distance_approx_max_error)
        elif "default_approx.json" in key[1]:
            print(f"DefaultApprox: {max_diff}")
            repr_approx_max_error = max(max_diff, repr_approx_max_error)


    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, sharex=True, sharey=True)
    fig.set_size_inches(text_width_in, 1.3)
    fig.subplots_adjust(left=0.12, right=0.99, bottom=0.225, top=0.78, wspace=0.1)

    plot_histogram(ax=ax1, data_precise=enum_precise, data_approx=enum_approx, title="Enum", x_label=True, y_label=True)
    plot_histogram(ax=ax2, data_precise=continuous_precise, data_approx=continuous_approx, title="Range",x_label=True, y_label=False)
    plot_histogram(ax=ax3, data_precise=distance_precise, data_approx=distance_approx, title="Distance", x_label=True, y_label=False)

    handles, _ = ax1.get_legend_handles_labels()
    ax1_pos = ax1.get_position().xmin

    fig.legend(handles, ["Precise", "Approximate"], loc='upper center', bbox_to_anchor=(0.5 + (ax1_pos / 2) - 0.01 / 2, 1), ncols=2)

    output(fig, "precision")

    register_variable("maxAbsErrorEnumApprox", enum_approx_max_error, precision=5)
    register_variable("maxAbsErrorRangeApprox", continuous_approx_max_error, precision=5)
    register_variable("maxAbsErrorDistanceApprox", distance_approx_max_error, precision=5)
    register_variable("maxAbsErrorRepresentativeQueryApprox", repr_approx_max_error, precision=5)