#include "sim.h"

#include <sim/jobs.h>

static constexpr const char* job_type_str(JobType type) noexcept {
	using JT = JobType;

	switch (type) {
	case JT::JUDGE_SUBMISSION: return "Judge submission";
	case JT::ADD_PROBLEM: return "Add problem";
	case JT::REUPLOAD_PROBLEM: return "Reupload problem";
	case JT::ADD_JUDGE_MODEL_SOLUTION: return "Add problem - set limits";
	case JT::REUPLOAD_JUDGE_MODEL_SOLUTION:
		return"Reupload problem - set limits";
	case JT::EDIT_PROBLEM: return "Edit problem";
	case JT::DELETE_PROBLEM: return "Delete problem";
	case JT::CONTEST_PROBLEM_RESELECT_FINAL_SUBMISSIONS:
		return "Reselect final submissions of a contest problem";
	case JT::VOID: return "Void";
	}

	return "Unknown";
}

void Sim::api_jobs() {
	STACK_UNWINDING_MARK;

	if (not session_open())
		return api_error403();

	using PERM = JobPermissions;

	// Get the overall permissions to the job queue
	jobs_perms = jobs_get_permissions();

	InplaceBuff<512> qfields, qwhere;
	qfields.append("SELECT j.id, added, j.type, j.status, j.priority, j.aux_id,"
			" j.info, j.creator, u.username");
	qwhere.append(" FROM jobs j LEFT JOIN users u ON creator=u.id"
		" WHERE j.type!=" JTYPE_VOID_STR);

	bool allow_access = uint(jobs_perms & PERM::VIEW_ALL);
	bool select_specified_job = false;

	PERM granted_perms = PERM::NONE;

	// Process restrictions
	StringView next_arg = url_args.extractNextArg();
	for (uint mask = 0; next_arg.size(); next_arg = url_args.extractNextArg()) {
		constexpr uint ID_COND = 1;
		constexpr uint AUX_ID_COND = 2;
		constexpr uint USER_ID_COND = 4;

		auto arg = decodeURI(next_arg);
		char cond = arg[0];
		StringView arg_id = StringView{arg}.substr(1);

		if (not isDigit(arg_id))
			return api_error400();

		// conditional
		if (isIn(cond, {'<', '>'}) and ~mask & ID_COND) {
			qwhere.append(" AND j.id", arg);
			mask |= ID_COND;

		} else if (cond == '=' and ~mask & ID_COND) {
			select_specified_job = true;
			// Get job information to grant permissions
			std::underlying_type_t<JobType> jtype;
			InplaceBuff<32> aux_id;
			auto stmt = mysql.prepare("SELECT type, aux_id FROM jobs"
				" WHERE id=?");
			stmt.bindAndExecute(arg_id);
			stmt.res_bind_all(jtype, aux_id);
			if (not stmt.next())
				return api_error404();

			// Grant permissions if possible
			if (is_problem_job(JobType(jtype)))
				granted_perms |= jobs_granted_permissions_problem(aux_id);
			else if (is_submission_job(JobType(jtype)))
				granted_perms |= jobs_granted_permissions_submission(aux_id);
			allow_access |= (granted_perms != PERM::NONE);

			if (not allow_access) {
				// If user cannot view all jobs, allow them to view their jobs
				allow_access = true;
				qwhere.append(" AND creator=", session_user_id);
			}

			qfields.append(", SUBSTR(data, 1, ",
				meta::ToString<JOB_LOG_VIEW_MAX_LENGTH + 1>{}, ')');
			qwhere.append(" AND j.id", arg);
			mask |= ID_COND;

		// problem
		} else if (cond == 'p' and ~mask & AUX_ID_COND) {
			// Check permissions - they may be granted
			granted_perms |= jobs_granted_permissions_problem(arg_id);
			allow_access |= (granted_perms != PERM::NONE);

			qwhere.append(" AND j.aux_id=", arg_id, " AND j.type IN("
				JTYPE_ADD_PROBLEM_STR ","
				JTYPE_ADD_JUDGE_MODEL_SOLUTION_STR ","
				JTYPE_REUPLOAD_PROBLEM_STR ","
				JTYPE_REUPLOAD_JUDGE_MODEL_SOLUTION_STR ","
				JTYPE_DELETE_PROBLEM_STR ","
				JTYPE_EDIT_PROBLEM_STR ")");

			mask |= AUX_ID_COND;

		// submission
		} else if (cond == 's' and ~mask & AUX_ID_COND) {
			// Check permissions - they may be granted
			granted_perms |= jobs_granted_permissions_submission(arg_id);
			allow_access |= (granted_perms != PERM::NONE);

			qwhere.append(" AND j.aux_id=", arg_id, " AND j.type="
				JTYPE_JUDGE_SUBMISSION_STR);

			mask |= AUX_ID_COND;

		// user (creator)
		} else if (cond == 'u' and ~mask & USER_ID_COND) {
			qwhere.append(" AND creator=", arg_id);
			if (arg_id == session_user_id)
				allow_access = true;

			mask |= USER_ID_COND;

		} else
			return api_error400();

	}

	if (not allow_access)
		return api_error403();

	// Execute query
	qfields.append(qwhere, " ORDER BY j.id DESC LIMIT 50");
	auto res = mysql.query(qfields);

	resp.headers["content-type"] = "text/plain; charset=utf-8";
	append("[");

	while (res.next()) {
		JobType job_type {JobType(strtoull(res[2]))};
		JobStatus job_status {JobStatus(strtoull(res[3]))};

		append("\n[", res[0], "," // job_id
			"\"", res[1], "\"," // added
			"\"", job_type_str(job_type), "\",");

		// Status: (CSS class, text)
		switch (job_status) {
		case JobStatus::PENDING: append("[\"\",\"Pending\"],"); break;
		case JobStatus::NOTICED_PENDING: append("[\"\",\"Pending\"],"); break;
		case JobStatus::IN_PROGRESS: append("[\"yellow\",\"In progress\"],");
			break;
		case JobStatus::DONE: append("[\"green\",\"Done\"],"); break;
		case JobStatus::FAILED: append("[\"red\",\"Failed\"],"); break;
		case JobStatus::CANCELED: append("[\"blue\",\"Cancelled\"],"); break;
		}

		append(res[4], ','); // priority

		// Creator
		if (res.is_null(7))
			append("null,");
		else
			append(res[7], ','); // creator

		// Username
		if (res.is_null(8))
			append("null,");
		else
			append("\"", res[8], "\","); // username

		// Additional info
		InplaceBuff<10> actions;
		append('{');

		switch (job_type) {
		case JobType::JUDGE_SUBMISSION: {
			append("\"problem\":", jobs::extractDumpedString(res[6]));
			append(",\"submission\":", res[5]); // aux_id
			break;
		}

		case JobType::ADD_PROBLEM:
		case JobType::REUPLOAD_PROBLEM:
		case JobType::ADD_JUDGE_MODEL_SOLUTION:
		case JobType::REUPLOAD_JUDGE_MODEL_SOLUTION: {
			auto ptype_to_str = [](ProblemType& ptype) {
				switch (ptype) {
				case ProblemType::VOID: return "void";
				case ProblemType::PUBLIC: return "public";
				case ProblemType::PRIVATE: return "private";
				case ProblemType::CONTEST_ONLY: return "contest only";
				}
				return "unknown";
			};
			jobs::AddProblemInfo info {res[6]};
			append("\"problem type\":\"", ptype_to_str(info.problem_type), '"');

			if (info.name.size())
				append(",\"name\":", jsonStringify(info.name));
			if (info.label.size())
				append(",\"label\":", jsonStringify(info.label));
			if (info.memory_limit)
				append(",\"memory limit\":\"", info.memory_limit, " MB\"");
			if (info.global_time_limit)
				append(",\"global time limit\":",
					usecToSecStr(info.global_time_limit, 6));

			append(",\"auto time limit setting\":", info.force_auto_limit ?
					"\"yes\"" : "\"no\"",
				",\"ignore simfile\":", info.ignore_simfile ?
					"\"yes\"" : "\"no\"");

			if (not res.is_null(5)) {
				// aux_id
				append(",\"problem\":", res[5]);
				actions.append('p'); // View problem
			}

			break;
		}

		case JobType::CONTEST_PROBLEM_RESELECT_FINAL_SUBMISSIONS: {
			append("\"contest problem\":", res[5]); // aux_id
			break;
		}

		case JobType::VOID:
		case JobType::EDIT_PROBLEM:
		case JobType::DELETE_PROBLEM:
			break;
		}
		append("},");


		// Append what buttons to show
		append('"', actions);

		// res[7] == creator
		auto perms = granted_perms |
			jobs_get_permissions(res[7], job_type, job_status);
		if (uint(perms & PERM::VIEW))
			append('v');
		if (uint(perms & PERM::DOWNLOAD_LOG))
			append('r');
		if (uint(perms & PERM::DOWNLOAD_UPLOADED_PACKAGE))
			append('u');
		if (uint(perms & PERM::CANCEL))
			append('C');
		if (uint(perms & PERM::RESTART))
			append('R');
		append('\"');

		// Append log view (whether there is more to load, data)
		if (select_specified_job and uint(perms & PERM::DOWNLOAD_LOG))
			append(",[", res[9].size() > JOB_LOG_VIEW_MAX_LENGTH, ',',
				jsonStringify(res[9]), ']'); // log view

		append("],");
	}

	if (resp.content.back() == ',')
		--resp.content.size;

	append("\n]");
}

