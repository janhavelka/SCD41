# SCD41 Industry-Readiness Hardening — Finding-Mapped Prompt Sequence

Use these prompts one by one with the coding agent. They are tailored directly to the SCD41 exploration report and are intended to fix the actual findings, not run another generic audit.

## Important operating model

- Work on the same SCD41 repository only.
- Start from the current repository state and create/use one hardening branch: `hardening/scd41-industry-readiness`.
- Feed prompts in numerical order.
- Each prompt is a bounded implementation chunk.
- The coding agent may spawn subagents in every chunk.
- After each prompt, the coding agent must run the relevant checks, update the cumulative progress report, commit, and push/sync the branch.
- Do not let the agent skip ahead. Later prompts depend on earlier design decisions.
- Do not let the agent invent hardware, ESP-IDF, or CI results.

## Finding coverage map

| Prompt | Exploration findings addressed |
| --- | --- |
| 01 | Setup, branch, AGENTS, baseline evidence, copy/move M-06, Doxygen/thread/ISR L-03, docs honesty, Version.h/package M-09 initial guard |
| 02 | H-02 default timing contract, H-04 inconsistent clocks, fallback timing, Arduino example hooks, quick-start docs |
| 03 | H-01 silent async `tick()` failures, async status/event surfacing, periodic/single-shot/self-test/FRC failure visibility |
| 04 | M-01 wake-up expected-NACK masking, L-01 CRC health policy, M-02/M-07 raw helper safety, M-03 truncated response contract |
| 05 | M-04 SCD41-only variant gating, L-05 temperature-offset scale, M-10 destructive command confirmation |
| 06 | H-03 blocking/variable latency, `begin()`/`readSettings()`/due `tick()` latency, M-05 stale cached samples across reset epochs, L-02 probe side effects |
| 07 | M-08 test-suite structure, H-05 ESP-IDF CI/build proof, M-09 package/generated header proof, L-04/L-06 validation docs/guards |
| 08 | H-05 hardware/HIL matrix, safe smoke, fault and destructive opt-in procedures, final report and release readiness |

## Required cumulative report

Every chunk must update this file:

```text
docs/SCD41_HARDENING_PROGRESS.md
```

The final chunk must produce or update:

```text
docs/SCD41_HARDENING_FINAL_REPORT.md
```

## Standard end-of-chunk routine

At the end of every prompt, the coding agent must run all checks available for that chunk, then:

```bash
git diff --check
git status --short
git add <changed-files>
git commit -m "<concise scope message>"
git push -u origin hardening/scd41-industry-readiness
```

If there is no remote, no network, or push fails, the agent must say exactly that and leave the commit local. If checks cannot be run, the agent must record exactly why.

