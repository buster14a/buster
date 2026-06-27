#!/usr/bin/env bash
set -euo pipefail

avd_name=${BUSTER_ANDROID_AVD:-buster-ci}
gpu_mode=${BUSTER_ANDROID_EMULATOR_GPU:-host}
headless=${BUSTER_ANDROID_EMULATOR_HEADLESS:-1}
boot_timeout_seconds=${BUSTER_ANDROID_EMULATOR_BOOT_TIMEOUT_SECONDS:-180}
create_avd=${BUSTER_ANDROID_CREATE_AVD:-1}
device_profile=${BUSTER_ANDROID_DEVICE:-pixel_6}
system_image=${BUSTER_ANDROID_SYSTEM_IMAGE:-}
if [[ -z $system_image ]]; then
    system_image="system-images;android-35;google_apis;x86_64"
fi
IFS=';' read -r system_image_prefix system_image_platform system_image_tag system_image_abi <<< "$system_image"
if [[ $system_image_prefix != system-images || -z $system_image_platform || -z $system_image_tag || -z $system_image_abi ]]; then
    echo "error: BUSTER_ANDROID_SYSTEM_IMAGE must look like system-images;android-35;google_apis;x86_64" >&2
    exit 1
fi

android_sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-/opt/android-sdk}}
export ANDROID_HOME=$android_sdk
export ANDROID_SDK_ROOT=$android_sdk
android_user_home=${ANDROID_USER_HOME:-${HOME}/.android}
android_avd_home=${ANDROID_AVD_HOME:-${android_user_home}/avd}
export ANDROID_USER_HOME=$android_user_home
export ANDROID_AVD_HOME=$android_avd_home
export PATH="$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator:$ANDROID_HOME/cmdline-tools/latest/bin:$PATH"

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: timeout command is required for Android emulator startup" >&2
    exit 1
fi
if ! command -v adb >/dev/null 2>&1; then
    echo "error: adb was not found; check ANDROID_HOME/ANDROID_SDK_ROOT" >&2
    exit 1
fi
if ! command -v emulator >/dev/null 2>&1; then
    echo "error: emulator was not found; install the Android SDK emulator package" >&2
    exit 1
fi

log_dir=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
emulator_log=${BUSTER_ANDROID_EMULATOR_LOG:-${log_dir%/}/buster-android-emulator.log}
started_marker=${BUSTER_ANDROID_EMULATOR_STARTED_MARKER:-${log_dir%/}/buster-android-emulator.started}
mkdir -p "$(dirname "$started_marker")"
mkdir -p "$ANDROID_USER_HOME" "$ANDROID_AVD_HOME"
rm -f "$started_marker"

adb start-server >/dev/null

adb_has_device() {
    timeout 10 adb devices | awk 'NR > 1 && $2 == "device" { found = 1 } END { exit found ? 0 : 1 }'
}

if adb_has_device; then
    echo "Android device is already available; not starting an emulator."
    adb devices
    exit 0
fi

if ! emulator -list-avds | grep -Fx -- "$avd_name" >/dev/null; then
    echo "Android AVD '$avd_name' was not found for user $(id -un) with HOME=${HOME:-<unset>}" >&2
    echo "ANDROID_USER_HOME=$ANDROID_USER_HOME" >&2
    echo "ANDROID_AVD_HOME=$ANDROID_AVD_HOME" >&2
    echo "Available AVDs for this user:" >&2
    emulator -list-avds >&2 || true
    if [[ $create_avd == 0 || $create_avd == false || $create_avd == FALSE ]]; then
        exit 1
    fi
    if ! command -v avdmanager >/dev/null 2>&1; then
        echo "error: avdmanager was not found; install Android SDK cmdline-tools or pre-create '$avd_name' for the runner user" >&2
        exit 1
    fi

    system_image_directory="$ANDROID_HOME/system-images/$system_image_platform/$system_image_tag/$system_image_abi"
    if [[ ! -d $system_image_directory ]]; then
        echo "error: Android system image '$system_image' is not installed at '$system_image_directory'" >&2
        echo "Install it into the SDK used by the runner, for example:" >&2
        echo "  sudo env ANDROID_HOME=\"$ANDROID_HOME\" ANDROID_SDK_ROOT=\"$ANDROID_SDK_ROOT\" sdkmanager --install \"$system_image\"" >&2
        exit 1
    fi

    echo "Creating Android AVD '$avd_name' from '$system_image' with device '$device_profile'..."
    if ! echo no | avdmanager create avd --force --name "$avd_name" --package "$system_image" --device "$device_profile"; then
        echo "warning: avdmanager failed; creating a minimal CI AVD directly" >&2
        avd_directory="$ANDROID_AVD_HOME/$avd_name.avd"
        mkdir -p "$avd_directory"
        cat >"$ANDROID_AVD_HOME/$avd_name.ini" <<EOF
