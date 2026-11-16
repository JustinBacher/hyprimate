#!/usr/bin/env bash
# Usage: tools/demo_paper.sh [CONFIG] [OUTPUT]
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/demo_hyprimate.sh" --style paper "$@"
