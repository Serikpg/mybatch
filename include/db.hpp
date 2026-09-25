#pragma once
#include <string>
#include <vector>

struct Job {
    int id = 0;
    std::string script_path;
    std::string work_dir;
    std::string status;
    std::string slurm_job_id;
    std::string created_at;
    std::string updated_at;
    int submit_attempts = 0;
    std::string next_retry_at;
    std::string stdout_path;
    std::string stderr_path;
    bool is_local = false;
    std::string remote_host;
};

class Database {
public:
    Database();
    ~Database();
    
    int add_job(const std::string& script_path, const std::string& work_dir, bool is_local = false, const std::string& remote_host = "");
    std::vector<Job> get_recent_jobs(int limit);
    bool get_job(int id, Job& job);
    void update_status(int id, const std::string& status);
    void update_slurm_id_and_status(int id, const std::string& slurm_id, const std::string& status, const std::string& remote_host = "");
    void update_logs(int id, const std::string& stdout_path, const std::string& stderr_path);
    void update_retry(int id, int attempts, const std::string& next_retry_at);
    std::vector<Job> get_active_jobs();
    bool get_next_queued_job(Job& job);
    
private:
    struct sqlite3* db;
};
