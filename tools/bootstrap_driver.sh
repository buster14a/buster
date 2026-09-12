#!/usr/bin/env bash
# Content-addressed, immutable TCC bootstrap publication for build.sh.

buster_bootstrap_sha256()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        printf 'error: build bootstrap requires sha256sum or shasum\n' >&2
        return 127
    fi
}

buster_bootstrap_dependencies()
{
    dependency_file=$1
    output_file=$2
    awk '
        BEGIN { after_target = 0; escaped = 0; token = "" }
        {
            for (i = 1; i <= length($0); i += 1) {
                character = substr($0, i, 1)
                if (!after_target) {
                    if (character == ":") after_target = 1
                } else if (escaped) {
                    token = token character
                    escaped = 0
                } else if (character == "\\") {
                    escaped = 1
                } else if (character == " " || character == "\t") {
                    if (length(token)) { print token; token = "" }
                } else {
                    token = token character
                }
            }
            if (escaped) escaped = 0
        }
        END { if (length(token)) print token }
    ' "$dependency_file" |
        sed -e 's#^\./##' -e '/^$/d' |
        LC_ALL=C sort -u >"$output_file"
}

buster_bootstrap_snapshot()
{
    local repository_root=$1
    local dependency_list=$2
    local output_file=$3
    local hash_file="$output_file.hashes"
    local dependency dependency_path dependency_hash
    local -a dependency_paths=()
    : >"$output_file"
    while IFS= read -r dependency; do
        case "$dependency" in
        ../*|*/../*)
            printf 'error: unsafe bootstrap dependency path: %s\n' "$dependency" >&2
            return 1
            ;;
        esac
        dependency_path=$dependency
        if [[ $dependency != /* ]]; then
            dependency_path="$repository_root/$dependency"
        fi
        if [[ ! -f $dependency_path ]]; then
            printf 'error: missing bootstrap dependency: %s\n' "$dependency" >&2
            return 1
        fi
        dependency_paths+=("$dependency_path")
    done <"$dependency_list"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "${dependency_paths[@]}" | awk '{print $1}' >"$hash_file"
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "${dependency_paths[@]}" | awk '{print $1}' >"$hash_file"
    else
        printf 'error: build bootstrap requires sha256sum or shasum\n' >&2
        return 127
    fi
    exec 3<"$hash_file"
    while IFS= read -r dependency; do
        if ! IFS= read -r dependency_hash <&3; then
            printf 'error: incomplete bootstrap dependency hash output\n' >&2
            exec 3<&-
            rm -f -- "$hash_file"
            return 1
        fi
        printf 'dependency\t%s\t%s\n' "$dependency" "$dependency_hash" >>"$output_file"
    done <"$dependency_list"
    exec 3<&-
    rm -f -- "$hash_file"
}

buster_bootstrap_manifest_valid()
{
    local repository_root=$1
    local entry_directory=$2
    local marker=$3
    local expected_config=$4
    local artifact_name=
    local artifact_hash=
    local saw_header=0
    local saw_config=0
    local saw_artifact=0
    local saw_dependency=0
    local saw_build_c=0
    local saw_end=0
    local valid=1
    local dependency_path kind value digest extra
    local validation_prefix="$marker.validate-$$-$RANDOM"
    local dependency_list="$validation_prefix.list"
    local expected_snapshot="$validation_prefix.expected"
    local actual_snapshot="$validation_prefix.actual"
    : >"$dependency_list"
    : >"$expected_snapshot"
    while IFS=$'\t' read -r kind value digest extra; do
        if [[ $kind == BUSTER_BOOTSTRAP_CACHE_V1 && -z $value && $saw_header == 0 ]]; then
            saw_header=1
        elif [[ $kind == config && $value == "$expected_config" && -z $digest && $saw_header == 1 && $saw_config == 0 && $saw_end == 0 ]]; then
            saw_config=1
        elif [[ $kind == artifact && -n $value && -n $digest && -z $extra && $saw_config == 1 && $saw_artifact == 0 && $saw_end == 0 ]]; then
            case "$value" in */*|*\\*) valid=0; break ;; esac
            artifact_name=$value
            artifact_hash=$digest
            saw_artifact=1
        elif [[ $kind == dependency && -n $value && -n $digest && -z $extra && $saw_artifact == 1 && $saw_end == 0 ]]; then
            case "$value" in ../*|*/../*) valid=0; break ;; esac
            dependency_path=$value
            if [[ $value != /* ]]; then
                dependency_path="$repository_root/$value"
            fi
            printf '%s\n' "$value" >>"$dependency_list"
            printf 'dependency\t%s\t%s\n' "$value" "$digest" >>"$expected_snapshot"
            saw_dependency=1
            [[ $dependency_path == "$repository_root/build.c" ]] && saw_build_c=1
        elif [[ $kind == END && -z $value && $saw_dependency == 1 && $saw_build_c == 1 && $saw_end == 0 ]]; then
            saw_end=1
        else
            valid=0
            break
        fi
    done <"$marker"
    if [[ $valid == 1 && $saw_header == 1 && $saw_config == 1 && $saw_artifact == 1 && $saw_dependency == 1 && $saw_build_c == 1 &&
          $saw_end == 1 && -n $artifact_name && -f "$entry_directory/$artifact_name" ]] &&
       buster_bootstrap_snapshot "$repository_root" "$dependency_list" "$actual_snapshot" &&
       cmp -s "$expected_snapshot" "$actual_snapshot" &&
       [[ $(buster_bootstrap_sha256 "$entry_directory/$artifact_name") == "$artifact_hash" ]]; then
        BUSTER_BOOTSTRAP_ARTIFACT="$entry_directory/$artifact_name"
    else
        valid=0
    fi
    rm -f -- "$dependency_list" "$expected_snapshot" "$actual_snapshot" "$actual_snapshot.hashes"
    [[ $valid == 1 ]]
}

buster_bootstrap_driver()
{
    repository_root=$1
    shift
    cd "$repository_root"

    tcc_path=$(command -v tcc) || {
        printf 'error: tcc was not found in PATH\n' >&2
        return 127
    }
    case "$tcc_path" in
    /*) ;;
    *) tcc_path=$(cd -- "$(dirname -- "$tcc_path")" && pwd -P)/$(basename -- "$tcc_path") ;;
    esac

    bootstrap_flags=(-Isrc -Wall -Werror -Wno-unused-function -g -MD)
    cache_root="$repository_root/.cache/bootstrap-driver/posix"
    mkdir -p "$cache_root"
    token="$$-$RANDOM"
    temporary_prefix="$cache_root/.bootstrap-$token"
    config_payload="$temporary_prefix.config"
    dependency_raw="$temporary_prefix.d"
    dependency_list="$temporary_prefix.list"
    dependency_snapshot="$temporary_prefix.snapshot"
    post_snapshot="$temporary_prefix.post"
    temporary_probe="$temporary_prefix.probe"
    temporary_artifact="$temporary_prefix.tmp"
    temporary_marker="$temporary_prefix.marker"
    trap 'rm -f -- "$config_payload" "$dependency_raw" "$dependency_list" "$dependency_snapshot" "$post_snapshot" "$temporary_probe" "$temporary_artifact" "$temporary_marker"' EXIT

    {
        printf 'BUSTER_BOOTSTRAP_CONFIG_V1\ncompiler\t%s\ncompiler-sha256\t%s\n' "$tcc_path" "$(buster_bootstrap_sha256 "$tcc_path")"
        printf 'flag\t%s\n' "${bootstrap_flags[@]}"
        printf 'helper-sha256\t%s\n' "$(buster_bootstrap_sha256 "$repository_root/tools/bootstrap_driver.sh")"
    } >"$config_payload"
    config=$(buster_bootstrap_sha256 "$config_payload") || return
    entry_directory="$cache_root/$config"
    mkdir -p "$entry_directory"

    for marker in "$entry_directory"/*.complete; do
        if [[ -f $marker ]] && buster_bootstrap_manifest_valid "$repository_root" "$entry_directory" "$marker" "$config"; then
            "$BUSTER_BOOTSTRAP_ARTIFACT" "$@"
            return $?
        fi
    done

    "$tcc_path" "${bootstrap_flags[@]}" -MF "$dependency_raw" build.c -o "$temporary_probe"
    buster_bootstrap_dependencies "$dependency_raw" "$dependency_list"
    buster_bootstrap_snapshot "$repository_root" "$dependency_list" "$dependency_snapshot"

    "$tcc_path" "${bootstrap_flags[@]}" -MF "$dependency_raw" build.c -o "$temporary_artifact"
    buster_bootstrap_dependencies "$dependency_raw" "$dependency_list"
    buster_bootstrap_snapshot "$repository_root" "$dependency_list" "$post_snapshot"
    if ! cmp -s "$dependency_snapshot" "$post_snapshot"; then
        printf 'error: bootstrap inputs changed while tcc was compiling; retry the command\n' >&2
        return 75
    fi

    current_compiler_hash=$(buster_bootstrap_sha256 "$tcc_path") || return
    if ! grep -Fqx "compiler-sha256"$'\t'"$current_compiler_hash" "$config_payload"; then
        printf 'error: tcc changed while compiling the build driver; retry the command\n' >&2
        return 75
    fi

    artifact_name="build-$token"
    artifact_hash=$(buster_bootstrap_sha256 "$temporary_artifact") || return
    mv -- "$temporary_artifact" "$entry_directory/$artifact_name"
    {
        printf 'BUSTER_BOOTSTRAP_CACHE_V1\nconfig\t%s\nartifact\t%s\t%s\n' "$config" "$artifact_name" "$artifact_hash"
        cat "$post_snapshot"
        printf 'END\n'
    } >"$temporary_marker"
    mv -- "$temporary_marker" "$entry_directory/$artifact_name.complete"
    BUSTER_BOOTSTRAP_ARTIFACT="$entry_directory/$artifact_name"
    "$BUSTER_BOOTSTRAP_ARTIFACT" "$@"
}
