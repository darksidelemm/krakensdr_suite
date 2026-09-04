#!/usr/bin/env bash
#
# install-ubuntu-service.sh - Headless systemd services for Ubuntu.
#
# Installs systemd units that start Heimdall and the KrakenSDR DoA client at
# boot without tmux, X11/Wayland autostart, VNC, HDMI, or Raspberry Pi tooling.
#
# Usage:
#   ./install-ubuntu-service.sh
#   APP_DIR=/opt/krakensdr_suite ./install-ubuntu-service.sh
#   KRAKEN_TUNERS=5 ./install-ubuntu-service.sh
#   ./install-ubuntu-service.sh --uninstall
#
# Runtime flags can be edited after install in:
#   /etc/default/krakensdr
#
set -euo pipefail

RUN_USER="${RUN_USER:-${SUDO_USER:-$USER}}"
RUN_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"
RUN_GROUP="$(id -gn "$RUN_USER")"
APP_DIR="${APP_DIR:-$RUN_HOME/krakensdr_suite}"
KRAKEN_TUNERS="${KRAKEN_TUNERS:-5}"

ENV_FILE=/etc/default/krakensdr
USB_WAIT=/usr/local/bin/kraken-wait-usb
TARGET=/etc/systemd/system/krakensdr.target
HEIMDALL_SVC=/etc/systemd/system/krakensdr-heimdall.service
DOA_SVC=/etc/systemd/system/krakensdr-doa.service
CHASEMAPPER_SVC=/etc/systemd/system/krakensdr-chasemapper.service
CHASEMAPPER_BRIDGE=kraken_doa_v2/tools/doa_value_to_chasemapper.py
PYTHON3="$(command -v python3 || true)"

say()  { echo -e "\033[36;1m==>\033[0m \033[1m$*\033[0m"; }
ok()   { echo -e "\033[32m   ok  $*\033[0m"; }
warn() { echo -e "\033[33m   !   $*\033[0m"; }
die()  { echo -e "\033[31;1mError:\033[0m $*" >&2; exit 1; }

if [[ "${1:-}" == "--uninstall" ]]; then
    sudo systemctl disable --now krakensdr.target 2>/dev/null || true
    sudo systemctl disable --now krakensdr-chasemapper.service 2>/dev/null || true
    sudo systemctl disable --now krakensdr-doa.service 2>/dev/null || true
    sudo systemctl disable --now krakensdr-heimdall.service 2>/dev/null || true
    sudo rm -f "$TARGET" "$HEIMDALL_SVC" "$DOA_SVC" "$CHASEMAPPER_SVC" "$USB_WAIT"
    sudo systemctl daemon-reload
    ok "Uninstalled Ubuntu headless services."
    exit 0
fi

[[ -n "$PYTHON3" ]] || die "python3 not found. Install Python 3 before installing the services."
[[ -x "$APP_DIR/heimdall_v2/heimdall" ]] ||
    die "Heimdall binary not found at $APP_DIR/heimdall_v2/heimdall. Run install.sh first, or set APP_DIR=."
[[ -x "$APP_DIR/kraken_doa_v2/kraken_doa" ]] ||
    die "DoA binary not found at $APP_DIR/kraken_doa_v2/kraken_doa. Run install.sh first, or set APP_DIR=."
[[ -f "$APP_DIR/$CHASEMAPPER_BRIDGE" ]] ||
    die "Chasemapper bridge not found at $APP_DIR/$CHASEMAPPER_BRIDGE."

say "Installing Ubuntu headless services"
echo "     user:    $RUN_USER"
echo "     group:   $RUN_GROUP"
echo "     app dir: $APP_DIR"

sudo install -d -m 0755 /etc/default
if [[ ! -f "$ENV_FILE" ]]; then
    sudo tee "$ENV_FILE" >/dev/null <<EOF
# KrakenSDR headless service configuration.
#
# Extra arguments passed to heimdall. Examples:
#   HEIMDALL_ARGS="--wideband"
#   HEIMDALL_ARGS="--kerberos"
#   HEIMDALL_ARGS="--kerberos_sw"
#   HEIMDALL_ARGS="-n 5 --serials 1000,1001,1002,1003,1004"
HEIMDALL_ARGS=""

# Extra arguments passed to kraken_doa. Use "--wideband" when heimdall also
# runs in wideband mode. Most KerberosSDR state is auto-detected from Heimdall.
KRAKEN_DOA_ARGS=""

# Number of RTL-SDR dongles to wait for before starting Heimdall.
KRAKEN_TUNERS=$KRAKEN_TUNERS

# Full stdout is noisy under systemd. Leave these unset for errors-focused
# journal logs, or uncomment either line for full startup/debug output.
#HEIMDALL_VERBOSE_LOG=1
#KRAKEN_DOA_VERBOSE_LOG=1
EOF
    ok "Created $ENV_FILE"
else
    ok "Keeping existing $ENV_FILE"
