#!/usr/bin/env python3
"""
Nuclear medicine preprocessing, PatDiscover cohort (Pennekamp et al. 2025)
Input:  PatDiscover JSON (data.json) or CSV with named columns
Output: data.json compatible with schemas/nuclear_medicine.json

Usage:
    python nuclear_medicine.py --input /data/PatDiscover/data.json --output data.json [--limit N]
    python nuclear_medicine.py --input /data/patdiscover.csv --output data.json [--limit N]

JSON input (data.json):  camelCase fields, tumorPosition nested {x,y,z}
CSV input:               flat columns per CSV_COL_MAP below
"""

import argparse
import csv
import json
import uuid
from pathlib import Path

# CSV column map (adjust if actual CSV headers differ)
CSV_COL_MAP = {
    'WHO_Grade':      ('whoGrade',               lambda v: int(float(v))),
    'Tumor_Type':     ('tumorType',              lambda v: {'Glioblastoma': 0, 'Oligodendroglioma': 1,
                                                             'Astrocytoma': 2, 'BrainMetastasis': 3}.get(v.strip(), 0)),
    'MGMT':           ('mgmtPromoterMethylation', lambda v: 1 if v.strip() in ('1', 'Yes', 'TRUE', 'true') else 0),
    'Codeletion':     ('codeletion1q19q',         lambda v: 1 if v.strip() in ('1', 'Yes', 'TRUE', 'true') else 0),
    'IDH_WildType':   ('idhWildType',             lambda v: 1 if v.strip() in ('1', 'Yes', 'TRUE', 'true') else 0),
    'Pos_X_cm':       ('tumorPos_x',              lambda v: max(0, min(40, round(float(v) / 10)))),
    'Pos_Y_cm':       ('tumorPos_y',              lambda v: max(0, min(40, round(float(v) / 10)))),
    'Pos_Z_cm':       ('tumorPos_z',              lambda v: max(0, min(40, round(float(v) / 10)))),
    'Biopsy':         ('biopsy',                  lambda v: _tristate(v)),
    'FET_PET':        ('fetPet',                  lambda v: _tristate(v)),
    'Resection':      ('resection',               lambda v: _tristate(v)),
    'CeTeg':          ('ceTegProtocol',           lambda v: _tristate(v)),
    'Radiotherapy':   ('radioTherapy',            lambda v: _tristate(v)),
    'Chemo':          ('chemoTherapy',            lambda v: _tristate(v)),
    'Boost':          ('boostTherapy',            lambda v: _tristate(v)),
    'TMZ':            ('tmzTherapy',              lambda v: _tristate(v)),
    'ActiveTumor':    ('activeTumorTissue',       lambda v: 1 if v.strip() in ('1', 'Yes', 'TRUE', 'true') else 0),
    'Progression':    ('tumorProgression',        lambda v: 1 if v.strip() in ('1', 'Yes', 'TRUE', 'true') else 0),
    'Age':            ('age',                     lambda v: max(0, min(120, int(float(v))))),
}

TRISTATE = {'No': 0, 'no': 0, '0': 0, 'Yes': 1, 'yes': 1, '1': 1}

def _tristate(v):
    v = v.strip()
    return TRISTATE.get(v, 2)  # unknown = 2

REQUIRED_FIELDS = {'whoGrade', 'tumorType', 'tumorPos_x', 'tumorPos_y', 'tumorPos_z', 'age'}


def _from_json_record(rec: dict) -> dict:
    pos = rec.get('tumorPosition', {})
    return {
        'id':                     rec.get('id', str(uuid.uuid4())),
        'whoGrade':               max(0, min(4, int(rec.get('whoGrade', 0)))),
        'tumorType':              max(0, min(3, int(rec.get('tumorType', 0)))),
        'mgmtPromoterMethylation': int(bool(rec.get('mgmtPromoterMethylation', 0))),
        'codeletion1q19q':        int(bool(rec.get('codeletion1q19q', 0))),
        'idhWildType':            int(bool(rec.get('idhWildType', 0))),
        # tumorPosition values are already in 0.1 m quantized units (0-40)
        'tumorPos_x':             max(0, min(40, round(float(pos.get('x', 0))))),
        'tumorPos_y':             max(0, min(40, round(float(pos.get('y', 0))))),
        'tumorPos_z':             max(0, min(40, round(float(pos.get('z', 0))))),
        'biopsy':                 max(0, min(2, int(rec.get('biopsy', 2)))),
        'fetPet':                 max(0, min(2, int(rec.get('fetPet', 2)))),
        'resection':              max(0, min(2, int(rec.get('resection', 2)))),
        'ceTegProtocol':          max(0, min(2, int(rec.get('ceTegProtocol', 2)))),
        'radioTherapy':           max(0, min(2, int(rec.get('radioTherapy', 2)))),
        'chemoTherapy':           max(0, min(2, int(rec.get('chemoTherapy', 2)))),
        'boostTherapy':           max(0, min(2, int(rec.get('boostTherapy', 2)))),
        'tmzTherapy':             max(0, min(2, int(rec.get('tmzTherapy', 2)))),
        'activeTumorTissue':      int(bool(rec.get('activeTumorTissue', 0))),
        'tumorProgression':       int(bool(rec.get('tumorProgression', 0))),
        'age':                    max(0, min(120, int(float(rec.get('age', 0))))),
    }


def preprocess_json(input_path: Path, output_path: Path, limit: int = None):
    with open(input_path, encoding='utf-8') as f:
        raw = json.load(f)

    if limit:
        raw = raw[:limit]

    records = []
    dropped = 0
    for rec in raw:
        try:
            out = _from_json_record(rec)
            if not out.get('age') and 'age' in REQUIRED_FIELDS:
                raise ValueError('missing age')
        except (ValueError, KeyError, TypeError):
            dropped += 1
            continue
        records.append(out)

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(records, f, indent=2)

    print(f'Written {len(records)} records to {output_path}  (dropped {dropped})')


def preprocess_csv(input_path: Path, output_path: Path, limit: int = None):
    records = []
    dropped = 0

    with open(input_path, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for i, row in enumerate(reader):
            if limit and i >= limit:
                break
            record = {'id': str(uuid.uuid4())}
            try:
                for csv_col, (fecad_field, transform) in CSV_COL_MAP.items():
                    val = row.get(csv_col, '').strip()
                    if val == '' or val.lower() in ('nan', 'none', 'null', 'na'):
                        if fecad_field in REQUIRED_FIELDS:
                            raise ValueError(f'missing required field {fecad_field}')
                        record[fecad_field] = 0
                    else:
                        record[fecad_field] = transform(val)
            except (ValueError, KeyError) as e:
                dropped += 1
                continue
            records.append(record)

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(records, f, indent=2)

    print(f'Written {len(records)} records to {output_path}  (dropped {dropped})')


def preprocess(input_path: Path, output_path: Path, limit: int = None):
    if input_path.suffix.lower() == '.json':
        preprocess_json(input_path, output_path, limit)
    else:
        preprocess_csv(input_path, output_path, limit)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--input',  required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--limit',  type=int, default=None)
    args = p.parse_args()
    preprocess(args.input, args.output, args.limit)
