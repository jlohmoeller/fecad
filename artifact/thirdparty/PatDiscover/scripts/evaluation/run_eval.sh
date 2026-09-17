#! /bin/bash

mkdir -p python_logs
python3 -u scripts/evaluation/run_eval.py &> python_logs/$(date +"%Y-%m-%d_%H-%M-%S").log &
