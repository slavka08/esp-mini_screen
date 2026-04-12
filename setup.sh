#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TLS_HEADER="${ROOT_DIR}/esp_mini_screen_camera/tls_local.h"
TLS_SCRIPT="${ROOT_DIR}/esp_mini_screen_camera/generate_tls_cert.sh"

FORCE="${1:-}"

if [[ "${FORCE}" == "--force" || ! -f "${TLS_HEADER}" ]]; then
  bash "${TLS_SCRIPT}"
else
  echo "TLS file already exists: ${TLS_HEADER}"
  echo "Use: bash setup.sh --force (to regenerate)"
fi

echo "Setup complete."
