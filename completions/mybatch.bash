# Bash completion for mybatch suite (mybatch, mybatchd, mycancel, mystatus, mylogs)

_mybatch_ssh_hosts() {
    local hosts=""
    if [[ -f ~/.ssh/config ]]; then
        hosts=$(grep -E -i '^\s*Host\s+' ~/.ssh/config 2>/dev/null | grep -v '\*' | awk '{for(i=2;i<=NF;++i) print $i}')
    fi
    if [[ -f ~/.ssh/known_hosts ]]; then
        local kh_hosts
        kh_hosts=$(cut -f 1 -d ' ' ~/.ssh/known_hosts 2>/dev/null | cut -f 1 -d ',' | grep -v '^\[' | grep -v '\*' | grep -v '^|')
        hosts="$hosts $kh_hosts"
    fi
    echo "$hosts"
}

_mybatch_db_jobs() {
    local filter="$1"
    local db="$HOME/.slurm_queue/queue.db"
    if [[ -f "$db" ]] && command -v sqlite3 >/dev/null 2>&1; then
        if [[ "$filter" == "active" ]]; then
            sqlite3 "$db" "SELECT id FROM jobs WHERE status IN ('QUEUED', 'SUBMITTED', 'RUNNING') ORDER BY id DESC LIMIT 50;" 2>/dev/null
        else
            sqlite3 "$db" "SELECT id FROM jobs ORDER BY id DESC LIMIT 50;" 2>/dev/null
        fi
    fi
}

_mybatch_comp() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    local opts="-f --from -w --workdir --chdir -C -D -l --local -r --remote -h --help"

    case "$prev" in
        -r|--remote)
            local hosts
            hosts=$(_mybatch_ssh_hosts)
            COMPREPLY=( $(compgen -W "$hosts" -- "$cur") )
            return 0
            ;;
        -f|--from|-w|--workdir|--chdir|-C|-D)
            COMPREPLY=( $(compgen -d -- "$cur") )
            return 0
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
        return 0
    fi

    # Fallback to standard filename completion
    if declare -F _filedir >/dev/null 2>&1; then
        _filedir
    else
        COMPREPLY=( $(compgen -f -- "$cur") )
    fi
}

_mybatchd_comp() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    local opts="-r --remote -i --interval -h --help"

    case "$prev" in
        -r|--remote)
            local hosts
            hosts=$(_mybatch_ssh_hosts)
            COMPREPLY=( $(compgen -W "$hosts" -- "$cur") )
            return 0
            ;;
        -i|--interval)
            COMPREPLY=( $(compgen -W "5 10 15 30 60" -- "$cur") )
            return 0
            ;;
    esac

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
        return 0
    fi
}

_mycancel_comp() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "-h --help" -- "$cur") )
        return 0
    fi
    local active_jobs
    active_jobs=$(_mybatch_db_jobs "active")
    COMPREPLY=( $(compgen -W "$active_jobs" -- "$cur") )
}

_mylogs_comp() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "-h --help" -- "$cur") )
        return 0
    fi
    local all_jobs
    all_jobs=$(_mybatch_db_jobs "all")
    COMPREPLY=( $(compgen -W "$all_jobs" -- "$cur") )
}

_mystatus_comp() {
    local cur prev
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    if [[ "$prev" == "-r" || "$prev" == "--remote" ]]; then
        local hosts
        hosts=$(_mybatch_ssh_hosts)
        COMPREPLY=( $(compgen -W "$hosts" -- "$cur") )
        return 0
    fi

    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "-a --all -r --remote -v --verbose -h --help" -- "$cur") )
        return 0
    fi
}

complete -F _mybatch_comp -o default -o filenames mybatch
complete -F _mybatchd_comp mybatchd
complete -F _mycancel_comp mycancel
complete -F _mylogs_comp mylogs
complete -F _mystatus_comp mystatus

# Also support completions when invoked with ./mybatch or path
complete -F _mybatch_comp -o default -o filenames ./mybatch
complete -F _mybatchd_comp ./mybatchd
complete -F _mycancel_comp ./mycancel
complete -F _mylogs_comp ./mylogs
complete -F _mystatus_comp ./mystatus
