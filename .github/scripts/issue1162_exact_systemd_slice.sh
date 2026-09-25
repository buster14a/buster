#!/usr/bin/env bash
# Temporary, isolated #1162 validation transport. Never install on a protected host.
set -Eeuo pipefail
subject=5eb501486f9b713c88d0b611cdc060079892d5b2
baseline=ade6ac4b6ecb21f30b61b656439bac476c145e2f
evidence="${RUNNER_TEMP:?}/issue1162-exact-slice-evidence"
source_root="$RUNNER_TEMP/issue1162-source"
payload="$RUNNER_TEMP/issue1162-install"
guest="issue1162-exact-${GITHUB_RUN_ID:?}"
image="issue1162-exact:${GITHUB_RUN_ID}"
mkdir -p "$evidence" "$payload/binaries" "$payload/sources" "$payload/units"
chmod 0700 "$evidence"
exec > >(tee "$evidence/run.log") 2>&1
retain() {
  result=$?
  trap - EXIT
  set +e
  echo "RETAIN original_exit=$result"
  if sudo docker inspect "$guest" >/dev/null 2>&1; then
    sudo docker inspect "$guest" >"$evidence/container.json" 2>&1
    sudo docker logs "$guest" >"$evidence/container.log" 2>&1
    sudo docker exec "$guest" journalctl --no-pager -b -u buster-bench.service -u 'buster-bench-*.service' -u 'buster-bench-systemd-broker@*.service' >"$evidence/journal.log" 2>&1
    sudo docker exec "$guest" systemctl list-units --all 'buster-bench*' >"$evidence/units-final.txt" 2>&1
    sudo docker exec "$guest" sh -c 'find /var/lib/buster-bench -xdev -printf "%m %u:%g %s %p\n" | sort' >"$evidence/state-inventory.txt" 2>&1
    sudo docker exec "$guest" sh -c 'stat -c "lease device=%d inode=%i links=%h mode=%a owner=%u:%g" /var/lib/buster-bench/lease/host.lock' >"$evidence/lease-final.txt" 2>&1
    sudo docker exec "$guest" tar -C /var/lib -czf - buster-bench >"$evidence/retained-state.tgz" 2>"$evidence/state-tar.log"
    tar -tzf "$evidence/retained-state.tgz" >"$evidence/state-tar-inventory.txt" 2>>"$evidence/state-tar.log"
    sha256sum "$evidence/retained-state.tgz" >"$evidence/state-tar-sha256.txt"
    sudo docker rm -f "$guest" >"$evidence/container-removal.txt" 2>&1
    if sudo docker ps -a --format '{{.Names}}' | grep -Fx "$guest"; then result=1; fi
  fi
  echo "FINAL original_exit=$result" | tee "$evidence/final.txt"
  exit "$result"
}
trap retain EXIT
printf 'SUBJECT_SHA=%s BASE_SHA=%s WORKFLOW_SHA=%s RUN=%s\n' "$subject" "$baseline" "$GITHUB_SHA" "$GITHUB_RUN_ID"
git rev-parse "$subject" "$subject^{tree}" "$baseline" "$baseline^{tree}" | tee "$evidence/revisions.txt"
test "$(git rev-parse "$subject^{tree}")" = 8b90d31bc37abfb7b0c2a93bbb719911c1e2a7dc
test "$(git rev-parse "$baseline^{tree}")" = 4c5306221fdb22fccc929b55e333163742de17d0
git worktree add --detach "$source_root" "$subject"
test -z "$(git -C "$source_root" status --porcelain=v1)"
git -C "$source_root" ls-tree -r -z --full-tree "$subject" >"$evidence/source-git-inventory.nul"
git -C "$source_root" show --no-patch --format=raw "$subject" >"$evidence/source-commit.txt"
for rev in "$baseline" "$subject"; do
  mkdir -p "$payload/sources/$rev"
  git archive "$rev" -- src cmake CMakeLists.txt | tar -x -C "$payload/sources/$rev"
  REV="$rev" ROOT="$payload/sources/$rev" python3 - <<'PY'
