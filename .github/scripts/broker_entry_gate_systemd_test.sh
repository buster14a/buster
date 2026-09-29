#!/usr/bin/env bash
# Source-bound, disposable synthetic component proof on a GitHub-hosted Linux
# guest only. This never starts the real broker or reaches a physical runner.
set -euo pipefail
proof="$RUNNER_TEMP/broker-entry-gate-systemd-proof"
payload="$RUNNER_TEMP/broker-entry-gate-systemd-image"
guest="bqeg-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
image="bqeg:${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
mkdir -p "$proof" "$payload" "$proof/binaries"
cleanup() {
  code=$?
  trap - EXIT
  set +e
  sudo timeout 20 docker inspect "$guest" > "$proof/container-inspect.json" 2>&1
  inspect_status=$?
  sudo timeout 20 docker logs "$guest" > "$proof/container-console.log" 2>&1
  console_status=$?
  sudo timeout 30 docker exec "$guest" /usr/bin/tar -C /run -cf - buster-bench-entry-test \
    | head -c 33554433 > "$proof/guest-proof.tar"
  export_statuses=("${PIPESTATUS[@]}")
  copy_status=0
  if [[ ${export_statuses[0]} != 0 || ${export_statuses[1]} != 0 || $(wc -c < "$proof/guest-proof.tar") -gt 33554432 ]]; then
    copy_status=1
  fi
  sudo timeout 30 docker rm --force "$guest" > "$proof/container-removal.log" 2>&1
  removal=$?
  sudo chown -R "$(id -u):$(id -g)" "$proof"
  if [[ $removal != 0 || $copy_status != 0 || $inspect_status != 0 || $console_status != 0 ]]; then code=1; fi
  printf 'test_status=%s\ncontainer_removal_status=%s\nproof_copy_status=%s\ncontainer_inspect_status=%s\nconsole_status=%s\n' \
    "$code" "$removal" "$copy_status" "$inspect_status" "$console_status" > "$proof/outcome.txt"
  exit "$code"
}
trap cleanup EXIT
{
  date -u '+%Y-%m-%dT%H:%M:%SZ'
  git rev-parse HEAD 'HEAD^{tree}'
  git status --porcelain
  clang --version
  uname -a
  sha256sum tools/bench_service/broker_entry_gate.c tools/bench_service/broker_entry_gate_systemd_test.c src/buster/lib/hash.c
} > "$proof/source-and-toolchain.txt"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char)
clang "${flags[@]}" -Isrc -static -Wl,-z,noexecstack \
  tools/bench_service/broker_entry_gate.c src/buster/lib/hash.c -o "$payload/buster-bench-broker-entry-gate"
clang "${flags[@]}" -Isrc -Wl,-z,noexecstack \
  tools/bench_service/broker_entry_gate_systemd_test.c src/buster/lib/hash.c \
  -o "$payload/broker-entry-gate-systemd-test"
