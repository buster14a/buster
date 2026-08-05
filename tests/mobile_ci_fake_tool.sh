#!/usr/bin/env bash
set -euo pipefail

tool=${FAKE_TOOL_NAME:-${0##*/}}

case "$tool" in
    adb)
        state=${FAKE_ANDROID_STATE_DIR:?FAKE_ANDROID_STATE_DIR is required}
        if [[ ${1:-} == -s ]]; then
            shift 2
        fi
        command=${1:-}
        shift || true
        case "$command" in
            start-server|uninstall|install)
                if [[ $command == install ]]; then
                    printf '%s\n' "${1:-}" >"$state/installed_apk"
                fi
                exit 0
                ;;
            devices)
                printf 'List of devices attached\n'
                if [[ -f $state/device ]]; then
                    printf 'emulator-5554\tdevice\n'
                fi
                exit 0
                ;;
            shell)
                shell_command=${1:-}
                shift || true
                case "$shell_command" in
                    getprop)
                        property=${1:-}
                        case "$property" in
                            sys.boot_completed)
                                if [[ -f $state/device ]]; then
                                    printf '1\n'
                                else
                                    printf '0\n'
                                fi
                                ;;
                            ro.product.cpu.abi)
                                printf '%s\n' "${FAKE_ANDROID_ABI:-x86_64}"
                                ;;
                            ro.build.version.sdk)
                                printf '35\n'
                                ;;
                            *)
                                printf '\n'
                                ;;
                        esac
                        ;;
                    am|settings|input)
                        ;;
                    *)
                        ;;
                esac
                exit 0
                ;;
            logcat)
                if [[ ${1:-} == -d ]]; then
                    printf 'fake Android logcat diagnostics\n'
                elif [[ ${FAKE_ANDROID_TEST_RESULT:-0} == 0 ]]; then
                    printf 'BUSTER_ANDROID_TEST_RESULT:0\n'
                else
                    printf 'BUSTER_ANDROID_TEST_RESULT:1\n'
                fi
                exit 0
                ;;
            emu)
                if [[ ${FAKE_ANDROID_ADB_KILL_STATUS:-0} -ne 0 ]]; then
                    exit "${FAKE_ANDROID_ADB_KILL_STATUS}"
                fi
                : >"$state/kill"
                exit 0
                ;;
            version)
                printf 'Android Debug Bridge version fake\n'
                exit 0
                ;;
            *)
                printf 'fake adb: unsupported command %s\n' "$command" >&2
                exit 1
                ;;
        esac
        ;;
    emulator)
        state=${FAKE_ANDROID_STATE_DIR:?FAKE_ANDROID_STATE_DIR is required}
        if [[ ${1:-} == -list-avds ]]; then
            printf 'buster-ci\n'
            exit 0
        fi
        if [[ ${1:-} == -accel-check ]]; then
            printf 'accel: fake hardware acceleration\n'
            exit 0
        fi
        sleep "${FAKE_ANDROID_BOOT_DELAY_SECONDS:-1}"
        if [[ ${FAKE_ANDROID_NEVER_BOOT:-0} == 0 ]]; then
            : >"$state/device"
        fi
        while [[ ! -f $state/kill ]]; do
            sleep 1
        done
        exit 0
        ;;
    cmake)
        if [[ ${1:-} == --version ]]; then
            printf 'cmake version 99.0-fake\n'
            exit 0
        fi
        build_dir=
        config=
        target=
        previous=
        for argument in "$@"; do
            if [[ $previous == B ]]; then
                build_dir=$argument
                previous=
            elif [[ $previous == C ]]; then
                config=$argument
                previous=
            elif [[ $previous == T ]]; then
                target=$argument
                previous=
            elif [[ $argument == -B ]]; then
                previous=B
            elif [[ $argument == --config ]]; then
                previous=C
            elif [[ $argument == --target ]]; then
                previous=T
            fi
        done
        if [[ ${1:-} == --build ]]; then
            build_dir=$2
            if [[ ${FAKE_CMAKE_BUILD_DELAY_SECONDS:-0} != 0 ]]; then
                sleep "${FAKE_CMAKE_BUILD_DELAY_SECONDS}"
            fi
            mkdir -p "$build_dir/$config"
            if [[ $target == apk ]]; then
                printf '%s\n' "$config" >"$build_dir/buster.apk"
            elif [[ $target == ide ]]; then
                mkdir -p "$build_dir/$config/ide.app"
                printf '%s\n' "$config" >"$build_dir/$config/ide.app/build-config"
            fi
            if [[ -n ${FAKE_CMAKE_LOG:-} ]]; then
                printf '%s %s\n' "$config" "$target" >>"$FAKE_CMAKE_LOG"
            fi
        else
            mkdir -p "$build_dir"
        fi
        exit 0
        ;;
    ninja)
        printf '1.0-fake\n'
        exit 0
        ;;
    codesign)
        if [[ ${FAKE_IOS_CODESIGN_STATUS:-0} -ne 0 ]]; then
            exit "${FAKE_IOS_CODESIGN_STATUS}"
        fi
        exit 0
        ;;
    xcrun)
        state=${FAKE_IOS_STATE_DIR:?FAKE_IOS_STATE_DIR is required}
        log=${FAKE_IOS_LOG:?FAKE_IOS_LOG is required}
        printf '%s\n' "$*" >>"$log"
        if [[ ${1:-} == --sdk ]]; then
            if [[ ${3:-} == --show-sdk-path ]]; then
                printf '/fake/iPhoneSimulator.sdk\n'
                exit 0
            fi
            shift 3
        fi
        if [[ ${1:-} != simctl ]]; then
            exit 1
        fi
        shift
        command=${1:-}
        shift || true
        case "$command" in
            list)
                if [[ ${1:-} == devices ]]; then
                    case " $* " in
                        *" -j "*) printf '{"devices":{}}\n' ;;
                        *) printf 'fake iOS simulator list\n' ;;
                    esac
                elif [[ ${1:-} == runtimes ]]; then
                    printf '{"runtimes":[]}\n'
                else
                    printf 'fake iOS simulator list\n'
                fi
                ;;
            delete)
                ;;
            boot)
                : >"$state/booted"
                ;;
            bootstatus)
                printf 'Device booted\n'
                ;;
            install)
                printf '%s\n' "${2:-}" >"$state/installed"
                ;;
            spawn)
                spawn_udid=${1:-}
                shift || true
                spawn_command=${1:-}
                shift || true
                case "$spawn_command" in
                    ps)
                        if [[ ${FAKE_IOS_APP_ALIVE:-0} == 1 ]]; then
                            printf '12345 1 S dev.buster.ide\n'
                            exit 0
                        elif [[ ${1:-} == -p ]]; then
                            # The fake launcher emits a result immediately, so
                            # no live app process is needed for the success path.
                            exit 1
                        fi
                        printf 'fake simulator process table\n'
                        ;;
                    log)
                        printf 'fake unified log: no crash diagnostics\n'
                        ;;
                    sh)
                        ;;
                    *)
                        ;;
                esac
                ;;
            launch)
                sleep "${FAKE_IOS_LAUNCH_SLEEP_SECONDS:-0}"
                installed=${state}/installed
                installed_path=
                if [[ -f $installed ]]; then
                    installed_path=$(cat "$installed")
                fi
                if [[ ${FAKE_IOS_NO_MARKER:-0} == 1 ]]; then
                    printf 'dev.buster.ide: 12345\n'
                elif [[ -n ${FAKE_IOS_FAIL_LABEL:-} && $installed_path == *"/${FAKE_IOS_FAIL_LABEL}/"* ]]; then
                    printf 'BUSTER_IOS_RESULT: FAILURE\n'
                else
                    printf 'BUSTER_IOS_RESULT: SUCCESS\n'
                fi
                ;;
            shutdown)
                if [[ ${FAKE_IOS_SHUTDOWN_STATUS:-0} -ne 0 ]]; then
                    exit "${FAKE_IOS_SHUTDOWN_STATUS}"
                fi
                : >"$state/shutdown"
                ;;
            create)
                printf 'FAKE-UDID\n'
                ;;
            runtime)
                ;;
            *)
                printf 'fake xcrun: unsupported simctl command %s\n' "$command" >&2
                exit 1
                ;;
        esac
        exit 0
        ;;
    *)
        printf 'unsupported fake tool %s\n' "$tool" >&2
        exit 1
        ;;
esac
