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
    Config config = load_config();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--remote" || arg == "-r") {
            if (i + 1 < argc) config.remote = argv[++i];
            else { std::cerr << "Error: --remote requires argument\n"; return 1; }
        } else if (arg == "--interval" || arg == "-i") {
            if (i + 1 < argc) config.interval = std::stoi(argv[++i]);
            else { std::cerr << "Error: --interval requires argument\n"; return 1; }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: mybatchd [options]\n"
                      << "  -r, --remote <host>    Default SSH remote host (or alias from ~/.ssh/config)\n"
                      << "  -i, --interval <sec>   Polling interval in seconds (default: 15)\n";
            return 0;
        }
    }

    log_msg("Starting Slurm local queue daemon.");
    if (!config.remote.empty()) {
        log_msg("Active remote host: " + config.remote);
    } else {
        log_msg("Running in local mode (no remote specified in config or arguments).");
    }
    log_msg("Polling interval: " + std::to_string(config.interval) + "s.");

    Database db;

    while (true) {
        try {
            auto active_jobs = db.get_active_jobs();

            for (const auto& j : active_jobs) {
                std::string target_remote = !j.remote_host.empty() ? j.remote_host : config.remote;
                auto slurm_jobs = check_user_slurm_jobs(target_remote);

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
                            get_job_logs_from_scontrol(j.slurm_job_id, out_path, err_path, target_remote);
                            if (!out_path.empty() || !err_path.empty()) {
                                db.update_logs(j.id, out_path, err_path);
                            }
                        }
                    }
                } else {
                    std::string sacct_state = get_job_state_from_sacct(j.slurm_job_id, target_remote);
                    std::string final_state = "FAILED";
                    if (sacct_state.find("COMPLETED") == 0) final_state = "COMPLETED";
                    else if (sacct_state.find("CANCELLED") == 0) final_state = "CANCELLED";
                    else if (sacct_state.find("FAILED") == 0 || sacct_state.find("TIMEOUT") == 0 || sacct_state.find("OUT_OF_MEMORY") == 0 || sacct_state.find("NODE_FAIL") == 0) final_state = "FAILED";
                    
                    log_msg("Job " + std::to_string(j.id) + " (Slurm " + j.slurm_job_id + ") ended with state " + final_state + " (sacct: " + sacct_state + ")");
                    db.update_status(j.id, final_state);
                }
            }

            Job q_job;
            if (db.get_next_queued_job(q_job)) {
                std::string target_remote = !q_job.remote_host.empty() ? q_job.remote_host : config.remote;
                auto slurm_jobs_after = check_user_slurm_jobs(target_remote);

                if (slurm_jobs_after.empty()) {
                    log_msg("No active jobs on " + (target_remote.empty() ? "localhost" : target_remote) + 
                            ". Submitting local queue job " + std::to_string(q_job.id) + " (" + q_job.script_path + ")");

                    CmdResult res;
                    std::string effective_work_dir = !q_job.work_dir.empty() ? q_job.work_dir : config.work_dir;

                    if (!target_remote.empty()) {
                        std::string exec_script_path = q_job.script_path;
                        if (q_job.is_local) {
                            // Stage local file to remote machine
                            log_msg("Staging local script " + q_job.script_path + " to " + target_remote);
                            run_subprocess({"ssh", target_remote, "mkdir -p ~/.slurm_queue/staged"});
                            std::string remote_staged = "~/.slurm_queue/staged/job_" + std::to_string(q_job.id) + ".sh";
                            run_subprocess({"scp", q_job.script_path, target_remote + ":" + remote_staged});
                            exec_script_path = remote_staged;
                        }

                        std::string sbatch_cmd = "sbatch";
                        if (!effective_work_dir.empty()) {
                            sbatch_cmd += " --chdir=" + effective_work_dir;
                        }
                        sbatch_cmd += " " + exec_script_path;

                        if (!effective_work_dir.empty()) {
                            sbatch_cmd = "cd " + effective_work_dir + " && " + sbatch_cmd;
                        }
                        log_msg("Executing on " + target_remote + ": " + sbatch_cmd);
                        res = run_subprocess({"ssh", target_remote, sbatch_cmd});
                    } else {
                        std::vector<std::string> local_cmd = {"sbatch"};
                        if (!effective_work_dir.empty()) {
                            local_cmd.push_back("--chdir=" + effective_work_dir);
                        }
                        local_cmd.push_back(q_job.script_path);
                        res = run_subprocess(local_cmd, effective_work_dir);
                    }

                    if (res.exit_code == 0) {
                        std::regex re("Submitted batch job (\\d+)");
                        std::smatch match;
                        if (std::regex_search(res.stdout_str, match, re)) {
                            std::string s_id = match[1];
                            db.update_slurm_id_and_status(q_job.id, s_id, "SUBMITTED", target_remote);
                            log_msg("Successfully submitted job " + std::to_string(q_job.id) + " to Slurm. Slurm ID: " + s_id);
                        } else {
                            log_msg("Error: Could not parse Slurm job ID from output: " + res.stdout_str);
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

        std::this_thread::sleep_for(std::chrono::seconds(config.interval));
    }
    return 0;
}
