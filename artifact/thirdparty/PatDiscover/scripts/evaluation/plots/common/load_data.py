import json
import numpy as np
import os
import typing

from scipy.stats import linregress, bootstrap

from dataclasses import dataclass
from enum import Enum
from scipy import stats

from .latex_export import register_variable
from .parameters import test_id, confidence

from collections import defaultdict

class EncryptionType(Enum):
    CIPHERTEXT_REAL = 0
    CIPHERTEXT_TOY = 1
    PLAINTEXT = 2

"""
-------------------------------------------
----------- Classes for Parsing -----------
-------------------------------------------
"""

@dataclass
class ClientData:
    attributeConfig: str
    query: str
    patientCount: int
    timings: dict[str, int]
    communicationCosts: dict[str, int]
    plaintext: bool
    dataHash: str
    maxRamUsage: int


@dataclass
class ServerData:
    attributeConfig: str
    storageSize: int
    timings: dict[str, int]
    communicationCosts: dict[str, int]
    plaintext: bool
    maxRamUsage: int


@dataclass
class TaData:
    storageSize: int
    timings: dict[str, int]
    communicationCosts: dict[str, int]
    toyParameters: bool
    maxRamUsage: int


@dataclass
class Costs:
    time: dict[str, int]
    communication: dict[str, int]
    storageSize: typing.Optional[int]
    maxRamUsage: int


@dataclass
class CombinedData:
    runId: typing.Optional[str]
    attributeConfig: str
    query: str
    dataHash: str
    patientCount: int
    clientCosts: Costs
    serverCosts: Costs
    taCosts: typing.Optional[Costs]
    combinedMetrics: dict[str, int]
    encryptionType: EncryptionType

"""
-------------------------------------------
------- Classes for Aggregate Data --------
-------------------------------------------
"""

@dataclass
class ConfidenceInfo:
    mean: float
    lower_bound: float
    upper_bound: float

    def __truediv__(self, other):
        if isinstance(other, float) or isinstance(other, int):
            return ConfidenceInfo(
                mean=self.mean / other, 
                lower_bound=self.lower_bound / other,
                upper_bound=self.upper_bound / other,
            )


@dataclass
class AvgCosts:
    time: dict[str, ConfidenceInfo]
    communication: dict[str, ConfidenceInfo]
    storageSize: typing.Optional[ConfidenceInfo]
    maxRamUsage: ConfidenceInfo


@dataclass
class AvgCombinedData:
    runId: typing.Optional[str]
    attributeConfig: str
    query: str
    dataHash: str
    patientCount: int
    clientCosts: AvgCosts
    serverCosts: AvgCosts
    taCosts: typing.Optional[AvgCosts]
    combinedMetrics: dict[str, ConfidenceInfo]
    encryptionType: EncryptionType

"""
-------------------------------------------
--------- Loading and Aggregation ---------
-------------------------------------------
"""

def calculate_conf_int(samples: list[float], conf: float):
    if len(np.unique(samples)) == 1:
        return ConfidenceInfo(mean=samples[0], lower_bound=samples[0], upper_bound=samples[0])

    m = float(np.mean(samples))
    bt_res = bootstrap((np.array(samples),), np.mean, confidence_level=conf)

    return ConfidenceInfo(mean=m, lower_bound=bt_res.confidence_interval.low, upper_bound=bt_res.confidence_interval.high)


def add_for_agg(data: Costs, timing_dict: dict[str, list[int]], cost_dict: dict[str, list[int]]):
    for timing_label in data.time:
        if timing_label not in timing_dict:
            timing_dict[timing_label] = []
        timing_dict[timing_label].append(data.time[timing_label])

    for com_cost_label in data.communication:
        if com_cost_label not in cost_dict:
            cost_dict[com_cost_label] = []
        cost_dict[com_cost_label].append(data.communication[com_cost_label])


