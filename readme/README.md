# DDS-240 shared documentation pointer

## Current checkpoint — 2026-10-06, session closed

Resume from the [Thermo handoff](../../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md). T01–T15 are BUILD with partial
bench evidence; T16/Release remains deferred. CAN, recovery and 30-minute
results are recorded in the shared report §§33.42–33.73. Full acceptance
is still open; one transient missing channel recovered during the long run.
Next: watchdog R03 on the separate WatchdogTest image. Four fault hooks,
test/production builds and host checks are ready; the user checked the build.
Flashing the test image is not confirmed; hardware R03 has not run.
See `tests/WATCHDOG_R03.md` in the project root. Ordinary Debug excludes hooks.
R04–R06 and phase-specific R01 remain open. Queue changes are deferred.
All checkpoints below are historical.

## Historical test-plan preparation — 2026-10-06

T01–T15 are BUILD; hardware acceptance is pending. Release/T16 is deferred
by the user until testing and functional verification. Use the shared
[Thermo test plan](../../DDS-240_readme/DDS-240_eko_system/Thermo/THERMO_EXECUTOR_TEST_PLAN.md).
At the time of plan preparation, all bench cases were NOT RUN. The checkpoints below are historical.

## Historical checkpoint — 2026-10-05

Session finished. Resume from the [Thermo handoff](../../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md).
T01–T13 are BUILD; hardware acceptance remains open. T13 preserves successful
samples across errors, applies the agreed 9000 ms age limit and clears a
channel sample after ROM-map application (D05).
Next: finish agreeing T14 against HC scheduling before providing manual edits.
The proposed 3000 ms start-to-start period is not implemented or finally agreed.
T15/T16 remain open. Last incremental Debug build: text=40052, data=100,
bss=14264; working-source host model passed. No commit, flashing or bench work
this session. Firmware changes remain uncommitted. Entries below are history.

## Historical checkpoint — 2026-10-02

Resume from the [Thermo handoff](../../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md).
T07/T08 are BUILD; hardware acceptance remains open. T09 scope is discussed,
but its code is unchanged: preserve PRIMASK in CAN_Diagnostics_GetSnapshot.
Define each change's boundaries from the HC reference and Thermo requirements
before proposing code. Session finished; entries below are historical.

## Historical checkpoint — 2026-10-01

Use the [current Thermo handoff](../../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md).
T01–T06 have build confirmation; T07 is partially implemented and the user
confirmed a clean build of its first part. PrepareReset is not connected to
F002/F006 yet. Continue driver cancellation and domain/map protection before
dispatcher integration. Testing follows completed unification.
The older resume-T01 instruction below is historical.

## Current Thermo entry point — 2026-09-30

[Thermo handoff and rules](../../DDS-240_readme/DDS-240_eko_system/Thermo/NEXT_SESSION_PROMPT.md)
is the active entry point for this project. Use the adjacent Thermo ↔ HC audit
matrix before each implementation block. T01 is implemented: Debug/model
PASS, hardware acceptance pending. Next session resumes T01 in Thermo;
see report §33.14 and the handoff for the remaining check.
The generic ecosystem handoff below is retained
as a navigation link, not a replacement for Thermo's current scope.

---

This project uses the shared DDS-240 documentation folder from the STM32CubeIDE
workspace instead of keeping a local copy of ecosystem documents.

## Shared documentation

Common documentation root:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme
```

Ecosystem documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system
```

Thermo executor documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Thermo
```

## Key entry points

Ecosystem standard:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md
```

Next session prompt:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/NEXT_SESSION_PROMPT.md
```

Thermo status report:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Thermo/TEMP_SENSORS_EXECUTOR_REPORT.md
```