import hashlib, os
from pathlib import Path
root = Path(os.environ["ROOT"])
rev = os.environ["REV"]
files = sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix().encode())
assert all(not p.is_symlink() for p in root.rglob("*"))
lines = ["BQ-SOURCE-V1", "repository=buster14a/buster", "revision=" + rev]
for path in files:
    lines.append(hashlib.sha256(path.read_bytes()).hexdigest() + " " + path.relative_to(root).as_posix())
manifest = ("\n".join(lines) + "\n").encode()
expected = {
    "ade6ac4b6ecb21f30b61b656439bac476c145e2f": (375, 42585, "ebf4a4b4e5943dc60dd9fd9d175af643fbe0d0eee72e70e245c688aaabcb3007"),
    "5eb501486f9b713c88d0b611cdc060079892d5b2": (372, 42199, "35031a4f9b8b05d9f84abc39043ace9fc7581ebbcfc3a5ac7d17e1fd48d3a9d1"),
}[rev]
actual = (len(files), len(manifest), hashlib.sha256(manifest).hexdigest())
print("SOURCE_CLOSURE", rev, "files", actual[0], "bytes", actual[1], "sha256", actual[2], flush=True)
assert actual == expected, (actual, expected)
(root / "source.manifest").write_bytes(manifest)
PY
done
cd "$source_root"
mkdir -p build
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -fwrapv -fno-strict-aliasing -funsigned-char -g build.c -o build/buster-bench-build
readelf -W -l build/buster-bench-build | tee "$evidence/build-driver-elf.txt"
grep -E 'GNU_STACK.*RW[[:space:]]' "$evidence/build-driver-elf.txt"
build/buster-bench-build bench_service capabilities
build/buster-bench-build bench_service_broker
clang -Isrc -DBUSTER_SINGLE_THREADED=1 -std=c11 -O2 -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char tools/throughput/throughput.c tools/throughput/shared.c -lm -o build/throughput
install -m 0755 build/buster-bench-build "$payload/binaries/buster-bench-build"
install -m 0755 build/bench-service-tools/service "$payload/binaries/buster-bench-service"
install -m 0755 build/bench-service-tools/systemd-broker "$payload/binaries/buster-bench-systemd-broker"
install -m 0755 build/bench-service-tools/systemd-broker-live-test "$payload/binaries/systemd-broker-live-test"
install -m 0755 build/throughput "$payload/binaries/buster-bench-throughput"
cp tools/bench_service/deploy/{buster-bench.service,buster-bench.slice,buster-bench-systemd-broker.socket,buster-bench-systemd-broker@.service,buster-bench.tmpfiles.conf} "$payload/units/"
mkdir -p "$payload/recipes"
cp tools/bench_service/profiles/validate-buster-v1.recipe "$payload/recipes/"
sha256sum "$payload"/binaries/* "$payload"/units/* "$payload"/recipes/* | tee "$evidence/installed-sha256.txt"
clang --version | head -1 | tee "$evidence/toolchains.txt"
cmake --version | head -1 | tee -a "$evidence/toolchains.txt"
ninja --version | tee -a "$evidence/toolchains.txt"
cat > "$payload/Dockerfile" <<'DOCKERFILE'
FROM ubuntu@sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends systemd systemd-sysv dbus util-linux python3-minimal clang cmake ninja-build binutils build-essential git ca-certificates && rm -rf /var/lib/apt/lists/*
STOPSIGNAL SIGRTMIN+3
CMD ["/sbin/init"]
DOCKERFILE
cat > "$payload/provision.sh" <<'GUEST'
#!/bin/sh
set -eu
groupadd -g 65000 buster-bench
groupadd -g 65001 buster-bench-candidate
groupadd -g 65002 buster-github-runner
useradd -u 65000 -g 65000 -G 65001 -M -s /usr/sbin/nologin buster-bench
useradd -u 65001 -g 65001 -M -s /usr/sbin/nologin buster-bench-candidate
useradd -u 65002 -g 65002 -M -s /usr/sbin/nologin buster-github-runner
install -d -m 0755 /usr/local/libexec /etc/systemd/system /etc/buster-bench
cp /root/issue1162-install/binaries/* /usr/local/libexec/
chown root:root /usr/local/libexec/buster-bench-* /usr/local/libexec/systemd-broker-live-test
chmod 0755 /usr/local/libexec/buster-bench-* /usr/local/libexec/systemd-broker-live-test
cp /root/issue1162-install/units/*.service /root/issue1162-install/units/*.socket /root/issue1162-install/units/*.slice /etc/systemd/system/
chown root:root /etc/systemd/system/buster-bench*
chmod 0644 /etc/systemd/system/buster-bench*
cp /root/issue1162-install/units/buster-bench.tmpfiles.conf /etc/tmpfiles.d/issue1162-buster-bench.conf
systemd-tmpfiles --create /etc/tmpfiles.d/issue1162-buster-bench.conf
install -d -m 0710 -o buster-bench -g buster-bench /var/lib/buster-bench/workspaces/results
# GNU install and chmod preserve the parent's setgid bit on this filesystem.
# The installed results root must have exactly 0710 for the production worker.
chmod 0710 /var/lib/buster-bench/workspaces/results
chmod g-s /var/lib/buster-bench/workspaces/results
test "$(stat -c %a /var/lib/buster-bench/workspaces/results)" = 710
test "$(stat -c %u:%g /var/lib/buster-bench/workspaces/results)" = 65000:65000
install -d -m 0750 -o root -g buster-bench /opt/buster-bench
install -d -m 0550 -o root -g buster-bench /opt/buster-bench/installed
cp -a /root/issue1162-install/sources /opt/buster-bench/installed/
cp -a /root/issue1162-install/recipes /opt/buster-bench/installed/
chown -R root:buster-bench /opt/buster-bench/installed
find /opt/buster-bench/installed -type d -exec chmod 0550 {} +
find /opt/buster-bench/installed -type f -exec chmod 0440 {} +
chown buster-bench:buster-bench /var/lib/buster-bench/lease
touch /var/lib/buster-bench/lease/host.lock
chown buster-bench:buster-bench /var/lib/buster-bench/lease/host.lock
chmod 0640 /var/lib/buster-bench/lease/host.lock
printf 'device=%s\ninode=%s\n' \
  "$(stat -c %d /var/lib/buster-bench/lease/host.lock)" \
  "$(stat -c %i /var/lib/buster-bench/lease/host.lock)" \
  > /etc/buster-bench/systemd-broker-lease.identity
chown root:root /etc/buster-bench/systemd-broker-lease.identity
chmod 0444 /etc/buster-bench/systemd-broker-lease.identity
printf 'GUEST_IDENTITIES\n'; id buster-bench; id buster-bench-candidate; id buster-github-runner
stat -c 'LEASE_DEVICE=%d LEASE_INODE=%i MODE=%a OWNER=%u:%g LINKS=%h' /var/lib/buster-bench/lease/host.lock
stat -c '%a %u:%g %n' /var/lib/buster-bench /var/lib/buster-bench/queue /var/lib/buster-bench/workspaces /var/lib/buster-bench/lease /var/lib/buster-bench/workspaces/results
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/buster-bench.service /etc/systemd/system/buster-bench-systemd-broker.socket /etc/systemd/system/buster-bench-systemd-broker@.service /etc/systemd/system/buster-bench.slice
GUEST
sudo docker build --pull --no-cache -t "$image" "$payload"
sudo docker run -d --name "$guest" --privileged --cgroup-parent=docker.slice --cgroupns=private --network none --env container=docker --tmpfs /tmp --tmpfs /run --tmpfs /run/lock "$image"
for attempt in $(seq 1 40); do
  if sudo docker exec "$guest" systemctl show --property=Version --property=SystemState >"$evidence/manager.txt" 2>&1; then break; fi
  sleep 1
done
cat "$evidence/manager.txt"
sudo docker exec "$guest" sh -ec 'test "$(cat /proc/1/comm)" = systemd; test "$(stat -fc %T /sys/fs/cgroup)" = cgroup2fs; test -w /sys/fs/cgroup/cgroup.subtree_control; grep -qw cpu /sys/fs/cgroup/cgroup.controllers; grep -qw cpuset /sys/fs/cgroup/cgroup.controllers; grep -qw memory /sys/fs/cgroup/cgroup.controllers; grep -qw pids /sys/fs/cgroup/cgroup.controllers'
sudo docker cp "$payload" "$guest:/root/issue1162-install"
sudo docker exec "$guest" sh /root/issue1162-install/provision.sh | tee "$evidence/provision.txt"
sudo docker exec "$guest" systemctl start dbus.socket
sudo docker exec "$guest" systemctl start buster-bench-systemd-broker.socket
sudo docker exec "$guest" systemctl start buster-bench.service
sudo docker exec "$guest" systemctl show buster-bench.service -p ActiveState -p MainPID -p ControlGroup -p RestrictSUIDSGID -p NoNewPrivileges -p CapabilityBoundingSet | tee "$evidence/service-effective.txt"
sudo docker exec "$guest" systemctl is-active --quiet buster-bench.service
key="issue1162-${GITHUB_RUN_ID}"
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway capabilities | tee "$evidence/gateway-capabilities.txt"
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway submit "$key" "$baseline" "$subject" | tee "$evidence/submit.txt"
job="$(sed -nE 's/^job=([0-9]+) .*/\1/p' "$evidence/submit.txt" | head -1)"
test -n "$job"
echo "JOB=$job"
finished=false
for attempt in $(seq 1 360); do
  sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway status "$job" >"$evidence/status-latest.txt"
  cat "$evidence/status-latest.txt"
  sudo docker exec "$guest" systemctl list-units --all --plain 'buster-bench*' >>"$evidence/units-observed.txt" 2>&1
  if grep -q 'phase=finished' "$evidence/status-latest.txt"; then finished=true; break; fi
  failure="$(sed -nE 's/^job=.* failure=([^ ]+).*/\1/p' "$evidence/status-latest.txt" | head -1)"
  if grep -q 'phase=preparing' "$evidence/status-latest.txt" && [[ -n "$failure" && "$failure" != ok ]]; then
    echo 'EARLY_FAILURE: preparing job has a recorded non-ok failure; retain the guest evidence.'
    exit 1
  fi
  sleep 5
