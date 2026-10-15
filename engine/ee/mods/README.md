# `engine/ee/mods/`

**Phase:** 9.3  
**Purpose:** ModHost: manifest loader, registry resolution, hard-fail on unresolved, detour install via the update_function_impl seam. OFF unless --mods. Mods-off is the only certified configuration.

## Belongs here

- ModHost, ManifestLoader, DetourInstaller

## Does not belong here

- workarounds (HOOK §6: never mods)

## Transplanted from (OLD, read-only)

- `— (new)`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
