#!/usr/bin/env python3
"""Adversarial DT-12 fixtures. No game, CTest suite, console data or build runs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import leg_spec
import regression_ref
import retirement_dt12 as gate
import retirement_gate as history
import retirement_scope
import retirement_counters


class Fixture:
    def __init__(self, root):
        self.root = root
        self.baseline, self.battery, self.ref, score, _ = history.test_fixture(root)
        # Nonmapped fixture first; a separate test exercises measured scene data.
        (self.baseline / "coldboot_attract/LEG.tsv").write_text(
            leg_spec.format_spec(13, 1, 60, 60, "coldboot", [(7, 13)], "DT-12 fixture"))
        self.ref_path = self.baseline / "REGRESSION_REF.tsv"
        self.ref_path.write_text(regression_ref.format_ref(score, "test fixture"))
        self.source = root / "source"
        self.decomp = root / "decomp"
        self.old = root / "old"
        self.decomp.mkdir()
        self.old.mkdir()
        self.source.mkdir()
        self.sources = {}
        for name, contents in {
            "engine/ipu/ipu_device.cpp": "++command_counts_[cmd];\n",
            "app/host/subsystem_census.cpp": 'meta.Set("ipu_commands", commands);\n',
            "movie.cpp": "constexpr bool kMovie = true;\n++pictures_completed;\n",
        }.items():
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
            self.sources[name] = gate.digest(path)
        self.source_manifest = root / "sources.json"
        self.write(self.source_manifest, {"schema": gate.SOURCE_SCHEMA, "root": str(self.source), "files": self.sources})
        (root / "game").write_bytes(b"fabricated game binary; never executed")
        (root / "oracle-test").write_bytes(b"fabricated test binary; never executed")
        (self.decomp / "movie.cpp").write_text("Movie::DecodePicture\n")
        (self.old / "movie_bug.md").write_text("BUG-FIXTURE: preserve pictures across a reopen\n")
        self.inventory_path, self.ctest_path = root / "ctest-inventory.json", root / "ctest.log"
        self.test = "MovieReplacementOracle"
        inventory = {"kind": "ctestInfo", "version": {"major": 1, "minor": 0}, "tests": [
            {"name": "Render_TitleFrameReplay", "command": [str(root / "unused-pcsx2-test")]},
            {"name": self.test, "command": [str(root / "oracle-test"), "--both-paths"]},
            {"name": "NewWriterFixture", "command": [str(root / "writer-test")]},
            {"name": "DisabledByConfiguration", "properties": [{"name": "DISABLED", "value": True}]},
        ]}
        self.write(self.inventory_path, inventory)
        self.ctest_path.write_text(
            "1/3 Test #2: MovieReplacementOracle .... Passed 0.01 sec\n"
            "2/3 Test #3: NewWriterFixture .... Passed 0.01 sec\n"
            "3/3 Test #4: DisabledByConfiguration .... ***Not Run (Disabled) 0.00 sec\n"
            "100% tests passed, 0 tests failed out of 2\n")
        self.manifest_path = root / "evidence.json"
        self.manifest = {
            "schema": gate.SCHEMA,
            "authorities": {"decomp": str(self.decomp), "old": str(self.old)},
            "build": {"binary": self.blob(root / "game"), "sources": self.blob(self.source_manifest)},
            "ctest": {"inventory": self.blob(self.inventory_path), "log": self.blob(self.ctest_path),
                      "source_sha256": gate.digest(self.source_manifest)},
            "replacements": [{
                "id": "MOVIE-PICTURE", "intent": "decode one movie picture",
                "source_intents": [self.reference(self.decomp / "movie.cpp", "Movie::DecodePicture")],
                "toggle": {"name": "movie.decode", "model": "off", "native": "on", "default": "native",
                           "source": self.reference(self.source / "movie.cpp", "constexpr bool kMovie = true;")},
                "model_counters": ["IPU.command.idec"],
                "native_counter": {"field": "movie_decoder", "member": "pictures",
                                   "source": self.reference(self.source / "movie.cpp", "++pictures_completed;")},
                "oracles": ["movie-reopen"],
                "old_bug_pins": [{"id": "BUG-FIXTURE", "record": self.reference(
                    self.old / "movie_bug.md", "BUG-FIXTURE"),
                    "test": self.test, "oracle": "movie-reopen"}],
            }],
            "oracles": [{"id": "movie-reopen", "test": self.test, "executable": self.blob(root / "oracle-test")}],
        }
        for mode in ("model", "native"):
            folder = root / mode
            folder.mkdir()
            (folder / "input.bin").write_bytes(b"same owned input to NEW paths")
            (folder / "picture.bin").write_bytes(b"picture-bytes")
            outputs = {"picture": self.blob(folder / "picture.bin")}
            receipt = {
                "schema": gate.ORACLE_SCHEMA, "mode": mode, "test": self.test,
                "command": [str(root / "oracle-test"), "--both-paths"],
                "exit_code": 0, "completed": True,
                "binary_sha256": self.manifest["oracles"][0]["executable"]["sha256"],
                "source_sha256": gate.digest(self.source_manifest),
                "input_sha256": gate.digest(folder / "input.bin"),
                "settings": {"movie.decode": "off" if mode == "model" else "on", "shared": "deterministic"},
                "outputs": {name: record["sha256"] for name, record in outputs.items()},
                "counters": {"ipu_commands": "idec=3" if mode == "model" else "idec=0",
                             "movie_decoder": "pictures=0" if mode == "model" else "pictures=3"},
            }
            self.write(folder / "receipt.json", receipt)
            self.manifest["oracles"][0][mode] = {
                "input": self.blob(folder / "input.bin"), "outputs": outputs,
                "receipt": self.blob(folder / "receipt.json"),
            }
        for path in self.battery.iterdir():
            meta = history.metadata(path)
            meta.update(ipu_commands="idec=0", movie_decoder="pictures=3")
            self.write_meta(path, meta)
        self.save()
        gate.seal_battery(self.battery, self.ref["legs"], gate.build_fingerprint(self.manifest_path))

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")

    @staticmethod
    def blob(path):
        return {"path": str(path), "sha256": gate.digest(path)}

    def reference(self, path, needle):
        return {"file": self.blob(path), "needle": needle}

    def save(self):
        self.write(self.manifest_path, self.manifest)

    @staticmethod
    def write_meta(path, meta):
        (path / "run.meta").write_text("".join("%s=%s\n" % item for item in meta.items()))

    def reseal(self, path):
        receipt = path / "retirement-run.json"
        value = json.loads(receipt.read_text())
        value["artifacts"] = {name: gate.digest(path / name) for name in value["artifacts"]}
        self.write(receipt, value)

    def change_meta(self, name, change):
        path = self.battery / name
        meta = history.metadata(path)
        change(meta)
        self.write_meta(path, meta)
        self.reseal(path)

    def change_oracle_receipt(self, mode, change):
        record = self.manifest["oracles"][0][mode]["receipt"]
        path = Path(record["path"])
        value = json.loads(path.read_text())
        change(value)
        self.write(path, value)
        record["sha256"] = gate.digest(path)
        self.save()

    def output(self, mode, contents):
        path = self.root / mode / "picture.bin"
        path.write_bytes(contents)
        self.manifest["oracles"][0][mode]["outputs"]["picture"] = self.blob(path)
        self.change_oracle_receipt(mode, lambda receipt: receipt["outputs"].update(picture=gate.digest(path)))

    def evaluate(self):
        self.save()
        return gate.evaluate(self.manifest_path, self.ref_path, self.battery, self.ctest_path, self.inventory_path)


class RetirementTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="dt12_")
        self.addCleanup(self.temp.cleanup)
        self.fixture = Fixture(Path(self.temp.name))

    def reject(self, phrase=None):
        try:
            result = self.fixture.evaluate()
        except (ValueError, OSError, KeyError, TypeError, AttributeError) as error:
            if phrase:
                self.assertIn(phrase, str(error))
        else:
            self.assertEqual(result["verdict"], "FAIL", result)
            self.assertTrue(result["failures"])
            if phrase:
                self.assertIn(phrase, str(result["failures"]))

    def test_complete_evidence(self):
        result = self.fixture.evaluate()
        self.assertEqual(result["verdict"], "PASS", result)
        self.assertEqual(result["policy"], "DT-12")
        self.assertEqual(len(result["evidence"]["battery_receipts"]), 39)
        self.assertEqual(result["ctest_scope"]["selected"],
                         ["DisabledByConfiguration", "MovieReplacementOracle", "NewWriterFixture"])
        self.assertEqual(result["replacements"][0]["retired_operations"],
                         {"IPU.command.idec": "IPU command dispatch: idec"})

    def test_memory_and_device_snapshots_not_required(self):
        for path in self.fixture.battery.iterdir():
            (path / "mem.mhash").unlink()
        self.assertEqual(self.fixture.evaluate()["verdict"], "PASS")

    def test_source_mutation(self):
        (self.fixture.source / "movie.cpp").write_text("changed source")
        self.reject("source fingerprint")

    def test_binary_mutation(self):
        (self.fixture.root / "game").write_bytes(b"another build")
        self.reject("digest mismatch")

    def test_missing_baseline(self):
        (self.fixture.baseline / "coldboot/frames.fhash").unlink()
        self.reject()

    def test_missing_scenario(self):
        shutil.rmtree(self.fixture.battery / "coldboot.rep3")
        self.reject("repetition set")

    def test_extra_scenario(self):
        (self.fixture.battery / "unrequested.rep1").mkdir()
        self.reject("repetition set")

    def test_changed_output_after_receipt(self):
        file = self.fixture.battery / "coldboot.rep1/frames.fhash"
        file.write_text(file.read_text().replace("0000000000000001", "0000000000000003"))
        self.reject("changed after collection")

    def test_frames_unstable_even_if_resealed(self):
        path = self.fixture.battery / "coldboot.rep1"
        file = path / "frames.fhash"
        file.write_text(file.read_text().replace("0000000000000001", "0000000000000003"))
        self.fixture.reseal(path)
        self.reject("repetition mismatch")

    def test_audio_regression_even_if_stable(self):
        for rep in (1, 2, 3):
            path = self.fixture.battery / ("coldboot.rep%d" % rep)
            file = path / "audio.ahash"
            file.write_text(file.read_text().replace("0x0000000000000001", "0x0000000000000003"))
            self.fixture.reseal(path)
        self.reject("audio first-divergent key 0")

    def test_later_divergence_accepted(self):
        for rep in (1, 2, 3):
            path = self.fixture.battery / ("coldboot.rep%d" % rep)
            file = path / "frames.fhash"
            file.write_text(file.read_text().replace("4,0x0000000000000002", "4,0x0000000000000001")
                            .replace("5,0x0000000000000001", "5,0x0000000000000002"))
            self.fixture.reseal(path)
        self.assertEqual(self.fixture.evaluate()["verdict"], "PASS")

    def test_nonzero_model_in_single_repetition(self):
        self.fixture.change_meta("scenario3.rep2", lambda meta: meta.update(ipu_commands="idec=1"))
        self.reject("replaced model IPU.command.idec executed")

    def test_missing_counter_not_zero(self):
        self.fixture.change_meta("coldboot.rep1", lambda meta: meta.pop("ipu_commands"))
        self.reject("ipu_commands")

    def test_duplicate_counter_member(self):
        self.fixture.change_meta("coldboot.rep1", lambda meta: meta.update(ipu_commands="idec=1,idec=0"))
        self.reject("malformed counter")

    def test_negative_counter(self):
        self.fixture.change_meta("coldboot.rep1", lambda meta: meta.update(ipu_commands="idec=-1"))
        self.reject("invalid counter")

    def test_zero_only_unobserved_replacement(self):
        for name in [path.name for path in self.fixture.battery.iterdir()]:
            self.fixture.change_meta(name, lambda meta: meta.update(movie_decoder="pictures=0"))
        self.reject("zero-only")

    def test_some_unobserved_legs_reported(self):
        for rep in (1, 2, 3):
            self.fixture.change_meta("coldboot.rep%d" % rep, lambda meta: meta.update(movie_decoder="pictures=0"))
        result = self.fixture.evaluate()
        self.assertEqual(result["verdict"], "PASS")
        self.assertEqual(len(result["replacements"][0]["unexercised_runs"]), 3)

    def test_work_census_unstable(self):
        self.fixture.change_meta("coldboot.rep1", lambda meta: meta.update(movie_decoder="pictures=4"))
        self.reject("unstable replacement")

    def test_wrapper_cannot_stand_for_model(self):
        self.fixture.manifest["replacements"][0]["model_counters"] = ["graphics_kick_lowering.raw"]
        self.reject("unreviewed model")

    def test_error_counter_cannot_stand_for_model(self):
        self.fixture.manifest["replacements"][0]["model_counters"] = ["ipu_unmodelled"]
        self.reject("unreviewed model")

    def test_no_replacements_is_not_retirement(self):
        self.fixture.manifest["replacements"] = []
        self.reject("requires replacements")

    def test_no_self_oracle(self):
        self.fixture.manifest["oracles"] = []
        self.reject("requires replacements")

    def test_default_must_be_native(self):
        self.fixture.manifest["replacements"][0]["toggle"]["default"] = "model"
        self.reject("native default")

    def test_missing_old_bug_pin(self):
        self.fixture.manifest["replacements"][0]["old_bug_pins"] = []
        self.reject("no OLD-bug")

    def test_old_bug_pin_must_be_executed_oracle(self):
        self.fixture.manifest["replacements"][0]["old_bug_pins"][0]["test"] = "NewWriterFixture"
        self.reject("lacks executed replacement oracle")

    def test_missing_old_record(self):
        (self.fixture.old / "movie_bug.md").unlink()
        self.reject()

    def test_reference_needle_must_exist(self):
        self.fixture.manifest["replacements"][0]["source_intents"][0]["needle"] = "invented function"
        self.reject("needle absent")

    def test_build_receipt_must_match(self):
        path = self.fixture.battery / "coldboot.rep1/retirement-run.json"
        value = json.loads(path.read_text())
        value["binary_sha256"] = "f" * 64
        self.fixture.write(path, value)
        self.reject("build receipt mismatch")

    def test_missing_run_receipt(self):
        (self.fixture.battery / "coldboot.rep1/retirement-run.json").unlink()
        self.reject()

    def test_alias_is_not_an_independent_run(self):
        a = self.fixture.battery / "coldboot.rep1/audio.ahash"
        b = self.fixture.battery / "coldboot.rep2/audio.ahash"
        b.unlink()
        os.link(a, b)
        self.reject("reuses another run")

    def test_oracle_inputs_must_match(self):
        path = self.fixture.root / "native/input.bin"
        path.write_bytes(b"different owned input")
        self.fixture.manifest["oracles"][0]["native"]["input"] = self.fixture.blob(path)
        self.fixture.change_oracle_receipt("native", lambda r: r.update(input_sha256=gate.digest(path)))
        self.reject("inputs differ")

    def test_oracle_input_receipt_binding(self):
        self.fixture.change_oracle_receipt("native", lambda r: r.update(input_sha256="a" * 64))
        self.reject("build/input mismatch")

    def test_oracle_toggle_must_flip(self):
        self.fixture.change_oracle_receipt("model", lambda r: r["settings"].update({"movie.decode": "on"}))
        self.reject("toggle")

    def test_oracle_no_other_setting_can_change(self):
        self.fixture.change_oracle_receipt("native", lambda r: r["settings"].update(shared="different"))
        self.reject("settings differ beyond")

    def test_oracle_model_must_execute(self):
        self.fixture.change_oracle_receipt("model", lambda r: r["counters"].update(ipu_commands="idec=0"))
        self.reject("never exercised")

    def test_oracle_native_must_not_execute_model(self):
        self.fixture.change_oracle_receipt("native", lambda r: r["counters"].update(ipu_commands="idec=2"))
        self.reject("still executes")

    def test_oracle_native_must_produce_work(self):
        self.fixture.change_oracle_receipt("native", lambda r: r["counters"].update(movie_decoder="pictures=0"))
        self.reject("no completed replacement work")

    def test_oracle_failure_cannot_claim_success(self):
        self.fixture.change_oracle_receipt("native", lambda r: r.update(exit_code=1, verdict="PASS", skipped=0))
        self.reject("incomplete oracle")

    def test_oracle_outputs_missing(self):
        (self.fixture.root / "native/picture.bin").unlink()
        self.reject()

    def test_oracle_outputs_tampered(self):
        (self.fixture.root / "native/picture.bin").write_bytes(b"tampered")
        self.reject("digest mismatch")

    def test_oracle_outputs_unattributed(self):
        self.fixture.output("native", b"Picture-bytes")
        self.reject("unattributed")

    def test_empty_oracle_output(self):
        self.fixture.output("native", b"")
        self.reject("no observable output")

    def test_output_alias(self):
        source = self.fixture.root / "model/picture.bin"
        target = self.fixture.root / "native/picture.bin"
        target.unlink()
        os.link(source, target)
        self.reject("aliases")

    def add_attribution(self):
        self.fixture.output("native", b"Picture-bytes")
        expected = self.fixture.root / "expected.bin"
        expected.write_bytes(b"Picture-bytes")
        attribution = {
            "ranges": [[0, 1]], "reason": "fixture correction specified by Movie intent",
            "specification": self.fixture.reference(self.fixture.decomp / "movie.cpp", "Movie::DecodePicture"),
            "expected": self.fixture.blob(expected), "pin": self.fixture.test,
        }
        self.fixture.manifest["oracles"][0]["attributions"] = {"picture": attribution}
        return attribution

    def test_attributed_difference_bound_to_expected_bytes(self):
        self.add_attribution()
        self.assertEqual(self.fixture.evaluate()["verdict"], "PASS")

    def test_attribution_must_cover_all_actual_differences(self):
        self.add_attribution()["ranges"] = []
        self.reject("exact differing bytes")

    def test_attribution_expected_is_not_self_declared_pass(self):
        attr = self.add_attribution()
        path = self.fixture.root / "expected.bin"
        path.write_bytes(b"wrong expected result")
        attr["expected"] = self.fixture.blob(path)
        self.reject("attributed expected")

    def log_change(self, change):
        path = self.fixture.ctest_path
        path.write_text(change(path.read_text()))
        self.fixture.manifest["ctest"]["log"] = self.fixture.blob(path)

    def test_ctest_report_count_is_not_evidence(self):
        self.log_change(lambda _: "100% tests passed, 0 tests failed out of 2\n")
        self.reject("scope incomplete")

    def test_ctest_missing_unrelated_selected_test(self):
        self.log_change(lambda log: "\n".join(line for line in log.splitlines() if "NewWriterFixture" not in line))
        self.reject("scope incomplete")

    def test_ctest_skipped_test_not_pass(self):
        self.log_change(lambda log: log.replace("Passed", "***Skipped", 1))
        self.reject("required Passed")

    def add_unavailable_recorder(self, compiled=False):
        inventory = json.loads(self.fixture.inventory_path.read_text())
        command = ([str(self.fixture.root / "real-recorder")] if compiled else
                   [sys.executable, "-c", "import sys; print('SKIP: diagnostics build only'); sys.exit(77)"])
        inventory["tests"].append({"name": "CoreDrawlistRecorder", "command": command})
        self.fixture.write(self.fixture.inventory_path, inventory)
        self.fixture.manifest["ctest"]["inventory"] = self.fixture.blob(self.fixture.inventory_path)
        self.log_change(lambda log: log.replace("/3 Test", "/4 Test").replace(
            "100% tests passed, 0 tests failed out of 2",
            "4/4 Test #5: CoreDrawlistRecorder .... ***Skipped 0.00 sec\n"
            "100% tests passed, 0 tests failed out of 3"))

    def test_uncompiled_recorder_is_unavailable_not_a_bug_pin(self):
        self.add_unavailable_recorder()
        result = self.fixture.evaluate()
        self.assertEqual(result["verdict"], "PASS")
        self.assertEqual(result["ctest_scope"]["unavailable"], ["CoreDrawlistRecorder"])
        self.fixture.manifest["replacements"][0]["old_bug_pins"][0]["test"] = "CoreDrawlistRecorder"
        self.reject()

    def test_compiled_recorder_cannot_skip(self):
        self.add_unavailable_recorder(compiled=True)
        self.reject("required Passed")

    def test_ctest_failed_test(self):
        self.log_change(lambda log: log.replace("Passed", "***Failed", 1))
        self.reject("required Passed")

    def test_ctest_test_number_is_bound_to_inventory(self):
        self.log_change(lambda log: log.replace("Test #2", "Test #90"))
        self.reject("number does not match")

    def test_oracle_command_must_match_inventory(self):
        self.fixture.change_oracle_receipt("native", lambda receipt: receipt.update(command=["/another/test"]))
        self.reject("selected test command")

    def test_ctest_concatenated_invocations(self):
        self.log_change(lambda log: log + log)
        self.reject("duplicate/late")

    def test_ctest_excluded_pin_is_not_acceptance(self):
        self.log_change(lambda log: "Test #1: Render_TitleFrameReplay .... Passed 0.01 sec\n" + log)
        self.reject("outside DT-12 scope")

    def test_ctest_unknown_console_consumer_fails_inventory(self):
        value = json.loads(self.fixture.inventory_path.read_text())
        value["tests"].append({"name": "FutureConsolePin", "command": ["/bin/reader", "pcsx2_capture.bin"]})
        self.fixture.write(self.fixture.inventory_path, value)
        self.fixture.manifest["ctest"]["inventory"] = self.fixture.blob(self.fixture.inventory_path)
        self.reject("unclassified console")

    def test_ctest_inventory_hash_binding(self):
        self.fixture.inventory_path.write_text("{}")
        self.reject("digest mismatch")

    def test_scope_preserves_new_and_synthetic_tests(self):
        regex = retirement_scope.exclusion_regex()
        import re
        for name in retirement_scope.PCSX2_TESTS:
            self.assertIsNotNone(re.fullmatch(regex, name))
        for name in ("RootReleaseLanesIdentical", "Render_AttractReplaySelftest",
                     "RootTitleScreenEffectsSelftest", "RootAttractCutsceneFramesSelftest",
                     "VerifyScenePairingSelftest", "VerifyConsoleCompareSelftest", "VuCachedSuperblock"):
            self.assertIsNone(re.fullmatch(regex, name))

    def test_difference_ranges_include_tails(self):
        self.assertEqual(gate.difference_ranges(b"abcd", b"aXcd12"), [[1, 2], [4, 6]])
        self.assertEqual(gate.difference_ranges(b"abc", b"a"), [[1, 3]])

    def test_scene_pairing_uses_raw_new_ram_without_device_saves(self):
        import struct
        import scene_pairing
        fixture = self.fixture
        leg = "coldboot_attract"
        path = fixture.baseline / leg
        source = leg_spec.format_spec(13, 1, 60, 60, "coldboot", [(7, 13)], "measured fixture",
                                      pair_map="old_b6=new_b12,old_b7=new_b13", vf_offset=0)
        (path / "LEG.tsv").write_text(source)
        (path / "SCENE_KEYS.tsv").write_text("clock_word\t0x100\nboot_sm_word\t0x200\n")
        pairing = scene_pairing.make(leg_spec.parse(source), 6, "EDGE-FOUND", "image-census", 13,
                                     "b5:case6/bit63=0/clock1;b6:case7/bit63=0/clock2")
        captured = fixture.root / "edge"
        scene_pairing.write(str(captured), pairing)
        for boundary, case, clock in ((5, 6, 1), (6, 7, 2)):
            ram = bytearray(32 * 1024 * 1024)
            struct.pack_into("<I", ram, 0x100, clock)
            struct.pack_into("<Q", ram, 0x200, case << 44)
            (captured / ("memory_b%d.bin" % boundary)).write_bytes(ram)
        shutil.copyfile(fixture.battery / (leg + ".rep1/run.meta"), captured / "run.meta")
        proof = {
            "record": fixture.blob(captured / scene_pairing.FILE),
            "run_meta": fixture.blob(captured / "run.meta"),
            "before": fixture.blob(captured / "memory_b5.bin"), "before_boundary": 5,
            "after": fixture.blob(captured / "memory_b6.bin"), "after_boundary": 6,
            "binary_sha256": fixture.manifest["build"]["binary"]["sha256"],
            "source_sha256": fixture.manifest["build"]["sources"]["sha256"],
        }
        fixture.manifest["pairings"] = {leg: proof}
        self.assertEqual(fixture.evaluate()["verdict"], "PASS")
        proof["before_boundary"] = 4
        self.reject("not adjacent")
        proof["before_boundary"] = 5
        proof["binary_sha256"] = "a" * 64
        self.reject("fingerprint mismatch")
        proof["binary_sha256"] = fixture.manifest["build"]["binary"]["sha256"]
        # Even a hash-rebound bad capture cannot manufacture the scene edge.
        after = captured / "memory_b6.bin"
        ram = bytearray(after.read_bytes())
        struct.pack_into("<Q", ram, 0x200, 8 << 44)
        after.write_bytes(ram)
        proof["after"] = fixture.blob(after)
        self.reject("raw captures disagree")
        after.unlink()
        self.reject()

    def test_malformed_cli_replaces_stale_pass(self):
        self.fixture.manifest_path.write_text('{"schema":"dt12-retirement-v1","schema":"forged"}')
        report = self.fixture.root / "result.json"
        report.write_text('{"verdict":"PASS"}')
        process = subprocess.run(
            [sys.executable, str(Path(history.__file__)), "--evidence", str(self.fixture.manifest_path),
             "--battery", str(self.fixture.battery), "--ref", str(self.fixture.ref_path),
             "--ctest-log", str(self.fixture.ctest_path), "--ctest-inventory", str(self.fixture.inventory_path),
             "--json", str(report)], capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 1, process.stderr)
        result = json.loads(report.read_text())
        self.assertEqual(result["policy"], "DT-12")
        self.assertEqual(result["verdict"], "FAIL")
        self.assertIn("duplicate JSON", str(result["failures"]))

    def test_gate_run_default_retirement_does_not_enter_legacy_pipeline(self):
        # Invalid evidence must fail in preflight without launching guard/game/probes.
        self.fixture.manifest_path.write_text("{}")
        out = self.fixture.root / "run-gate"
        process = subprocess.run(
            [sys.executable, str(Path(history.__file__).with_name("gate_run.py")),
             "--retirement", "--evaluate-only", "--out", str(out),
             "--retirement-evidence", str(self.fixture.manifest_path),
             "--baseline", str(self.fixture.baseline), "--ctest-log", str(self.fixture.ctest_path),
             "--ctest-inventory", str(self.fixture.inventory_path)],
            capture_output=True, text=True, timeout=10)
        self.assertEqual(process.returncode, 1, process.stderr)
        self.assertEqual(json.loads((out / "retirement.json").read_text())["policy"], "DT-12")
        self.assertFalse((out / "probes").exists())
        self.assertFalse((out / "guard.log").exists())
        self.assertFalse((out / "battery").exists())

    def counter_catalog(self):
        root = (self.fixture.root / "catalog-repository").resolve()
        path = root / "games/example/verify/retirement_counters.json"
        path.parent.mkdir(parents=True)
        descriptor = {
            "field": "source_ops", "member": "draws", "scope": "captured draw body",
            "source": "games/example/hle/draw.cpp", "increment": "++original_draws;",
            "publisher": "games/example/hle/services.cpp",
            "publish": 'meta.Set("source_ops", Census());',
        }
        document = {"schema": "dt12-reviewed-counters-v1", "counters": {"Example.draw": descriptor}}
        path.write_text(json.dumps(document))
        return root, path, document

    def test_title_catalog_extends_without_overriding_core(self):
        root, _, document = self.counter_catalog()
        actual = retirement_counters.load_title_catalogs(root, {"Core.draw": {"scope": "core"}})
        self.assertEqual(actual["Core.draw"], {"scope": "core"})
        self.assertEqual(actual["Example.draw"]["member"], "draws")
        self.assertEqual(actual["Example.draw"]["catalog"], "games/example/verify/retirement_counters.json")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            retirement_counters.load_title_catalogs(root, document["counters"])

    def test_title_catalog_rejects_escape_and_duplicate_members(self):
        root, path, document = self.counter_catalog()
        document["counters"]["Example.draw"]["source"] = "games/other/hle/draw.cpp"
        path.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, "outside"):
            retirement_counters.load_title_catalogs(root, {})
        path.write_text('{"schema":"dt12-reviewed-counters-v1","schema":"forged","counters":{}}')
        with self.assertRaisesRegex(ValueError, "duplicate"):
            retirement_counters.load_title_catalogs(root, {})

    def test_title_catalog_rejects_symlink(self):
        root, path, _ = self.counter_catalog()
        moved = path.with_suffix(".owned")
        path.rename(moved)
        path.symlink_to(moved)
        with self.assertRaisesRegex(ValueError, "owned regular path"):
            retirement_counters.load_title_catalogs(root, {})

    def test_title_catalog_accepts_only_reviewed_count_provider(self):
        root, path, document = self.counter_catalog()
        descriptor = document["counters"]["Example.draw"]
        descriptor["provider"] = "DispatchTable.CountCalls"
        path.write_text(json.dumps(document))
        actual = retirement_counters.load_title_catalogs(root, {})
        self.assertEqual(actual["Example.draw"]["provider"], "DispatchTable.CountCalls")
        descriptor["provider"] = "candidate/fictitious_counter.cpp"
        path.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, "unreviewed counter provider"):
            retirement_counters.load_title_catalogs(root, {})

    def test_title_counter_provider_requires_actual_reviewed_source(self):
        name = next(name for name, item in gate.COUNTERS.items() if "provider" in item)
        descriptor = gate.COUNTERS[name]
        provider = retirement_counters.COUNT_PROVIDERS[descriptor["provider"]]
        for file in (descriptor["catalog"], descriptor["source"], descriptor["publisher"]):
            target = self.fixture.source / file
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((gate.REPO_ROOT / file).read_bytes())
            self.fixture.sources[file] = gate.digest(target)

        def evidence():
            self.fixture.write(self.fixture.source_manifest, {
                "schema": gate.SOURCE_SCHEMA, "root": str(self.fixture.source), "files": self.fixture.sources})
            self.fixture.manifest["build"]["sources"] = self.fixture.blob(self.fixture.source_manifest)
            self.fixture.save()
            return gate.Evidence(self.fixture.manifest_path)

        with self.assertRaisesRegex(ValueError, "provider missing"):
            evidence().model_selector(name)
        target = self.fixture.source / provider["source"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("counter->fetch_add(1, std::memory_order_relaxed);\noriginal(ctx, entry);\n")
        self.fixture.sources[provider["source"]] = gate.digest(target)
        with self.assertRaisesRegex(ValueError, "provider changed"):
            evidence().model_selector(name)
        target.write_bytes((gate.REPO_ROOT / provider["source"]).read_bytes())
        self.fixture.sources[provider["source"]] = gate.digest(target)
        self.assertEqual(evidence().model_selector(name), descriptor)

    def test_title_counter_requires_catalog_build_fingerprint(self):
        name = next(name for name, item in gate.COUNTERS.items() if "catalog" in item)
        with self.assertRaisesRegex(ValueError, "catalog missing from build manifest"):
            gate.Evidence(self.fixture.manifest_path).model_selector(name)


def run_tests():
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RetirementTests)
    return unittest.TextTestRunner(verbosity=1).run(suite).wasSuccessful()


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
