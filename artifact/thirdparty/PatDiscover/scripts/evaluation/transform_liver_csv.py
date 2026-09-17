import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
import json
import uuid


class DictionaryEncoder:

    def __init__(self):
        self.dict = dict()
        self.counter = 0

    def encode(self, value: str) -> int:
        if value not in self.dict:
            self.dict[value] = self.counter
            self.counter += 1

        return self.dict[value]

    def verify(self, max_num: int):
        assert self.counter <= max_num

    def write(self, file: str):
        with open(file, 'w') as f:
            json.dump(self.dict, f)


max_num = 65536
df = pd.read_csv(sys.argv[1], dtype={
                 'ICD_VERSION': 'string', 'ICD_CODE': 'string'})

df['id'] = [uuid.uuid4().hex for _ in range(len(df.index))]
df['Gender'] = df['GENDER'].fillna(0)
df['BirthYear'] = df['BIRTH_YEAR'].fillna(0)
df['BodyWeight'] = df['BODY_WEIGHT'].fillna(0)
df['BodyHeight'] = df['BODY_HEIGHT'].fillna(0)
df['WaistCircumference'] = df['WAIST_CIRCUMFERENCE'].fillna(0)
df['CorposcularVolume'] = df['CORPOSCULAR_VOLUME'].fillna(0)
df['PlateletCount'] = df['PLATELET_COUNT'].fillna(0)
df['AlanineAminotransferase'] = df['ALANINE_AMINOTRANSFERASE'].fillna(0)
df['AlkalinePhosphatase'] = df['ALKALINE_PHOSPHATASE'].fillna(0)
df['AspartateAminotransferase'] = df['ASPARTATE_AMINOTRANSFERASE'].fillna(0)
df['Glucose'] = df['GLUCOSE'].fillna(0)

df['ICD_VERSION'] = df['ICD_VERSION'].fillna('')
df['ICD_CODE'] = df['ICD_CODE'].fillna('')

df['WaistCircumference'] = np.ceil(
    (df['WaistCircumference'] / df['WaistCircumference'].max()) * max_num)
df['CorposcularVolume'] = np.ceil(
    (df['CorposcularVolume'] / df['CorposcularVolume'].max()) * max_num)
df['PlateletCount'] = np.ceil(
    (df['PlateletCount'] / df['PlateletCount'].max()) * max_num)
df['AlanineAminotransferase'] = (
    (df['AlanineAminotransferase'] / df['AlanineAminotransferase'].max()) * max_num)
df['AlkalinePhosphatase'] = np.ceil(
    (df['AlkalinePhosphatase'] / df['AlkalinePhosphatase'].max()) * max_num)
df['AspartateAminotransferase'] = np.ceil(
    (df['AspartateAminotransferase'] / df['AspartateAminotransferase'].max()) * max_num)
df['Glucose'] = np.ceil((df['Glucose'] / df['Glucose'].max()) * max_num)

icd_9_category = []
icd_9_subcategory = []
icd_10_category = []
icd_10_subcategory = []

icd_9_category_encoder = DictionaryEncoder()
icd_9_subcategory_encoder = DictionaryEncoder()
icd_10_category_encoder = DictionaryEncoder()
icd_10_subcategory_encoder = DictionaryEncoder()

for icd_version, icd_code in zip(df['ICD_VERSION'], df['ICD_CODE']):
    if '.' not in icd_code:
        parts = [icd_code, '']
    else:
        parts = icd_code.split(".")

    if icd_version == "ICD9":
        icd_9_category.append(icd_9_category_encoder.encode(parts[0]))
        icd_9_subcategory.append(icd_9_subcategory_encoder.encode(parts[1]))
        icd_10_category.append(icd_10_category_encoder.encode(''))
        icd_10_subcategory.append(icd_10_subcategory_encoder.encode(''))
    elif icd_version == "ICD10":
        icd_9_category.append(icd_9_category_encoder.encode(''))
        icd_9_subcategory.append(icd_9_subcategory_encoder.encode(''))
        icd_10_category.append(icd_10_category_encoder.encode(parts[0]))
        icd_10_subcategory.append(icd_10_subcategory_encoder.encode(parts[1]))
    elif icd_version == '':
        icd_9_category.append(icd_9_category_encoder.encode(''))
        icd_9_subcategory.append(icd_9_subcategory_encoder.encode(''))
        icd_10_category.append(icd_10_category_encoder.encode(''))
        icd_10_subcategory.append(icd_10_subcategory_encoder.encode(''))
    else:
        raise ValueError("Encountered invalid ICD version")

df['Icd9Category'] = icd_9_category
df['Icd9Subcategory'] = icd_9_subcategory
df['Icd10Category'] = icd_10_category
df['Icd10Subcategory'] = icd_10_subcategory

df['PrimaryData'] = (~df['EXPLORYS_PATIENT_ID'].duplicated()).astype(int)

df = df.drop(columns=['GENDER', 'BIRTH_YEAR', 'BODY_WEIGHT', 'BODY_HEIGHT', 'WAIST_CIRCUMFERENCE', 'CORPOSCULAR_VOLUME', 'PLATELET_COUNT',
             'ALANINE_AMINOTRANSFERASE', 'ALKALINE_PHOSPHATASE', 'ASPARTATE_AMINOTRANSFERASE', 'GLUCOSE', 'ICD_VERSION', 'ICD_CODE'])
df = df.sample(frac=1).reset_index(drop=True)  # Shuffle data frame

primary_data = df[df['PrimaryData'] == 1]
print(f"Patient Count: {len(primary_data)}")

primary_data_chunks = [primary_data.head(
    x) for x in range(100_000, 700_000, 100_000)]
full_data_chunks = [df[df["EXPLORYS_PATIENT_ID"].isin(
    chunk['EXPLORYS_PATIENT_ID'])] for chunk in primary_data_chunks]

ctr = 100
for chunk in full_data_chunks:
    chunk.drop(columns=['EXPLORYS_PATIENT_ID']).to_json(
        f"data/patient_data/explorys_{ctr}k.json", orient='records')
    ctr += 100

df.drop(columns=['EXPLORYS_PATIENT_ID']).to_json(
    "data/patient_data/explorys_all.json", orient='records')

icd_9_category_encoder.verify(max_num)
icd_9_subcategory_encoder.verify(max_num)
icd_10_category_encoder.verify(max_num)
icd_10_subcategory_encoder.verify(max_num)

icd_9_category_encoder.write("explorys_icd9_category_encoding.json")
icd_9_subcategory_encoder.write("explorys_icd9_subcategory_encoding.json")
icd_10_category_encoder.write("explorys_icd10_category_encoding.json")
icd_10_subcategory_encoder.write("explorys_icd10_subcategory_encoding.json")

print(f"ICD 9 Category Counter: {icd_9_category_encoder.counter}")
print(f"ICD 9 Subcategory Counter: {icd_9_subcategory_encoder.counter}")
print(f"ICD 10 Category Counter: {icd_10_category_encoder.counter}")
print(f"ICD 10 Subcategory Counter: {icd_10_subcategory_encoder.counter}")

print(f"ICD 9 Code for HCC: {icd_9_category_encoder.encode('159')}")
print(f"ICD 10 Code for HCC: {icd_10_category_encoder.encode('C22')}")
