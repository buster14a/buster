#!/bin/bash
# Fixtures for join-tailnet.sh, with no network and no real daemon: stand-ins for tailscale, tailscaled and pgrep on
# PATH, a /proc of the fixture's own (each daemon there a real sleep process, so that the hook's signals land on
# something the test can check), and the default socket, its /var/run link to /run and a state file in a temporary
# directory. Each case says which daemons must survive, which must be stopped, and whether a new one starts.
set -eu
source=$(cd "$(dirname "$0")/../.." && pwd)
hook=$source/.claude/hooks/join-tailnet.sh
temporary=$(mktemp -d)
trap 'for p in $(cat "$temporary"/*/daemons 2> /dev/null); do kill -9 "$p" 2> /dev/null || :; done; rm -rf "$temporary"' 0
trap 'exit 1' HUP INT TERM
unset CLAUDE_CODE_REMOTE BUSTER_TAILNET_DISPOSABLE
bin=$temporary/bin
mkdir -p "$bin"

# pgrep -x tailscaled: the fixture's daemons.
cat > "$bin/pgrep" << 'EOF'
#!/bin/sh
cat "$FIX/daemons" 2> /dev/null || :
EOF
# tailscale [--socket=PATH] COMMAND ...: status fails where the fixture says so, else lists benchpress online; ping
# answers unless the fixture says not; up is recorded.
cat > "$bin/tailscale" << 'EOF'
#!/bin/sh
case $1 in --socket=*) shift ;; esac
case $1 in
status)
    [ ! -e "$FIX/status-fails" ] || exit 1
    echo "100.64.0.2    claude-cloud-test  claude-cloud-test.example.ts.net  linux  -"
    echo "100.64.0.3    benchpress         tagged-devices                    linux  -"
    ;;
ping) [ ! -e "$FIX/ping-fails" ] || exit 1 ;;
up) echo "$*" >> "$FIX/up" ;;
esac
EOF
# tailscaled ...: records its arguments and binds its socket, as the daemon does when it's ready.
cat > "$bin/tailscaled" << 'EOF'
#!/bin/sh
echo "$*" >> "$FIX/started"
for argument in "$@"; do
    case $argument in --socket=*) python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "${argument#--socket=}" ;; esac
done
EOF
chmod +x "$bin/pgrep" "$bin/tailscale" "$bin/tailscaled"

failures=0
number=0

fail()
{
    echo "FAIL: $name: $*" >&2
    failures=$((failures + 1))
}

# A new fixture: process 1's name ($1), the run directory with /var/run linked to it, and a state file.
fixture()
{
    number=$((number + 1))
    FIX=$temporary/$number
    export FIX
    mkdir -p "$FIX/proc/1" "$FIX/root/run/tailscale" "$FIX/root/var/lib/tailscale"
    ln -s ../run "$FIX/root/var/run"
    echo "$1" > "$FIX/proc/1/comm"
    echo persistent > "$FIX/root/var/lib/tailscale/tailscaled.state"
    default=$FIX/root/var/run/tailscale/tailscaled.sock
    # The default socket as the host's daemon left it.
    python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "$default"
    socket_inode=$(stat -c %i "$default")
}

# A daemon with these arguments: a sleep, orphaned so that its parent reaps it when it's stopped, whose command line
# in the fixture's /proc is tailscaled's.
daemon()
{
    local pid
    pid=$( (sleep 600 > /dev/null 2>&1 & echo $!) )
    mkdir -p "$FIX/proc/$pid"
    printf '%s\0' tailscaled "$@" > "$FIX/proc/$pid/cmdline"
    echo "$pid" >> "$FIX/daemons"
    last=$pid
}

alive()
{
    [ -e "/proc/$1" ] && ! grep -q '^[0-9]* (.*) Z' "/proc/$1/stat" 2> /dev/null
}

run_hook()
{
    env PATH="$bin:$PATH" TS_AUTHKEY="${authkey-tskey-test}" BUSTER_TAILNET_PROC="$FIX/proc" BUSTER_TAILNET_SOCKET="$default" \
        BUSTER_TAILNET_LOG="$FIX/root/var/lib/tailscale/log" "$@" bash "$hook" || fail "the hook exited with $?"
    [ "$(cat "$FIX/root/var/lib/tailscale/tailscaled.state")" = persistent ] || fail "the state file changed"
}

survives()
{
    alive "$1" || fail "daemon $1 ($(tr '\0' ' ' < "$FIX/proc/$1/cmdline")) was stopped"
}

