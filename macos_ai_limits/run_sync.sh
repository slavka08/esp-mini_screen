#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
exec /opt/homebrew/bin/python3 "$SCRIPT_DIR/ai_limits_sync.py" push "$@"
