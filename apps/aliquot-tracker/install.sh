#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
INSTALL_DIR="/opt/aliquot-tracker"
BIN_DIR="$INSTALL_DIR/bin"
STATE_OWNER="${SUDO_USER:-$(whoami)}"

echo "[aliquot-tracker] Building libttak base library..."
cd "$REPO_ROOT"
make

echo "[aliquot-tracker] Building tracker binary..."
cd "$SCRIPT_DIR"
make clean
make

echo "[aliquot-tracker] Staging binary into $INSTALL_DIR ..."
sudo mkdir -p "$BIN_DIR"
sudo cp "$SCRIPT_DIR/bin/aliquot_tracker" "$BIN_DIR/"

# Ensure ownership so git operations run under the invoking user.
sudo chown -R "$STATE_OWNER":"$STATE_OWNER" "$INSTALL_DIR"

if [ ! -d "$INSTALL_DIR/.git" ]; then
    echo "[aliquot-tracker] Initializing git repo for checkpoint history..."
    sudo -u "$STATE_OWNER" git -C "$INSTALL_DIR" init
    if [ ! -f "$INSTALL_DIR/.gitignore" ]; then
        cat <<'GITEOF' | sudo -u "$STATE_OWNER" tee "$INSTALL_DIR/.gitignore" >/dev/null
*.o
*.tmp
*.log
bin/
GITEOF
    fi
fi

for ledger in "aliquot_found.jsonl" "aliquot_jump.jsonl" "aliquot_queue.json"; do
    sudo -u "$STATE_OWNER" touch "$INSTALL_DIR/$ledger"
done

cat <<EOFMSG
[aliquot-tracker] Deployment complete.
- Binary: $BIN_DIR/aliquot_tracker
- Ledger repo: $INSTALL_DIR (owned by $STATE_OWNER)
Edit or add a git remote under $INSTALL_DIR to sync ledgers, then run git pull/push
whenever you want to compare JSON diffs across systems.

Run manually:
  ALIQUOT_STATE_DIR=$INSTALL_DIR $BIN_DIR/aliquot_tracker
EOFMSG
