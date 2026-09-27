#!/usr/bin/env bash
# Hosted disposable component test only. No physical runner, service dispatch,
# candidate workload, or performance result is involved.
set -euo pipefail
proof="$RUNNER_TEMP/credential-gate-systemd-proof"
payload="$RUNNER_TEMP/credential-gate-systemd-image"
guest="bqcg-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
image="bqcg:${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
mkdir -p "$proof" "$payload"
cleanup() {
  code=$?
  trap - EXIT
  set +e
  sudo timeout 20 docker inspect "$guest" > "$proof/container-inspect.json" 2>&1
  inspect_status=$?
  sudo timeout 20 docker logs "$guest" > "$proof/container-console.log" 2>&1
  console_status=$?
  sudo timeout 30 docker cp "$guest:/run/buster-bench-credential-gate-test" "$proof/guest-proof"
  copy_status=$?
  sudo timeout 30 docker rm --force "$guest" > "$proof/container-removal.log" 2>&1
  removal=$?
  sudo chown -R "$(id -u):$(id -g)" "$proof"
  if [[ $removal != 0 || $copy_status != 0 || $inspect_status != 0 || $console_status != 0 ]]; then code=1; fi
  printf 'test_status=%s\ncontainer_removal_status=%s\nproof_copy_status=%s\ncontainer_inspect_status=%s\nconsole_status=%s\n' "$code" "$removal" "$copy_status" "$inspect_status" "$console_status" > "$proof/outcome.txt"
  exit "$code"
}
trap cleanup EXIT
{
  date -u '+%Y-%m-%dT%H:%M:%SZ'
  git rev-parse HEAD HEAD^{tree}
  git status --porcelain
  clang --version
  uname -a
} > "$proof/source-and-toolchain.txt"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -fwrapv -fno-strict-aliasing -funsigned-char)
clang "${flags[@]}" -static -no-pie -Wl,-z,noexecstack tools/bench_service/credential_gate.c -o "$payload/buster-bench-credential-gate"
clang "${flags[@]}" tools/bench_service/credential_gate_systemd_test.c -o "$payload/credential-gate-systemd-test"
readelf -W -l "$payload/buster-bench-credential-gate" > "$proof/gate-elf.txt"
if grep -Eq 'INTERP|DYNAMIC|GNU_STACK.*RWE' "$proof/gate-elf.txt"; then exit 1; fi
sha256sum "$payload/buster-bench-credential-gate" "$payload/credential-gate-systemd-test" > "$proof/binary-sha256.txt"
cat > "$payload/Dockerfile" <<'DOCKERFILE'
FROM ubuntu@sha256:496754492fb28b4d3049432f2ca787449331e23fb14f0dd3fffea86bf5a93eb4
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends systemd systemd-sysv dbus passwd && rm -rf /var/lib/apt/lists/*
COPY buster-bench-credential-gate /usr/local/libexec/buster-bench-credential-gate
COPY credential-gate-systemd-test /root/credential-gate-systemd-test
COPY credential-gate-systemd-test /usr/local/libexec/buster-bench-build
COPY credential-gate-systemd-test /usr/local/libexec/buster-bench-service
RUN chmod 0755 /usr/local/libexec/buster-bench-* && chmod 0700 /root/credential-gate-systemd-test
STOPSIGNAL SIGRTMIN+3
CMD ["/sbin/init"]
DOCKERFILE
sudo timeout 300 docker build --pull --no-cache -t "$image" "$payload" 2>&1 | tee "$proof/image-build.log"
sudo docker image inspect "$image" > "$proof/image-inspect.json"
sudo docker run -d --name "$guest" --privileged --cgroup-parent=docker.slice --cgroupns=private --network none --env container=docker --tmpfs /tmp --tmpfs /run --tmpfs /run/lock "$image"
ready=false
for attempt in {1..30}; do
  if sudo timeout 5 docker exec "$guest" systemctl show --property=Version --value > "$proof/systemd-version.txt"; then ready=true; break; fi
  sleep 1
done
[[ $ready == true ]]
printf 'buster14a/buster disposable credential-gate component test\n' > "$payload/opt-in"
chmod 0644 "$payload/opt-in"
sudo docker cp "$payload/opt-in" "$guest:/run/buster-bench-credential-gate-test.opt-in"
sudo timeout 600 docker exec --env BUSTER_CREDENTIAL_GATE_DISPOSABLE_SYSTEMD=isolated-docker-systemd-test "$guest" /root/credential-gate-systemd-test --run 2>&1 | tee "$proof/fixture.log"
