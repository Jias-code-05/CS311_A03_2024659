#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH=/usr/bin:/ucrt64/bin:$PATH
exec make "$@"
