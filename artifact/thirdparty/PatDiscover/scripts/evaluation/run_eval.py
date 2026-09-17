import os
import time
import pathlib
import shutil
import subprocess
import sys
import uuid
import json
from datetime import datetime
from enum import Enum

import generate_data

file_path = pathlib.Path(__file__)
os.chdir(str(file_path.parent.parent.parent))
current_test_id = uuid.uuid4().hex
num_iters = 30

database_file = os.path.abspath("db/eval.db")
ta_storage_dir = os.path.abspath("ta-storage")

bfv_batch_size = 32768
ckks_batch_size = 65536
scheme_switch_batch_size = 1024

attribute_default_precise_file = os.path.abspath("data/attribute_config/default_precise.json")
attribute_default_approx_file = os.path.abspath("data/attribute_config/default_approx.json")
attribute_boolean_file = os.path.abspath("data/attribute_config/boolean_only.json")
attribute_enum_precise_file = os.path.abspath("data/attribute_config/enum_precise_only.json")
attribute_enum_approx_file = os.path.abspath("data/attribute_config/enum_approx_only.json")
attribute_continuous_precise_file = os.path.abspath("data/attribute_config/continuous_precise_only.json")
attribute_continuous_approx_file = os.path.abspath("data/attribute_config/continuous_approx_only.json")
attribute_distance_precise_file = os.path.abspath("data/attribute_config/distance_precise_only.json")
attribute_distance_approx_file = os.path.abspath("data/attribute_config/distance_approx_only.json")
attribute_attr_count_file = os.path.abspath("data/attribute_config/attr_count.json")
attribute_query_depth_file = os.path.abspath("data/attribute_config/query_depth.json")
attribute_mimic_file = os.path.abspath("data/attribute_config/mimic.json")
attribute_explorys_file = os.path.abspath("data/attribute_config/explorys.json")

query_boolean_file = os.path.abspath("data/queries/boolean_only.json")
query_enum_precise_file = os.path.abspath("data/queries/enum_precise_only.json")
query_enum_approx_file = os.path.abspath("data/queries/enum_approx_only.json")
query_continuous_precise_file = os.path.abspath("data/queries/continuous_precise_only.json")
query_continuous_approx_file = os.path.abspath("data/queries/continuous_approx_only.json")
query_distance_precise_file = os.path.abspath("data/queries/distance_precise_only.json")
query_distance_approx_file = os.path.abspath("data/queries/distance_approx_only.json")

query_mimic_file = os.path.abspath("data/queries/mimic.json")
query_explorys_data_available_file = os.path.abspath("data/queries/explorys_data_available.json")
query_explorys_hcc_file = os.path.abspath("data/queries/explorys_hcc.json")
query_explorys_complete_file = os.path.abspath("data/queries/explorys_complete.json")
query_fib4_data_available_file = os.path.abspath("data/queries/fib_4_availability.json")

query_default_precise = os.path.abspath("data/queries/default_precise.json")
query_default_approx = os.path.abspath("data/queries/default_approx.json")

data_mimic_file = os.path.abspath("data/patient_data/mimic.json")
data_explorys_100k_file = os.path.abspath("data/patient_data/explorys_100k.json")
data_explorys_200k_file = os.path.abspath("data/patient_data/explorys_200k.json")
data_explorys_300k_file = os.path.abspath("data/patient_data/explorys_300k.json")
data_explorys_400k_file = os.path.abspath("data/patient_data/explorys_400k.json")
data_explorys_500k_file = os.path.abspath("data/patient_data/explorys_500k.json")
data_explorys_600k_file = os.path.abspath("data/patient_data/explorys_600k.json")
data_explorys_all_file = os.path.abspath("data/patient_data/explorys_all.json")


class EncryptionType(Enum):
    CIPHERTEXT_REAL = 0
    CIPHERTEXT_TOY = 1
    PLAINTEXT = 2

def log_message(message: str):
    print(f"[{datetime.now()}] {message}")

def rm_dir_rec(path: pathlib.Path):
    if path.is_dir():
        for child in path.iterdir():
            rm_dir_rec(child)
        path.rmdir()
    else:
        path.unlink()