fi

sudo tee "$USB_WAIT" >/dev/null <<'EOF'
#!/usr/bin/env bash
want="${KRAKEN_TUNERS:-5}"
if ! command -v lsusb >/dev/null 2>&1; then
    echo "lsusb not found; skipping RTL-SDR enumeration wait" >&2
    exit 0
fi
for _ in $(seq 1 45); do
    n=$(lsusb | grep -ciE '0bda:(2838|2832)' || true)
    [ "$n" -ge "$want" ] && exit 0
    sleep 1
done
echo "only ${n:-0} of $want RTL-SDR tuners enumerated; starting anyway" >&2
exit 0
EOF
sudo chmod +x "$USB_WAIT"
ok "Installed $USB_WAIT"

sudo tee "$HEIMDALL_SVC" >/dev/null <<EOF
[Unit]
Description=KrakenSDR Heimdall coherent receiver
Documentation=file:$APP_DIR/heimdall_v2/README.md
Wants=network-online.target
After=network-online.target
PartOf=krakensdr.target

[Service]
Type=simple
User=$RUN_USER
Group=$RUN_GROUP
WorkingDirectory=$APP_DIR/heimdall_v2
Environment=HOME=$RUN_HOME
Environment=HEIMDALL_NO_TUI=1
EnvironmentFile=-$ENV_FILE
ExecStartPre=$USB_WAIT
ExecStart=$APP_DIR/heimdall_v2/heimdall \$HEIMDALL_ARGS
Restart=on-failure
RestartSec=10
TimeoutStartSec=120
TimeoutStopSec=30
KillSignal=SIGTERM

[Install]
WantedBy=krakensdr.target
EOF
ok "Wrote $(basename "$HEIMDALL_SVC")"

sudo tee "$DOA_SVC" >/dev/null <<EOF
[Unit]
Description=KrakenSDR DoA client
Documentation=file:$APP_DIR/kraken_doa_v2/README.md
Requires=krakensdr-heimdall.service
After=krakensdr-heimdall.service
PartOf=krakensdr.target

[Service]
Type=simple
User=$RUN_USER
Group=$RUN_GROUP
WorkingDirectory=$APP_DIR/kraken_doa_v2
Environment=HOME=$RUN_HOME
Environment=KRAKEN_DOA_NO_TUI=1
EnvironmentFile=-$ENV_FILE
ExecStartPre=/bin/sleep 3
ExecStart=$APP_DIR/kraken_doa_v2/kraken_doa \$KRAKEN_DOA_ARGS
Restart=on-failure
RestartSec=10
TimeoutStartSec=120
TimeoutStopSec=30
KillSignal=SIGTERM

[Install]
WantedBy=krakensdr.target
EOF
ok "Wrote $(basename "$DOA_SVC")"

sudo tee "$TARGET" >/dev/null <<'EOF'
[Unit]
Description=KrakenSDR headless stack
Wants=krakensdr-heimdall.service krakensdr-doa.service krakensdr-chasemapper.service
After=krakensdr-heimdall.service krakensdr-doa.service krakensdr-chasemapper.service

[Install]
WantedBy=multi-user.target
EOF
ok "Wrote $(basename "$TARGET")"

sudo tee "$CHASEMAPPER_SVC" >/dev/null <<EOF
[Unit]
Description=KrakenSDR Chasemapper UDP bridge
Documentation=file:$APP_DIR/$CHASEMAPPER_BRIDGE
Requires=krakensdr-doa.service
After=krakensdr-doa.service
PartOf=krakensdr.target

[Service]
Type=simple
User=$RUN_USER
Group=$RUN_GROUP
WorkingDirectory=$APP_DIR/kraken_doa_v2
Environment=HOME=$RUN_HOME
Environment=PYTHONUNBUFFERED=1
ExecStart=$PYTHON3 $APP_DIR/$CHASEMAPPER_BRIDGE
Restart=on-failure
RestartSec=10
TimeoutStartSec=120
TimeoutStopSec=30
KillSignal=SIGTERM

[Install]
WantedBy=krakensdr.target
EOF
ok "Wrote $(basename "$CHASEMAPPER_SVC")"

sudo systemctl daemon-reload
sudo systemctl enable krakensdr.target >/dev/null
ok "Enabled krakensdr.target"

cat <<EOF

Installed. Start now with:

  sudo systemctl start krakensdr.target

Check status/logs:

  systemctl status krakensdr.target krakensdr-heimdall krakensdr-doa krakensdr-chasemapper
  journalctl -u krakensdr-heimdall -u krakensdr-doa -u krakensdr-chasemapper -f

Web interfaces:

  Heimdall: http://<host>:8070
  DoA UI:   https://<host>:8080
  DoA CSV:  http://<host>:8081/DOA_value.html

Configure flags in:

  $ENV_FILE

Remove with:

  ./install-ubuntu-service.sh --uninstall

EOF