void Sim::api_job() {
	STACK_UNWINDING_MARK;
	using JT = JobType;

	if (not session_open())
		return api_error403();

	jobs_jid = url_args.extractNextArg();
	if (not isDigit(jobs_jid))
		return api_error400();

	InplaceBuff<32> jcreator, aux_id;
	InplaceBuff<256> jinfo;
	std::underlying_type_t<JT> jtype;
	std::underlying_type_t<JobStatus> jstatus;

	auto stmt = mysql.prepare("SELECT creator, type, status, aux_id, info"
		" FROM jobs WHERE id=?");
	stmt.bindAndExecute(jobs_jid);
	stmt.res_bind_all(jcreator, jtype, jstatus, aux_id, jinfo);
	if (not stmt.next())
		return api_error404();

	jobs_perms = jobs_get_permissions(jcreator, JobType(jtype),
		JobStatus(jstatus));
	// Grant permissions if possible
	if (is_problem_job(JobType(jtype)))
		jobs_perms |= jobs_granted_permissions_problem(aux_id);
	else if (is_submission_job(JobType(jtype)))
		jobs_perms |= jobs_granted_permissions_submission(aux_id);

	StringView next_arg = url_args.extractNextArg();
	if (next_arg == "cancel")
		return api_job_cancel();
	else if (next_arg == "restart")
		return api_job_restart(JT(jtype), jinfo);
	else if (next_arg == "log")
		return api_job_download_log();
	else if (next_arg == "uploaded-package")
		return api_job_download_uploaded_package();
	else
		return api_error400();
}

