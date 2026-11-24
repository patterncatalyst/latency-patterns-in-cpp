#!/bin/sh
# profile.sh - record a GENERIC machine/VM profile label for a measurement run.
#
# Every number in this repo ships labelled with the profile that produced it
# (see CLAUDE.md, "Measurement discipline"). This script prints that label.
# It never prints a CPU model, a hostname, an account name, a kernel build
# number, or any other host-identifying value (see CLAUDE.md, "Public-repo
# host-id scrub rule") - only the generic shape of the host: a core/thread
# class, the governor, pinning, isolation, and bare-metal vs VM.
#
# Usage: scripts/profile.sh [label]
#   label   optional free-text tag for this run (e.g. "ch01-baseline").
#           Do not put a hostname or account name in it; nothing here checks
#           that for you.
#
# Output: one KEY=value pair per line, suitable for `. `-sourcing or for
# `grep` by a results-writer script.

set -eu

run_label="${1:-unlabeled}"

# --- logical CPU count ------------------------------------------------
logical="unknown"
if command -v getconf >/dev/null 2>&1; then
  logical=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo unknown)
fi
if [ "$logical" = "unknown" ] && command -v nproc >/dev/null 2>&1; then
  logical=$(nproc 2>/dev/null || echo unknown)
fi

# --- physical core count (Linux /proc/cpuinfo only) --------------------
physical="unknown"
if [ -r /proc/cpuinfo ]; then
  physical=$(awk '
    /^physical id/ { pid = $NF }
    /^core id/     { print pid "-" $NF }
  ' /proc/cpuinfo | sort -u | wc -l | tr -d ' ')
  [ "$physical" -eq 0 ] 2>/dev/null && physical="unknown"
fi

# --- chassis class: generic laptop/desktop/server, never the model -----
chassis="unknown"
if [ -r /sys/class/dmi/id/chassis_type ]; then
  chassis_type=$(cat /sys/class/dmi/id/chassis_type 2>/dev/null || echo "")
  case "$chassis_type" in
    8 | 9 | 10 | 14) chassis="laptop" ;;
    3 | 4 | 6 | 7) chassis="desktop" ;;
    17 | 23 | 28) chassis="server" ;;
    *) chassis="unknown" ;;
  esac
fi

# --- CPU governor --------------------------------------------------------
governor="unknown"
if [ -r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
  governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)
fi

# --- isolation: isolcpus= kernel param, read-only check -----------------
isolation="none"
if [ -r /sys/devices/system/cpu/isolated ]; then
  isolated_list=$(cat /sys/devices/system/cpu/isolated 2>/dev/null || echo "")
  [ -n "$isolated_list" ] && isolation="isolcpus=$isolated_list"
fi

# --- pinning: is this process's own CPU affinity narrower than all
# logical CPUs? Parsed from /proc/self/status so no extra tool is needed.
pinning="none"
if [ -r /proc/self/status ] && [ "$logical" != "unknown" ]; then
  allowed_list=$(awk -F: '/^Cpus_allowed_list:/ { gsub(/\t/, "", $2); print $2 }' /proc/self/status)
  if [ -n "$allowed_list" ]; then
    allowed_count=0
    old_ifs=$IFS
    IFS=,
    for tok in $allowed_list; do
      case "$tok" in
        *-*)
          range_start=${tok%-*}
          range_end=${tok#*-}
          allowed_count=$((allowed_count + range_end - range_start + 1))
          ;;
        *)
          allowed_count=$((allowed_count + 1))
          ;;
      esac
    done
    IFS=$old_ifs
    if [ "$allowed_count" -lt "$logical" ] 2>/dev/null; then
      pinning="pinned(${allowed_count}/${logical})"
    fi
  fi
fi

# --- bare metal vs VM ------------------------------------------------------
virt="bare-metal"
if command -v systemd-detect-virt >/dev/null 2>&1; then
  detected_virt=$(systemd-detect-virt 2>/dev/null) || detected_virt="none"
  [ "$detected_virt" != "none" ] && virt="vm:$detected_virt"
elif [ -r /proc/cpuinfo ] && grep -q '^flags.*hypervisor' /proc/cpuinfo 2>/dev/null; then
  virt="vm:unknown"
fi

# --- kernel major.minor only (never the full build string) ---------------
kernel="unknown"
if command -v uname >/dev/null 2>&1; then
  kernel=$(uname -r | cut -d. -f1,2)
fi

cpu_class="${physical}-core/${logical}-thread"
if [ "$chassis" != "unknown" ]; then
  cpu_class="${cpu_class} ${chassis} CPU"
else
  cpu_class="${cpu_class} CPU"
fi

printf 'profile_label=%s\n' "$run_label"
printf 'cpu_class=%s\n' "$cpu_class"
printf 'governor=%s\n' "$governor"
printf 'pinning=%s\n' "$pinning"
printf 'isolation=%s\n' "$isolation"
printf 'virt=%s\n' "$virt"
printf 'kernel=%s\n' "$kernel"
