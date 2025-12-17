#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

SECRETS_FILE="${SECRETS_FILE:-$ROOT_DIR/secrets.env}"

usage() {
  cat <<EOF
Usage: $0 <relay|agent|admin> [console|hidden]

Targets:
  relay    - build relay_server
  agent    - build remote_agent (console|hidden required)
  admin    - build admin_client

Options (agent only):
  console  - build agent with console window
  hidden   - build agent with GUI subsystem (no console, -mwindows)

Env:
  SECRETS_FILE (optional) - path to secrets env (default: ./secrets.env)
EOF
  exit 1
}

if [[ $# -lt 1 ]]; then
  usage
fi

TARGET="$1"
MODE="${2:-}"

if [[ ! -f "$SECRETS_FILE" ]]; then
  echo "Secrets file not found: $SECRETS_FILE" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$SECRETS_FILE"

required_vars=(DEFAULT_PORT DEFAULT_RELAY_HOST TELEGRAM_BOT_TOKEN TELEGRAM_CHAT_ID)
for v in "${required_vars[@]}"; do
  if [[ -z "${!v:-}" ]]; then
    echo "Missing required var in secrets: $v" >&2
    exit 1
  fi
done

# Common flags
CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -Wall -Wextra -O2 -I."
DEFS=(
  "-DDEFAULT_PORT=${DEFAULT_PORT}"
  "-DDEFAULT_RELAY_HOST=\"${DEFAULT_RELAY_HOST}\""
  "-DTELEGRAM_BOT_TOKEN=\"${TELEGRAM_BOT_TOKEN}\""
  "-DTELEGRAM_CHAT_ID=\"${TELEGRAM_CHAT_ID}\""
)

case "$TARGET" in
  relay)
    echo "[BUILD] relay_server"
    set -x
    $CXX $CXXFLAGS "${DEFS[@]}" -o relay_server relay/main.cpp relay/relay_server.cpp -pthread
    set +x
    ;;

  agent)
    if [[ -z "$MODE" ]]; then
      echo "Agent build requires mode: console|hidden" >&2
      usage
    fi
    EXTRA=()
    if [[ "$MODE" == "hidden" ]]; then
      EXTRA+=("-mwindows")
    fi
    echo "[BUILD] remote_agent ($MODE)"
    set -x
    $CXX $CXXFLAGS "${DEFS[@]}" "${EXTRA[@]}" -o remote_agent agent/main.cpp agent/agent.cpp -pthread
    set +x
    ;;

  admin)
    echo "[BUILD] admin_client"
    set -x
    $CXX $CXXFLAGS "${DEFS[@]}" -o admin_client admin/main.cpp admin/admin_client.cpp -pthread
    set +x
    ;;

  *)
    usage
    ;;
esac

echo "[DONE] Built $TARGET"

