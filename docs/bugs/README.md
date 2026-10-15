# `docs/bugs/`

The bug knowledge base (ARCH §17, D8): `TEMPLATE.md` (entry contract), `INDEX.md` (by
subsystem and by symptom), `TRIAGE.md` (symptom → subsystem → first checks), and one
`BUG-NNN-*.md` per root-caused defect. Every root-caused defect during construction adds or
updates an entry in the same change (CONSTRUCTION_PLAN §5.5); open OLD defects carried as
work items: BUG-026, BUG-038 (Phase 4), P4FIX-F1 (Phase 3 → closure Phase 4/5).

Belongs: bug entries with the PS2 rule violated and the evidence record. Does not belong:
plans, unverified hypotheses (those live in the phase IMPL record until root-caused).
