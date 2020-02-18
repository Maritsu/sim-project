#pragma once

#include "job_handler.h"

#include <sim/jobs.h>

namespace job_handlers {

class MergeProblems final : public JobHandler {
	const uint64_t donor_problem_id_;
	const jobs::MergeProblemsInfo info_;

public:
	MergeProblems(uint64_t job_id, uint64_t donor_problem_id,
	              const jobs::MergeProblemsInfo& info)
	   : JobHandler(job_id), donor_problem_id_(donor_problem_id), info_(info) {}

	void run() override final;

private:
	void run_impl();
};

} // namespace job_handlers
