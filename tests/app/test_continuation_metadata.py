#!/usr/bin/env python3
"""Emitter pins: extra work must revoke a restart proof, not bless a thunk."""
import importlib.util
import contextlib
import sys
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# The generated recomp TUs: PS2R_RECOMP_DIR (the build directory) or the tree (tests/recomp/recomp_paths.py).
sys.path.insert(0, str(ROOT / "tests/recomp"))
from recomp_paths import recomp_dir  # noqa: E402
spec = importlib.util.spec_from_file_location(
    "continuations", ROOT / "tools/recompiler_v2/continuations.py")
emitter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(emitter)
census_spec = importlib.util.spec_from_file_location(
    "recomp_symbol_census", ROOT / "tools/recomp_symbol_census.py")
census = importlib.util.module_from_spec(census_spec)
census_spec.loader.exec_module(census)

BODY = """// Function: FUN_002d65c8 at 0x2d65c8
void FUN_002d65c8(CpuContext& ctx, uint32_t addr) {
bool branch_taken;
bool cond;
    switch (addr) {
        case 0x2d65d8: goto Label_0001;
    }
Label_0000: // 0x2d65c8
  {
        ctx.cpuRegs.GPR.r[3].SD[0] = static_cast<int32_t>(static_cast<int32_t>(ctx.cpuRegs.GPR.r[0].SL[0]) + 0x56);
        runtime_syscall_dispatcher(ctx.cpuRegs.GPR.r[3].UL[0], ctx);
       //nop
    return; // Return from function
  }
Label_0001: // 0x2d65d8
  { memory::write<uint32_t>(0x1000, 42); return; }
}
"""


class MetadataPins(unittest.TestCase):
    def collect(self, body):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recompiled_shard_000.cpp"
            path.write_text(body)
            return emitter.collect([path])[0]

    def test_merged_wait_only_replays_entry_block(self):
        self.assertEqual(self.collect(BODY), [(0x2d65c8, [0x2d65c8, 0x2d65d8], 3)])

    def test_extra_effect_before_wait_revokes_restart(self):
        changed = BODY.replace("        runtime_syscall",
                               "        memory::write<uint32_t>(0x1000, 1);\n        runtime_syscall")
        self.assertEqual(self.collect(changed)[0][2], 0)

    def test_extra_prologue_revokes_continuations(self):
        changed = BODY.replace("bool cond;", "bool cond;\nctx.cpuRegs.GPR.r[2].UL[0] = 1;")
        self.assertEqual(self.collect(changed), [])

    def test_missing_exact_target_fails_generation(self):
        with self.assertRaises(ValueError):
            self.collect(BODY.replace("goto Label_0001;", "goto Missing;"))

    def test_containment_and_non_entry_cases_do_not_make_resume_points(self):
        changed = BODY.replace("return; // Return", "switch(ctx.cpuRegs.pc) { case 0x2d65cc: break; }\nreturn; // Return")
        self.assertNotIn(0x2d65cc, self.collect(changed)[0][1])

    def test_wrong_syscall_revokes_restart(self):
        self.assertEqual(self.collect(BODY.replace("+ 0x56)", "+ 0x55)"))[0][2], 0)

    def test_audit_is_a_byte_proof_not_an_address_exception(self):
        directory = recomp_dir(ROOT)
        source = (directory / "recompiled_shard_008.cpp").read_text()
        prologue = source.split("void FUN_0017d558(CpuContext& ctx, uint32_t addr) {", 1)[1]
        prologue = prologue.split("    switch (addr)", 1)[0]
        self.assertTrue(emitter.audited("prologues", 0x17d558, prologue))
        self.assertFalse(emitter.audited("prologues", 0x17d558, prologue + "ctx.cpuRegs.pc = 1;"))
        self.assertFalse(emitter.audited("prologues", 0x17d558 + 4, prologue))

    def test_instrumented_wrapper_mutation_revokes_its_audit(self):
        directory = recomp_dir(ROOT)
        source = (directory / "recompiled.cpp").read_text()
        guest_call = "        FUN_0017ce50(ctx, addr);"
        self.assertEqual(source.count(guest_call), 1)
        changed = source.replace(guest_call, "        ctx.cpuRegs.pc = 1;\n" + guest_call, 1)
        output = emitter.instrument_audited_wrappers(changed)
        self.assertNotIn("DispatchTable::GeneratedBody(0x17ce50,", output)
        self.assertIn("recompiled_functions[0x17ce50] = [](CpuContext&", output)

    def census_fixture(self, common, provider):
        with tempfile.TemporaryDirectory() as directory:
            if provider is not None:
                (Path(directory) / "continuations.h").write_text(provider)
            sources = [("recompiled.cpp", common)] + census.continuation_provider(directory, common)
            with contextlib.redirect_stdout(io.StringIO()):
                return census.unlisted_scan(sources)

    def test_included_generated_lookup_is_a_definition(self):
        provider = emitter.metadata([(0x1000, [0x1000], 0)], [])
        common = '#include "continuations.h"\nvoid call() { recomp_continuations(0x1000); }\n'
        self.assertEqual(self.census_fixture(common, provider), [])

    def test_lookup_declaration_cannot_replace_its_definition(self):
        common = '#include "continuations.h"\nvoid call() { recomp_continuations(0x1000); }\n'
        provider = "DispatchTable::Continuations recomp_continuations(uint32_t fn);\n"
        self.assertEqual(self.census_fixture(common, provider), ["recomp_continuations"])

    def test_unincluded_sidecar_cannot_supply_a_definition(self):
        provider = emitter.metadata([(0x1000, [0x1000], 0)], [])
        common = "void call() { recomp_continuations(0x1000); }\n"
        self.assertEqual(self.census_fixture(common, provider), ["recomp_continuations"])

    def test_missing_sidecar_is_not_ignored(self):
        with self.assertRaises(FileNotFoundError):
            self.census_fixture('#include "continuations.h"\n', None)

    def test_new_unresolved_provider_seam_still_fails(self):
        provider = emitter.metadata([(0x1000, [0x1000], 0)], [])
        provider += "extern void recomp_unknown_fixture();\n"
        common = '#include "continuations.h"\nvoid call() { recomp_continuations(0x1000); }\n'
        self.assertEqual(self.census_fixture(common, provider), ["recomp_unknown_fixture"])


if __name__ == "__main__":
    unittest.main()