def run_apps_with_params(test_id: str, attribute_file: str, query_file: str, encryption_type: EncryptionType, patient_count: int, data_file_input: str = None):
    run_id = uuid.uuid4().hex

    log_message(f"--------- Executing run {run_id} ---------")

    ta_exec = os.path.abspath("cmake-build-release/trusted_authority/app/pat_disc_ta")
    server_exec = os.path.abspath("cmake-build-release/server/app/pat_disc_server")
    client_exec = os.path.abspath("cmake-build-release/client/app/pat_disc_client")

    # Remove database to get accurate results
    pathlib.Path(database_file).unlink(missing_ok=True)

    # Remove old context and keys
    ta_storage_path = pathlib.Path(ta_storage_dir)
    if ta_storage_path.exists():
        rm_dir_rec(pathlib.Path(ta_storage_dir))

    if data_file_input is None:
        data_path = pathlib.Path("data.json")
        if patient_count > 0:
            generate_data.main(attribute_file, str(data_path.absolute()), patient_count)

        data_file = data_path.absolute()
    else:
        data_file = data_file_input

    # Wait with starting other applications until "Starting server" observed
    ta_proc = None
    if encryption_type != EncryptionType.PLAINTEXT:
        ta_args = [ta_exec, f"--test-id={test_id}", f"--run-id={run_id}", f"--attribute-config-file={attribute_file}"]
        if encryption_type == EncryptionType.CIPHERTEXT_TOY:
            ta_args += ["--use-toy-parameters"]

        ta_proc = subprocess.Popen(ta_args, stdout=subprocess.PIPE, stderr=sys.stderr.buffer)

        line = ta_proc.stdout.readline().decode()
        while "Starting server" not in line:
            line = ta_proc.stdout.readline().decode()
    
    server_args = [server_exec, f"--test-id={test_id}", f"--run-id={run_id}", f"--attribute-config-file={attribute_file}", "-D",
                   f"--database-file={database_file}"]
    if encryption_type == EncryptionType.PLAINTEXT:
        server_args.append("-P")

    time.sleep(0.5)
    server_proc = subprocess.Popen(args=server_args, stdout=subprocess.PIPE, stderr=sys.stderr.buffer)

    line = server_proc.stdout.readline().decode()
    while "Starting server" not in line:
        line = server_proc.stdout.readline().decode()

    client_args = [client_exec, f"--test-id={test_id}", f"--run-id={run_id}", f"--attribute-config-file={attribute_file}",
                   f"--patient-data-file={data_file}", f"--query-file={query_file}", "--shutdown-servers"]
    if encryption_type == EncryptionType.PLAINTEXT:
        client_args.append("-P")

    time.sleep(0.5)
    client_proc = subprocess.Popen(args=client_args, stdout=subprocess.PIPE, stderr=sys.stderr.buffer)

    if encryption_type != EncryptionType.PLAINTEXT:
        ta_ret_code = ta_proc.wait()

        if ta_ret_code != 0:
            log_message("Trusted authority exited with non-zero return code")

    server_ret_code = server_proc.wait()

    if server_ret_code != 0:
        log_message("Server exited with non-zero return code")

    client_ret_code = client_proc.wait()

    if client_ret_code != 0:
        log_message("Client exited with non-zero return code")

    data_file_copy = pathlib.Path(f"test-results/input-data/{test_id}/{run_id}.json")
    data_file_copy.mkdir(parents=True, exist_ok=True)
    shutil.copy(data_file, data_file_copy)

    if patient_count > 0:
        data_path.unlink()


def perform_run_patient_counts(test_id: str, attribute_file: str, query_file: str, encryption_type: EncryptionType, counts: list[int]):
    for count in counts:
        log_message(f"Patient Count: {count}")
        run_apps_with_params(test_id, attribute_file, query_file, encryption_type, count)


def run_single_attr(attribute_file: str, query_file: str, count: int):
    data_path = pathlib.Path("data.json")
    generate_data.main(attribute_file, str(data_path.absolute()), count)

    log_message("---- Ciphertext Real ----")
    run_apps_with_params(current_test_id, attribute_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0)

    # log_message("---- Ciphertext Toy ----")
    # run_apps_with_params(current_test_id, attribute_file, query_file, EncryptionType.CIPHERTEXT_TOY, 0)

    log_message("---- Plaintext ----")
    run_apps_with_params(current_test_id, attribute_file, query_file, EncryptionType.PLAINTEXT, 0)

    data_path.unlink()


def replay_input_for_plaintext(new_test_id: str, old_test_id: str, attribute_file: str, query_file: str, counts: list[int]):
    path = pathlib.Path(f"test-results/Client/{old_test_id}")

    for c in counts:
        run_ids = []

        for item in path.iterdir():
            if item.is_file():
                with open(item, "r") as f:
                    data = json.load(f)

                    if attribute_file == data['attributeConfig'] and query_file == data['query'] and not data['plaintext'] and data['patientCount'] == c:
                        run_ids.append(data['runId'])
        
        for id in run_ids:
            input_data = pathlib.Path(f"test-results/input-data/{old_test_id}/{id}.json/data.json")
            data_path = pathlib.Path("data.json")

            shutil.copyfile(input_data, data_path)

            run_apps_with_params(current_test_id, attribute_file, query_file, EncryptionType.PLAINTEXT, 0)

            data_path.unlink()


