# Native Test Layout

`test_basic.cpp` is a public-contract native Unity suite for the
framework-neutral SCD41 core. It uses an injected fixed-memory sensor transport
model; production code contains no fake transport.

Coverage includes:

- zero-I2C bind/admission/cancel/result/end contracts
- callback budgets, exact operation identity, deadlines, cancellation, and
  32-bit clock wrap across the distinct state-machine topologies
- deadline-aware next-poll hints during sensor waits, including clock wrap,
  without shortening retained command-safety windows
- successful execution and per-transfer fault injection for every public
  `OperationKind`
- attach convergence, mode admission, expected NACKs, and retained safety gates
- dedicated sensor-variant command/CRC decoding, exact attach/identity phase
- exact returned-setting domains, cache exclusion, partial masks, and full
  uint16 offset/ASC-target/FRC request boundaries
- zero-I2C typed stop rejection while idle without weakening attach recovery
- deterministic fixed-memory stress, mixed-stress, and selfcheck sequencing
  counts, the vendor serial example, and strict SCD40/SCD43/unknown rejection
- CRC-atomic sample/config publication, cache epochs, dirty/verified settings,
  EEPROM uncertainty, and passive health channels
- contradictory transport results, completion-clock failures, and command
  spacing after failed attempts
- response-phase diagnostics, read-only cancellation, repeated sampled owner
  time, idle/result-retention wrap, and cancellation safety-gate preservation
- numeric sensor execution waits and full settle retention after late or
  ambiguous writes, with one deadline reconciliation policy
- non-strict SCD40/SCD43 admission, diagnostic word counts and CRC-atomic
  payloads, zero offline threshold, and RHT-only CO2 validity
- reset/reinit dirty-state timing, cancellation/deadline boundaries before,
  at, and after settle (including clock wrap), and persistence uncertainty
  after failed identity verification
- fixed-width/copy/size checks for owner-boundary value types
- independent literal command/payload/CRC vectors from the datasheet
- both ASC periods accepting zero, the 4-hour minimum step and 65532-hour
  maximum, rejecting unsupported encodings without I2C, verifying readback,
  skipping unchanged writes, and persisting only with explicit confirmation
- equal-value compensation-source selection and its lifecycle invalidation
- malformed transfer enums/counts retaining reconciliation and settle windows
- unknown operation admission preserving the accepted owner clock
- non-strict SCD40 composite reads rejecting unsupported ASC period commands

Prefer public API assertions. Do not expose private driver state to make a test
easy; observable snapshots and terminal results are part of the contract.
