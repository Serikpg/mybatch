# Slurm Local Broker Daemon

A lightweight, C++ based local job broker for Slurm environments that enforce strict QoS policies like `MaxSubmitJobs=1`. 

This tool acts as an intermediary queue. It allows you to stage multiple `sbatch` execution tasks locally, while a background daemon ensures that at most **one** job is actively queued or running in Slurm at any given time for your user.

---

## Features

- **Remote-First Design:** By default, `mybatch` accepts remote paths on the target cluster without requiring local file existence.
- **SSH Remote Support:** Run the client and daemon from your local laptop targeting a remote HPC cluster over SSH, or run directly on the cluster login node.
- **Persistent Queue:** Uses an embedded SQLite database (`~/.slurm_queue/queue.db`) to preserve job states across SSH disconnects and shell restarts.
- **Background Daemon:** Actively monitors Slurm (`squeue`, `sacct`) and dispatches jobs only when your active job count drops to zero.
- **Fault-Tolerant Retry:** Features exponential backoff if a transient node/Slurm error causes `sbatch` to fail.
- **Client Binaries:** Lightweight, non-blocking CLI tools mimicking standard Slurm commands (`mybatch`, `mycancel`, `mystatus`, `mylogs`).

---

## Dependencies

- **C++17** compatible compiler (GCC/Clang)
- **CMake** 3.10+
- **SQLite3** (`libsqlite3-dev` or similar package)
- **OpenSSH** client (`ssh`, `scp` when using remote execution)
- **Slurm Workload Manager** (`sbatch`, `squeue`, `sacct`, `scontrol` available locally or on remote host)

---

## Build Instructions

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

All compiled binaries (`mybatch`, `mycancel`, `mystatus`, `mylogs`, `mybatchd`) will be generated inside the `build/` directory.

---

## Configuration: Defining the SSH Remote

You can define the remote cluster in three ways (in order of priority):

### 1. Configuration File (Recommended)
Edit `~/.slurm_queue/config` (automatically created on first run):
```ini
# SSH remote host (alias from ~/.ssh/config or user@hostname)
remote = mn5

# Optional default working directory on the remote cluster
workdir = /gpfs/projects/bsc/myproject

# Polling interval in seconds (default: 15)
interval = 15
```

### 2. Environment Variables
```bash
export MYBATCH_REMOTE="user@cluster.hpc.edu"
export MYBATCH_WORKDIR="/gpfs/scratch/user"
```

### 3. Command-Line Options
Pass `--remote <host>` or `-r <host>` directly to `mybatch` or `mybatchd`.

> [!NOTE]
> **No rebuild needed:** The configuration file (`~/.slurm_queue/config`) is read dynamically at runtime on every execution or daemon cycle. You can freely change `remote`, `workdir`, or `interval` at any time without recompiling the application.

---

## Bash Autocompletion

To enable `<Tab>` autocompletion for flags (`--remote`, `--local`, `--workdir`), script files, SSH host aliases, and job IDs:

Add this line to your `~/.bashrc`:
```bash
source /path/to/mybatch/completions/mybatch.bash
```
Or install it for your current user:
```bash
mkdir -p ~/.local/share/bash-completion/completions
cp completions/mybatch.bash ~/.local/share/bash-completion/completions/mybatch
```
After reloading your shell (`source ~/.bashrc`), pressing `<Tab>` will automatically complete options and arguments for `mybatch`, `mybatchd`, `mycancel`, `mystatus`, and `mylogs`.

## Usage

### 1. Start the Daemon (`mybatchd`)
Run the server daemon in a persistent background session (e.g. `tmux`, `screen`, or systemd):
```bash
./mybatchd
```
You can also override the remote host or polling interval:
```bash
./mybatchd --remote mn5 --interval 15
```

### 2. Submit Jobs (`mybatch`)
By default, `mybatch` operates on **remote paths** (no local file checks):
```bash
# Path on the remote cluster filesystem
./mybatch /gpfs/projects/bsc/user/sim_task1.sh

# Specify where the job should be delivered from (working directory):
./mybatch --from /gpfs/scratch/bsc18/bsc094088/cvdp-servers2 runners/cvdp-update-3/subset/run.sh
# (Aliases: -f, --chdir, -C, -D, -w, --workdir)

# Override target remote host for a specific job
./mybatch -r cluster2 /gpfs/home/job.sh
```

#### Submitting Local Scripts (`--local` / `-l`)
If your script is on your local machine and you want `mybatchd` to stage/transfer it to the remote cluster automatically before running `sbatch`, use the `--local` flag:
```bash
./mybatch --local ./my_local_script.sh
```

### 3. Check Queue Status (`mystatus`)
```bash
./mystatus
```
Output:
```
ID   Remote         Slurm ID   Status     Type    Submitted At         Attempts Script
-----------------------------------------------------------------------------------------------
1    mn5            1234567    RUNNING    remote  2026-09-25 07:14:43  0        sim_task1.sh
2    mn5            -          QUEUED     remote  2026-09-25 07:15:10  0        sim_task2.sh
```

### 4. View Output / Error Logs (`mylogs`)
Fetches and displays the output from Slurm (retrieved via SSH if on a remote cluster):
```bash
./mylogs <local_id>
```

### 5. Cancel a Job (`mycancel`)
Removes the job from the local queue, or invokes `scancel` on the remote cluster if already active:
```bash
./mycancel <local_id>
```
