Infrastructure notes
====================

Paper measurement platform
--------------------------
A cluster of machines with 2x Xeon Platinum 8160 CPUs (48 cores, 96 threads),
192 GB RAM and local SSD scratch, entities co-located as separate services
on separate loopback endpoints.

Artiface Evaluation platform
------------------
The scale-1 and scale-2 figures in each claim's expected/ directory were taken
on a CloudLab r7525 node: 2x 32-core AMD EPYC 7542 sockets (64 cores, 128
threads), 512 GB RAM, Ubuntu 24.04, co-located as on the paper's machine. Its
only local disk is a HDD, so we placed scratch on a tmpfs; see "Expected resource 
use".

EPYC is a more modern CPU and has 128 hardware threads against 96, which reduces 
contention between co-located providers and was found to flatten the federation-size 
curve: 25 co-located providers cost 62 s here against 218.9 s in the paper.

Reviewer requirements
---------------------
All claims run on a single machine, or in the container below, at the
scaled-down levels each claim.txt describes.

  claims 01-04   any x86-64 Linux host or the container. 8 GB RAM suffices at
                 scale 1; scale 2 requires 16 GB, and the CGO backends in
                 claim 01 at scale 2 need 32 GB.

Container
---------
  docker build --target go -t fecad:go -f infrastructure/Dockerfile .
  mkdir -p /scratch # on a fast filesystem, see above
  docker run --rm -it \
      -v /scratch:/scratch -e TMPDIR=/scratch fecad:go bash
  cd /opt/fecad && claims/01-backend-comparison/run.sh 1

Bind-mount the scratch directory as shown, since otherwise the per-patient
cancellation ciphertexts land in the container's writable layer, about 10 GB at
scale 2 might use a slow filesystem on the machine.

The full target additionally builds the C++ bridges for HE3DB, Engorgio and
PatDiscover, pulling the first two from GitHub and compiling SEAL and OpenFHE
from source, which took 7 minutes on this machine's 128 threads and much longer
on a laptop.

Public testbeds
---------------
The container runs on a single CloudLab or Chameleon instance.

Expected resource use
---------------------
The per-query consent layer dominates query completion time. Every enrolled
patient contributes a cancellation ciphertext of about 1 MiB at the Lattigo
parameter set, so a cohort of 10000 patients needs roughly 10 GB of scratch per
provider, and the scaled-down levels keep cohorts small for that reason.

The prototype writes those ciphertexts one file at a time and fsyncs each, which
a rotating disk handles badly: on the r7525's 7200 rpm drive a 5000-record run
took over four minutes, against 16 seconds with scratch in RAM. Point TMPDIR at
an SSD or a tmpfs before running anything:

  sudo mount -t tmpfs -o size=300G,mode=1777 tmpfs /mnt/tmp
  export TMPDIR=/mnt/tmp

Size the tmpfs above the largest cohort you intend to run; scale 2 peaks at
about 30 GB, in claim 02 at 30000 records.
