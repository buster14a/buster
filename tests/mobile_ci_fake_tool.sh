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
        if [[ -z ${FAKE_IOS_CODESIGN_FAIL_LABEL:-} || ${!#} == *"/${FAKE_IOS_CODESIGN_FAIL_LABEL}/"* ]]; then
            if [[ ${FAKE_IOS_REAL_CODESIGN:-0} == 1 ]]; then
                exec /usr/bin/codesign "$@"
            fi
            printf 'fake codesign stdout: bundle rejected\n'
            printf 'fake codesign stderr: signing diagnostic\n' >&2
            if [[ ${FAKE_IOS_LARGE_OUTPUT:-0} == 1 ]]; then
                python3 -c 'import sys; sys.stdout.write("X" * 131072)'
            fi
            sleep "${FAKE_IOS_CODESIGN_SLEEP_SECONDS:-0}"
            exit "${FAKE_IOS_CODESIGN_STATUS:-0}"
        fi
        exit 0
        ;;
    xcrun)
        state=${FAKE_IOS_STATE_DIR:?FAKE_IOS_STATE_DIR is required}
        log=${FAKE_IOS_LOG:?FAKE_IOS_LOG is required}
        printf '%s\n' "$*" >>"$log"
        if [[ ${1:-} == --sdk ]]; then
            if [[ ${FAKE_IOS_REAL_CONTEXT:-0} == 1 ]]; then
                exec /usr/bin/xcrun "$@"
            fi
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
                if [[ ${FAKE_IOS_REAL_CONTEXT:-0} == 1 ]]; then
                    exec /usr/bin/xcrun simctl list "$@"
                fi
                if [[ ${1:-} == devices ]]; then
                    case " $* " in
                        *" -j "*)
                            if [[ ${FAKE_IOS_BORROWED_DEVICE:-0} == 1 ]]; then
                                printf '%s\n' '{"devices":{"com.apple.CoreSimulator.SimRuntime.iOS-26-5":[{"name":"buster-ci","udid":"00000000-0000-0000-0000-0000000000b0","isAvailable":true,"state":"Shutdown"}]}}'
                            else
                                printf '{"devices":{}}\n'
                            fi
                            ;;
                        *) printf 'fake iOS simulator list\n' ;;
                    esac
                elif [[ ${1:-} == runtimes ]]; then
                    if [[ ${FAKE_IOS_RUNTIME_AVAILABLE:-0} == 1 ]]; then
                        printf '%s\n' '{"runtimes":[{"identifier":"com.apple.CoreSimulator.SimRuntime.iOS-26-5","version":"26.5","isAvailable":true,"supportedDeviceTypes":[{"identifier":"com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro","name":"iPhone 17 Pro","productFamily":"iPhone"}]}]}'
                    else
                        printf '{"runtimes":[]}\n'
                    fi
                else
                    printf 'fake iOS simulator list\n'
                fi
                ;;
            delete)
                if [[ ${1:-} != unavailable ]]; then
                    if [[ ${FAKE_IOS_DELETE_SLEEP_SECONDS:-0} -ne 0 ]]; then
                        sleep "${FAKE_IOS_DELETE_SLEEP_SECONDS}"
                    fi
                    if [[ ${FAKE_IOS_DELETE_STATUS:-0} -ne 0 ]]; then
                        printf 'delete rejected\n' >&2
                        exit "${FAKE_IOS_DELETE_STATUS}"
                    fi
                    printf '%s\n' "${1:-}" >>"$state/deleted"
                fi
                ;;
            boot)
                : >"$state/booted"
                ;;
            bootstatus)
                bootstatus_count_file="$state/bootstatus.count"
                bootstatus_count=0
                if [[ -f $bootstatus_count_file ]]; then
                    bootstatus_count=$(cat "$bootstatus_count_file")
                fi
                bootstatus_count=$((bootstatus_count + 1))
                printf '%s\n' "$bootstatus_count" >"$bootstatus_count_file"
                case "${FAKE_IOS_BOOTSTATUS_MODE:-success}" in
                    timeout-first)
                        if [[ $bootstatus_count -eq 1 ]]; then
                            printf 'first readiness diagnostic\n'
                            sleep "${FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS:-60}"
                        fi
                        ;;
                    always-timeout)
                        printf 'readiness diagnostic\n'
                        sleep "${FAKE_IOS_BOOTSTATUS_SLEEP_SECONDS:-60}"
                        ;;
                    numeric-124)
                        printf 'numeric timeout-like status\n'
                        exit 124
                        ;;
                    reject)
                        printf 'readiness rejected\n' >&2
                        exit "${FAKE_IOS_BOOTSTATUS_STATUS:-9}"
                        ;;
                esac
                printf 'Device booted\n'
                ;;
            install)
                install_path=${2:-}
                printf 'fake install stdout: %s\n' "$install_path"
                printf 'fake install stderr: lifecycle diagnostic\n' >&2
                if [[ -z ${FAKE_IOS_INSTALL_FAIL_LABEL:-} || $install_path == *"/${FAKE_IOS_INSTALL_FAIL_LABEL}/"* ]]; then
                    sleep "${FAKE_IOS_INSTALL_SLEEP_SECONDS:-0}"
                    if [[ ${FAKE_IOS_INSTALL_STATUS:-0} -ne 0 ]]; then
                        exit "${FAKE_IOS_INSTALL_STATUS}"
                    fi
                fi
                printf '%s\n' "$install_path" >"$state/installed"
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
                if [[ ${FAKE_IOS_REAL_SHUTDOWN:-0} == 1 ]]; then
                    exec /usr/bin/xcrun simctl shutdown "$@"
                fi
                printf 'fake shutdown stdout: device shutdown rejected\n'
                printf 'fake shutdown stderr: lifecycle diagnostic\n' >&2
                sleep "${FAKE_IOS_SHUTDOWN_SLEEP_SECONDS:-0}"
                if [[ ${FAKE_IOS_SHUTDOWN_STATUS:-0} -ne 0 ]]; then
                    exit "${FAKE_IOS_SHUTDOWN_STATUS}"
                fi
                : >"$state/shutdown"
                ;;
            create)
                create_count_file="$state/create.count"
                create_count=0
                if [[ -f $create_count_file ]]; then
                    create_count=$(cat "$create_count_file")
                fi
                create_count=$((create_count + 1))
                printf '%s\n' "$create_count" >"$create_count_file"
                if [[ ${FAKE_IOS_CREATE_STATUS:-0} -ne 0 \
                    && (${FAKE_IOS_CREATE_FAIL_AFTER_FIRST:-0} != 1 || $create_count -gt 1) ]]; then
                    printf 'create rejected\n' >&2
                    exit "${FAKE_IOS_CREATE_STATUS}"
                fi
                create_udid=$(printf '00000000-0000-0000-0000-00000000000%s' "$create_count")
                if [[ ${FAKE_IOS_CREATE_INVALID_OUTPUT:-0} == 1 && $create_count -gt 1 ]]; then
                    printf 'create rejected diagnostic\n'
                    exit 0
                fi
                if [[ ${FAKE_IOS_CREATE_SLEEP_SECONDS:-0} -ne 0 \
                    && (${FAKE_IOS_CREATE_FAIL_AFTER_FIRST:-0} != 1 || $create_count -gt 1) ]]; then
                    if [[ ${FAKE_IOS_CREATE_EMIT_BEFORE_HANG:-0} == 1 ]]; then
                        printf '%s\n' "$create_udid"
                    fi
                    sleep "${FAKE_IOS_CREATE_SLEEP_SECONDS}"
                fi
                if [[ ${FAKE_IOS_CREATE_EMIT_BEFORE_HANG:-0} != 1 \
                    || ${FAKE_IOS_CREATE_SLEEP_SECONDS:-0} -eq 0 \
                    || ${FAKE_IOS_CREATE_FAIL_AFTER_FIRST:-0} != 1 \
                    || $create_count -eq 1 ]]; then
                    printf '%s\n' "$create_udid"
                fi
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
