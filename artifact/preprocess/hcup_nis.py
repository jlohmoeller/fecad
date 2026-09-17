#!/usr/bin/env python3
"""
AHRQ HCUP National Inpatient Sample (NIS 2020) preprocessing
Input:  fixed-width ASCII Core file (e.g. nis-core.asc)
Output: data.json compatible with schemas/hcup_nis.json

Usage:
    python hcup_nis.py --input ${FECAD_DATASETS:-./datasets}/hcup/nis-core.asc \
                       --output data.json [--limit N]

NIS 2020 Core layout; spec offsets 1-indexed inclusive, Python slices 0-indexed
half-open:

    AGE         1- 3
    FEMALE     37-38
    I10_NDX   339-340
    I10_DX1.. 55- 61, 62- 68, ... each 7 chars, 40 slots through 328-334
    I10_NPR   341-342
    I10_PR1.. 343-349, 350-356, ... each 7 chars, 25 slots through 511-517
    record length = 647 bytes + CRLF

Cohort flags; ICD-9 codes from publications crosswalked to ICD-10-CM/PCS:
    dx_cervical_ca   any DX startswith C53                    (was 180.x)
    pr_radical_hyst  any PR startswith 0UT9                   (was 68.6/.61/.69)
    dx_oroph         any DX startswith C09 or C10             (Chung et al. oropharynx)
    pr_pharyn        any PR startswith 0CBC, 0CBD, 0CBM       (pharyngeal excision)
    dx_tongue        any DX startswith C01 or C02             (Chung et al. tongue)
    pr_gloss         any PR startswith 0CB7                   (glossectomy)
    dx_breast_ca     any DX startswith C50 or D05             (was 174.x, 233.0)
    pr_mastectomy    any PR startswith 0HTT, 0HTU, 0HTV       (was 85.33-.48)

Chung et al. Q2 cohort = (dx_oroph AND pr_pharyn) OR (dx_tongue AND pr_gloss),
written in SQL relying on parser precedence AND > OR
"""

import argparse
import json
import uuid
from pathlib import Path

# spec offsets are 1-indexed inclusive, these are 0-indexed slice bounds
AGE_S, AGE_E = 0, 3
FEMALE_S, FEMALE_E = 36, 38
NDX_S, NDX_E = 338, 340
NPR_S, NPR_E = 340, 342

# DX: 40 slots, 7 chars, spec byte 55 = offset 54
DX_START = 54
DX_WIDTH = 7
DX_COUNT = 40

# PR: 25 slots, 7 chars, spec byte 343 = offset 342
PR_START = 342
PR_WIDTH = 7
PR_COUNT = 25


def parse_int(raw: str, default: int = 0) -> int:
    """parse int, '.' missing"""
    s = raw.strip()
    if not s or s.startswith('.'):
        return default
    try:
        return int(s)
    except ValueError:
        return default


def slot_codes(record: str, start: int, width: int, count: int) -> list:
    """extract fixed-width codes"""
    codes = []
    for i in range(count):
        s = start + i * width
        code = record[s:s + width].strip()
        if code:
            codes.append(code)
    return codes


def any_prefix(codes: list, prefixes: tuple) -> bool:
    return any(c.startswith(p) for c in codes for p in prefixes)


def extract_flags(dx_codes: list, pr_codes: list) -> dict:
    dx_cervical = any_prefix(dx_codes, ('C53',))
    pr_radhyst  = any_prefix(pr_codes, ('0UT9',))

    dx_oroph  = any_prefix(dx_codes, ('C09', 'C10'))
    dx_tongue = any_prefix(dx_codes, ('C01', 'C02'))
    pr_pharyn = any_prefix(pr_codes, ('0CBC', '0CBD', '0CBM'))
    pr_gloss  = any_prefix(pr_codes, ('0CB7',))

    dx_breast = any_prefix(dx_codes, ('C50', 'D05'))
    pr_mast   = any_prefix(pr_codes, ('0HTT', '0HTU', '0HTV'))

    return {
        'dx_cervical_ca':  int(dx_cervical),
        'pr_radical_hyst': int(pr_radhyst),
        'dx_oroph':        int(dx_oroph),
        'pr_pharyn':       int(pr_pharyn),
        'dx_tongue':       int(dx_tongue),
        'pr_gloss':        int(pr_gloss),
        'dx_breast_ca':    int(dx_breast),
        'pr_mastectomy':   int(pr_mast),
    }


def preprocess(input_path: Path, output_path: Path, limit: int | None = None):
    records = []
    n_read = n_dropped = 0

    with open(input_path, 'r', encoding='ascii', errors='replace') as f:
        for line in f:
            if limit is not None and len(records) >= limit:
                break
            n_read += 1

            if len(line) < PR_START + PR_COUNT * PR_WIDTH:
                n_dropped += 1
                continue

            age = max(0, min(120, parse_int(line[AGE_S:AGE_E])))
            female_raw = parse_int(line[FEMALE_S:FEMALE_E], default=-1)
            female = 1 if female_raw == 1 else 0

            ndx = parse_int(line[NDX_S:NDX_E], default=DX_COUNT)
            npr = parse_int(line[NPR_S:NPR_E], default=PR_COUNT)
            ndx = max(0, min(DX_COUNT, ndx))
            npr = max(0, min(PR_COUNT, npr))

            dx_codes = slot_codes(line, DX_START, DX_WIDTH, ndx)
            pr_codes = slot_codes(line, PR_START, PR_WIDTH, npr)

            record = {'id': str(uuid.uuid4()), 'age': age, 'female': female}
            record.update(extract_flags(dx_codes, pr_codes))
            records.append(record)

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(records, f, indent=2)

    print(f'Read {n_read} discharges, wrote {len(records)} records to {output_path} (dropped {n_dropped})')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--input',  required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--limit',  type=int, default=None)
    args = p.parse_args()
    preprocess(args.input, args.output, args.limit)
