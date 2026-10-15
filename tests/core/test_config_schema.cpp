// tests/core/test_config_schema.cpp -- docs/filemap/settings_table.tsv (generated
// by tools/gen_settings_table.py from FLAG_LEDGER SETTING rows + PHASE1_PLAN
// section 3) enumerated against the compiled Config (CONSTRUCTION_PLAN R14):
// every row is a key, every key is a row, peak defaults and the deterministic
// preset's values match column for column, the value domains parse, and the
// TSV's deterministic_fingerprint header equals both the fingerprint derived
// from its own rows and Config::Fingerprint() of the applied preset.
// argv[1] = path to settings_table.tsv.
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/core/config/deterministic_profile.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

using namespace ps2r;

struct Row {
    std::string key, type, peak, det, source, ledger;
};

static std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return out;
}

static std::string Unquote(const std::string& v) { return v == "\"\"" ? std::string() : v; }

static bool LoadTable(const std::string& path, std::vector<Row>* rows, std::string* fingerprint,
                      int* declared_rows) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    bool header_seen = false;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') {
            const std::string fp_tag = "# deterministic_fingerprint=";
            const std::string rows_tag = "# rows=";
            if (line.compare(0, fp_tag.size(), fp_tag) == 0) *fingerprint = line.substr(fp_tag.size());
            if (line.compare(0, rows_tag.size(), rows_tag) == 0) *declared_rows = std::atoi(line.c_str() + rows_tag.size());
            continue;
        }
        if (!header_seen) {   // column header line
            header_seen = true;
            CHECK(line == "key\ttype\tpeak\tdet\tsource_var\tledger");
            continue;
        }
        std::vector<std::string> c = SplitTabs(line);
        if (c.size() != 6) {
            std::printf("FAIL: row with %zu columns: %s\n", c.size(), line.c_str());
            g_fail++;
            continue;
        }
        rows->push_back(Row{c[0], c[1], Unquote(c[2]), Unquote(c[3]), c[4], c[5]});
    }
    return header_seen;
}

static std::string Get(const Config& c, const std::string& key) {
    std::string v;
    c.Get(key, &v);
    return v;
}

static std::vector<std::string> EnumNames(const std::string& type) {
    std::vector<std::string> names;
    const size_t open = type.find('('), close = type.rfind(')');
    if (open == std::string::npos || close == std::string::npos) return names;
    std::string body = type.substr(open + 1, close - open - 1);
    size_t start = 0;
    for (;;) {
        size_t bar = body.find('|', start);
        names.push_back(body.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return names;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: test_config_schema <settings_table.tsv>\n");
        return 2;
    }
    std::vector<Row> rows;
    std::string fp_header;
    int declared_rows = -1;
    if (!LoadTable(argv[1], &rows, &fp_header, &declared_rows)) {
        std::printf("FAIL: cannot read %s\n", argv[1]);
        return 1;
    }
    CHECK(!fp_header.empty());
    CHECK(declared_rows == (int)rows.size());

    // Bijection with Config::Keys(), in the same (sorted) order.
    const std::vector<std::string>& keys = Config::Keys();
    CHECK(keys.size() == rows.size());
    for (size_t i = 0; i < rows.size() && i < keys.size(); i++) {
        if (rows[i].key != keys[i]) {
            std::printf("FAIL: row %zu is %s, Config::Keys()[%zu] is %s\n", i, rows[i].key.c_str(), i,
                        keys[i].c_str());
            g_fail++;
        }
    }
    std::map<std::string, const Row*> by_key;
    for (const Row& r : rows) by_key[r.key] = &r;
    for (const std::string& k : keys) {
        if (!by_key.count(k)) {
            std::printf("FAIL: Config key %s has no settings_table row\n", k.c_str());
            g_fail++;
        }
    }

    // Peak defaults, deterministic values, value domains.
    const Config peak;
    Config det;
    DeterministicProfile::Apply(det);
    std::string derived_fp;
    for (const Row& r : rows) {
        if (!Config::IsKey(r.key)) {
            std::printf("FAIL: %s is not a Config key\n", r.key.c_str());
            g_fail++;
            continue;
        }
        const std::string have_peak = Get(peak, r.key);
        if (have_peak != r.peak) {
            std::printf("FAIL: %s peak: table %s, Config %s\n", r.key.c_str(), r.peak.c_str(), have_peak.c_str());
            g_fail++;
        }
        const std::string want_det = r.det == "=" ? r.peak : r.det;
        const std::string have_det = Get(det, r.key);
        if (have_det != want_det) {
            std::printf("FAIL: %s det: table %s, preset %s\n", r.key.c_str(), want_det.c_str(), have_det.c_str());
            g_fail++;
        }
        const bool excluded = r.key.compare(0, 8, "harness.") == 0 || r.key.compare(0, 6, "paths.") == 0 ||
                              r.key.compare(0, 5, "meta.") == 0;
        if (!excluded && want_det != r.peak) derived_fp += r.key + "=" + want_det + ";";

        Config probe;
        std::string err;
        if (r.type == "bool") {
            CHECK(probe.Set(r.key, "on", &err) && Get(probe, r.key) == "on");
            CHECK(probe.Set(r.key, "off", &err) && Get(probe, r.key) == "off");
            CHECK(!probe.Set(r.key, "maybe", &err));
        } else if (r.type == "int") {
            CHECK(probe.Set(r.key, "-7", &err) && Get(probe, r.key) == "-7");
            CHECK(!probe.Set(r.key, "seven", &err));
        } else if (r.type == "float") {
            CHECK(probe.Set(r.key, "0.5", &err) && Get(probe, r.key) == "0.5");
            CHECK(!probe.Set(r.key, "half", &err));
        } else if (r.type == "str") {
            CHECK(probe.Set(r.key, "anything goes", &err) && Get(probe, r.key) == "anything goes");
        } else if (r.type.compare(0, 5, "enum(") == 0) {
            const std::vector<std::string> names = EnumNames(r.type);
            CHECK(names.size() >= 2);
            for (const std::string& n : names) {
                if (!(probe.Set(r.key, n, &err) && Get(probe, r.key) == n)) {
                    std::printf("FAIL: %s enum name %s does not round-trip\n", r.key.c_str(), n.c_str());
                    g_fail++;
                }
            }
            CHECK(!probe.Set(r.key, "not-a-member", &err));
        } else {
            std::printf("FAIL: %s unknown type %s\n", r.key.c_str(), r.type.c_str());
            g_fail++;
        }
        CHECK(!r.source.empty() && !r.ledger.empty());
    }
    derived_fp += "profile=" + Get(det, "meta.profile");

    // The single-source golden: header == derived from rows == the preset.
    CHECK(derived_fp == fp_header);
    CHECK(det.Fingerprint() == fp_header);
    if (det.Fingerprint() != fp_header) std::printf("preset fingerprint: %s\n", det.Fingerprint().c_str());

    if (g_fail) {
        std::printf("test_config_schema: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("settings_table: %zu rows == Config::Keys() (%zu); peak + deterministic columns match; "
                "deterministic_fingerprint header == rows == preset\n",
                rows.size(), keys.size());
    std::printf("test_config_schema: all checks passed\n");
    return 0;
}
