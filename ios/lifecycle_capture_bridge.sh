#!/usr/bin/env bash
# Caller clocks own only bootstrap/collector groups; initialized owners detach.
set -u
bridge_started=$SECONDS

read_frame() {
    frame=
    IFS= read -r -n 256 frame <&"$1" && [[ ${#frame} -lt 256 ]]
}

if [[ ${1:-} == --bootstrap ]]; then
    shift
    ipc=$1 generation=$2
    exec 3<>"$ipc/control" 4<>"$ipc/ready" 8>"$ipc/setup" 9<>"$ipc/authorize" || exit 125
    printf 'SETUP %s\n' "$generation" >&8 || exit 125
    read_frame 4 || exit 125
    read -r tag received pid pgid sid extra <<<"$frame"
    [[ $tag == READY && $received == "$generation" && -z ${extra:-} \
        && $pid =~ ^[1-9][0-9]*$ && $pid == "$pgid" && $pid == "$sid" ]] || exit 125
    printf 'READY-OBSERVED %s\n' "$generation" >&8 || exit 125
    read_frame 9 && [[ $frame == "GO-AUTHORIZE $generation" ]] || exit 125
    printf 'GO %s\n' "$generation" >&3 || exit 125
    read_frame 4 && [[ $frame == "DONE $generation" ]] || exit 125
    exit 0
fi

timeout_bin=$1 prefix=$2 command_seconds=$3 capture_seconds=$4
shift 4
[[ ${1:-} == -- ]] && shift
bridge_path=${BASH_SOURCE[0]}
case $bridge_path in
    */*) helper_path="${bridge_path%/*}/lifecycle_capture.py" ;;
    *) helper_path=./lifecycle_capture.py ;;
esac
run=$(mktemp -d "${prefix}.capture.XXXXXXXX") || exit 125
observe_stage() {
    # Private, fixed-count wall observations never authorize an action or alter
    # either timer. Failed observation writes do not alter protocol decisions.
    printf 'BUSTER_IOS_BRIDGE_STAGE ordinal=%s stage=%s elapsed_seconds=%s\n' \
        "$1" "$2" "$((SECONDS - bridge_started))" >>"$run/bridge-stages.log" || :
}
observe_stage 1 private-created
generation=${run##*.}
private_prefix="$run/${prefix##*/}"
ipc="$run/ipc"
mkdir "$ipc" || exit 125
mkfifo "$ipc/control" "$ipc/ready" "$ipc/completion" "$ipc/lifetime" \
    "$ipc/adjudication" "$ipc/setup" "$ipc/authorize" || exit 125
observe_stage 2 ipc-created

reported=unavailable invocation=unavailable admission=0 reason=starting
record_caller() {
    for target in "${prefix}.caller-status.log" "${prefix}.caller-fields.log"; do
        [[ ! -L $target && ( ! -e $target || -f $target ) ]] || return 1
    done
    monitor=pending
    if [[ -f $run/command-monitor-status ]]; then
        IFS= read -r monitor <"$run/command-monitor-status" || monitor=invalid
        [[ $monitor =~ ^(0|[1-9][0-9]{0,2})$ && $monitor -le 255 ]] || monitor=invalid
    fi
    printf 'BUSTER_IOS_CALLER version=1 generation=%s admission=%s helper_status=%s invocation_status=%s command_monitor_status=%s reason=%s\n' \
        "$generation" "$admission" "$reported" "$invocation" "$monitor" "$reason" >"${prefix}.caller-status.log" \
        || return 1
    printf '%s %s %s %s %s %s\n' "$admission" "$reported" "$invocation" "$generation" "$monitor" "$reason" \
        >"${prefix}.caller-fields.log"
}
refuse() {
    reason=$1
    record_caller || true
    exit 1
}
cancel() {
    trap - INT TERM
    # One small message fits an empty private pipe; no saved owner ID is used.
    printf 'CANCEL %s %s\n' "$generation" "$1" >&6 || true
    reason="cancelled-$1"
    record_caller || true
    exit "$((128 + $1))"
}

# These RDWR descriptors are setup dummies. READY proves the child's final
# opens completed before C retires every duplicate writer and authorizes GO.
exec 3<>"$ipc/control" 4<>"$ipc/ready" 5<>"$ipc/completion" 6<>"$ipc/lifetime" \
    7<>"$ipc/adjudication" 8<>"$ipc/setup" 9<>"$ipc/authorize" 14<"$ipc/completion" || exit 125
trap 'cancel 2' INT
trap 'cancel 15' TERM
printf '%s\n' "$run" >"${prefix}.caller-private-directory.log" || exit 125
record_caller || exit 125
observe_stage 3 caller-initial-recorded

observe_stage 4 monitor-launch
(
    exec 3>&- 4>&- 5>&- 6>&- 8>&- 9>&- 14>&- 7>"$ipc/adjudication" || exit 125
    if "$timeout_bin" --signal=KILL "${command_seconds}s" bash "$bridge_path" \
        --bootstrap "$ipc" "$generation" 7>&-; then monitor_status=0; else monitor_status=$?; fi
    printf '%s\n' "$monitor_status" >"$run/command-monitor-status"
    printf 'COMMAND-MONITOR %s %s\n' "$generation" "$monitor_status" >&7
) &

read_frame 8 && [[ $frame == "SETUP $generation" ]] || refuse bootstrap-setup
observe_stage 5 setup-observed
observe_stage 6 invocation-launch
python3 -S "$helper_path" --prefix "$private_prefix" --command-seconds "$command_seconds" \
    --capture-seconds "$capture_seconds" --bridge-generation "$generation" \
    --bridge-control-fd 3 --bridge-ready-fd 4 --bridge-completion-fd 5 \
    --bridge-lifetime-fd 6 --bridge-adjudication-fd 7 -- "$@" \
    3<"$ipc/control" 4>"$ipc/ready" 5>"$ipc/completion" 6<"$ipc/lifetime" \
    7<"$ipc/adjudication" 8>&- 9>&- 14>&- &
invocation_pid=$!
observe_stage 7 invocation-registered
read_frame 8 && [[ $frame == "READY-OBSERVED $generation" ]] || refuse owner-ready
observe_stage 8 ready-observed
exec 3>&- 4>&- 5>&- 7>&- 8>&-
printf 'GO-AUTHORIZE %s\n' "$generation" >&9 || refuse go-authorize
observe_stage 9 go-authorized
exec 9>&-

read_frame 14 || refuse completion-frame
read -r tag received reported extra <<<"$frame"
[[ $tag == COMPLETE && $received == "$generation" && -z ${extra:-} \
    && $reported =~ ^(0|[1-9][0-9]{0,2})$ && $reported -le 255 ]] || refuse completion-frame
observe_stage 10 complete-observed
extra=
if IFS= read -r -n 1 extra <&14 || [[ -n $extra ]]; then refuse completion-trailing-data; fi
observe_stage 11 completion-eof
exec 14<&-
# I is C's still-owned registered direct child. EOF is not its exit status.
if wait "$invocation_pid"; then invocation=0; else invocation=$?; fi
[[ $invocation -eq $reported ]] || refuse invocation-status
observe_stage 12 invocation-waited

# O and its invoking shim have ended. Only this generation's closed snapshot
# can write stable names; old residual owners keep their private prefixes.
observe_stage 13 snapshot-start
snapshot_parent=${prefix%/*}
if [[ $snapshot_parent == "$prefix" ]]; then snapshot_parent=.
elif [[ -z $snapshot_parent ]]; then snapshot_parent=/; fi
snapshot_sources=()
for suffix in .log .native-status.log .command-elapsed.log .capture-elapsed.log \
    .log.capture-status.log .supervisor-fields.log .supervisor-status.log; do
    source="${private_prefix}${suffix}"
    [[ -f $source && ! -L $source ]] || continue
    target="${prefix}${suffix}"
    [[ ! -L $target && ( ! -e $target || -f $target ) ]] || refuse snapshot
    snapshot_sources+=("$source")
done
# One batch preserves binary bytes without a copy/rename process pair for each
# receipt. Partial copies are never admitted: the final caller receipt follows
# only a successful batch, still inside the caller's unchanged capture clock.
if [[ ${snapshot_sources[0]:-} ]]; then
    cp -- "${snapshot_sources[@]}" "$snapshot_parent" || refuse snapshot
fi
observe_stage 14 snapshot-complete
admission=1 reason=complete
record_caller || exit 125
observe_stage 15 caller-final-recorded
exit 0
