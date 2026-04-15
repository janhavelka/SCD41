# Contributing

Thanks for contributing to this repository.

## Quick Start

1. Fork the repository.
2. Create a focused branch: `git checkout -b feature/my-change`.
3. Keep device behavior aligned with `docs/SCD41_datasheet.md`.
4. Run the repository checks that apply to your change:
   - `python tools/check_core_timing_guard.py`
   - `python tools/check_cli_contract.py`
   - `pio test -e native`
   - `pio run -e esp32s3dev`
   - `pio run -e esp32s2dev`
5. Update `CHANGELOG.md` and package docs when behavior or metadata changes.
6. Commit with a clear message and open a Pull Request.

## Guidelines

### Source Of Truth

- Device behavior comes from `docs/SCD41_datasheet.md` first.
- If the datasheet is ambiguous, follow the strongest existing family pattern and document the assumption in `ASSUMPTIONS.md`.
- Do not present undocumented SCD41 behavior as certain.

### Code Style

- Follow the repository naming and layout conventions from `AGENTS.md`.
- Use `constexpr` instead of macros for constants.
- No heap allocation in steady-state library code.
- Keep long sensor operations bounded and `tick()`-driven instead of blocking for hundreds or thousands of milliseconds.
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