def calc_average(timing_dict: dict[str, list[int]], cost_dict: dict[str, list[int]], conf: float):
    time_res: dict[str, ConfidenceInfo] = dict()
    cost_res: dict[str, ConfidenceInfo] = dict()

    for key in timing_dict:
        time_res[key] = calculate_conf_int(timing_dict[key], conf)

    for key in cost_dict:
        cost_res[key] = calculate_conf_int(cost_dict[key], conf)

    return time_res, cost_res


def load_data(test_id: str) -> list[CombinedData]:
    # Result directories
    client_data_dir = f"test-results/Client/{test_id}"
    server_data_dir = f"test-results/Server/{test_id}"
    ta_data_dir = f"test-results/TA/{test_id}"

    client_data = dict()
    server_data = dict()
    ta_data = dict()

    for file in os.listdir(client_data_dir):
        with open(f"{client_data_dir}/{file}") as f:
            j = json.load(f)
            run_id = j["runId"]
            attr_config = j["attributeConfig"]
            query = j["query"]
            patient_count = j["patientCount"]
            timings = j["timings"]
            communication_costs = j["communicationCost"]
            plaintext = j["plaintext"]
            data_hash = j["patientDataHash"]
            max_ram_usage = j["maxRamUsage"]
            client_data[run_id] = ClientData(
                attributeConfig=attr_config,
                query=query,
                patientCount=patient_count,
                timings=timings,
                communicationCosts=communication_costs,
                plaintext=plaintext,
                dataHash=data_hash,
                maxRamUsage=max_ram_usage,
            )

    for file in os.listdir(server_data_dir):
        with open(f"{server_data_dir}/{file}") as f:
            j = json.load(f)
            run_id = j["runId"]
            attr_config = j["attributeConfig"]
            communication_costs = j["communicationCost"]
            timings = j["timings"]
            plaintext = j["plaintext"]
            storage_size = j["storageSize"]
            max_ram_usage = j["maxRamUsage"]
            server_data[run_id] = ServerData(
                attributeConfig=attr_config,
                storageSize=storage_size,
                timings=timings,
                communicationCosts=communication_costs,
                plaintext=plaintext,
                maxRamUsage=max_ram_usage,
            )

    for file in os.listdir(ta_data_dir):
        with open(f"{ta_data_dir}/{file}") as f:
            j = json.load(f)
            run_id = j["runId"]
            communication_costs = j["communicationCost"]
            timings = j["timings"]
            storage_size = j["storageSize"]
            toy_parameters = j["toyParameters"]
            max_ram_usage = j["maxRamUsage"]
            ta_data[run_id] = TaData(
                storageSize=storage_size,
                timings=timings,
                communicationCosts=communication_costs,
                toyParameters=toy_parameters,
                maxRamUsage=max_ram_usage,
            )

    combined_data: list[CombinedData] = list()
    for key in client_data.keys():
        encryption_type = EncryptionType.CIPHERTEXT_REAL
        if client_data[key].plaintext:
            encryption_type = EncryptionType.PLAINTEXT
        if key in ta_data and ta_data[key].toyParameters:
            encryption_type = EncryptionType.CIPHERTEXT_TOY

        if key in ta_data:
            ta_costs = Costs(
                time=ta_data[key].timings,
                communication=ta_data[key].communicationCosts,
                storageSize=ta_data[key].storageSize,
                maxRamUsage=ta_data[key].maxRamUsage
            )
        else:
            ta_costs = None

        combined_metrics = dict()
        combined_metrics["Compute Time"] = (
            client_data[key].timings.get("Query Execution", 0) + 
            server_data[key].timings.get("Switch CKKS->FHEW", 0) + 
            server_data[key].timings.get("Switch FHEW->CKKS", 0) + 
            server_data[key].timings.get("BinFHE Eval Sign", 0) + 
            server_data[key].timings.get("Query Processing", 0)
        )

        combined_data.append(
            CombinedData(
                runId=key,
                attributeConfig=client_data[key].attributeConfig,
                query=client_data[key].query,
                patientCount=client_data[key].patientCount,
                clientCosts=Costs(
                    time=client_data[key].timings,
                    communication=client_data[key].communicationCosts,
                    storageSize=None,
                    maxRamUsage=client_data[key].maxRamUsage
                ),
                serverCosts=Costs(
                    time=server_data[key].timings,
                    communication=server_data[key].communicationCosts,
                    storageSize=server_data[key].storageSize,
                    maxRamUsage=server_data[key].maxRamUsage
                ),
                taCosts=ta_costs,
                combinedMetrics=combined_metrics,
                encryptionType=encryption_type,
                dataHash=client_data[key].dataHash,
            )
        )

    return combined_data


