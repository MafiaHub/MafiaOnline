#!/usr/bin/env bash
# Builds the CEF front end (ui/) into resources/ui. Dependencies install into
# ui/node_modules and the npm cache stays inside the repository.
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(cd -- "$project/../../.." && pwd)
ui="$project/ui"
export npm_config_cache="${npm_config_cache:-$root/builds/.cache/npm}"

command -v npm >/dev/null || { echo "npm is required to build the Mafia1Online UI" >&2; exit 1; }
if [[ ! -d "$ui/node_modules" || "$ui/package-lock.json" -nt "$ui/node_modules/.package-lock.json" ]]; then
    npm --prefix "$ui" ci --no-audit --no-fund
fi
npm --prefix "$ui" run build
