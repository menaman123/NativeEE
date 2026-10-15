// tools/gs_replay/census.h -- Census: the per-recording numeric census in the
// corpus grammar (tag = "[GSCENSUS]" for the census mode, "[GSREPLAY]" after a
// replay): CENSUS / PRIM / FLAGS / JOBS / HLE / CLASS lines, plus -- under
// --configs -- one CFG row per distinct draw / upload / scanout configuration
// and a CFG summary. Classes come from the engine's ClassClassifier under the
// title's rules; HLE names from the recording's name table. Maps are ordered, so
// every line set prints in lexicographic key order.
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "engine/gs/hle/classrdr/class_classifier.h"
#include "tests/lib/gsdl_reader.h"

namespace ps2r {
namespace tools {

class Census {
public:
    Census(const gs::ClassClassifier::Rules& rules, const gs::GsdlReader::Recording& rec, bool want_cfg);

    void AddFrame() { frames_++; }
    void AddDraw(const gs::DrawRec& x);
    void AddJob(const gs::JobRec& j);
    void Print(const char* tag) const;

    uint64_t Draws() const { return draws_; }
    uint64_t Verts() const { return verts_; }
    uint64_t Frames() const { return frames_; }

    // The class name of a recorded draw (bbox from the record; the HLE name from
    // the recording's table when the draw is stamped).
    static const char* ClassOf(const gs::ClassClassifier::Rules& rules, const gs::GsdlReader::Recording& r,
                               const gs::DrawRec& x);
    static gs::ClassClassifier::Class ClassifyRec(const gs::ClassClassifier::Rules& rules,
                                                  const gs::GsdlReader::Recording& r, const gs::DrawRec& x);
    static std::string PsmName(uint32_t p);

private:
    std::string CfgKey(const gs::DrawRec& x) const;

    const gs::ClassClassifier::Rules& rules_;
    const gs::GsdlReader::Recording& rec_;
    bool want_cfg_;
    uint64_t frames_ = 0, draws_ = 0, verts_ = 0, jobs_ = 0;
    uint64_t prim_[8] = {};
    uint64_t textured_ = 0, blended_ = 0, date_ = 0, fogged_ = 0, gouraud_ = 0;
    uint64_t job_by_type_[16] = {};
    uint64_t uploads_with_payload_ = 0, uploads_no_payload_ = 0;
    std::map<std::string, uint64_t> hle_;
    std::map<std::string, uint64_t> cls_draws_, cls_verts_;
    std::map<std::string, uint64_t> cfg_, cfg_up_, cfg_scan_;
};

}  // namespace tools
}  // namespace ps2r