avd.ini.encoding=UTF-8
path=$avd_directory
target=$system_image_platform
EOF
        cat >"$avd_directory/config.ini" <<EOF
AvdId=$avd_name
abi.type=$system_image_abi
avd.ini.displayname=$avd_name
avd.ini.encoding=UTF-8
disk.dataPartition.size=6G
fastboot.forceColdBoot=yes
hw.cpu.arch=$system_image_abi
hw.device.name=$device_profile
hw.gpu.enabled=yes
hw.gpu.mode=auto
hw.keyboard=yes
hw.lcd.density=420
hw.lcd.height=2400
hw.lcd.width=1080
hw.mainKeys=no
hw.ramSize=2048
image.sysdir.1=system-images/$system_image_platform/$system_image_tag/$system_image_abi/
runtime.network.latency=none
runtime.network.speed=full
sdcard.size=512M
showDeviceFrame=no
tag.id=$system_image_tag
tag.display=$system_image_tag
vm.heapSize=256
EOF
    fi
    if ! emulator -list-avds | grep -Fx -- "$avd_name" >/dev/null; then
        echo "error: avdmanager reported success, but emulator still cannot see Android AVD '$avd_name'" >&2
        echo "ANDROID_USER_HOME=$ANDROID_USER_HOME" >&2
        echo "ANDROID_AVD_HOME=$ANDROID_AVD_HOME" >&2
        echo "AVD directory contents:" >&2
        find "$ANDROID_AVD_HOME" -maxdepth 2 -type f -o -type d >&2 || true
        exit 1
    fi
fi

echo "Android emulator acceleration check:"
set +e
accel_output=$(emulator -accel-check 2>&1)
accel_status=$?
set -e
printf '%s\n' "$accel_output"
if [[ $accel_status -ne 0 && $system_image == *x86* ]]; then
    echo "error: Android x86/x86_64 emulators require hardware acceleration, but it is not available" >&2
    echo "Use a runner with KVM/VMX/SVM enabled, a physical Android device, or set BUSTER_ANDROID_SYSTEM_IMAGE to a non-x86 image." >&2
    exit 1
fi

emulator_args=(-avd "$avd_name" -no-snapshot -no-boot-anim -no-audio -no-metrics -gpu "$gpu_mode")
if [[ $headless != 0 && $headless != false && $headless != FALSE ]]; then
    emulator_args+=(-no-window)
fi

echo "Starting Android emulator '$avd_name' with gpu=$gpu_mode headless=$headless"
emulator "${emulator_args[@]}" >"$emulator_log" 2>&1 &
emulator_pid=$!
printf '%s\n' "$emulator_pid" >"$started_marker"

echo "Waiting up to ${boot_timeout_seconds}s for the emulator to connect..."
connect_deadline=$((SECONDS + boot_timeout_seconds))
while true; do
    if adb_has_device; then
        break
    fi
    if ! kill -0 "$emulator_pid" >/dev/null 2>&1; then
        echo "error: emulator exited before connecting to adb" >&2
        tail -n 200 "$emulator_log" >&2 || true
        exit 1
    fi
    if (( SECONDS >= connect_deadline )); then
        echo "error: emulator did not connect within ${boot_timeout_seconds}s" >&2
        tail -n 200 "$emulator_log" >&2 || true
        exit 1
    fi
    sleep 2
done

echo "Waiting up to ${boot_timeout_seconds}s for Android boot to complete..."
boot_deadline=$((SECONDS + boot_timeout_seconds))
while true; do
    boot_completed=$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r' || true)
    if [[ $boot_completed == 1 ]]; then
        break
    fi
    if ! kill -0 "$emulator_pid" >/dev/null 2>&1; then
        echo "error: emulator exited before Android finished booting" >&2
        tail -n 200 "$emulator_log" >&2 || true
        exit 1
    fi
    if (( SECONDS >= boot_deadline )); then
        echo "error: Android did not finish booting within ${boot_timeout_seconds}s" >&2
        tail -n 200 "$emulator_log" >&2 || true
        exit 1
    fi
    sleep 2
done

adb shell settings put global window_animation_scale 0 >/dev/null 2>&1 || true
adb shell settings put global transition_animation_scale 0 >/dev/null 2>&1 || true
adb shell settings put global animator_duration_scale 0 >/dev/null 2>&1 || true
adb shell input keyevent 82 >/dev/null 2>&1 || true

adb devices
adb shell getprop ro.product.cpu.abi
