#!/usr/bin/env bash
# Verify closed lifecycle receipts without launching a process for every field.
# The launcher owns the files; these helpers only observe retained evidence.
lifecycle_receipt_read() {
    local path=$1 status=1
    lifecycle_receipt_line=
    if [[ -s $path ]]; then
        # Empty delimiter means NUL on Bash 3.2 as well as modern Bash.
        # A successful read found binary corruption; never normalize it away.
        if IFS= read -r -d '' lifecycle_receipt_line <"$path"; then
            :
        elif [[ $lifecycle_receipt_line == *$'\n' \
            && ${lifecycle_receipt_line%$'\n'} != *$'\n'* ]]; then
            lifecycle_receipt_line=${lifecycle_receipt_line%$'\n'}
            status=0
        fi
    fi
    return "$status"
}
lifecycle_receipt_field() {
    local line=$1 key=$2 pattern=$3 field count=0 status=1
    local fields=()
    lifecycle_receipt_value=
    # Space is the protocol delimiter, as in the previous grep expressions.
    IFS=' ' read -r -a fields <<<"$line"
    for field in "${fields[@]}"; do
        case "$field" in
            "$key="*)
                count=$((count + 1))
                lifecycle_receipt_value=${field#*=}
                ;;
        esac
    done
    if [[ $count -eq 1 && $lifecycle_receipt_value =~ ^($pattern)$ ]]; then
        status=0
    fi
    return "$status"
}
