#include "db.hpp"
#include "utils.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <regex>

void log_msg(const std::string& msg) {
    std::cout << get_current_timestamp() << " - mybatchd - " << msg << std::endl;
}

int main(int argc, char** argv) {
    int interval = 15;
    if (argc == 3 && std::string(argv[1]) == "--interval") {
        interval = std::stoi(argv[2]);
    }

    log_msg("Starting Slurm local queue daemon. Polling every " + std::to_string(interval) + "s.");

    Database db;

    while (true) {
        try {
            auto active_jobs = db.get_active_jobs();
            auto slurm_jobs = check_user_slurm_jobs();

            for (const auto& j : active_jobs) {
                if (slurm_jobs.find(j.slurm_job_id) != slurm_jobs.end()) {
                    std::string slurm_state = slurm_jobs[j.slurm_job_id];
                    std::string new_status = j.status;
                    if (slurm_state == "R") new_status = "RUNNING";
                    else if (slurm_state == "PD" || slurm_state == "CF") new_status = "SUBMITTED";
                    else if (slurm_state == "CG") new_status = "RUNNING";

                    if (new_status != j.status) {
                        log_msg("Job " + std::to_string(j.id) + " (Slurm " + j.slurm_job_id + ") transitioned " + j.status + " -> " + new_status);
                        db.update_status(j.id, new_status);

                        if (new_status == "RUNNING") {
                            std::string out_path, err_path;
                            get_job_logs_from_scontrol(j.slurm_job_id, out_path, err_path);
                            if (!out_path.empty() || !err_path.empty()) {
                                db.update_logs(j.id, out_path, err_path);
                            }
                        }
                    }
                } else {
                    std::string sacct_state = get_job_state_from_sacct(j.slurm_job_id);
                    std::string final_state = "FAILED";
                    if (sacct_state.find("COMPLETED") == 0) final_state = "COMPLETED";
                    else if (sacct_state.find("CANCELLED") == 0) final_state = "CANCELLED";
                    else if (sacct_state.find("FAILED") == 0 || sacct_state.find("TIMEOUT") == 0 || sacct_state.find("OUT_OF_MEMORY") == 0 || sacct_state.find("NODE_FAIL") == 0) final_state = "FAILED";
                    
                    log_msg("Job " + std::to_string(j.id) + " (Slurm " + j.slurm_job_id + ") ended with state " + final_state + " (sacct: " + sacct_state + ")");
                    db.update_status(j.id, final_state);
                }
            }

            auto slurm_jobs_after = check_user_slurm_jobs();
            if (slurm_jobs_after.empty()) {
                Job q_job;
                if (db.get_next_queued_job(q_job)) {
                    log_msg("Found no active slurm jobs. Submitting local job " + std::to_string(q_job.id) + ": " + q_job.script_path);
                    
                    CmdResult res = run_subprocess({"sbatch", q_job.script_path}, q_job.work_dir);
                    if (res.exit_code == 0) {
                        std::regex re("Submitted batch job (\\d+)");
                        std::smatch match;
                        if (std::regex_search(res.stdout_str, match, re)) {
                            std::string s_id = match[1];
                            db.update_slurm_id_and_status(q_job.id, s_id, "SUBMITTED");
                            log_msg("Successfully submitted job " + std::to_string(q_job.id) + " to Slurm. Slurm ID: " + s_id);
                        } else {
                            log_msg("Error: Could not parse slurm job id from output: " + res.stdout_str);
                        }
                    } else {
                        log_msg("Failed to submit job " + std::to_string(q_job.id) + ": " + res.stderr_str);
                        int new_attempts = q_job.submit_attempts + 1;
                        int backoff = std::min((1 << new_attempts) * 10, 3600);
                        std::string next_retry = get_timestamp_plus_seconds(backoff);
                        db.update_retry(q_job.id, new_attempts, next_retry);
                        log_msg("Scheduled retry for job " + std::to_string(q_job.id) + " at " + next_retry + " (attempt " + std::to_string(new_attempts) + ")");
                    }
                }
            }
        } catch (const std::exception& e) {
            log_msg(std::string("Daemon error: ") + e.what());
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval));
    }
    return 0;
}
