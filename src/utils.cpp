#include "utils.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <algorithm>

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string get_db_path() {
    const char* home = getenv("HOME");
    std::string db_dir = std::string(home ? home : ".") + "/.slurm_queue";
    std::string cmd = "mkdir -p " + db_dir;
    system(cmd.c_str());
    return db_dir + "/queue.db";
}

std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string get_timestamp_plus_seconds(int seconds) {
    auto future = std::chrono::system_clock::now() + std::chrono::seconds(seconds);
    std::time_t t = std::chrono::system_clock::to_time_t(future);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string get_absolute_path(const std::string& path) {
    char* abs_path = realpath(path.c_str(), NULL);
    if (abs_path) {
        std::string res(abs_path);
        free(abs_path);
        return res;
    }
    return "";
}

std::string get_cwd() {
    char buf[1024];
    if (getcwd(buf, sizeof(buf))) {
        return std::string(buf);
    }
    return "";
}

CmdResult run_subprocess(const std::vector<std::string>& cmd, const std::string& cwd) {
    CmdResult result;
    result.exit_code = -1;
    if (cmd.empty()) return result;

    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) == -1 || pipe(err_pipe) == -1) return result;

    pid_t pid = fork();
    if (pid == -1) return result;

    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);

        if (!cwd.empty()) {
            if (chdir(cwd.c_str()) != 0) {
                exit(1);
            }
        }

        std::vector<char*> args;
        for (const auto& arg : cmd) args.push_back(const_cast<char*>(arg.c_str()));
        args.push_back(nullptr);

        execvp(args[0], args.data());
        exit(1);
    } else {
        close(out_pipe[1]);
        close(err_pipe[1]);

        char buf[1024];
        ssize_t n;
        while ((n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
            result.stdout_str.append(buf, n);
        }
        while ((n = read(err_pipe[0], buf, sizeof(buf))) > 0) {
            result.stderr_str.append(buf, n);
        }

        close(out_pipe[0]);
        close(err_pipe[0]);

        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        }
    }
    return result;
}

std::map<std::string, std::string> check_user_slurm_jobs() {
    std::map<std::string, std::string> jobs;
    const char* user = getenv("USER");
    if (!user) return jobs;

    CmdResult res = run_subprocess({"squeue", "-u", user, "-h", "-o", "%i %t"});
    if (res.exit_code == 0) {
        std::istringstream iss(res.stdout_str);
        std::string line;
        while (std::getline(iss, line)) {
            std::istringstream ls(line);
            std::string id, state;
            if (ls >> id >> state) {
                jobs[id] = state;
            }
        }
    }
    return jobs;
}

std::string get_job_state_from_sacct(const std::string& slurm_job_id) {
    CmdResult res = run_subprocess({"sacct", "-j", slurm_job_id, "-X", "-n", "-P", "-o", "State"});
    if (res.exit_code == 0) {
        std::istringstream iss(res.stdout_str);
        std::string state;
        while (std::getline(iss, state)) {
            state = trim(state);
            if (!state.empty()) return state;
        }
    }
    return "UNKNOWN";
}

void get_job_logs_from_scontrol(const std::string& slurm_job_id, std::string& out_path, std::string& err_path) {
    CmdResult res = run_subprocess({"scontrol", "show", "job", slurm_job_id});
    if (res.exit_code == 0) {
        std::istringstream iss(res.stdout_str);
        std::string word;
        while (iss >> word) {
            if (word.rfind("StdOut=", 0) == 0) {
                out_path = word.substr(7);
            } else if (word.rfind("StdErr=", 0) == 0) {
                err_path = word.substr(7);
            }
        }
    }
}