def average_data(combined_data: list[CombinedData], iterations: int) -> list[AvgCombinedData]:
    # Group so that we can calculate the average on the multi-runs
    combined_data_grouped: dict[tuple[str, str, EncryptionType, int], list[CombinedData]] = {}
    for item in combined_data:
        key = (item.attributeConfig, item.query, item.encryptionType, item.patientCount)
        if key not in combined_data_grouped:
            combined_data_grouped[key] = []
        combined_data_grouped[key].append(item)

    combined_data_averaged = list()
    for key in combined_data_grouped:
        if len(combined_data_grouped[key]) != iterations:
            raise RuntimeError("Invalid Number of Iterations")

        client_timings = dict()
        client_communication_costs = dict()
        client_ram_usage = []

        server_timings = dict()
        server_communication_costs = dict()
        server_storage_size = []
        server_ram_usage = []

        ta_timings = dict()
        ta_communication_costs = dict()
        ta_storage_size = []
        ta_ram_usage = []

        combined_metrics_group = defaultdict(list)

        for item in combined_data_grouped[key]:
            add_for_agg(item.clientCosts, client_timings, client_communication_costs)
            add_for_agg(item.serverCosts, server_timings, server_communication_costs)

            if item.taCosts:
                add_for_agg(item.taCosts, ta_timings, ta_communication_costs)

            server_storage_size.append(item.serverCosts.storageSize)
            client_ram_usage.append(item.clientCosts.maxRamUsage)
            server_ram_usage.append(item.serverCosts.maxRamUsage)

            if "Switch CKKS->FHEW" in item.serverCosts.time and "Switch FHEW->CKKS" in item.serverCosts.time:
                if "Switching" not in server_timings:
                    server_timings["Switching"] = []
                server_timings["Switching"].append(item.serverCosts.time["Switch CKKS->FHEW"] + item.serverCosts.time["Switch FHEW->CKKS"])

            if "Query Processing" in item.serverCosts.time:
                if "Combined Query" not in server_timings:
                    server_timings["Combined Query"] = []
                server_timings["Combined Query"].append(
                    item.serverCosts.time.get("Switch CKKS->FHEW", 0) +
                    item.serverCosts.time.get("Switch FHEW->CKKS", 0) +
                    item.serverCosts.time.get("BinFHE Eval Sign", 0) +
                    item.serverCosts.time["Query Processing"]
                )

            if item.taCosts:
                ta_storage_size.append(item.taCosts.storageSize)
                ta_ram_usage.append(item.taCosts.maxRamUsage)

                if "Context Generation" in item.taCosts.time and "Context Serving" in item.taCosts.time:
                    if "Context Initialization" in item.serverCosts.time and "Context Initialization" in item.clientCosts.time:
                        if "Init" not in ta_timings:
                            ta_timings["Init"] = []
                        ta_timings["Init"].append(
                            item.taCosts.time["Context Generation"] +
                            item.taCosts.time["Context Serving"] +
                            item.serverCosts.time["Context Initialization"] +
                            item.clientCosts.time["Context Initialization"]
                        )
            
            for cmb_metrics_key in item.combinedMetrics:
                combined_metrics_group[cmb_metrics_key].append(item.combinedMetrics[cmb_metrics_key])

        cl_time, cl_costs = calc_average(client_timings, client_communication_costs, confidence)
        serv_time, serv_costs = calc_average(server_timings, server_communication_costs, confidence)
        serv_storage_size = calculate_conf_int(server_storage_size, confidence)
        cl_ram_usage = calculate_conf_int(client_ram_usage, confidence)
        serv_ram_usage = calculate_conf_int(server_ram_usage, confidence)

        first = combined_data_grouped[key][0]

        cc = AvgCosts(
            time=cl_time,
            communication=cl_costs,
            storageSize=None,
            maxRamUsage=cl_ram_usage
        )
        sc = AvgCosts(
            time=serv_time,
            communication=serv_costs,
            storageSize=serv_storage_size,
            maxRamUsage=serv_ram_usage
        )

        tc = None
        if first.taCosts:
            t_time, t_costs = calc_average(ta_timings, ta_communication_costs, confidence)
            t_storage_size = calculate_conf_int(ta_storage_size, confidence)
            t_ram_usage = calculate_conf_int(ta_ram_usage, confidence)
            tc = AvgCosts(
                time=t_time,
                communication=t_costs,
                storageSize=t_storage_size,
                maxRamUsage=t_ram_usage
            )
        
        averaged_combined_metrics = dict()
        for cmb_metrics_key in combined_metrics_group:
            averaged_combined_metrics[cmb_metrics_key] = calculate_conf_int(combined_metrics_group[cmb_metrics_key], confidence)

        combined_data_averaged.append(
            AvgCombinedData(
                runId=None,
                attributeConfig=key[0],
                query=key[1],
                patientCount=key[3],
                clientCosts=cc,
                serverCosts=sc,
                taCosts=tc,
                combinedMetrics=averaged_combined_metrics,
                encryptionType=key[2],
                dataHash=first.dataHash,
            )
        )

    return combined_data_averaged