void Sim::api_job_cancel() {
	STACK_UNWINDING_MARK;
	using PERM = JobPermissions;

	if (request.method != server::HttpRequest::POST)
		return api_error400();

	if (uint(~jobs_perms & PERM::CANCEL)) {
		if (uint(jobs_perms & PERM::VIEW))
			return api_error400("Job has already been canceled or done");
		return api_error403();
	}

	// Cancel job
	auto stmt = mysql.prepare("UPDATE jobs SET status=" JSTATUS_CANCELED_STR
		" WHERE id=?");
	stmt.bindAndExecute(jobs_jid);
}

void Sim::api_job_restart(JobType job_type, StringView job_info) {
	STACK_UNWINDING_MARK;
	using PERM = JobPermissions;

	if (request.method != server::HttpRequest::POST)
		return api_error400();

	if (uint(~jobs_perms & PERM::RESTART)) {
		if (uint(jobs_perms & PERM::VIEW))
			return api_error400("Job has already been restarted");
		return api_error403();
	}

	jobs::restart_job(mysql, jobs_jid, job_type, job_info, true);
}

void Sim::api_job_download_log() {
	STACK_UNWINDING_MARK;
	using PERM = JobPermissions;

	if (uint(~jobs_perms & PERM::DOWNLOAD_LOG))
		return api_error403();

	// Assumption: permissions are already checked
	resp.headers["Content-type"] = "application/text";
	resp.headers["Content-Disposition"] =
		concat_tostr("attachment; filename=job-", jobs_jid, "-log");

	// Fetch the log
	auto stmt = mysql.prepare("SELECT data FROM jobs WHERE id=?");
	stmt.bindAndExecute(jobs_jid);
	stmt.res_bind_all(resp.content);
	throw_assert(stmt.next());
}

void Sim::api_job_download_uploaded_package() {
	STACK_UNWINDING_MARK;
	using PERM = JobPermissions;

	if (uint(~jobs_perms & PERM::DOWNLOAD_UPLOADED_PACKAGE))
		return api_error403();

	resp.headers["Content-Disposition"] =
		concat_tostr("attachment; filename=", jobs_jid, ".zip");
	resp.content_type = server::HttpResponse::FILE;
	resp.content = concat("jobs_files/", jobs_jid, ".zip");
}
