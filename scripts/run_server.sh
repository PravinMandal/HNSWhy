#!/usr/bin/env bash
# Start the HNSWhy server with optional .env configuration.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -f "${root}/.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    source "${root}/.env"
    set +a
fi

binary="${root}/build/backend/hnswhy_server"
if [[ ! -x "${binary}" ]]; then
    echo "hnswhy_server is not built yet." >&2
    echo "Run: cmake --preset release && cmake --build --preset release" >&2
    exit 1
fi

cd "${root}"
exec "${binary}" "$@"