def register_statistics_variables(data: list[AvgCombinedData]):
    precise_data = []
    approx_data = []

    counter = 0
    client_query_percentage_approx = 0
    for item in data:
        if item.encryptionType != EncryptionType.CIPHERTEXT_REAL:
            continue

        if "default_precise" in item.attributeConfig:
            precise_data.append(
                (item.patientCount,
                 (item.clientCosts.time["Query Execution"].mean + item.serverCosts.time["Combined Query"].mean) / (1000 * 60))
                )
            if item.patientCount == 100_000:
                register_variable("patientStoragePrecise100k", item.serverCosts.storageSize.mean / (1000 ** 3), unit="giga\\byte")
                register_variable("clientQueryTimePrecise100k", item.clientCosts.time["Query Execution"].mean / 1000, unit="second")
            elif item.patientCount == 500_000:
                register_variable("taKeySizeReprQueryPrecise", item.taCosts.storageSize.mean / (1000 ** 3), unit="giga\\byte")
                register_variable("maxRamReprQueryPrecise500k", item.serverCosts.maxRamUsage.mean / (1024 * (1000 ** 2)), unit="giga\\byte")

        if "default_approx" in item.attributeConfig:
            counter += 1
            client_query_percentage_approx += item.clientCosts.time["Query Execution"].mean / (
                        item.clientCosts.time["Query Execution"].mean + item.serverCosts.time["Combined Query"].mean)

            approx_data.append(
                (item.patientCount,
                 (item.clientCosts.time["Query Execution"].mean + item.serverCosts.time["Combined Query"].mean) / (1000 * 60))
                )

            if item.patientCount == 100_000:
                register_variable("patientStorageApprox100k", item.serverCosts.storageSize.mean / (1000 ** 3), unit="giga\\byte")
                register_variable("clientQueryTimeApprox100k", item.clientCosts.time["Query Execution"].mean / 1000, unit="second")
            if item.patientCount == 500_000:
                register_variable("taKeySizeReprQueryApprox", item.taCosts.storageSize.mean / (1000 ** 3), unit="giga\\byte")
                register_variable("maxRamReprQueryApprox500k", item.serverCosts.maxRamUsage.mean / (1024 * (1000 ** 2)), unit="giga\\byte")

        if "distance_precise_only" in item.attributeConfig:
            register_variable("distancePreciseStorage", item.serverCosts.storageSize.mean / (1000 ** 2), unit="mega\\byte")

        if "distance_approx_only" in item.attributeConfig:
            register_variable("distanceApproxStorage", item.serverCosts.storageSize.mean / (1000 ** 2), unit="mega\\byte")

    register_variable("clientQueryPercentageApprox", client_query_percentage_approx / counter * 100, unit="percent")

    register_variable("booleanPREKeySize", 4_720_051 / (1000 ** 2), unit="mega\\byte")
    register_variable("enumPrecisePREKeySize", 75_508_697 / (1000 ** 2), unit="mega\\byte")
    register_variable("rangePrecisePREKeySize", 102_775_297 / (1000 ** 2), unit="mega\\byte")
    register_variable("distancePrecisePREKeySize", 102_775_297 / (1000 ** 2), unit="mega\\byte")

    register_variable("enumApproxPREKeySize", 125_836_579 / (1000 ** 2), unit="mega\\byte")
    register_variable("rangeApproxPREKeySize", 169_879_085 / (1000 ** 2), unit="mega\\byte")
    register_variable("distanceApproxPREKeySize", 220_213_453 / (1000 ** 2), unit="mega\\byte")

    register_variable("booleanEvalKeySize", 4_720_107 / (1000 ** 2), unit="mega\\byte")
    register_variable("enumPreciseEvalKeySize", 80_228_639 / (1000 ** 2), unit="mega\\byte")
    register_variable("rangePreciseEvalKeySize", 183_003_771 / (1000 ** 2), unit="mega\\byte")
    register_variable("distancePreciseEvalKeySize", 285_777_843 / (1000 ** 2), unit="mega\\byte")

    register_variable("enumApproxEvalKeySize", 125, unit="mega\\byte")
    register_variable("rangeApproxEvalKeySize", 287, unit="mega\\byte")
    register_variable("distanceApproxEvalKeySize", 497, unit="mega\\byte")

    precise_data.sort(key=lambda x: x[0])
    approx_data.sort(key=lambda x: x[0])

    precise_data_x = [item[0] for item in precise_data]
    precise_data_y = [item[1] for item in precise_data]

    approx_data_x = [item[0] for item in approx_data]
    approx_data_y = [item[1] for item in approx_data]

    p_slope, _, p_r, _, _ = linregress(precise_data_x, precise_data_y)
    a_slope, _, a_r, _, _ = linregress(approx_data_x, approx_data_y)

    register_variable("preciseCombinedQuerySlope", p_slope, precision=6)
    register_variable("approxCombinedQuerySlope", a_slope, precision=6)
    register_variable("preciseCombinedQueryR2", p_r * p_r, precision=2)
    register_variable("approxCombinedQueryR2", a_r * a_r, precision=2)


def build_grouped_for_progressing_patient_count(data: list[AvgCombinedData]) -> dict[tuple[str, str, EncryptionType], list[AvgCombinedData]]:
    grouped_for_prog_patient_count: dict[tuple[str, str, EncryptionType], list[AvgCombinedData]] = dict()
    for item in data:
        key = (item.attributeConfig, item.query, item.encryptionType)
        if key not in grouped_for_prog_patient_count:
            grouped_for_prog_patient_count[key] = []
        grouped_for_prog_patient_count[key].append(item)

    for key in grouped_for_prog_patient_count:
        grouped_for_prog_patient_count[key].sort(key=lambda x: x.patientCount)

    return grouped_for_prog_patient_count


def extract_query_results(run_id: str, t: list[str]) -> dict[str, list[float]]:
    query_results_dir = f"test-results/query-results/{test_id}"

    result: dict[str, list[float]] = {}
    with open(f"{query_results_dir}/{run_id}.json", "r") as f:
        data = json.load(f)
        if data:
            for item in data:
                r = []
                for attr in t:
                    r.append(float(item[attr]))
                result[item["patientId"]] = r

    return result