stopped()
{
    ! alive "$1" || fail "daemon $1 ($(tr '\0' ' ' < "$FIX/proc/$1/cmdline")) is still running"
}

# Nothing started, and the default socket the one the fixture made.
nothing_started()
{
    [ ! -e "$FIX/started" ] || fail "a daemon was started: $(cat "$FIX/started")"
    [ ! -e "$FIX/up" ] || fail "tailscale up ran: $(cat "$FIX/up")"
    [ -S "$default" ] && [ "$(stat -c %i "$default")" = "$socket_inode" ] || fail "the default socket was removed or replaced"
}

# One daemon started, in memory on the default socket, and logged in with the key.
replaced()
{
    [ "$(cat "$FIX/started" 2> /dev/null | wc -l)" = 1 ] || fail "expected one daemon started, got: $(cat "$FIX/started" 2> /dev/null)"
    grep -q -- "--socket=$default --state=mem:" "$FIX/started" 2> /dev/null || fail "the new daemon isn't in memory on the default socket"
    grep -q -- "--authkey=tskey-test" "$FIX/up" 2> /dev/null || fail "tailscale up didn't run with the key"
    [ -S "$default" ] || fail "no socket"
}

name='a persistent host: a healthy daemon with its state on disk'
fixture systemd
daemon --state=/var/lib/tailscale/tailscaled.state
run_hook
survives "$last"
nothing_started

name='a persistent host: a daemon that answers no status'
fixture systemd
daemon
touch "$FIX/status-fails"
run_hook
survives "$last"
nothing_started

name='a persistent host: a daemon that does not reach benchpress'
fixture init
daemon --state=mem:
touch "$FIX/ping-fails"
run_hook
survives "$last"
nothing_started

name='a cloud session: a healthy daemon with its state on disk'
fixture systemd
daemon --tun=userspace-networking --socks5-server=localhost:1055
run_hook CLAUDE_CODE_REMOTE=true
stopped "$last"
replaced

name='a cloud session: the default socket named through /run'
fixture systemd
daemon "--socket=$FIX/root/run/tailscale/tailscaled.sock" --statedir=/var/lib/tailscale
run_hook CLAUDE_CODE_REMOTE=true
stopped "$last"
replaced

name='a cloud session: the default socket as a separate argument, with one dash'
fixture systemd
daemon -socket "$FIX/root/var/run/tailscale/tailscaled.sock" -state /var/lib/tailscale/tailscaled.state
run_hook CLAUDE_CODE_REMOTE=true
stopped "$last"
replaced

name='a cloud session: a daemon on a socket of its own, and none on the default one'
fixture systemd
daemon "--socket=$FIX/own.sock" --state=/var/lib/tailscale/own.state
touch "$FIX/status-fails"
run_hook CLAUDE_CODE_REMOTE=true
survives "$last"
replaced

name='a cloud session: a healthy daemon in memory'
fixture systemd
daemon --tun=userspace-networking --state=mem:
run_hook CLAUDE_CODE_REMOTE=true
survives "$last"
nothing_started

name='a cloud session: a daemon in memory that benchpress does not answer'
fixture systemd
daemon "--socket=$FIX/root/var/run/tailscale/tailscaled.sock" --state=mem:
touch "$FIX/ping-fails"
run_hook CLAUDE_CODE_REMOTE=true
stopped "$last"
replaced

name='a persistent host whose process 1 is runit: a healthy daemon with its state on disk'
fixture runit
daemon --state=/var/lib/tailscale/tailscaled.state
run_hook
survives "$last"
nothing_started

name='a persistent host whose process 1 is s6-svscan: a daemon that answers no status'
fixture s6-svscan
daemon "--socket=$FIX/root/run/tailscale/tailscaled.sock"
touch "$FIX/status-fails"
run_hook
survives "$last"
nothing_started

name='a container without CLAUDE_CODE_REMOTE or the opt-in: a healthy daemon with its state on disk'
fixture process_api
daemon --statedir=/var/lib/tailscale
run_hook
survives "$last"
nothing_started

name='BUSTER_TAILNET_DISPOSABLE=1 on a host with an init system'
fixture systemd
daemon
run_hook BUSTER_TAILNET_DISPOSABLE=1
stopped "$last"
replaced

name='a cloud session without an auth key'
fixture systemd
daemon
touch "$FIX/status-fails"
authkey= run_hook CLAUDE_CODE_REMOTE=true
survives "$last"
nothing_started

if [ "$failures" -ne 0 ]; then
    echo "test-join-tailnet: $failures checks failed in $number cases" >&2
    exit 1
fi
echo "test-join-tailnet: $number cases pass"
