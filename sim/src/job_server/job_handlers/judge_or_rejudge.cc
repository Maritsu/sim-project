#include "src/job_server/job_handlers/judge_or_rejudge.hh"
#include "sim/submissions/submission.hh"
#include "sim/submissions/update_final.hh"
#include "src/job_server/main.hh"

using sim::submissions::Submission;

namespace job_server::job_handlers {

void JudgeOrRejudge::run() {
    STACK_UNWINDING_MARK;

    // Gather the needed information about the submission
    auto stmt = mysql.prepare("SELECT s.file_id, s.language, s.owner,"
                              " s.contest_problem_id, s.problem_id,"
                              " s.last_judgment, p.file_id, p.last_edit "
                              "FROM submissions s, problems p "
                              "WHERE p.id=problem_id AND s.id=?");
    stmt.bind_and_execute(submission_id_);
    uint64_t submission_file_id = 0;
    uint64_t problem_file_id = 0;
    uint64_t problem_id = 0;
    mysql::Optional<uint64_t> sowner;
    mysql::Optional<uint64_t> contest_problem_id;
    InplaceBuff<64> last_judgment;
    InplaceBuff<64> p_last_edit;
    EnumVal<Submission::Language> lang{};
    stmt.res_bind_all(
        submission_file_id, lang, sowner, contest_problem_id, problem_id, last_judgment,
        problem_file_id, p_last_edit);
    // If the submission doesn't exist (probably was removed)
    if (not stmt.next()) {
        return set_failure(
            "Failed the job of judging the submission ", submission_id_,
            ", since there is no such submission.");
    }

    // If the problem wasn't modified since last judgment and submission has
    // already been rejudged after the job was created
    if (last_judgment > p_last_edit and last_judgment > job_creation_time_) {
        // Skip the job - the submission has already been rejudged
        return job_cancelled(
            "Skipped judging of the submission ", submission_id_,
            " because it has already been rejudged after this "
            "job had been scheduled");
    }

    std::string judging_began = mysql_date();

    job_log("Judging submission ", submission_id_, " (problem: ", problem_id, ')');
    load_problem_package(sim::internal_files::path_of(problem_file_id));

    auto update_submission = [&](decltype(Submission::initial_status) initial_status,
                                 decltype(Submission::full_status) full_status,
                                 std::optional<int64_t> score, auto&& initial_report,
                                 auto&& final_report) {
        {
            auto transaction = mysql.start_transaction();
            sim::submissions::update_final_lock(mysql, sowner, problem_id);

            using ST = Submission::Type;
            // Get the submission's ACTUAL type
            stmt = mysql.prepare("SELECT type FROM submissions WHERE id=?");
            stmt.bind_and_execute(submission_id_);
            EnumVal<ST> stype{};
            stmt.res_bind_all(stype);
            if (not stmt.next()) {
                return; // Ignore errors (deleted submission)
            }

            // Update submission
            stmt = mysql.prepare("UPDATE submissions "
                                 "SET final_candidate=?, initial_status=?,"
                                 " full_status=?, score=?, last_judgment=?,"
                                 " initial_report=?, final_report=? "
                                 "WHERE id=?");

            if (is_fatal(full_status)) {
                stmt.bind_and_execute(
                    false, initial_status, full_status, nullptr, judging_began, initial_report,
                    final_report, submission_id_);
            } else {
                stmt.bind_and_execute(
                    (stype == ST::NORMAL and score.has_value()), initial_status, full_status,
                    score, judging_began, initial_report, final_report, submission_id_);
            }

            sim::submissions::update_final(
                mysql, sowner, problem_id, contest_problem_id, false);

            transaction.commit();
        }
    };

    auto compilation_errors =
        compile_solution(sim::internal_files::path_of(submission_file_id), to_sol_lang(lang));
    if (compilation_errors.has_value()) {
        update_submission(
            Submission::Status::COMPILATION_ERROR, Submission::Status::COMPILATION_ERROR,
            std::nullopt,
            concat(
                "<pre class=\"compilation-errors\">", html_escape(compilation_errors.value()),
                "</pre>"),
            "");

        return job_done();
    }

    // Compile checker
    compilation_errors = compile_checker();
    if (compilation_errors.has_value()) {
        errlog(
            "Job ", job_id_, " (submission ", submission_id_, ", problem ", problem_id,
            "): Checker compilation failed");
        update_submission(
            Submission::Status::CHECKER_COMPILATION_ERROR,
            Submission::Status::CHECKER_COMPILATION_ERROR, std::nullopt, "", "");

        return job_done();
    }

    auto send_judge_report =
        [&, initial_status = Submission::Status::OK, initial_report = InplaceBuff<1 << 16>(),
         initial_score = static_cast<int64_t>(0)](
            const sim::JudgeReport& jreport, bool final, bool partial) mutable {
            auto rep = construct_report(jreport, final);
            auto status = calc_status(jreport);
            // Count score
            int64_t score = 0;
            for (auto&& group : jreport.groups) {
                score += group.score;
            }

            // Log reports
            auto job_log_len = job_log_holder_.size;
            job_log(
                "Job ", job_id_, " -> submission ", submission_id_, " (problem ", problem_id,
                ")\n", (partial ? "Partial j" : "J"), "udge report: ", jreport.judge_log);

            stmt = mysql.prepare("UPDATE jobs SET data=? WHERE id=?");
            stmt.bind_and_execute(get_log(), job_id_);
            if (partial) {
                job_log_holder_.size = job_log_len;
            }

            if (not final) {
                initial_report = rep;
                initial_status = status;
                initial_score = score;
                return update_submission(
                    status, Submission::Status::PENDING, std::nullopt, rep, "");
            }

            // Final
            score += initial_score;
            // If initial tests haven't passed
            if (initial_status != Submission::Status::OK and
                status != Submission::Status::JUDGE_ERROR) {
                status = initial_status;
            }

            update_submission(initial_status, status, score, initial_report, rep);
        };

    try {
        // Judge
        sim::VerboseJudgeLogger logger(true);

        sim::JudgeReport initial_jrep =
            jworker_.judge(false, logger, [&](const sim::JudgeReport& partial) {
                send_judge_report(partial, false, true);
            });
        send_judge_report(initial_jrep, false, false);

        sim::JudgeReport final_jrep =
            jworker_.judge(true, logger, [&](const sim::JudgeReport& partial) {
                send_judge_report(partial, true, true);
            });
        send_judge_report(final_jrep, true, false);

        // Log checker errors
        for (auto&& rep : {initial_jrep, final_jrep}) {
            for (auto&& group : rep.groups) {
                for (auto&& test : group.tests) {
                    if (test.status == sim::JudgeReport::Test::CHECKER_ERROR) {
                        errlog(
                            "Checker error: submission ", submission_id_,
                            " (problem id: ", problem_id, ") test `", test.name, '`');
                    }
                }
            }
        }

        // Log syscall problems (to errlog)
        for (auto&& rep : {initial_jrep, final_jrep}) {
            for (auto&& group : rep.groups) {
                for (auto&& test : group.tests) {
                    if (has_one_of_prefixes(
                            test.comment,
                            "Runtime error (Error: ", "Runtime error (failed to get syscall",
                            "Runtime error (forbidden syscall"))
                    {
                        errlog(
                            "Submission ", submission_id_, " (problem ", problem_id,
                            "): ", test.name, " -> ", test.comment);
                    }
                }
            }
        }

        return job_done();

    } catch (const std::exception& e) {
        ERRLOG_CATCH(e);

        job_log("Judge error.");
        job_log("Caught exception -> ", e.what());

        update_submission(
            Submission::Status::JUDGE_ERROR, Submission::Status::JUDGE_ERROR, std::nullopt, "",
            "");

        return job_done();
    }
}

} // namespace job_server::job_handlers
