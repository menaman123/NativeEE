# Shared graphics composition and complete-suite findings

GraphicsOutput now shares the renderer's actual surface allocations and submits
owned work through the ordered render consumer. It preserves the existing Strip
selection, upload ordering, clear history and render-thread ownership. This
does not activate the unfinished Movie/UI/world producers.

The Release-configured Debug/Release guard previously selected the Release
executable for both arms. It now uses distinct builds and rejects the same
file as both inputs. The negative control fails as intended; the real guard
passes cold boot and the restored gameplay checkpoint in every compared lane.

## Validation

Artifacts are under `<work>/int_work/m_wave/`.

- `build-stage10-release.log` and `build-stage10-debug.log`: full Release
  build and matching Debug application pass.
- `ctest-stage10-graphics.log`: five shared-scene/resource/renderer/bridge/
  service checks pass.
- `stage10-full-suite/`: 531 selected declarations, with the unchanged 20
  PCSX2 consumers excluded. **513 passed, seven failed, nine skipped and two
  disabled.** The scope inventory's four declared unavailable stubs are not
  the complete runtime skip list; `ctest.log` lists all nine.
- Every executed application test passes, including windowed movies, wall
  pacing, MTVU, restored checkpoints, Debug/Release identity and inline/
  threaded rendering identity. This is not the 13-leg ×3 retirement battery.
- The suite's source and binary identity checks report no changes while it
  ran. Its initial failures remain recorded rather than relabelled as a pass.

## Findings and repairs

| Failing test | Cause and repair |
| --- | --- |
| LintOneClass | Move Sky's rounding scope and mixer fixture providers into matching helper files. Preserve arithmetic and common-Mixer comparison arms. |
| LintSizeBudget | Extract capture ownership and diagnostic census from bootstrap; split independent service-oracle cases. Existing ceilings remain unchanged. |
| LintSessionTags | Express the continuation mutation pin at its unique guest-call site without a forbidden session-like identifier. |
| CoreSingleGetenvSite | Move movie-oracle export configuration to explicit test arguments. |
| RecompSymbolCensus | Scan the actually included generated continuation provider for definitions; missing, unincluded or unresolved providers still fail. |
| FiberGuardPage | The test's out-of-object array arithmetic let Release remove its faulting access. Use an explicit virtual-address probe; Debug and Release pass. |
| SifRpcBridge | A REALTIME fixture contaminated the next FRAME fixture's host fiber context. Isolate its lifecycle and assert unchanged caller ownership; model code is unchanged. |

The later integration stage records the repaired checks against its own source
and build. No model retirement or new default is accepted here. Frame-time
delta remains unmeasured; concurrent development runs are not the scheduled
idle-machine performance round.
