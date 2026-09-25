#include "db.hpp"
#include "utils.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>

void usage(const std::string& prog) {
    std::cerr << "Usage:\n"
              << "  mybatch <script_path>\n"
              << "  mystatus\n"
              << "  mycancel <local_id>\n"
              << "  mylogs <local_id>\n";
    exit(1);
}

int main(int argc, char** argv) {
    if (argc < 1) return 1;
    std::string prog(argv[0]);
    
    std::string base_prog = prog.substr(prog.find_last_of("/\\") + 1);

    Database db;

    if (base_prog == "mybatch") {
        if (argc != 2) usage(base_prog);
        std::string script = get_absolute_path(argv[1]);
        if (script.empty()) {
            std::cerr << "Error: script " << argv[1] << " not found.\n";
            return 1;
        }
        std::string cwd = get_cwd();
        int id = db.add_job(script, cwd);
        if (id > 0) {
            std::cout << "Job " << id << " successfully added to local queue.\n";
        } else {
            std::cerr << "Failed to add job to queue.\n";
        }
    } 
    else if (base_prog == "mystatus") {
        auto jobs = db.get_recent_jobs(20);
        if (jobs.empty()) {
            std::cout << "Queue is empty.\n";
            return 0;
        }
        std::cout << std::left 
                  << std::setw(6) << "ID" 
                  << std::setw(12) << "Slurm ID" 
                  << std::setw(12) << "Status" 
                  << std::setw(22) << "Submitted At" 
                  << std::setw(10) << "Attempts" 
                  << "Script\n";
        std::cout << std::string(80, '-') << "\n";
        for (const auto& j : jobs) {
            std::string s_id = j.slurm_job_id.empty() ? "-" : j.slurm_job_id;
            std::string script_name = j.script_path.substr(j.script_path.find_last_of("/\\") + 1);
            std::cout << std::left 
                      << std::setw(6) << j.id 
                      << std::setw(12) << s_id 
                      << std::setw(12) << j.status 
                      << std::setw(22) << j.created_at 
                      << std::setw(10) << j.submit_attempts 
                      << script_name << "\n";
        }
    } 
    else if (base_prog == "mycancel") {
        if (argc != 2) usage(base_prog);
        int id = std::stoi(argv[1]);
        Job j;
        if (!db.get_job(id, j)) {
            std::cerr << "Error: Local job " << id << " not found.\n";
            return 1;
        }
        if (j.status == "COMPLETED" || j.status == "FAILED" || j.status == "CANCELLED") {
            std::cout << "Job " << id << " is already in terminal state: " << j.status << "\n";
        } else {
            if (!j.slurm_job_id.empty() && (j.status == "SUBMITTED" || j.status == "RUNNING")) {
                std::cout << "Cancelling Slurm job " << j.slurm_job_id << "...\n";
                run_subprocess({"scancel", j.slurm_job_id});
            }
            db.update_status(id, "CANCELLED");
            std::cout << "Local job " << id << " has been cancelled.\n";
        }
    } 
    else if (base_prog == "mylogs") {
        if (argc != 2) usage(base_prog);
        int id = std::stoi(argv[1]);
        Job j;
        if (!db.get_job(id, j)) {
            std::cerr << "Error: Local job " << id << " not found.\n";
            return 1;
        }
        std::string out = j.stdout_path;
        if (out.empty() && !j.slurm_job_id.empty()) {
            out = j.work_dir + "/slurm-" + j.slurm_job_id + ".out";
        }
        if (!out.empty()) {
            std::cout << "--- STDOUT (" << out << ") ---\n";
            std::ifstream f(out);
            if (f) std::cout << f.rdbuf() << "\n";
            else std::cout << "Could not read " << out << "\n";
        } else {
            std::cout << "STDOUT log path unknown.\n";
        }
        
        if (!j.stderr_path.empty() && j.stderr_path != out) {
            std::cout << "\n--- STDERR (" << j.stderr_path << ") ---\n";
            std::ifstream f(j.stderr_path);
            if (f) std::cout << f.rdbuf() << "\n";
            else std::cout << "Could not read " << j.stderr_path << "\n";
        }
    } 
    else {
        usage(base_prog);
    }
    return 0;
}
