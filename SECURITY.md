# Security policy

## Reporting a vulnerability

Please report security issues **privately** through GitHub Security Advisories: open the
repository's *Security* tab → *Report a vulnerability* (this creates a private draft advisory visible
only to the maintainers). Do not open a public issue or pull request for a security problem.

Include what you can: the affected build (`BUILD-ID`, printed by `nativeee --version`), the
platform, the steps or input that trigger the problem, and the triage bundle if the process crashed
(`<user data>/logs/triage/<time>/` — see `README.md`, *Something went wrong?*). You will get an
acknowledgement, and a fix or a written assessment, through the advisory thread.

## Supported versions

Only the `main` branch is supported. Fixes land on `main`; there are no maintenance branches for
older tags.

## Scope

In scope — anything in this repository that handles untrusted input or runs with the user's
privileges:

- the disc-image reader and first-run extractor (`engine/cdvd`, `app/host/first_run.cpp`,
  `tools/disc_extract`): ISO 9660 parsing, the SHA-256 gate, path construction under the user data
  folder;
- the save-state, memory-card, settings (`ps2r.ini`, `bindings.ini`) and texture-pack loaders;
- the recompiled-code runtime and HLE services (memory bounds, host file access from guest
  requests);
- the build scripts and CI workflows (`build.sh`, `build.ps1`, `.github/workflows/`);
- anything that could make the repository or an artifact carry game data (`tools/ci/asset_scan.py`
  bypasses).

Out of scope:

- bugs in the game's own logic that only affect gameplay (open a normal issue);
- vulnerabilities in third-party components (SDL2, FFmpeg, Dear ImGui, …) — report them upstream;
  tell us too if NativeEE's use of the component makes them reachable;
- the user's own disc image, savestates or settings being readable by the user.

NativeEE ships no game data and never transmits anything over a network.
