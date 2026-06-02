# SCD41 Industry-Readiness Hardening — Chunked Prompt Sequence

This bundle is designed to be fed to the coding agent one prompt at a time.

The audit report classifies the repository as a strong pre-production SCD41 driver, not yet industry-grade. The main blockers are asynchronous error visibility from `tick()`, broken/default timing behavior in Arduino-facing usage, inconsistent clocks, blocking/variable latency, missing ESP-IDF proof, missing hardware/HIL evidence, and several protocol-safety gaps.

Recommended order:

1. `01_branch_agents_baseline_and_low_risk_contracts.md`
2. `02_timing_contract_and_clock_model.md`
3. `03_tick_async_completion_and_stale_state.md`
4. `04_protocol_safety_crc_variant_raw_destructive.md`
5. `05_latency_state_machine_and_public_api_bounds.md`
6. `06_tests_ci_package_and_examples.md`
7. `07_hil_matrix_final_report_and_release_readiness.md`

Each prompt instructs the coder to commit and sync after completion. Do not skip ahead unless the current prompt is completed or explicitly blocked.

Expected final outcome:

- one clean hardening branch,
- updated `AGENTS.md`, README, Doxygen, docs, examples, CI/scripts, and native tests,
- a comprehensive final hardening report,
- honest readiness verdict and remaining hardware/HIL work.

If a prompt reveals that an architectural decision is risky, the coder should stop and write a decision note instead of forcing a bad refactor.
