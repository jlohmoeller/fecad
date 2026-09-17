from enum import Enum

import pandas as pd


class TumorType(Enum):
    GLIOBLASTOMA = 0
    OLIGODENDROGLIOMA = 1
    ASTROCYTOMA = 2
    BRAIN_METASTASIS = 3
    UNKNOWN = 4


class WHOGrade(Enum):
    I = 0
    II = 1
    III = 2
    IV = 3
    UNKNOWN = 4


class BooleanAttribute(Enum):
    YES = 0
    NO = 1
    UNKNOWN = 2


def main():
    patient_data = pd.read_excel('../../../Data/data_insert.xlsx', sheet_name='Data')
    patient_data = patient_data.rename(columns={"ID": "id", "Tumortyp": "tumorType", "WHO Grad": "whoGrade", "IDH Wildtyp": "idhWildType",
                                                "MGMT-Promotor methyliert": "mgmtPromoterMethylation",
                                                "1p/19q Kodeletion": "codeletion1q19q", "Biopsie": "biopsy", "FET-PET": "fetPet",
                                                "Resektion": "resection", "CeTeg-Protokoll": "ceTegProtocol",
                                                "Radiotherapie": "radioTherapy", "Chemotherapie": "chemoTherapy",
                                                "Boost-Therapie": "boostTherapy", "TMZ-Therapie": "tmzTherapy",
                                                "Aktives Tumorgewebe": "activeTumorTissue", "Tumorprogression": "tumorProgression",
                                                "Age": "age"})

    for col in patient_data.columns:
        if col == "age":
            continue

        if col == "id":
            patient_data[col] = patient_data[col].transform(lambda x: str(x))
        elif col == "tumorType":
            patient_data[col] = patient_data[col].transform(lambda x: TumorType[x].value)
        elif col == "whoGrade":
            patient_data[col] = patient_data[col].transform(lambda x: WHOGrade[x].value)
        else:
            patient_data[col] = patient_data[col].transform(lambda x: BooleanAttribute[x].value)

    output = patient_data.to_json(orient='records')

    with open("out.json", "w") as f:
        f.write(output)


if __name__ == '__main__':
    main()
