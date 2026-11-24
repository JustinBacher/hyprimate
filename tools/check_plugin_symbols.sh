#!/bin/sh
# Check trusted ELF binaries only: ldd may execute code from its input.

LC_ALL=C
export LC_ALL

fail() {
    printf '%s\n' "plugin symbol check failed: $*" >&2
    exit 1
}

if [ "$#" -ne 3 ]; then
    fail "usage: check_plugin_symbols.sh PLUGIN HOST NM"
fi
plugin=$1
host=$2
nm=$3

tmp=$(mktemp -d "${TMPDIR:-/tmp}/plugin-symbol-check.XXXXXX") || fail "cannot create temporary directory"
trap 'rm -rf "$tmp"' 0
trap 'exit 1' HUP INT TERM

# Parse POSIX nm output, preserving the current checker's malformed-output error.
read_symbols() {
    binary=$1
    selection=$2
    output=$3
    raw=$tmp/nm.out
    err=$tmp/nm.err
    bad=$tmp/nm.bad
    : > "$bad"
    if "$nm" -D "$selection" --format=posix "$binary" > "$raw" 2> "$err"; then
        :
    else
        detail=$(cat "$err")
        if [ -z "$detail" ]; then
            detail=$(cat "$raw")
        fi
        [ -n "$detail" ] || detail="no diagnostic"
        fail "$nm -D $selection --format=posix $binary failed: $detail"
    fi
    if ! awk -v bad="$bad" '
        NF < 2 || length($2) != 1 { print $0 >> bad; invalid = 1; next }
        { print $1, $2 }
        END { if (invalid) exit 1 }
    ' "$raw" > "$output"; then
        line=$(awk 'NR == 1 { print; exit }' "$bad")
        fail "unexpected nm output for $binary: $line"
    fi
}

read_dependencies() {
    binary=$1
    output=$tmp/ldd.out
    err=$tmp/ldd.err
    deps=$tmp/ldd.paths
    missing=$tmp/ldd.missing
    unexpected=$tmp/ldd.unexpected
    : > "$deps"
    : > "$missing"
    : > "$unexpected"
    if ldd "$binary" > "$output" 2> "$err"; then
        :
    else
        detail=$(cat "$err")
        if [ -z "$detail" ]; then
            detail=$(cat "$output")
        fi
        [ -n "$detail" ] || detail="no diagnostic"
        fail "ldd $binary failed: $detail"
    fi
    awk -v deps="$deps" -v missing="$missing" -v unexpected="$unexpected" '
        {
            line = $0
            sub(/^[[:space:]]+/, "", line)
            sub(/[[:space:]]+$/, "", line)
            if (line == "") next
            arrow = index(line, " => ")
            if (arrow) {
                name = substr(line, 1, arrow - 1)
                target = substr(line, arrow + 4)
                if (target == "not found") { print name >> missing; next }
            } else {
                target = line
                if (target ~ /^linux-vdso[.]so/ || target ~ /^linux-gate[.]so/) next
            }
            separator = 0
            for (i = 1; i < length(target); i++)
                if (substr(target, i, 2) == " (") separator = i
            if (!separator) { print line >> unexpected; next }
            target = substr(target, 1, separator - 1)
            if (target !~ /^\//) print line >> unexpected
            else print target >> deps
        }
    ' "$output" || fail "cannot parse ldd output for $binary"

    name=$(awk 'NR == 1 { print; exit }' "$missing")
    [ -z "$name" ] || fail "missing dependency $name required by $binary"
    line=$(awk 'NR == 1 { print; exit }' "$unexpected")
    [ -z "$line" ] || fail "unexpected ldd output for $binary: $line"
    cat "$deps" >> "${tmp}/providers" || fail "cannot record dependencies for $binary"
}

: > "${tmp}/providers"
: > "${tmp}/imports"
: > "${tmp}/exports"
read_symbols "$plugin" "--undefined-only" "${tmp}/plugin.undefined"
awk '$2 != "w" && $2 != "v" { print $1 }' "${tmp}/plugin.undefined" >> "${tmp}/imports" || fail "cannot parse plugin imports"

printf '%s\n' "$host" >> "${tmp}/providers"
read_dependencies "$host"
read_dependencies "$plugin"

if ! sort -u "${tmp}/providers" > "${tmp}/providers.sorted"; then
    fail "cannot sort dependency paths"
fi
while IFS= read -r provider || [ -n "$provider" ]; do
    [ -n "$provider" ] || continue
    read_symbols "$provider" "--defined-only" "${tmp}/provider.defined"
    awk '
        {
            name = $1
            if (index(name, "@@")) {
                at = index(name, "@@")
                base = substr(name, 1, at - 1)
                version = substr(name, at + 2)
                print base
                print base "@" version
            } else print name
        }
    ' "${tmp}/provider.defined" >> "${tmp}/exports" || fail "cannot parse exports from $provider"
done < "${tmp}/providers.sorted"

if ! sort -u "${tmp}/imports" > "${tmp}/imports.sorted"; then
    fail "cannot sort plugin imports"
fi
if ! sort -u "${tmp}/exports" > "${tmp}/exports.sorted"; then
    fail "cannot sort provider exports"
fi
if ! comm -23 "${tmp}/imports.sorted" "${tmp}/exports.sorted" > "${tmp}/unresolved"; then
    fail "cannot compare plugin imports"
fi

if [ -s "${tmp}/unresolved" ]; then
    {
        printf '%s\n' 'plugin symbol check failed: unresolved strong imports:'
        sed 's/^/  /' "${tmp}/unresolved"
    } >&2
    exit 1
fi
printf 'plugin symbol check passed: %s\n' "$plugin"