done
test "$finished" = true
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway result "$job" | tee "$evidence/result.txt"
grep -q 'outcome=succeeded' "$evidence/result.txt"
grep -q 'result-bound=1' "$evidence/result.txt"
token="$(sed -nE 's/^job=[0-9]+ token=([0-9]+) .*/\1/p' "$evidence/result.txt" | head -1)"
digest="$(sed -nE 's/^full-result-sha256=([a-f0-9]{64})$/\1/p' "$evidence/result.txt" | head -1)"
test -n "$token" && test -n "$digest"
sudo docker exec "$guest" runuser -u buster-bench -- /usr/local/libexec/buster-bench-service gateway export "$job" "$token" "$digest" >"$evidence/result.bqexport" 2>"$evidence/export-stderr.txt"
cat "$evidence/export-stderr.txt"
receipt="$(sed -nE 's/^export-receipt-sha256=([a-f0-9]{64})$/\1/p' "$evidence/export-stderr.txt" | head -1)"
test -n "$receipt"
sha256sum "$evidence/result.bqexport" | tee "$evidence/archive-sha256.txt"
mkdir -m 0700 "$evidence/replay"
build/bench-service-tools/service unpack-export "$evidence/result.bqexport" "$evidence/replay/result" "$receipt" | tee "$evidence/replay.txt"
sudo docker exec "$guest" sh -ec 'test ! -e /var/lib/buster-bench/workspaces/.lease-handoff; test "$(stat -c %h /var/lib/buster-bench/lease/host.lock)" = 1'
echo "FULL_SLICE_EXECUTION_PASS source=$subject job=$job token=$token; no protected host involved"