def run_mimic():
    run_apps_with_params(current_test_id, attribute_mimic_file, query_mimic_file, EncryptionType.CIPHERTEXT_REAL, 0, data_mimic_file)

def run_explorys_query(query_file: str):
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_100k_file)
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_200k_file)
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_300k_file)
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_400k_file)
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_500k_file)
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_600k_file)
    run_apps_with_params(current_test_id, attribute_explorys_file, query_file, EncryptionType.CIPHERTEXT_REAL, 0, data_explorys_all_file)

def run_explorys_data_available():
    run_explorys_query(query_explorys_data_available_file)

def run_explorys_hcc():
    run_explorys_query(query_explorys_hcc_file)

def run_explorys_complete():
    run_explorys_query(query_explorys_complete_file)

def run_fib4_available():
    run_explorys_query(query_fib4_data_available_file)

def main():
    for i in range(num_iters):
        log_message(f"---- Iteration {i} ----")

        # log_message("--------- Single Attributes ---------")
        # log_message("---- Boolean ----")
        # run_single_attr(attribute_boolean_file, query_boolean_file, bfv_batch_size)

        # log_message("---- Enum Precise ----")
        # run_single_attr(attribute_enum_precise_file, query_enum_precise_file, bfv_batch_size)

        # log_message("---- Enum Approx ----")
        # run_single_attr(attribute_enum_approx_file, query_enum_approx_file, ckks_batch_size)

        # log_message("---- Continuous Precise ----")
        # run_single_attr(attribute_continuous_precise_file, query_continuous_precise_file, bfv_batch_size)

        # log_message("---- Continuous Approx ----")
        # run_single_attr(attribute_continuous_approx_file, query_continuous_approx_file, ckks_batch_size)

        # log_message("---- Distance Precise ----")
        # run_single_attr(attribute_distance_precise_file, query_distance_precise_file, bfv_batch_size)

        # log_message("---- Distance Approx ----")
        # run_single_attr(attribute_distance_approx_file, query_distance_approx_file, ckks_batch_size)

        # log_message("--------- Attribute counts ---------")
        # log_message("---- Attribute Count 2 ----")
        # perform_run_patient_counts(current_test_id, attribute_attr_count_file, query_attr_count_2, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Attribute Count 4 ----")
        # perform_run_patient_counts(current_test_id, attribute_attr_count_file, query_attr_count_4, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Attribute Count 6 ----")
        # perform_run_patient_counts(current_test_id, attribute_attr_count_file, query_attr_count_6, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Attribute Count 8 ----")
        # perform_run_patient_counts(current_test_id, attribute_attr_count_file, query_attr_count_8, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Attribute Count 10 ----")
        # perform_run_patient_counts(current_test_id, attribute_attr_count_file, query_attr_count_10, EncryptionType.CIPHERTEXT_REAL, [4096])
        
        # log_message("--------- Query Depths ---------")
        # log_message("---- Depth 1 ----")
        # perform_run_patient_counts(current_test_id, attribute_query_depth_file, query_depth_1, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Depth 2 ----")
        # perform_run_patient_counts(current_test_id, attribute_query_depth_file, query_depth_2, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Depth 3 ----")
        # perform_run_patient_counts(current_test_id, attribute_query_depth_file, query_depth_3, EncryptionType.CIPHERTEXT_REAL, [4096])
        # log_message("---- Depth 4 ----")
        # perform_run_patient_counts(current_test_id, attribute_query_depth_file, query_depth_4, EncryptionType.CIPHERTEXT_REAL, [4096])

        # log_message("--------- End to End ---------")
        # counts = [100_000, 200_000, 300_000, 400_000, 500_000]
        
        # log_message("---- Precise ----")
        # for c in counts:
        #     run_single_attr(attribute_default_precise_file, query_default_precise, c)

        # log_message("---- Approx ----")
        # for c in counts:
        #     run_single_attr(attribute_default_approx_file, query_default_approx, c)

        # run_mimic()

        # log_message("--------- Explorys Benchmarks ---------")
        #log_message("---- Data Available ----")
        #run_explorys_data_available()

        # log_message("---- HCC ----")
        # run_explorys_hcc()

        # log_message("---- Complete ----")
        # run_explorys_complete()

        log_message("---- Fib4 ----")
        run_fib4_available()

    log_message("---- Finished eval run! ----")


if __name__ == "__main__":
    log_message(f"Running test with ID: {current_test_id}")
    main()
