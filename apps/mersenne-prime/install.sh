#!/bin/bash

# Senior Systems Engineer Deployment Script for Mersenne Prime Searcher
# Target: Intel N150 Mini PC (Low Power Optimization)
set -e

echo "Starting deployment of libttak and Mersenne Prime Searcher..."

# 1. Build and install libttak
cd ../..
make clean
make
sudo make install
cd apps/mersenne-prime

# 2. Compile the application
echo "Compiling Mersenne Prime Searcher with GIMPS integration..."
make clean
make

# 3. Configure systemd service
echo "Configuring systemd service (Nice=19, IDLE policy)..."
cat <<EOF | sudo tee /etc/systemd/system/mersenne-search.service
[Unit]
Description=TTAK Mersenne Prime Searcher (GIMPS)
After=network.target

[Service]
Type=simple
ExecStart=$(pwd)/bin/mersenne_searcher
WorkingDirectory=$(pwd)
Restart=always
# Optimization for N150 background tasks
Nice=19
CPUSchedulingPolicy=idle
StandardOutput=append:$(pwd)/searcher.log
StandardError=append:$(pwd)/searcher.err

[Install]
WantedBy=multi-user.target
EOF

# 4. Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable mersenne-search.service
sudo systemctl start mersenne-search.service

echo "Deployment complete. Application is running in background (Nice=19, IDLE)."
echo "Monitor progress with: tail -f $(pwd)/searcher.log"