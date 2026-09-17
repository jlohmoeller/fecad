import pathlib

from common.load_data import average_data, build_grouped_for_progressing_patient_count, load_data, register_statistics_variables, test_id
from common.plot import graph_folder
from common.latex_export import store_variables
from common.parameters import eval_iterations

import max_ram_usage
import matching_types
import communication_costs
import repr_query_runtime
import precision
import explorys
import fib4

data = load_data(test_id)
avg_data = average_data(data, eval_iterations)
register_statistics_variables(avg_data)
grouped_for_prog_patient_count = build_grouped_for_progressing_patient_count(avg_data)

p = pathlib.Path(graph_folder)
p.mkdir(parents=True, exist_ok=True)

matching_types.plot(grouped_for_prog_patient_count)
communication_costs.plot(grouped_for_prog_patient_count)
repr_query_runtime.plot(grouped_for_prog_patient_count)
max_ram_usage.plot(grouped_for_prog_patient_count)
precision.plot(data)
explorys.plot(grouped_for_prog_patient_count)
fib4.plot(grouped_for_prog_patient_count)

store_variables("eval_values.tex")
