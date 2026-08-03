#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 <apksigner> <zipalign> <keystore> <unsigned-apk> <output-apk>" >&2
    exit 2
fi

apksigner=$1
zipalign=$2
keystore=$3
unsigned_apk=$4
output_apk=$5
sign_timeout_seconds=${BUSTER_ANDROID_APKSIGNER_TIMEOUT_SECONDS:-120}
verify_timeout_seconds=${BUSTER_ANDROID_APKSIGNER_VERIFY_TIMEOUT_SECONDS:-30}
aligned_apk="${output_apk}.aligned"

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: timeout command is required for bounded APK signing" >&2
    exit 1
fi

java_tool_options=${JAVA_TOOL_OPTIONS:-}
append_java_option_once() {
    local option=$1
    case " ${java_tool_options} " in
        *" ${option} "*) ;;
        *) java_tool_options="${option}${java_tool_options:+ ${java_tool_options}}" ;;
    esac
}

append_java_option_once "-Djava.security.egd=file:/dev/urandom"
if command -v java >/dev/null 2>&1 && java --help 2>&1 | grep -q -- '--enable-native-access'; then
    append_java_option_once "--enable-native-access=ALL-UNNAMED"
fi

rm -f "${aligned_apk}" "${output_apk}"
"${zipalign}" -f 4 "${unsigned_apk}" "${aligned_apk}"

echo "Signing ${output_apk} with apksigner (timeout ${sign_timeout_seconds}s)"
if JAVA_TOOL_OPTIONS=${java_tool_options} timeout --kill-after=10s "${sign_timeout_seconds}" \
        "${apksigner}" sign --verbose \
        --ks "${keystore}" \
        --ks-pass pass:android \
        --key-pass pass:android \
        --v1-signing-enabled true \
        --v2-signing-enabled true \
        --v3-signing-enabled true \
        --out "${output_apk}" \
        "${aligned_apk}"; then
    sign_status=0
else
    sign_status=$?
fi

case "${sign_status}" in
    0) ;;
    124|137)
        echo "error: apksigner did not finish within ${sign_timeout_seconds}s" >&2
        exit 1
        ;;
    *)
        echo "error: apksigner failed with status ${sign_status}" >&2
        exit "${sign_status}"
        ;;
esac

echo "Verifying ${output_apk} APK signatures"
JAVA_TOOL_OPTIONS=${java_tool_options} timeout --kill-after=10s "${verify_timeout_seconds}" \
    "${apksigner}" verify --verbose --print-certs "${output_apk}"
