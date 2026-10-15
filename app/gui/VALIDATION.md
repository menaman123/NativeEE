# Product integration validation

Base: main `70926b4`. Changes only in the product worker's assigned paths
on `port-product`; no main edits, shared build, full suite or game run.

## Commands and results

The product tests run as standalone CTest targets through
`app/gui/CMakeLists.txt`. Evidence is under
`<work>/int_work/m_wave/product-ui/`.

| Check | Result | Evidence |
| --- | --- | --- |
| First command stage | 130 checks, zero failures | `stage1-final-test.log`; commit `c4b7912` |
| Menu component stage | 151 command checks + 51 GUI/screenshot checks, zero failures | `stage2-click-test.log`, `stage2-final-panel.log`; commit `7a78847` |
| Current controls and real SDL input/motor handoff | 182 checks, zero failures | `runtime-final-test.log` |
| Real SDL + GL GUI, completed-picture orientation | 54 checks, zero failures | `runtime-final-test.log` |
| Actual main loop with synthetic cold-boot and restored-worker layouts | 22 checks, zero failures | `runtime-final-test.log` |
| Stuck synthetic worker | Production deadline reports timeout and exits 2 | `SessionShutdown` in `runtime-final-test.log` |
| AddressSanitizer + UndefinedBehaviorSanitizer | All four isolated CTest targets pass | `runtime-final-sanitize-test.log` |
| Production bootstrap and video source compilation | Both pass `-fsyntax-only`, Release flags, `-Wall -Wextra -Werror` | `ee_bootstrap.cpp.syntax.log`, `video_stack.cpp.syntax.log`; includes redirected to product worktree |
| Main builds UI without GL current; separate GL owner draws | Pass | `SessionGui` smoke target |
| Mouse Resume button uses the shared command queue | Pass | Synthetic SDL move/down/up in `SessionGui` smoke |
| Hotkey repeat/key-up and other-window routing | Pass | Both test targets |
| Wrong-owner SDL calls / missing current GL context rejected | Pass | `SessionGui` smoke target |
| Render restores viewport, depth-test and blend state | Pass | `SessionGui` smoke target |
| Closed-menu requests stay visible; result notices expire | Pass | Controlled-clock `SessionPanel::WantsFrame` fixture |
| Reproduction results remain pending/refused/failed until real completion | Pass | Command-stage OLD bug pins described in `INTEGRATION.md` |
| Provider sample/connection coherence; quick taps survive GUI-only pumps | Pass | `InputIsSampledOnlyOnMain` |
| Motors use actual capability/result on main; reset stops motors | Pass | `MotorRequestsUseActualSdlResults`, SDL virtual device callback |
| Pause keeps main responsive; F10 admits one boundary; window close wakes paused owner | Pass, both worker layouts | `RuntimeKeepsMainResponsive` |
| Capture refusal preserves submitted destination and does not claim a file | Pass | Real runtime hook fixture |
| Official ImGui source unchanged | All 16 checksums match extracted pinned upstream archive | `third_party/imgui/SOURCE_SHA256.txt` |
| One-primary-type/header-global lints | Zero findings | Scoped `tests/lint/` checks |
| Rendered menu | Visually inspected | `session-menu.png` / `session-menu.ppm` |

The tests use actual SDL/OpenGL and a synthetic game-boundary worker.
They execute no title code, read no game save, and replace no service.
Both GUI and real main-loop behavior are tested, including the case where
the boundary worker differs from the bootstrap/join supervisor.

Official primary documentation was read for SDL_PollEvent,
SDL_GL_MakeCurrent, SDL_GameControllerHasRumble and SDL_GameControllerRumble
from the SDL2 wiki. Snapshots are in the evidence directory; these establish
the event-owner rule and actual rumble success/failure semantics.

## Parent integration still required

Follow `INTEGRATION.md` for two shared CMake lines and the provider API.
Parent integrates prerequisite `4b0d22f` separately and preserves its service,
section-owner, checkpoint-ownership and post-restore reset hunks. Product
does not edit shared CMake, engine Input or title Input/Save composition.

Source inspection confirms GUI composition follows the renderer's regression
grab. Actual title execution, threaded/inline interactive presentation,
volume, fullscreen, close, and recorded play legs remain parent integration
checks. This worker did not run the game or the main suite.

Capture reports real quiescence refusal, then refuses unavailable fresh live
continuations even if owners are idle. Runtime restore is disabled. These are
open implementation items, not passed capture/restore tests. EOF does not
override parent's active movie-buffer ownership check, and any future runtime
restore must call `game_libraries.ResetAfterRestore()` before resuming.

No retirement census or game frame-time delta is claimed for this product
stage. The separate main-owned presenter adds a completed-picture readback
and upload. Its cost needs measurement at the integrated Release milestone.
