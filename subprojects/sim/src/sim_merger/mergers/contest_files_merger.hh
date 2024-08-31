#pragma once

#include "../sim_instance.hh"
#include "contests_merger.hh"
#include "internal_files_merger.hh"
#include "users_merger.hh"

namespace mergers {

class ContestFilesMerger {
    const SimInstance& main_sim; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    const SimInstance& other_sim; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ContestsMerger& contests_merger;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const InternalFilesMerger& internal_files_merger;
    const UsersMerger& users_merger; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)

public:
    ContestFilesMerger(
        const SimInstance& main_sim,
        const SimInstance& other_sim,
        const ContestsMerger& contests_merger,
        const InternalFilesMerger& internal_files_merger,
        const UsersMerger& users_merger
    );

    void merge_and_save();
};

} // namespace mergers
