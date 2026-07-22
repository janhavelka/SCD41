# Native Test Layout

`test_basic.cpp` is a public-contract native Unity suite for the
framework-neutral SCD41 core. It uses an injected fixed-memory sensor transport
model; production code contains no fake transport.

Coverage includes:

- zero-I2C bind/admission/cancel/result/end contracts
- callback budgets, exact operation identity, deadlines, cancellation, and
  32-bit clock wrap across the distinct state-machine topologies
- successful execution and per-transfer fault injection for every public
  `OperationKind`
- attach convergence, mode admission, expected NACKs, and retained safety gates
- CRC-atomic sample/config publication, cache epochs, dirty/verified settings,
  EEPROM uncertainty, and passive health channels
- contradictory transport results, completion-clock failures, and command
  spacing after failed attempts
- fixed-width/copy/size checks for owner-boundary value types

Prefer public API assertions. Do not expose private driver state to make a test
easy; observable snapshots and terminal results are part of the contract.
