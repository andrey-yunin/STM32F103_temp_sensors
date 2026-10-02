# DDS-240 shared documentation pointer

## Current checkpoint — 2026-10-02

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
