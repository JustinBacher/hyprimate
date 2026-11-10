#!/usr/bin/env bash
# Kitty's `shell` for the paper-desk test: a fastfetch in blue ink, then a plain bash with a title-setting prompt.
if command -v fastfetch >/dev/null; then
    printf '[%s@%s ~]$ fastfetch\n' "$USER" "${HOSTNAME%%.*}"
    fastfetch -c none -s title:os:host:kernel:uptime:packages:de:terminal --logo small \
        --logo-color-1 blue --color-keys blue --color-title blue
    printf '\n'
fi
exec env PS1='\[\e]0;\u@\h ~\a\][\u@\h ~]$ ' bash --noprofile --norc -i
