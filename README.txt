Artifact for "FeCaD: Federated Patient Discovery with Fine-Grained Consent"
==========================================================================

Badges requested: Available, Functional, optional Reproduced.

We supply runnable evidence toward Results Reproduced, though reproduction
at the published scale is impractical for a reviewer: the underlying
measurements total some 2400 hours of query time at 30 repetitions per data
point. We therefore provide a scaled-down runner with the full-scale medians
beside it in expected/; the sufficiency of that evidence remains the
reviewer's judgement.

Getting started
---------------
  ./install.sh                                   # Go backends, about 2 minutes
  claims/01-backend-comparison/run.sh 1          # about 2 minutes

install.sh closes with a one-provider run.

The three CGO backends require ./install.sh --full, hence cmake, NTL and GMP,
or the container in infrastructure/; that build compiles SEAL and OpenFHE from
source, taking 7 minutes on the reference machine's 128 threads and approaching
an hour on a laptop.

Claims
------
Run them directly, from the repository root:

  claims/01-backend-comparison/run.sh 1

Each runner takes a scale level: 1 (default, the smallest configuration that
still exercises the whole protocol) or 2 (larger, closer to the paper's
operating point). Both levels perform a single run per data point against the
paper's mean of 30, so compare trends and orderings rather than absolute values.

  01-backend-comparison    plaintext baseline against four FHE backends
  02-records-per-provider  scaling in records held by one provider
  03-number-of-providers   scaling in federation size, co-located
  04-dataset-diversity     the same protocol across three cohort schemas

Runtimes refer to a CloudLab r7525 instance (full node).

Data
----
The artifact contains no patient data: runs synthesize records from the schemas 
in artifact/schemas/. The real cohorts require data use agreements; artifact/preprocess/
holds the conversion scripts if you have access to the datasets.

Output
------
A run writes per-entity performance_metrics.csv files under the claim's
output/ directory and appends a one-line summary to output/summary.txt; the
column of interest is the researcher-query-total phase, the query completion
time the paper reports.

artifact/scripts/plots.ipynb redraws the paper's figures from our recorded
measurements, see artifact/measurements/README.txt.
