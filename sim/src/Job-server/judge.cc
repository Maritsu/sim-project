#include <sim/constants.h>
#include <sim/jobs.h>
#include <sim/mysql.h>
#include <simlib/sim/conver.h>
#include <simlib/sim/judge_worker.h>
#include <simlib/time.h>
#include <simlib/utilities.h>

using sim::JudgeReport;
using sim::JudgeWorker;
using std::string;

extern MySQL::Connection db_conn;

void judgeSubmission(StringView job_id, StringView submission_id,
	StringView job_creation_time)
{
	// Gather the needed information about the submission

	auto stmt = db_conn.prepare("SELECT s.owner, round_id, problem_id,"
			" last_judgment, p.last_edit"
		" FROM submissions s, problems p"
		" WHERE p.id=problem_id AND s.id=?");
	stmt.bindAndExecute(submission_id);
	uint64_t sowner, round_id, problem_id;
	InplaceBuff<64> last_judgment, p_last_edit;
	stmt.res_bind_all(sowner, round_id, problem_id, last_judgment, p_last_edit);
	// If the submission doesn't exist (probably was removed)
	if (not stmt.next()) {
		// Cancel the job
		stmt = db_conn.prepare("UPDATE job_queue"
			" SET status=" JQSTATUS_CANCELED_STR " WHERE id=?");
		stmt.bindAndExecute(job_id);
		stdlog("Judging of the submission ", submission_id, " canceled, since"
			" there is no such submission.");
		return;
	}

	// If the problem wasn't modified since last judgment and submission has
	// already been rejudged after the job was created
	if (last_judgment > p_last_edit and last_judgment > job_creation_time) {
		// Skit the job - the submission has already been rejudged
		stmt = db_conn.prepare("UPDATE job_queue"
			" SET status=" JQSTATUS_DONE_STR " WHERE id=?");
		stmt.bindAndExecute(job_id);
		stdlog("Judging of the submission ", submission_id, " skipped.");
		return;
	}

	JudgeWorker jworker;
	jworker.setVerbosity(true);

	stdlog("Judging submission ", submission_id, " (problem: ", problem_id,
		')');
	jworker.loadPackage(concat_tostr("problems/", problem_id),
		getFileContents(concat_tostr("problems/", problem_id, "/Simfile"))
	);

	// Variables
	SubmissionStatus status = SubmissionStatus::OK;
	InplaceBuff<65536> initial_report, final_report;
	int64_t total_score = 0;

	auto send_report = [&] {
		// TODO: support for the submissions in the problemset
		static_assert(int(SubmissionType::NORMAL) == 0 &&
			int(SubmissionType::FINAL) == 1, "Needed below where "
				"\"... type<= ...\"");

		/* Update submission */

		// Special status
		if (isIn(status, {SubmissionStatus::COMPILATION_ERROR,
		                  SubmissionStatus::CHECKER_COMPILATION_ERROR,
		                  SubmissionStatus::JUDGE_ERROR}))
		{
			stmt = db_conn.prepare(
				// x.id - id of a submission which will have set
				//   type=FINAL (latest with non-fatal status)
				//   UNION with 0
				// y.id - id of a submissions which will have set
				//   type=NORMAL (dropped from type=FINAL)
				//   UNION with 0
				// z.id - submission_id
				//
				// UNION with 0 - because if x or y was empty then the
				//   whole query wouldn't be executed (and 0 is safe
				//   because no submission with id=0 exists)
				"UPDATE submissions s,"
					" ((SELECT id FROM submissions WHERE owner=?"
								" AND round_id=? AND type<=" STYPE_FINAL_STR
								" AND status<" SSTATUS_PENDING_STR " AND id!=?"
							" ORDER BY id DESC LIMIT 1)"
						" UNION (SELECT 0 AS id)) x,"
					" ((SELECT id FROM submissions WHERE owner=?"
							" AND round_id=? AND type=" STYPE_FINAL_STR ")"
						" UNION (SELECT 0 AS id)) y,"
					" (SELECT (SELECT ?) AS id) z"
				// Set type properly and other columns ONLY for just
				//   judged submission
				" SET s.type=IF(s.id=x.id," STYPE_FINAL_STR ","
					"IF(s.type<=" STYPE_FINAL_STR "," STYPE_NORMAL_STR
						",s.type)),"
					"s.status=IF(s.id=z.id,?,s.status),"
					"s.last_judgment=IF(s.id=z.id,?,s.last_judgment),"
					"s.initial_report=IF(s.id=z.id,?,s.initial_report),"
					"s.final_report=IF(s.id=z.id,?,s.final_report),"
					"s.score=IF(s.id=z.id,NULL,s.score)"
				"WHERE s.id=x.id OR s.id=y.id OR s.id=z.id");

		// Normal status
		} else {
			stmt = db_conn.prepare(
				// x.id - id of a submission which will have set
				//   type=FINAL (latest with non-fatal status)
				//   UNION with 0
				// y.id - id of a submissions which will have set
				//   type=NORMAL (dropped from type=FINAL)
				//   UNION with 0
				// z.id - submission_id
				//
				// UNION with 0 - because if x or y was empty then the
				//   whole query wouldn't be executed (and 0 is safe
				//   because no submission with id=0 exists)
				"UPDATE submissions s,"
					" ((SELECT id FROM submissions WHERE owner=?"
							" AND round_id=? AND type<=" STYPE_FINAL_STR
							" AND (status<" SSTATUS_PENDING_STR " OR id=?)"
							" ORDER BY id DESC LIMIT 1)"
						" UNION (SELECT 0 AS id)) x,"
					" ((SELECT id FROM submissions WHERE owner=?"
							" AND round_id=? AND type=" STYPE_FINAL_STR ")"
						" UNION (SELECT 0 AS id)) y,"
					" (SELECT (SELECT ?) AS id) z"
				// Set type properly and other columns ONLY for just
				//   judged submission
				" SET s.type=IF(s.id=x.id," STYPE_FINAL_STR ","
					"IF(s.type<=" STYPE_FINAL_STR "," STYPE_NORMAL_STR
						",s.type)),"
					"s.status=IF(s.id=z.id,?,s.status),"
					"s.last_judgment=IF(s.id=z.id,?,s.last_judgment),"
					"s.initial_report=IF(s.id=z.id,?,s.initial_report),"
					"s.final_report=IF(s.id=z.id,?,s.final_report),"
					"s.score=IF(s.id=z.id,?,s.score)"
				"WHERE s.id=x.id OR s.id=y.id OR s.id=z.id");

			stmt.bind(10, total_score);
		}

		uint ustatus = static_cast<uint>(status);
		string curr_date = date();

		stmt.bind(0, sowner);
		stmt.bind(1, round_id);
		stmt.bind(2, submission_id);
		stmt.bind(3, sowner);
		stmt.bind(4, round_id);
		stmt.bind(5, submission_id);
		stmt.bind(6, ustatus);
		stmt.bind(7, curr_date);
		stmt.bind(8, initial_report);
		stmt.bind(9, final_report);
		stmt.fixBinds();
		stmt.execute();

		stmt = db_conn.prepare("UPDATE job_queue"
			" SET status=" JQSTATUS_DONE_STR " WHERE id=?");
		stmt.bindAndExecute(job_id);

	};

	string compilation_errors;

	// Compile checker
	stdlog("Compiling checker...");
	if (jworker.compileChecker(CHECKER_COMPILATION_TIME_LIMIT,
		&compilation_errors, COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
	{
		stdlog("Checker compilation failed.");

		status = SubmissionStatus::CHECKER_COMPILATION_ERROR;
		initial_report = concat("<pre class=\"compilation-errors\">",
			htmlEscape(compilation_errors), "</pre>");

		return send_report();
	}
	stdlog("Done.");

	// Compile solution
	stdlog("Compiling solution...");
	if (jworker.compileSolution(concat_tostr("solutions/", submission_id, ".cpp"),
		SOLUTION_COMPILATION_TIME_LIMIT, &compilation_errors,
		COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
	{
		stdlog("Solution compilation failed.");

		status = SubmissionStatus::COMPILATION_ERROR;
		initial_report = concat("<pre class=\"compilation-errors\">",
			htmlEscape(compilation_errors), "</pre>");

		return send_report();
	}
	stdlog("Done.");

	// Creates xml report from JudgeReport
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
				"<td>", usecToSecStr(test.runtime, 2, false), " / ",
					usecToSecStr(test.time_limit, 2, false), "</td>"
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
		auto span_status = [](JudgeReport::Test::Status s) {
			switch (s) {
			case JudgeReport::Test::OK: return "\033[1;32mOK\033[m";
			case JudgeReport::Test::WA: return "\033[1;31mWRONG\033[m";
			case JudgeReport::Test::TLE: return "\033[1;33mTLE\033[m";
			case JudgeReport::Test::MLE: return "\033[1;33mMLE\033[m";
			case JudgeReport::Test::RTE: return "\033[1;31mRTE\033[m";
			case JudgeReport::Test::CHECKER_ERROR:
				return "\033[1;33mCHECKER_ERROR\033[m";
			}

			return "UNKNOWN";
		};

		stdlog("Job ", job_id, " -> submission ", submission_id, " (problem ",
			problem_id, ")\n"
			"Initial judge report: ", rep1.pretty_dump(span_status), "\n"
			"Final judge report: ", rep2.pretty_dump(span_status), "\n");

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
		stdlog("Judge error.");

		status = SubmissionStatus::JUDGE_ERROR;
		initial_report = concat("<pre>", htmlEscape(e.what()), "</pre>");
		final_report = "";
	}

	send_report();
}

void judgeModelSolution(StringView job_id, JobQueueType original_job_type) {
	sim::Conver::ReportBuff report;
	report.append("Stage: Judging the model solution");

	auto set_failure= [&] {
		auto stmt = db_conn.prepare("UPDATE job_queue"
			" SET status=" JQSTATUS_FAILED_STR ", data=CONCAT(data,?)"
			" WHERE id=? AND status!=" JQSTATUS_CANCELED_STR);
		stmt.bindAndExecute(report.str, job_id);

		stdlog("Job: ", job_id, '\n', report.str);
	};

	StringBuff<PATH_MAX> package_path {"jobs_files/", job_id, '/'};
	string simfile_str = getFileContents(
		StringBuff<PATH_MAX>(package_path, "Simfile"));

	sim::Simfile simfile {simfile_str};

	JudgeWorker jworker;
	jworker.setVerbosity(true);
	jworker.loadPackage(package_path.str, std::move(simfile_str));

	string compilation_errors;

	// Compile checker
	report.append("Compiling checker...");
	if (jworker.compileChecker(CHECKER_COMPILATION_TIME_LIMIT,
		&compilation_errors, COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
	{
		report.append("Checker's compilation failed:");
		report.append(compilation_errors);
		return set_failure();
	}
	report.append("Done.");

	// Compile the model solution
	simfile.loadAll();
	report.append("Compiling the model solution...");
	if (jworker.compileSolution(StringBuff<65536>(package_path,
			simfile.solutions[0]),
		SOLUTION_COMPILATION_TIME_LIMIT, &compilation_errors,
		COMPILATION_ERRORS_MAX_LENGTH, PROOT_PATH))
	{
		report.append("Solution's compilation failed.");
		report.append(compilation_errors);
		return set_failure();
	}
	report.append("Done.");

	// Judge
	report.append("Judging...");
	JudgeReport rep1 = jworker.judge(false);
	JudgeReport rep2 = jworker.judge(true);

	report.append("Initial judge report: ", rep1.pretty_dump());
	report.append("Final judge report: ", rep2.pretty_dump());

	sim::Conver conver;
	conver.setVerbosity(true);
	conver.setPackagePath(package_path.str);

	try {
		conver.finishConstructingSimfile(simfile, rep1, rep2);

	} catch (const std::exception& e) {
		report.str += conver.getReport();
		report.append("Conver failed: ", e.what());
		return set_failure();
	}

	// Put the Simfile in the package
	putFileContents(package_path.append("Simfile"), simfile.dump());

	auto stmt = db_conn.prepare("UPDATE job_queue"
		" SET type=?, status=" JQSTATUS_PENDING_STR ","
			" data=CONCAT(data,?)"
		" WHERE id=? AND status!=" JQSTATUS_CANCELED_STR);
	stmt.bindAndExecute(uint(original_job_type), report.str, job_id);

	stdlog("Job: ", job_id, '\n', report.str);
}
