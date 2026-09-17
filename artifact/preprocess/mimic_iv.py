#!/usr/bin/env python3
"""
MIMIC-IV preprocessing, PhysioNet v3.1 hosp module
Input:  directory containing MIMIC-IV CSV or CSV.GZ files
Output: data.json compatible with schemas/mimic_iv.json

Usage:
    python mimic_iv.py --mimic-dir /data/mimic-iv/ --output data.json [--limit N]

Requires:
    mimic_dir/hosp/patients.csv(.gz)
    mimic_dir/hosp/diagnoses_icd.csv(.gz)
    mimic_dir/hosp/labevents.csv(.gz)      (large — may need filtering first)

ICD flags from diagnoses_icd, both ICD-9 and ICD-10
Lab values: last measurement per subject, clipped to schema range
One row per unique subject_id
"""

import argparse
import csv
import gzip
import json
import uuid
from collections import defaultdict
from pathlib import Path


def open_csv(path: Path):
    """open csv or csv.gz"""
    if not path.exists():
        gz = Path(str(path) + '.gz')
        if gz.exists():
            return gzip.open(gz, 'rt', newline='', encoding='utf-8')
        raise FileNotFoundError(f'Neither {path} nor {path}.gz found')
    return open(path, newline='', encoding='utf-8')

# per flag: ICD-10 prefix first, ICD-9 fallback second
ICD_PREFIXES = {
    'icd_sepsis':     ['A41', '038'],
    'icd_aki':        ['N17', '584'],
    'icd_pneumonia':  ['J18', '486'],
    'icd_heart_fail': ['I50', '428'],
}

# hosp/labevents itemid -> (fecad_field, scale, max_q)
LAB_ITEMS = {
    50912: ('creatinine_q', 10, 255),  # creatinine mg/dL x10, capped at 25.5
    50813: ('lactate_q',     10,  200),  # lactate mmol/L x10
}


def load_patients(patients_csv: Path) -> dict:
    patients = {}
    with open_csv(patients_csv) as f:
        for row in csv.DictReader(f):
            sid = int(row['subject_id'])
            try:
                age = max(0, min(120, int(float(row.get('anchor_age', 0)))))
                sex = 0 if row.get('gender', 'M').strip().upper() == 'M' else 1
            except (ValueError, TypeError):
                continue
            patients[sid] = {'age': age, 'sex': sex}
    return patients


def load_icd_flags(diag_csv: Path) -> dict:
    flags = defaultdict(lambda: {k: 0 for k in ICD_PREFIXES})
    with open_csv(diag_csv) as f:
        for row in csv.DictReader(f):
            try:
                sid = int(row['subject_id'])
                code = row.get('icd_code', '').strip().replace('.', '')
            except (ValueError, KeyError):
                continue
            for flag, prefixes in ICD_PREFIXES.items():
                if any(code.startswith(p.replace('.', '')) for p in prefixes):
                    flags[sid][flag] = 1
    return flags


def load_lab_values(lab_csv: Path, item_ids: set) -> dict:
    labs = defaultdict(dict)
    with open_csv(lab_csv) as f:
        for row in csv.DictReader(f):
            try:
                sid  = int(row['subject_id'])
                item = int(row['itemid'])
                val  = float(row['value'])
            except (ValueError, KeyError, TypeError):
                continue
            if item in item_ids:
                labs[sid][item] = val  # last row wins (file assumed time-sorted)
    return labs


def preprocess(mimic_dir: Path, output_path: Path, limit: int = None):
    patients = load_patients(mimic_dir / 'hosp' / 'patients.csv')
    print(f'Loaded {len(patients)} patients')

    icd_flags = load_icd_flags(mimic_dir / 'hosp' / 'diagnoses_icd.csv')
    print(f'Loaded ICD flags for {len(icd_flags)} subjects')

    labs = load_lab_values(mimic_dir / 'hosp' / 'labevents.csv', set(LAB_ITEMS))
    print(f'Loaded lab values for {len(labs)} subjects')

    records = []
    for sid, pat in patients.items():
        if limit and len(records) >= limit:
            break
        record = {'id': str(uuid.uuid4())}
        record.update(pat)

        flags = icd_flags.get(sid, {k: 0 for k in ICD_PREFIXES})
        record.update(flags)

        pat_labs = labs.get(sid, {})
        for itemid, (field, scale, max_q) in LAB_ITEMS.items():
            raw = pat_labs.get(itemid, 0.0)
            record[field] = max(0, min(max_q, round(raw * scale)))

        records.append(record)

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(records, f, indent=2)

    print(f'Written {len(records)} records to {output_path}')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--mimic-dir', required=True, type=Path)
    p.add_argument('--output',    required=True, type=Path)
    p.add_argument('--limit',     type=int, default=None)
    args = p.parse_args()
    preprocess(args.mimic_dir, args.output, args.limit)
