import json
import pathlib

test_id = "e14e00b2b9c54d55aa6307a5e6ecc9f3"

file_path = pathlib.Path(__file__)
root_dir = file_path.parent.parent.parent

client_results_dir = root_dir / "test-results" / "Client" / test_id
query_results_dir = root_dir / "test-results" / "query-results" / test_id


def data_available_predicate(item: dict[str, int]) -> bool:
    return (
        item["Gender"] != 0 and item["BirthYear"] != 0 and item["BodyWeight"] != 0 and item["BodyHeight"] != 0 and 
        item["WaistCircumference"] != 0 and item["CorposcularVolume"] != 0 and item["PlateletCount"] != 0 and
        item["AlanineAminotransferase"] != 0 and item["AlkalinePhosphatase"] and item["AspartateAminotransferase"] != 0 and item["Glucose"] and item["PrimaryData"] == 1
    )

def hcc_predicate(item: dict[str, int], icd_9_value: int, icd_10_value: int) -> bool:
    return item["Icd9Category"] == icd_9_value or item["Icd10Category"] == icd_10_value

def complete_predicate(item: dict[str, int], icd_9_value: int, icd_10_value: int):
    return (
        item["Gender"] != 0 and item["BirthYear"] != 0 and item["BodyWeight"] != 0 and item["BodyHeight"] != 0 and 
        item["WaistCircumference"] != 0 and item["CorposcularVolume"] != 0 and item["PlateletCount"] != 0 and
        item["AlanineAminotransferase"] != 0 and item["AlkalinePhosphatase"] and item["Glucose"] and 
        hcc_predicate(item , icd_9_value, icd_10_value)
    )


def check_plain_data():
    icd_9_num = -1
    icd_10_num = -1

    with open(root_dir / "explorys_icd9_category_encoding.json", "r") as f:
        data = json.load(f)
        icd_9_num = data["159"]

    with open(root_dir / "explorys_icd10_category_encoding.json", "r") as f:
        data = json.load(f)
        icd_10_num = data["C22"]

    with open(root_dir / "data" / "patient_data" / "explorys_all.json", "r") as f:
        data_available_matches = 0
        hcc_matches = 0
        complete_matches = 0

        data = json.load(f)
        for item in data:
            if data_available_predicate(item):
                data_available_matches += 1

            if hcc_predicate(item, icd_9_num, icd_10_num):
                hcc_matches += 1
            
            if complete_predicate(item, icd_9_num, icd_10_num):
                complete_matches += 1
    
    return (data_available_matches, hcc_matches, complete_matches)


def check_data_available_res(run_id: str, plain_res: int) -> bool:
    with open(query_results_dir / f"{run_id}.json", "r") as f:
        data = json.load(f)

        ctr = 0
        for item in data:
            if item["EnumPrecise"] == 1 and item["Boolean"] == 1:
                ctr += 1
        
        return plain_res == ctr


def check_hcc_res(run_id: str, plain_res: int) -> bool:
    with open(query_results_dir / f"{run_id}.json", "r") as f:
        data = json.load(f)

        ctr = 0
        for item in data:
            if item["EnumPrecise"] == 1:
                ctr += 1
        
        return plain_res == ctr


def check_complete_res(run_id: str, plain_res: int) -> bool:
    with open(query_results_dir / f"{run_id}.json", "r") as f:
        data = json.load(f)

        ctr = 0
        for item in data:
            if item["EnumPrecise"] == 1:
                ctr += 1
        
        return plain_res == ctr


def main():
    data_available_res, hcc_res, complete_res = check_plain_data()

    error_count = 0

    for item in client_results_dir.iterdir():
        with open(item, "r") as f:
            data = json.load(f)

            # Skip partial queries
            if data["patientCount"] != 2728608:
                continue

            if "explorys_data_available" in data["query"]:
                error_count += 1 - check_data_available_res(data["runId"], data_available_res)
            elif "explorys_hcc" in data["query"]:
                error_count += 1 - check_hcc_res(data["runId"], hcc_res)
            elif "explorys_complete" in data["query"]:
                error_count += 1 - check_complete_res(data["runId"], complete_res)
    
    if error_count == 0:
        print("All results are correct!")
    else:
        print(f"Found {error_count} errors.")


if __name__ == "__main__":
    main()
