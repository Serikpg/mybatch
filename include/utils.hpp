#pragma once
#include <string>
#include <vector>
#include <map>

struct CmdResult {
    int exit_code;
    std::string stdout_str;
    std::string stderr_str;
};

struct Config {
    std::string remote;
    std::string work_dir;
    int interval = 15;
};

CmdResult run_subprocess(const std::vector<std::string>& cmd, const std::string& cwd = "");
std::map<std::string, std::string> check_user_slurm_jobs(const std::string& remote = "");
std::string get_job_state_from_sacct(const std::string& slurm_job_id, const std::string& remote = "");
void get_job_logs_from_scontrol(const std::string& slurm_job_id, std::string& out_path, std::string& err_path, const std::string& remote = "");

Config load_config();
std::string get_config_path();
std::string get_current_timestamp();
std::string get_timestamp_plus_seconds(int seconds);
std::string get_absolute_path(const std::string& path);
std::string get_cwd();
std::string get_db_path();
std::string trim(const std::string& s);