readelf -W -l "$payload/buster-bench-broker-entry-gate" > "$proof/gate-elf.txt"
readelf -W -l "$payload/broker-entry-gate-systemd-test" > "$proof/fixture-elf.txt"
if grep -Eq 'INTERP|DYNAMIC|GNU_STACK.*RWE' "$proof/gate-elf.txt"; then exit 1; fi
if grep -Eq 'GNU_STACK.*RWE' "$proof/fixture-elf.txt"; then exit 1; fi
cp "$payload/buster-bench-broker-entry-gate" "$payload/broker-entry-gate-systemd-test" "$proof/binaries/"
(cd "$proof/binaries" && sha256sum buster-bench-broker-entry-gate broker-entry-gate-systemd-test) > "$proof/binary-sha256.txt"
printf 'buster14a/buster disposable broker-entry systemd component test\n' > "$payload/opt-in"
cat > "$payload/buster-bench-systemd-broker.socket" <<'UNIT'
[Unit]
Description=Disposable synthetic broker entry socket
[Socket]
ListenSequentialPacket=/run/buster-bench-systemd-broker/control.sock
Accept=yes
SocketUser=buster-bench
SocketGroup=buster-bench
SocketMode=0600
DirectoryMode=0710
RemoveOnStop=yes
MaxConnections=2
UNIT
cat > "$payload/Dockerfile" <<'DOCKERFILE'
FROM ubuntu@sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends systemd systemd-sysv dbus passwd && rm -rf /var/lib/apt/lists/*
RUN groupadd -g 65000 buster-bench && groupadd -g 65001 buster-bench-candidate && groupadd -g 65002 buster-github-runner && groupadd -g 65003 bqeg-extra && useradd -u 65000 -g buster-bench -G buster-bench-candidate -M -s /usr/sbin/nologin buster-bench && useradd -u 65001 -g buster-bench-candidate -M -s /usr/sbin/nologin buster-bench-candidate && useradd -u 65002 -g buster-github-runner -M -s /usr/sbin/nologin buster-github-runner
RUN cp /etc/nsswitch.conf /root/nsswitch-stock && sed -i -E 's/^(passwd|group):.*/\1: files/; s/^initgroups:.*/initgroups: files/' /etc/nsswitch.conf
RUN install -d -o root -g root -m 0755 /etc/buster-bench /opt/buster-bench/installed /var/lib/buster-bench /var/lib/buster-bench/sub
COPY opt-in /root/broker-entry-gate-test.opt-in
COPY buster-bench-broker-entry-gate /usr/local/libexec/buster-bench-broker-entry-gate
COPY broker-entry-gate-systemd-test /usr/local/libexec/buster-bench-systemd-broker
COPY broker-entry-gate-systemd-test /usr/local/libexec/broker-entry-gate-systemd-test
COPY broker-entry-gate-systemd-test /root/broker-entry-gate-systemd-test
COPY buster-bench-systemd-broker.socket /etc/systemd/system/buster-bench-systemd-broker.socket
RUN chmod 0755 /usr/local/libexec/buster-bench-broker-entry-gate /usr/local/libexec/buster-bench-systemd-broker /usr/local/libexec/broker-entry-gate-systemd-test && chmod 0700 /root/broker-entry-gate-systemd-test
STOPSIGNAL SIGRTMIN+3
CMD ["/sbin/init"]
DOCKERFILE
sudo timeout 300 docker build --pull --no-cache -t "$image" "$payload" 2>&1 | tee "$proof/image-build.log"
sudo docker image inspect "$image" > "$proof/image-inspect.json"
sudo docker run -d --name "$guest" --privileged --security-opt seccomp=unconfined \
  --cgroup-parent=docker.slice --cgroupns=private \
  --network none --env container=docker --tmpfs /tmp --tmpfs /run --tmpfs /run/lock "$image"
ready=false
for attempt in {1..30}; do
  if sudo timeout 5 docker exec "$guest" systemctl show --property=Version --value > "$proof/systemd-version.txt"; then ready=true; break; fi
  sleep 1
done
[[ $ready == true ]]
sudo docker exec "$guest" /usr/bin/grep -H '^Seccomp' /proc/1/status /proc/self/status \
  > "$proof/guest-baseline-seccomp.txt"
# PID1 can mount its own private /run; all setup and proof collection stay in
# that isolated guest namespace. Its installed broker is this harmless fixture.
sudo docker exec "$guest" /usr/bin/install -o root -g root -m 0644 \
  /root/broker-entry-gate-test.opt-in /run/buster-bench-entry-test.opt-in
sudo docker exec "$guest" /usr/bin/install -d -o buster-bench -g buster-bench -m 0710 \
  /run/buster-bench-systemd-broker
sudo docker exec "$guest" /usr/bin/stat -Lc '%n uid=%u gid=%g mode=%a links=%h dev=%d inode=%i' \
  /proc/1/exe /usr/lib/systemd/systemd /.dockerenv /run/buster-bench-entry-test.opt-in \
  /usr/local/libexec/buster-bench-broker-entry-gate /usr/local/libexec/buster-bench-systemd-broker \
  > "$proof/isolation-and-installation.txt"
sudo docker exec "$guest" /usr/bin/cat /etc/nsswitch.conf /etc/passwd /etc/group > "$proof/account-inventory.txt"
sudo docker exec "$guest" /usr/bin/cat /root/nsswitch-stock > "$proof/nsswitch-stock.txt"
sudo docker exec "$guest" /usr/bin/sha256sum /root/nsswitch-stock /etc/nsswitch.conf /etc/passwd /etc/group \
  > "$proof/account-file-sha256.txt"
sudo docker exec "$guest" /usr/bin/test ! -e /run/nscd/socket
sudo docker exec "$guest" /usr/bin/test "$(sudo docker exec "$guest" /usr/bin/readlink -f /var/run)" = /run
sudo timeout 900 docker exec --env BUSTER_BROKER_ENTRY_DISPOSABLE_SYSTEMD=isolated-docker-systemd-test \
  "$guest" /root/broker-entry-gate-systemd-test --run 2>&1 | tee "$proof/fixture.log"
