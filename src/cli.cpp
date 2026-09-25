#include "db.hpp"
#include "utils.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>

void usage(const std::string& prog, int exit_code = 1) {
    std::cout << "Usage:\n"
              << "  mybatch [options] <script_path>\n"
              << "    Options:\n"
              << "      -l, --local          Treat script_path as local file (default: remote path)\n"
              << "      -r, --remote <host>  Specify/override target SSH remote\n"
              << "      -w, --workdir <dir>  Specify working directory\n"
              << "  mystatus [options]\n"
              << "    Options:\n"
              << "      -a, --all            Show all jobs including locally cancelled\n"
              << "      -r, --remote <host>  Filter by remote host\n"
              << "  mycancel <local_id>\n"
              << "  mylogs <local_id>\n";
    exit(exit_code);
}

int main(int argc, char** argv) {
    if (argc < 1) return 1;
    std::string prog(argv[0]);
    std::string base_prog = prog.substr(prog.find_last_of("/\\") + 1);

    Config config = load_config();
    Database db;

    if (base_prog == "mybatch") {
        bool is_local = false;
        std::string remote_override = "";
        std::string workdir_override = "";
        std::string script_path = "";

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--local" || arg == "-l") {
                is_local = true;
            } else if (arg == "--remote" || arg == "-r") {
                if (i + 1 < argc) remote_override = argv[++i];
                else { std::cerr << "Error: --remote requires an argument\n"; return 1; }
            } else if (arg == "--workdir" || arg == "-w") {
                if (i + 1 < argc) workdir_override = argv[++i];
                else { std::cerr << "Error: --workdir requires an argument\n"; return 1; }
            } else if (arg == "--help" || arg == "-h") {
                usage(base_prog, 0);
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "Unknown option: " << arg << "\n";
                usage(base_prog);
            } else {
                if (script_path.empty()) {
                    script_path = arg;
                } else {
                    std::cerr << "Unexpected extra argument: " << arg << "\n";
                    return 1;
                }
            }
        }

        if (script_path.empty()) {
            std::cerr << "Error: Missing script path.\n\n";
            usage(base_prog);
        }

        std::string work_dir = workdir_override;
        std::string target_remote = !remote_override.empty() ? remote_override : config.remote;

        if (is_local) {
            std::string abs_path = get_absolute_path(script_path);
            if (abs_path.empty()) {
                std::cerr << "Error: local script not found: " << script_path << "\n";
                return 1;
            }
            script_path = abs_path;
            if (work_dir.empty()) work_dir = get_cwd();
        } else {
            // Default: Remote path mode!
            // No local filesystem check is performed.
            if (work_dir.empty()) work_dir = config.work_dir;
        }

        int id = db.add_job(script_path, work_dir, is_local, target_remote);
        if (id > 0) {
            std::cout << "Job " << id << " successfully added to local queue.\n";
            std::cout << "  Mode:        " << (is_local ? "Local file" : "Remote path (default)") << "\n";
            std::cout << "  Target host: " << (target_remote.empty() ? "localhost" : target_remote) << "\n";
            std::cout << "  Script path: " << script_path << "\n";
            if (!work_dir.empty()) {
                std::cout << "  Working dir: " << work_dir << "\n";
            }
        } else {
            std::cerr << "Failed to add job to queue.\n";
            return 1;
        }
    } 
    else if (base_prog == "mystatus") {
        bool show_all = false;
        std::string filter_remote = config.remote;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-a" || arg == "--all") {
                show_all = true;
            } else if (arg == "-r" || arg == "--remote") {
                if (i + 1 < argc) filter_remote = argv[++i];
                else { std::cerr << "Error: --remote requires an argument\n"; return 1; }
            } else if (arg == "-h" || arg == "--help") {
                std::cout << "Usage: mystatus [options]\n"
                          << "  -a, --all            Show all jobs including local aborts/cancelled drafts\n"
                          << "  -r, --remote <host>  Filter by remote host\n";
                return 0;
            }
        }

        auto jobs = db.get_recent_jobs(50);
        std::vector<Job> filtered;
        for (const auto& j : jobs) {
            if (!show_all) {
                // Exclude jobs that were cancelled locally before ever reaching Slurm
                if (j.status == "CANCELLED" && j.slurm_job_id.empty()) {
                    continue;
                }
                // If a remote is configured or specified, filter to relevant jobs
                if (!filter_remote.empty()) {
                    std::string effective_remote = !j.remote_host.empty() ? j.remote_host : "";
                    // If job was local without a remote and never reached Slurm, skip
                    if (effective_remote.empty() && j.slurm_job_id.empty()) {
                        continue;
                    }
                    if (!effective_remote.empty() && effective_remote != filter_remote) {
                        continue;
                    }
                }
            }
            filtered.push_back(j);
        }

        if (filtered.empty()) {
            std::cout << "No matching jobs found in queue.\n";
            return 0;
        }

        std::cout << std::left 
                  << std::setw(5)  << "ID" 
                  << std::setw(15) << "Remote"
                  << std::setw(11) << "Slurm ID" 
                  << std::setw(11) << "Status" 
                  << std::setw(8)  << "Type"
                  << std::setw(21) << "Submitted At" 
                  << std::setw(9)  << "Attempts" 
                  << "Script\n";
        std::cout << std::string(95, '-') << "\n";
        for (const auto& j : filtered) {
            std::string s_id = j.slurm_job_id.empty() ? "-" : j.slurm_job_id;
            std::string remote_display = j.remote_host.empty() ? (config.remote.empty() ? "local" : config.remote) : j.remote_host;
            std::string type_display = j.is_local ? "local" : "remote";
            std::string script_name = j.script_path.substr(j.script_path.find_last_of("/\\") + 1);
            std::cout << std::left 
                      << std::setw(5)  << j.id 
                      << std::setw(15) << remote_display
                      << std::setw(11) << s_id 
                      << std::setw(11) << j.status 
                      << std::setw(8)  << type_display
                      << std::setw(21) << j.created_at 
                      << std::setw(9)  << j.submit_attempts 
                      << script_name << "\n";
        }
    } 
    else if (base_prog == "mycancel") {
        if (argc < 2) usage(base_prog);
        int id = std::stoi(argv[1]);
        Job j;
        if (!db.get_job(id, j)) {
            std::cerr << "Error: Local job " << id << " not found.\n";
            return 1;
        }
        if (j.status == "COMPLETED" || j.status == "FAILED" || j.status == "CANCELLED") {
            std::cout << "Job " << id << " is already in terminal state: " << j.status << "\n";
        } else {
            std::string remote = !j.remote_host.empty() ? j.remote_host : config.remote;
            if (!j.slurm_job_id.empty() && (j.status == "SUBMITTED" || j.status == "RUNNING")) {
                std::cout << "Cancelling Slurm job " << j.slurm_job_id 
                          << (remote.empty() ? "" : (" on " + remote)) << "...\n";
                if (!remote.empty()) {
                    run_subprocess({"ssh", remote, "scancel " + j.slurm_job_id});
                } else {
                    run_subprocess({"scancel", j.slurm_job_id});
                }
            }
            db.update_status(id, "CANCELLED");
            std::cout << "Local job " << id << " has been cancelled.\n";
        }
    } 
    else if (base_prog == "mylogs") {
        if (argc < 2) usage(base_prog);
        int id = std::stoi(argv[1]);
        Job j;
        if (!db.get_job(id, j)) {
            std::cerr << "Error: Local job " << id << " not found.\n";
            return 1;
        }

        std::string remote = !j.remote_host.empty() ? j.remote_host : config.remote;
        std::string out = j.stdout_path;
        if (out.empty() && !j.slurm_job_id.empty()) {
            if (!j.work_dir.empty()) out = j.work_dir + "/slurm-" + j.slurm_job_id + ".out";
            else out = "slurm-" + j.slurm_job_id + ".out";
        }

        if (!out.empty()) {
            std::cout << "--- STDOUT (" << out << ") ---\n";
            if (!remote.empty()) {
                CmdResult res = run_subprocess({"ssh", remote, "cat " + out});
                if (res.exit_code == 0) std::cout << res.stdout_str << "\n";
                else std::cout << "Could not read remote file " << out << ": " << res.stderr_str << "\n";
            } else {
                std::ifstream f(out);
                if (f) std::cout << f.rdbuf() << "\n";
                else std::cout << "Could not read " << out << "\n";
            }
        } else {
            std::cout << "STDOUT log path unknown.\n";
        }
        
        if (!j.stderr_path.empty() && j.stderr_path != out) {
            std::cout << "\n--- STDERR (" << j.stderr_path << ") ---\n";
            if (!remote.empty()) {
                CmdResult res = run_subprocess({"ssh", remote, "cat " + j.stderr_path});
                if (res.exit_code == 0) std::cout << res.stdout_str << "\n";
                else std::cout << "Could not read remote file " << j.stderr_path << ": " << res.stderr_str << "\n";
            } else {
                std::ifstream f(j.stderr_path);
                if (f) std::cout << f.rdbuf() << "\n";
                else std::cout << "Could not read " << j.stderr_path << "\n";
            }
        }
    } 
    else {
        usage(base_prog);
    }
    return 0;
}
