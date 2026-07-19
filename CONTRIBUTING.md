# Contributing

Thanks for contributing to this repository.

## Quick Start

1. Fork the repository.
2. Create a focused branch: `git checkout -b feature/my-change`.
3. Keep device behavior aligned with `docs/reference/scd41-protocol.md`.
4. Run the repository checks that apply to your change:
   - `python tools/check_core_timing_guard.py`
   - `python tools/check_cli_contract.py`
   - `python tools/check_idf_example_contract.py`
   - `python scripts/generate_version.py check`
   - `pio test -e native`
   - `pio test -e native_ubsan`
   - `pio run -e esp32s3dev`
   - `pio run -e esp32s2dev`
5. Update `CHANGELOG.md` and package docs when behavior or metadata changes.
6. Commit with a clear message and open a Pull Request.

## Guidelines

### Source Of Truth

- Device behavior comes from `docs/reference/scd41-protocol.md` first.
- If the datasheet is ambiguous, follow the strongest existing family pattern and document the assumption in `ASSUMPTIONS.md`.
- Do not present undocumented SCD41 behavior as certain.

### Code Style

- Follow the repository naming and layout conventions from `AGENTS.md`.
- Use `constexpr` instead of macros for constants.
- No heap allocation in steady-state library code.
- Keep all sensor work in the one `start()` / callback-budgeted `poll()` /
  `takeResult()` model. `tick()` is only a one-callback compatibility executor.
- Keep admission, cancellation, and result consumption zero-I2C. Preserve
  absolute deadlines, `nextSafeCommandMs`, and one terminal result per ID.
- Keep transport attempts single-shot. Retry, bus reset, power cycling, and
  locking belong to the application owner.
- Keep EEPROM-writing commands explicit and rare.

### Pull Requests

- Keep PRs narrow and intentional.
- Do not edit unrelated libraries in the workspace.
- Do not hide device-specific assumptions; record them in `ASSUMPTIONS.md` or the README.
- Update documentation when the public API, examples, or command semantics change.
- Ensure CI passes before requesting review.

### Commits

Use [Conventional Commits](https://www.conventionalcommits.org/) where practical:

- `feat:` new feature
- `fix:` bug fix
- `docs:` documentation only
- `refactor:` internal improvement
- `test:` test changes
- `chore:` tooling or maintenance

## What We Accept

- Bug fixes
- Documentation improvements
- Better validation and tooling
- Carefully scoped feature work that matches the SCD41 datasheet and the family architecture

## What We Probably Won't Accept

- Breaking API changes without discussion
- Hidden persistence or background EEPROM writes
- Platform-specific code in the library core
- New dependencies that add steady-state heap use

## Questions

Open an issue or discussion with the exact command, bus setup, and device state involved.
