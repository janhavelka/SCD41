# Recorded software validation evidence

This file retains historical CI evidence from completed reviews. The results
apply only to the named implementation commits, not to subsequent changes or
the current working tree. Finding discussions and superseded audit inputs
remain in git history; current contracts are in the reference, integration,
porting, and validation guides.

No physical SCD41 or HIL run was performed for these records. Firmware builds,
host tests, and parser checks do not establish sensor operation. The physical
release gates remain in [the HIL guide](../validation/hardware-hil.md).

## 2026-09-08

Implementation commit: `ebcd1e4612986a03190d469313275299549a7fc6`.
[CI run 34232930910](https://github.com/janhavelka/SCD41/actions/runs/34232930910)
passed all seven jobs. The original review recorded inspection of the job logs.

| Check | Recorded evidence |
| --- | --- |
| Native public-contract tests | 68/68 passed. |
| Native undefined-behavior sanitizer | 68/68 passed on Linux. |
| HIL helper tests | 16/16 passed; no serial hardware was used. |
| Guard regressions | 7/7 passed. |
| Arduino ESP32-S2 / ESP32-S3 | Both firmware builds passed. |
| Native ESP-IDF v6.0.1 ESP32-S2 | `Project build complete`, 13:38:25 UTC. |
| Native ESP-IDF v6.0.1 ESP32-S3 | `Project build complete`, 13:38:29 UTC. |
| Package | Content, clean-consumer, and target-consumer checks passed. |
| Documentation and metadata | Doxygen, synchronized metadata, repository/CLI guards, generated-file and whitespace checks passed. |

The local Windows GCC lacked its UBSan runtime, and local native ESP-IDF was
unavailable. Those gaps were closed for this commit by the recorded isolated
CI jobs above, not by local or physical validation.

## 2026-09-05

Implementation commit: `98dd2f9aeaca0211c8569c0eeac74db60171a73d`.
[CI run 33987486144](https://github.com/janhavelka/SCD41/actions/runs/33987486144)
passed all seven jobs. The original review recorded 67/67 native and sanitizer
tests, Arduino and native ESP-IDF v6.0.1 firmware builds for both targets,
package-consumer checks, and documentation/tooling guards. This earlier record
is preserved as software evidence only.
