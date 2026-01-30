#!/bin/bash

# Senior Systems Engineer Deployment Script for Mersenne Prime Searcher
# Target: Intel N150 Mini PC (Low Power Optimization)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
INSTALL_DIR="/opt/libttak-mersenne"
SERVICE_FILE="/etc/systemd/system/mersenne-search.service"

echo "Starting deployment of libttak and Mersenne Prime Searcher..."

# 1. Build and install libttak
echo "Building libttak from ${REPO_ROOT}..."
cd "$REPO_ROOT"
make clean
make
sudo make install

# 2. Compile the application inside the source tree
echo "Compiling Mersenne Prime Searcher with GIMPS integration..."
cd "$SCRIPT_DIR"
make clean
make

# 3. Stage application under /opt
echo "Staging application into ${INSTALL_DIR}..."
sudo rm -rf "$INSTALL_DIR"
sudo mkdir -p "$INSTALL_DIR"
sudo cp -r "$SCRIPT_DIR/." "$INSTALL_DIR/"

# 4. Configure systemd service pointing at /opt
echo "Configuring systemd service (Nice=19, IDLE policy)..."
cat <<EOF | sudo tee "$SERVICE_FILE"
[Unit]
Description=TTAK Mersenne Prime Searcher (GIMPS)
After=network.target

[Service]
Type=simple
ExecStart=${INSTALL_DIR}/bin/mersenne_searcher
WorkingDirectory=${INSTALL_DIR}
Restart=always
# Optimization for N150 background tasks
Nice=19
CPUSchedulingPolicy=idle
StandardOutput=append:${INSTALL_DIR}/searcher.log
StandardError=append:${INSTALL_DIR}/searcher.err

[Install]
WantedBy=multi-user.target
EOF

# 5. Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable mersenne-search.service
sudo systemctl start mersenne-search.service

echo "Deployment complete. Application is running in background (Nice=19, IDLE)."
echo "Monitor progress with: tail -f ${INSTALL_DIR}/searcher.log"
