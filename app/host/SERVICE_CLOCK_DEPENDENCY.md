# Shared clock dependency for service restore

This branch consumes Bernoulli's committed M0 providers, unchanged at
`09931c9779f97e30202f61de89528c4f2d6ffe42`, to test the actual service restore
composition. These are integration dependencies, not a second clock owner:

- `48a6594`: original-image relationships, registry revision, duplicate tags
  and required flags for new captures.
- `515c2dc`: SessionClock and portable logical domain/detector state.
- `a1ba1b9`: original boot-image clock validation and completed frame keys.
- `09931c9`: pending tick requests and rejection during an active tick body.

The consumed files are SessionClock, SectionRegistry, EmuDomain,
RegimeDetector, TickSync, SessionCheckpoint, CheckpointCapture, their clock
and format tests, and `app/gui/clock.cmake`. The only dependency wiring change
is adding `include(clock.cmake)` to the existing isolated product module.
No product supervisor, InputSave, GUI or bootstrap implementation is imported.

Parent should integrate the original M0 stages and the subsequent service
join; skip this dependency synchronization when these exact providers are
already present. The service join must use the same GCLK provider, and must
never register a second GCLK or advance/re-anchor time after restored service
deadlines are installed.

Isolated validation: configure `app/gui` with `PS2R_PRODUCT_GUI=OFF`, build
`test_session_clock` and `test_checkpoint_clock` with `-j2`, then run
`SessionClock|CheckpointClock`. This does not build or run the game.
