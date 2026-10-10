#!/bin/sh
# macOS 原生管理员认证调用此脚本；凭据由系统管理，不进入脚本/日志。
set -eu
appendcd_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
appendcd_owner=$(/usr/bin/stat -f '%u:%g' "$appendcd_project")
appendcd_evidence="$appendcd_project/artifacts/physical"
mkdir -p "$appendcd_evidence"
trap '/usr/sbin/chown -R "$appendcd_owner" "$appendcd_evidence"' EXIT
cd "$appendcd_project"
"$appendcd_project/build/appendcd" "$@" --work-dir "$appendcd_evidence"
