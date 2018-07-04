#include "main.h"

#include <sim/jobs.h>
#include <sim/submission.h>
#include <simlib/sim/conver.h>
#include <simlib/sim/problem_package.h>
#include <simlib/time.h>
#include <simlib/zip.h>

using sim::JudgeReport;
using sim::JudgeWorker;
using std::string;

inline static sim::SolutionLanguage to_sol_lang(SubmissionLanguage lang) {
	switch (lang) {
	case SubmissionLanguage::C: return sim::SolutionLanguage::C;
	case SubmissionLanguage::CPP: return sim::SolutionLanguage::CPP;
	case SubmissionLanguage::PASCAL: return sim::SolutionLanguage::PASCAL;
	}

	THROW("Invalid Language: ",
		std::underlying_type_t<SubmissionLanguage>(lang));
}

void judgeSubmission(uint64_t job_id, StringView submission_id,
	StringView job_creation_time)
{
	STACK_UNWINDING_MARK;

	InplaceBuff<1 << 14> job_log;

	auto judge_log = [&](auto&&... args) {
		return DoubleAppender<decltype(job_log)>(stdlog, job_log,
			std::forward<decltype(args)>(args)...);
	};

	// Gather the needed information about the submission
	auto stmt = mysql.prepare("SELECT s.language, s.owner, contest_problem_id,"
			" problem_id, last_judgment, p.last_edit"
		" FROM submissions s, problems p"
		" WHERE p.id=problem_id AND s.id=?");
	stmt.bindAndExecute(submission_id);
	InplaceBuff<32> sowner, contest_problem_id, problem_id;
	InplaceBuff<64> last_judgment, p_last_edit;
	std::underlying_type_t<SubmissionLanguage> lang_val;
	stmt.res_bind_all(lang_val, sowner, contest_problem_id, problem_id,
		last_judgment, p_last_edit);
	// If the submission doesn't exist (probably was removed)
	if (not stmt.next()) {
		// Fail the job
		judge_log("Failed the job of judging the submission ", submission_id,
			", since there is no such submission.");
		stmt = mysql.prepare("UPDATE jobs"
			" SET status=" JSTATUS_FAILED_STR ", data=? WHERE id=?");
		stmt.bindAndExecute(job_log, job_id);
		return;
	}

	auto lang = SubmissionLanguage(lang_val);

	// If the problem wasn't modified since last judgment and submission has
	// already been rejudged after the job was created
	if (last_judgment > p_last_edit and last_judgment > job_creation_time) {
		// Skip the job - the submission has already been rejudged
		judge_log("Skipped judging of the submission ", submission_id,
			" because it has already been rejudged after this job had been"
			" scheduled");
		stmt = mysql.prepare("UPDATE jobs"
			" SET status=" JSTATUS_DONE_STR ", data=? WHERE id=?");
		stmt.bindAndExecute(job_log, job_id);
		return;
	}

	string judging_began = mysql_date();
	JudgeWorker jworker;
	jworker.setVerbosity(true);

	stdlog("Judging submission ", submission_id, " (problem: ", problem_id,
		')');
	{
		auto tmplog = judge_log("Loading problem package...");
		tmplog.flush_no_nl();

		auto package_path = concat<PATH_MAX>("problems/", problem_id, ".zip");
		auto pkg_master_dir = sim::zip_package_master_dir(package_path.to_cstr());

		jworker.loadPackage(package_path.to_string(),
			extract_file_from_zip(package_path.to_cstr(),
				concat(pkg_master_dir, "Simfile").to_cstr())
		);
		tmplog(" done.");
	}

	// Variables
	SubmissionStatus status = SubmissionStatus::OK;
	InplaceBuff<1 << 16> initial_report, final_report;
	int64_t total_score = 0;

	auto send_report = [&] {
		static_assert(int(SubmissionType::NORMAL) == 0 &&
			int(SubmissionType::FINAL) == 1, "Needed below where "
				"\"... type<= ...\"");

		{
			// Lock the table to be able to safely modify the submission
			// locking contest_problems is required by update_final()
			mysql.update("LOCK TABLES submissions WRITE, contest_problems READ");
			auto lock_guard = make_call_in_destructor([&]{
				mysql.update("UNLOCK TABLES");
			});

			using ST = SubmissionType;
			// Get the submission's ACTUAL type
			stmt = mysql.prepare("SELECT type FROM submissions WHERE id=?");
			stmt.bindAndExecute(submission_id);
			auto stype = std::underlying_type_t<ST>(ST::VOID);
			stmt.res_bind_all(stype);
			(void)stmt.next(); // Ignore errors (deleted submission)


			// Update submission
			stmt = mysql.prepare("UPDATE submissions"
				" SET final_candidate=?, status=?, score=?, last_judgment=?,"
					" initial_report=?, final_report=? WHERE id=?");

			if (is_fatal(status))
				stmt.bindAndExecute(false, (uint)status, nullptr,
					judging_began, initial_report, final_report, submission_id);
			else
				stmt.bindAndExecute(isIn(ST(stype), {ST::NORMAL, ST::FINAL}),
					(uint)status, total_score, judging_began, initial_report,
					final_report, submission_id);

			submission::update_final(mysql, sowner, contest_problem_id, false);
		}

		stmt = mysql.prepare("UPDATE jobs"
			" SET status=" JSTATUS_DONE_STR ", data=? WHERE id=?");
		stmt.bindAndExecute(job_log, job_id);
	};

	string compilation_errors;

	// Compile checker
	{
		auto tmplog = judge_log("Compiling checker...");
		tmplog.flush_no_nl();

		if (jworker.compileChecker(CHECKER_COMPILATION_TIME_LIMIT,
			&compilation_errors, COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
		{
			tmplog(" failed.");

			status = SubmissionStatus::CHECKER_COMPILATION_ERROR;
			initial_report = concat("<pre class=\"compilation-errors\">",
				htmlEscape(compilation_errors), "</pre>");

			return send_report();
		}
		tmplog(" done.");
	}

	// Compile solution
	{
		auto tmplog = judge_log("Compiling solution...");
		tmplog.flush_no_nl();

		if (jworker.compileSolution(
			concat("solutions/", submission_id).to_cstr(), to_sol_lang(lang),
			SOLUTION_COMPILATION_TIME_LIMIT, &compilation_errors,
			COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
		{
			tmplog(" failed.");

			status = SubmissionStatus::COMPILATION_ERROR;
			initial_report = concat("<pre class=\"compilation-errors\">",
				htmlEscape(compilation_errors), "</pre>");

			return send_report();
		}
		tmplog(" done.");
	}

	// Creates an xml report from JudgeReport
	auto construct_report = [](const JudgeReport& jr, bool final) {
		InplaceBuff<65536> report;
		if (jr.groups.empty())
			return report;

		report.append("<h2>", (final ? "Final" : "Initial"),
				" testing report</h2>"
			"<table class=\"table\">"
			"<thead>"
				"<tr>"
					"<th class=\"test\">Test</th>"
					"<th class=\"result\">Result</th>"
					"<th class=\"time\">Time [s]</th>"
					"<th class=\"memory\">Memory [KB]</th>"
					"<th class=\"points\">Score</th>"
				"</tr>"
			"</thead>"
			"<tbody>"
		);

		auto append_normal_columns = [&](const JudgeReport::Test& test) {
			auto asTdString = [](JudgeReport::Test::Status s) {
				switch (s) {
				case JudgeReport::Test::OK:
					return "<td class=\"status green\">OK</td>";
				case JudgeReport::Test::WA:
					return "<td class=\"status red\">Wrong answer</td>";
				case JudgeReport::Test::TLE:
					return "<td class=\"status yellow\">"
						"Time limit exceeded</td>";
				case JudgeReport::Test::MLE:
					return "<td class=\"status yellow\">"
						"Memory limit exceeded</td>";
				case JudgeReport::Test::RTE:
					return "<td class=\"status intense-red\">"
						"Runtime error</td>";
				case JudgeReport::Test::CHECKER_ERROR:
					return "<td class=\"status blue\">Checker error</td>";
				}

				throw_assert(false); // We shouldn't get here
			};
			report.append("<td>", htmlEscape(test.name), "</td>",
				asTdString(test.status),
				"<td>", usecToSecStr(test.runtime, 2, false),
					" / ", usecToSecStr(test.time_limit, 2, false), "</td>"
				"<td>", test.memory_consumed >> 10, " / ",
					test.memory_limit >> 10, "</td>");
		};

		bool there_are_comments = false;
		for (auto&& group : jr.groups) {
			throw_assert(group.tests.size() > 0);
			// First row
			report.append("<tr>");
			append_normal_columns(group.tests[0]);
			report.append("<td class=\"groupscore\" rowspan=\"",
				group.tests.size(), "\">", group.score, " / ", group.max_score,
				"</td></tr>");
			// Other rows
			std::for_each(group.tests.begin() + 1, group.tests.end(),
				[&](const JudgeReport::Test& test) {
					report.append("<tr>");
					append_normal_columns(test);
					report.append("</tr>");
				});

			for (auto&& test : group.tests)
				there_are_comments |= !test.comment.empty();
		}

		report.append("</tbody></table>");

		// Tests comments
		if (there_are_comments) {
			report.append("<ul class=\"tests-comments\">");
			for (auto&& group : jr.groups)
				for (auto&& test : group.tests)
					if (test.comment.size())
						report.append("<li>"
							"<span class=\"test-id\">", htmlEscape(test.name),
							"</span>", htmlEscape(test.comment), "</li>");

			report.append("</ul>");
		}

		return report;
	};

	try {
		// Judge
		JudgeReport rep1 = jworker.judge(false);
		JudgeReport rep2 = jworker.judge(true);
		// Make reports
		initial_report = construct_report(rep1, false);
		final_report = construct_report(rep2, true);

		// Log reports
		judge_log("Job ", job_id, " -> submission ", submission_id,
			" (problem ", problem_id, ")\n"
			"Initial judge report: ", rep1.judge_log,
			"\nFinal judge report: ", rep2.judge_log,
			"\n");

		// Count score
		for (auto&& rep : {rep1, rep2})
			for (auto&& group : rep.groups)
				total_score += group.score;

		/* Determine the submission status */

		// Sets status to OK or first encountered error and modifies it
		// with func
		auto set_status = [&status] (const JudgeReport& jr, auto&& func) {
			for (auto&& group : jr.groups)
				for (auto&& test : group.tests)
					if (test.status != JudgeReport::Test::OK) {
						switch (test.status) {
						case JudgeReport::Test::WA:
							status = SubmissionStatus::WA; break;
						case JudgeReport::Test::TLE:
							status = SubmissionStatus::TLE; break;
						case JudgeReport::Test::MLE:
							status = SubmissionStatus::MLE; break;
						case JudgeReport::Test::RTE:
							status = SubmissionStatus::RTE; break;
						default:
							// We should not get here
							throw_assert(false);
						}

						func(status);
						return;
					}

			status = SubmissionStatus::OK;
			func(status);
		};

		// Search for a CHECKER_ERROR
		for (auto&& rep : {rep1, rep2})
			for (auto&& group : rep.groups)
				for (auto&& test : group.tests)
					if (test.status == JudgeReport::Test::CHECKER_ERROR) {
						status = SubmissionStatus::JUDGE_ERROR;
						errlog("Checker error: submission ", submission_id,
							" (problem id: ", problem_id, ") test `", test.name,
							'`');

						return send_report();
					}

		// Log syscall problems (to errlog)
		for (auto&& rep : {rep1, rep2})
			for (auto&& group : rep.groups)
				for (auto&& test : group.tests)
					if (hasPrefixIn(test.comment, {"Runtime error (Error: ",
						"Runtime error (failed to get syscall",
						"Runtime error (forbidden syscall"}))
					{
						errlog("Submission ", submission_id, " (problem ",
							problem_id, "): ", test.name, " -> ", test.comment);
					}

		static_assert((int)SubmissionStatus::OK < 8, "Needed below");
		static_assert((int)SubmissionStatus::WA < 8, "Needed below");
		static_assert((int)SubmissionStatus::TLE < 8, "Needed below");
		static_assert((int)SubmissionStatus::MLE < 8, "Needed below");
		static_assert((int)SubmissionStatus::RTE < 8, "Needed below");

		static_assert(((int)SubmissionStatus::OK << 3) ==
			(int)SubmissionStatus::INITIAL_OK, "Needed below");
		static_assert(((int)SubmissionStatus::WA << 3) ==
			(int)SubmissionStatus::INITIAL_WA, "Needed below");
		static_assert(((int)SubmissionStatus::TLE << 3) ==
			(int)SubmissionStatus::INITIAL_TLE, "Needed below");
		static_assert(((int)SubmissionStatus::MLE << 3) ==
			(int)SubmissionStatus::INITIAL_MLE, "Needed below");
		static_assert(((int)SubmissionStatus::RTE << 3) ==
			(int)SubmissionStatus::INITIAL_RTE, "Needed below");

		// Initial status
		set_status(rep1, [](SubmissionStatus& s) {
			// s has only final status, we want the same initial and
			// final status in s
			int x = static_cast<int>(s);
			s = static_cast<SubmissionStatus>((x << 3) | x);
		});
		// If initial tests haven't passed
		if (status != (SubmissionStatus::OK | SubmissionStatus::INITIAL_OK))
			return send_report();

		// Final status
		set_status(rep2, [](SubmissionStatus& s) {
			// Initial tests have passed, so add INITIAL_OK
			s = s | SubmissionStatus::INITIAL_OK;
		});

	} catch (const std::exception& e) {
		ERRLOG_CATCH(e);
		judge_log("Judge error.");
		judge_log("Caught exception -> ", e.what());

		status = SubmissionStatus::JUDGE_ERROR;
		initial_report = concat("<pre>", htmlEscape(e.what()), "</pre>");
		final_report = "";
	}

	send_report();
}

void judgeModelSolution(uint64_t job_id, JobType original_job_type) {
	STACK_UNWINDING_MARK;

	InplaceBuff<1 << 14> job_log;

	auto judge_log = [&](auto&&... args) {
		return DoubleAppender<decltype(job_log)>(stdlog, job_log,
			std::forward<decltype(args)>(args)...);
	};

	stdlog("Job ", job_id, ':');
	judge_log("Stage: Judging the model solution");

	auto set_failure = [&] {
		auto stmt = mysql.prepare("UPDATE jobs"
			" SET status=" JSTATUS_FAILED_STR ", data=CONCAT(data,?)"
			" WHERE id=? AND status!=" JSTATUS_CANCELED_STR);
		stmt.bindAndExecute(job_log, job_id);
	};

	auto package_path = concat<PATH_MAX>("jobs_files/", job_id, ".zip.prep");
	string pkg_master_dir = sim::zip_package_master_dir(package_path.to_cstr());

	string simfile_str = extract_file_from_zip(package_path.to_cstr(),
		concat(pkg_master_dir, "Simfile").to_cstr());

	sim::Simfile simfile {simfile_str};
	simfile.loadAll();
	judge_log("Model solution: ", simfile.solutions[0]);

	JudgeWorker jworker;
	jworker.setVerbosity(true);
	jworker.loadPackage(package_path.to_string(), std::move(simfile_str));

	string compilation_errors;

	// Compile checker
	{
		auto tmplog = judge_log("Compiling checker...");
		tmplog.flush_no_nl();

		if (jworker.compileChecker(CHECKER_COMPILATION_TIME_LIMIT,
			&compilation_errors, COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
		{
			tmplog(" failed:\n");
			tmplog(compilation_errors);
			return set_failure();
		}
		tmplog(" done.");
	}

	// Compile the model solution
	{
		TemporaryFile sol_src("/tmp/problem_solution.XXXXXX");
		writeAll(sol_src, extract_file_from_zip(package_path.to_cstr(),
			concat(pkg_master_dir, simfile.solutions[0])));

		auto tmplog = judge_log("Compiling the model solution...");
		tmplog.flush_no_nl();
		if (jworker.compileSolution(sol_src.path(),
			sim::filename_to_lang(simfile.solutions[0]),
			SOLUTION_COMPILATION_TIME_LIMIT, &compilation_errors,
			COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
		{
			tmplog(" failed:\n");
			tmplog(compilation_errors);
			return set_failure();
		}
		tmplog(" done.");
	}

	// Judge
	judge_log("Judging...");
	JudgeReport rep1 = jworker.judge(false);
	JudgeReport rep2 = jworker.judge(true);

	judge_log("Initial judge report: ", rep1.judge_log);
	judge_log("Final judge report: ", rep2.judge_log);

	sim::Conver conver;
	conver.setPackagePath(package_path.to_string());

	try {
		conver.finishConstructingSimfile(simfile, rep1, rep2);

	} catch (const std::exception& e) {
		job_log.append(conver.getReport());
		judge_log("Conver failed: ", e.what());
		return set_failure();
	}

	// Put the Simfile in the package
	update_add_data_to_zip(simfile.dump(),
		concat(pkg_master_dir, "Simfile").to_cstr(), package_path.to_cstr());

	auto stmt = mysql.prepare("UPDATE jobs"
		" SET type=?, status=" JSTATUS_PENDING_STR ", data=CONCAT(data,?)"
		" WHERE id=? AND status!=" JSTATUS_CANCELED_STR);
	stmt.bindAndExecute(uint(original_job_type), job_log, job_id);
}
