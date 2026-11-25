#!/usr/bin/env bash
# Run a command with a controlled benchmark environment and ALWAYS restore the machine afterwards.
#
#   tests/nested/bench_env.sh [--cpu-governor GOV] [--gpu INDEX [--lgc MIN,MAX] [--lmc MIN,MAX]] -- COMMAND...
#   tests/nested/bench_env.sh --status [--gpu INDEX]
#
#   --cpu-governor GOV  set every CPU's scaling governor (e.g. performance); the previous per-CPU
#                       governors are restored on exit
#   --gpu INDEX         the GPU the clock options apply to (nvidia-smi index)
#   --lgc MIN,MAX       lock graphics clocks (MHz), restored with nvidia-smi -rgc
#   --lmc MIN,MAX       lock memory clocks (MHz), restored with nvidia-smi -rmc
#
# Needs passwordless sudo for cpupower/nvidia-smi. A lock that cannot be applied is a hard error
# (the command does not run on an uncontrolled machine). Restoration runs on normal exit, error,
# SIGINT and SIGTERM; the state actually in effect before/after is printed so a run can record it.
set -euo pipefail

governor="" gpu="" lgc="" lmc="" status=0
while (($#)); do
    case "$1" in
        --cpu-governor) governor=$2; shift 2 ;;
        --gpu) gpu=$2; shift 2 ;;
        --lgc) lgc=$2; shift 2 ;;
        --lmc) lmc=$2; shift 2 ;;
        --status) status=1; shift ;;
        --) shift; break ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

show_state() {
    echo "cpu governors: $(sort /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | uniq -c | tr '\n' ' ')"
    nvidia-smi ${gpu:+-i "$gpu"} \
        --query-gpu=index,name,pstate,clocks.gr,clocks.mem,clocks.max.gr,clocks.max.mem,power.limit \
        --format=csv,noheader
}

if ((status)); then
    show_state
    exit 0
fi
(($#)) || { echo "no command given (use -- COMMAND...)" >&2; exit 2; }
[[ -z $lgc$lmc || -n $gpu ]] || { echo "--lgc/--lmc need --gpu" >&2; exit 2; }
sudo -n true 2>/dev/null || { echo "passwordless sudo is required" >&2; exit 1; }

declare -A saved_governor=()
cpu_changed=0 gpu_gr_locked=0 gpu_mem_locked=0
restore() {
    local rc=$?
    trap - EXIT INT TERM
    if ((cpu_changed)); then
        local cpu
        for cpu in "${!saved_governor[@]}"; do
            echo "${saved_governor[$cpu]}" | sudo -n tee "/sys/devices/system/cpu/$cpu/cpufreq/scaling_governor" >/dev/null || true
        done
    fi
    ((gpu_gr_locked)) && { sudo -n nvidia-smi -i "$gpu" -rgc >/dev/null || echo "WARNING: could not reset graphics clocks on GPU $gpu" >&2; }
    ((gpu_mem_locked)) && { sudo -n nvidia-smi -i "$gpu" -rmc >/dev/null || echo "WARNING: could not reset memory clocks on GPU $gpu" >&2; }
    echo "== environment restored:"; show_state
    exit "$rc"
}
trap restore EXIT INT TERM

echo "== environment before:"; show_state
if [[ -n $governor ]]; then
    for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
        cpu=${path#/sys/devices/system/cpu/}; cpu=${cpu%%/*}
        saved_governor[$cpu]=$(<"$path")
    done
    cpu_changed=1
    for cpu in "${!saved_governor[@]}"; do
        echo "$governor" | sudo -n tee "/sys/devices/system/cpu/$cpu/cpufreq/scaling_governor" >/dev/null
    done
fi
if [[ -n $lgc ]]; then
    sudo -n nvidia-smi -i "$gpu" -lgc "$lgc" >/dev/null
    gpu_gr_locked=1
fi
if [[ -n $lmc ]]; then
    sudo -n nvidia-smi -i "$gpu" -lmc "$lmc" >/dev/null
    gpu_mem_locked=1
fi
echo "== environment in effect:"; show_state
"$@"
