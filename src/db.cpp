#include "db.hpp"
#include "utils.hpp"
#include <sqlite3.h>
#include <iostream>

Database::Database() {
    std::string db_path = get_db_path();
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
    }

    const char* sql = 
        "CREATE TABLE IF NOT EXISTS jobs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "script_path TEXT NOT NULL,"
        "work_dir TEXT NOT NULL,"
        "status TEXT NOT NULL,"
        "slurm_job_id TEXT,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "submit_attempts INTEGER DEFAULT 0,"
        "next_retry_at TIMESTAMP,"
        "stdout_path TEXT,"
        "stderr_path TEXT"
        ");";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql, 0, 0, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << "\n";
        sqlite3_free(err_msg);
    }
}

Database::~Database() {
    if (db) sqlite3_close(db);
}

int Database::add_job(const std::string& script_path, const std::string& work_dir) {
    std::string sql = "INSERT INTO jobs (script_path, work_dir, status) VALUES (?, ?, 'QUEUED');";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, script_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, work_dir.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        int id = sqlite3_last_insert_rowid(db);
        sqlite3_finalize(stmt);
        return id;
    }
    return -1;
}

static Job parse_job_row(sqlite3_stmt* stmt) {
    Job job;
    job.id = sqlite3_column_int(stmt, 0);
    job.script_path = (const char*)sqlite3_column_text(stmt, 1);
    job.work_dir = (const char*)sqlite3_column_text(stmt, 2);
    job.status = (const char*)sqlite3_column_text(stmt, 3);
    
    const char* s_id = (const char*)sqlite3_column_text(stmt, 4);
    if (s_id) job.slurm_job_id = s_id;
    
    const char* created = (const char*)sqlite3_column_text(stmt, 5);
    if (created) job.created_at = created;
    
    job.submit_attempts = sqlite3_column_int(stmt, 7);
    
    const char* out = (const char*)sqlite3_column_text(stmt, 9);
    if (out) job.stdout_path = out;
    
    const char* err = (const char*)sqlite3_column_text(stmt, 10);
    if (err) job.stderr_path = err;
    
    return job;
}

std::vector<Job> Database::get_recent_jobs(int limit) {
    std::vector<Job> jobs;
    std::string sql = "SELECT id, script_path, work_dir, status, slurm_job_id, created_at, updated_at, submit_attempts, next_retry_at, stdout_path, stderr_path FROM jobs ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            jobs.push_back(parse_job_row(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return jobs;
}

bool Database::get_job(int id, Job& job) {
    std::string sql = "SELECT id, script_path, work_dir, status, slurm_job_id, created_at, updated_at, submit_attempts, next_retry_at, stdout_path, stderr_path FROM jobs WHERE id = ?;";
    sqlite3_stmt* stmt;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            job = parse_job_row(stmt);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}

void Database::update_status(int id, const std::string& status) {
    std::string sql = "UPDATE jobs SET status = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::update_slurm_id_and_status(int id, const std::string& slurm_id, const std::string& status) {
    std::string sql = "UPDATE jobs SET status = ?, slurm_job_id = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, slurm_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::update_logs(int id, const std::string& stdout_path, const std::string& stderr_path) {
    std::string sql = "UPDATE jobs SET stdout_path = ?, stderr_path = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (!stdout_path.empty()) sqlite3_bind_text(stmt, 1, stdout_path.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, 1);
        
        if (!stderr_path.empty()) sqlite3_bind_text(stmt, 2, stderr_path.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, 2);
        
        sqlite3_bind_int(stmt, 3, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void Database::update_retry(int id, int attempts, const std::string& next_retry_at) {
    std::string sql = "UPDATE jobs SET submit_attempts = ?, next_retry_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, attempts);
        sqlite3_bind_text(stmt, 2, next_retry_at.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<Job> Database::get_active_jobs() {
    std::vector<Job> jobs;
    std::string sql = "SELECT id, script_path, work_dir, status, slurm_job_id, created_at, updated_at, submit_attempts, next_retry_at, stdout_path, stderr_path FROM jobs WHERE status IN ('SUBMITTED', 'RUNNING');";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            jobs.push_back(parse_job_row(stmt));
        }
        sqlite3_finalize(stmt);
    }
    return jobs;
}

bool Database::get_next_queued_job(Job& job) {
    std::string sql = "SELECT id, script_path, work_dir, status, slurm_job_id, created_at, updated_at, submit_attempts, next_retry_at, stdout_path, stderr_path FROM jobs WHERE status = 'QUEUED' AND (next_retry_at IS NULL OR next_retry_at <= CURRENT_TIMESTAMP) ORDER BY id ASC LIMIT 1;";
    sqlite3_stmt* stmt;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            job = parse_job_row(stmt);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    return found;
}
