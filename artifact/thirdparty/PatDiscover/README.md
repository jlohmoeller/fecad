# PatDiscover: Privacy-Preserving Discoverability of Patients

## About
This repository contains our prototype of PatDiscover, a system for privacy-preserving patient discovery across institutions.

> Sourcing medical experience in finding effective patient-treatment strategies is challenged by strong privacy requirements these days. Specifically, the cross-institutional discovery of “similar” patients based on certain attributes, e.g., to align treatment strategies or collect expert expertise on rare diseases, is currently either impossible, impractical, or it exposes sensitive data. Addressing this research gap, in this paper, we propose PatDiscover, which is a fully homomorphic encryption-based design that supports multiple attribute types of medical importance, such as Enum, Range, and Distance. This way, institutions may compose and submit complex queries to several other institutions to discover relevant patients elsewhere. We evaluate PatDiscover using real-world patient data from nuclear medicine and demonstrate its appropriate performance, scalability, precision, and security. In conclusion, our work enables the privacy-preserving discoverability of patients for various applications in healthcare (research) and beyond.

## Publication

* Jan Pennekamp, Johannes Lohmöller, Niels Pressel, Sandra Geisler, Felix M. Mottaghy, and Klaus Wehrle: *PatDiscover: Privacy-Preserving Discoverability of Patients*. In 2nd Workshop on Cybersecurity in Healthcare (HealthSec '25), IEEE, 2025.

If you use any portion of our work, please cite our publication.

```
@inproceedings{pennekamp2025patdiscover,
    author = {Pennekamp, Jan and Lohm{\"o}ller, Johannes and Pressel, Niels and Geisler, Sandra and Mottaghy, Felix M. and Wehrle, Klaus},
    title = {{PatDiscover: Privacy-Preserving Discoverability of Patients}},
	booktitle = {Proceedings of the 2nd Workshop on Cybersecurity in Healthcare (HealthSec '25)},
	year = {2025},
    month = {December},
    publisher = {IEEE}
}
```

## License
This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.

If you are planning to integrate parts of our work into a commercial product and do not want to disclose your source code, please contact us for other licensing options via email at pennekamp (at) comsys (dot) rwth-aachen (dot) de

Parts of the cmake files used for building the dependencies GMP and NTL are based on HElib's (cf. https://github.com/homenc/HElib) build files published under the Apache 2.0 license.

## Acknowledgments

This work has received funding from the Klaus Tschira Boost Fund, a joint initiative of GSO — Guidance, Skills & Opportunities e.V. and Klaus Tschira Stiftung.

## Building

The project has only been tested on macOS 15 and Ubuntu 24.04.
However, it should be fairly easy to port it to and execute it on other platforms.

### Prerequisites

* gcc 13.1 / clang 14 or newer (we need `std::format`)
* Vcpkg: [https://github.com/microsoft/vcpkg]()
  * You have to properly set up VCPKG_ROOT
  * The packages loaded using vcpkg can be found in `vcpkg.json`
* cmake 3.22 or newer
* pkg-config 2.0.0 or newer
* Python 3.9 or newer
* Java JRE 11 or newer

### Compiling

When all prerequisites are set up, compiling is just a matter of executing `build.sh`.
You may need to adjust the vcpkg toolchain file location in the build script.
The script takes care of setting the appropriate cmake variables and then generates a release build with eval code included.

### Build Configurations

Besides the default release configuration, all artifacts can also be compiled in debug mode using `DCMAKE_BUILD_TYPE=Debug`.
In debug mode, the TA provides an additional endpoint, allowing the server to obtain the private keys for easier debugging of the matching algorithms.
On the server, the private key is automatically attached to the OpenFHE crypto context in debug mode.

Furthermore, the build scripts expose a cmake option `PD_EVAL_MODE`, which determines whether the server and the TA expose shutdown endpoints (see client command line parameters) for easier automated evaluation runs.
The OpenFHE integration with Intel HEXL must be turned off by disabling `OPENFHE_USE_HEXL` in the root cmake file when building the implementation on a non-Intel system.

In both cases (debug and eval mode), the additional code is completely stripped from the build using pre-processor statements if not enabled.

## Repository Structure

The project consists of three main components: the trusted authority, the server, and the client.
These components share some common code, which can be found in `shared`, and protobuf definitions for data exchange, which are stored in `proto`.
The code of the three main components is divided into three parts: a library for the component, the executable of the component, and a test executable of the component.
This division of the code is needed to make the component testable: The test executable as well as the "normal" executable can link against the library to use the code for testing or "normal" execution.

### Trusted Authority

The trusted authority code can be found in `trusted_authority`.
The TA handles context and key generation and provides a gRPC server to request context and keys.

### Server

The server code can be found in `server`.
The server handles patient data storage and private query execution.
Like the TA, the server provides a gRPC endpoint to upload patient data or send queries.

### Client

The client code can be found in `client`.
The client connects to the server to upload patient data or send queries.
The client handles data preprocessing, encryption, and query postprocessing.

## Patient, Attribute, and Query Data

The definition files for the attribute and query configuration can be found in the `data` directory.
These configuration files are specified in JSON format.
Example patient data files can also be found in the `data/patient_data` directory.

### Attribute Configuration

The attribute configuration consists of the list of attributes, which should be usable in the system.
For each attribute, one needs to specify the name and the type of the attribute.
Currently, the query system supports the following attribute types: `Boolean`, `EnumPrecise`, `EnumApprox`, `ContinuousPrecise`, `ContinuousApprox`, `DistancePrecise`, and `DistanceApprox`.
Consult the default attribute configurations for examples of how to configure the system.

### Query Configuration

The query configuration consists of a list of root combine groups for the attribute types one wants to query.
In each combine group, one can add children that can either be a combine group or an attribute.
Furthermore, one needs to specify the aggregation operation performed on each combine group.

### Patient Data

In `data/patient_data` one finds examples of patient data with 16 and 4096 patients.
However, these files should not be created manually.
Instead, use the script `scripts/evaluation/generate_data.py` to generate a new patient data file.
In the script, one can specify the number of patient records to be generated.

## Evaluation

### Run the Evaluation

To run the evaluation, one can use the script in `./scripts/evaluation/run_eval.sh`.
This executes the corresponding Python script for running the evaluation.
In the Python script, one can adjust the number of iterations used for the evaluation.
Furthermore, if one changes the batch sizes of the matching algorithms, one also needs to change the batch sizes in the Python script.

The evaluation outputs the collected data to `./test-results/{entitiy}/{test-id}/`.
The `test-id` is automatically generated and can be found in the logs in `./python_logs/` containing the evaluation output.
The output of the individual entity runs can be found under `./logs/{entity}/{run-id}`.

In case a previous evaluation did not terminate correctly, one can use the script in `./scripts/evaluation/kill_eval.sh` to shut down any dangling processes.
You may need to adjust the script for your current user.

### Build the Plots

We provide a script in `./scripts/evaluation/plots/generate_graphs.py`, which enables direct creation of graphs based on the collected evaluation data.
We always create graphs for a specific `test-id`, which one needs to set in the script (`./scripts/evaluation/plots/common/parameters.py`) to use the correct evaluation data.
Furthermore, as for the evaluation run, one needs to adjust the batch sizes for the matching types in case they are changed.
To verify that the evaluation did run successfully, you must additionally set the number of iterations you executed the evaluation for.
Then, the script checks whether the specified iterations are available for all test cases.
Finally, one can also adjust the confidence for the error bars in the graphs.

The final graphs are output in PDF format to `./graphs/`.
However, for preview purposes, we also provide a `mode_show` variable in the script.
When this variable is set to true, all graphs are previewed using matplotlib.

## Command Line Parameters

All three entities can be configured using command line parameters.
For the evaluation, the `test-id` and `run-id` parameters are important.
The `test-id` remains the same for all different test cases and iterations, providing a common identifier among all system runs for the evaluation.
In contrast, the `run-id` is unique for each system run. However, the three entities must all be started with the same `run-id` to enable the merging of the individual
entity data.

### Trusted Authority

| Name                    | Required | Description                                                                                 | Example                                     |
|-------------------------|:--------:|---------------------------------------------------------------------------------------------|---------------------------------------------|
| `test-id`               |    ✅     | ID of the current eval test                                                                 | `--test-id="1a2b3c"`                        |
| `run-id`                |    ✅     | ID of the current eval run                                                                  | `--run-id="4d5e6f"`                         |
| `attribute-config-file` |    ✅     | Path to an attribute config JSON file. Determines the keys that are generated by the TA     | `--attribute-config-file="/test/attr.json"` |
| `use-cached-data, C`    |    ❌     | Use key data generated in a previous run.                                                   | `-C`                                        |
| `use-toy-parameters`    |    ❌     | Use the OpenFHE Toy Parameters for fast key generation and query execution. Testing only!!! | `--use-toy-parameters`                      |

### Server

| Name                     | Required | Description                                                                                                                                                                                                                                                                | Example                                     |
|--------------------------|:--------:|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------|
| `test-id`                |    ✅     | ID of the current eval test                                                                                                                                                                                                                                                | `--test-id="1a2b3c"`                        |
| `run-id`                 |    ✅     | ID of the current eval run                                                                                                                                                                                                                                                 | `--run-id="4d5e6f"`                         |
| `attribute-config-file`  |    ✅     | Path to an attribute config JSON file. Used for the correct table creation for the different attributes.                                                                                                                                                                   | `--attribute-config-file="/test/attr.json"` |
| `database-file`          |    ❌     | Path to the database file. Defaults to `db/server.db`                                                                                                                                                                                                                      | `--database-file="test.db"`                 |
| `drop-tables, D`         |    ❌     | Drop all the database tables on start.                                                                                                                                                                                                                                     | `-D`                                        |
| `plaintext, P`           |    ❌     | Perform matching on plaintext data emulating the encrypted operations                                                                                                                                                                                                      | `-P`                                        |

### Client

| Name                     | Required | Description                                                                                                                                  | Example                                     |
|--------------------------|:--------:|----------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------|
| `test-id`                |    ✅     | ID of the current eval test                                                                                                                  | `--test-id="1a2b3c"`                        |
| `run-id`                 |    ✅     | ID of the current eval run                                                                                                                   | `--run-id="4d5e6f"`                         |
| `attribute-config-file`  |    ✅     | Path to an attribute config JSON file. Provides the required values for pre-processing                                                       | `--attribute-config-file="/test/attr.json"` |
| `patient-data-file`      |    ✅     | Path to a JSON file containing the patient data for upload to the server                                                                     | `--patient-data-file="data.json"`           |
| `query-file`             |    ✅     | Path to a query JSON file which will be executed after the data upload.                                                                      | `--query-file="query.json"`                 |
| `plaintext, P`           |    ❌     | Upload plaintext data to the server for plaintext matching.                                                                                  | `-P`                                        |
| `upload-iterations`      |    ❌     | Number of times the provided patient data file is uploaded to the server. Defaults to `1`.                                                   | `--upload-iterations=10`                    |
| `random-ids ,R`          |    ❌     | Generate random IDs for the patients before upload. Use this in case of multiple upload iterations to prevent ID collisions.                 | `-R`                                        |
| `shutdown-servers`       |    ❌     | Send shutdown command to TA and server after the client completed its interaction with them. Only available if compiled for evaluation runs. | `--shutdown-servers`                        |
