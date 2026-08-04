#!/bin/bash
#
# install-service.sh - Run the KrakenSDR suite automatically at boot.
#
#   * systemd starts run.sh at boot (detached tmux session, no attach)
#   * a single desktop autostart opens ONE terminal viewing that session
#   * on a Lite/console image, tty1 autologin attaches instead
#
# Safe to re-run; every step is idempotent.
#
# Usage:
#   ./install-service.sh
#   VARIANT_FLAGS=--wideband ./install-service.sh
#   VARIANT_FLAGS=--kerberos_sw KRAKEN_TUNERS=4 ./install-service.sh
#   APP_DIR=/opt/krakensdr_suite ./install-service.sh
#   ./install-service.sh --uninstall
#
set -euo pipefail
 
SESSION="${TMUX_SESSION:-krakensdr}"
VARIANT_FLAGS="${VARIANT_FLAGS:-}"
KRAKEN_TUNERS="${KRAKEN_TUNERS:-5}"
WAIT_TIMEOUT="${WAIT_TIMEOUT:-600}"
 
RUN_USER="${SUDO_USER:-$USER}"
RUN_HOME="$(getent passwd "$RUN_USER" | cut -d: -f6)"
APP_DIR="${APP_DIR:-$RUN_HOME/krakensdr_suite}"
RUN_SH="$APP_DIR/run.sh"
SVC=/etc/systemd/system/krakensdr.service
 
say()  { echo -e "\033[36;1m==>\033[0m \033[1m$*\033[0m"; }
ok()   { echo -e "\033[32m   ok  $*\033[0m"; }
warn() { echo -e "\033[33m   !   $*\033[0m"; }
die()  { echo -e "\033[31;1mError:\033[0m $*" >&2; exit 1; }
asuser() { sudo -u "$RUN_USER" "$@"; }
 
# ---------------------------------------------------------------- uninstall --
if [[ "${1:-}" == "--uninstall" ]]; then
    sudo systemctl disable --now krakensdr.service 2>/dev/null || true
    sudo rm -f "$SVC" /usr/local/bin/kraken-term /usr/local/bin/kraken-wait-usb
    rm -f "$RUN_HOME/.config/autostart/kraken-term.desktop"
    sed -i '/kraken-term/d' "$RUN_HOME/.config/labwc/autostart" 2>/dev/null || true
    sed -i '/kraken-term/d' "$RUN_HOME/.config/wayfire.ini" 2>/dev/null || true
    sed -i '/# --- KrakenSDR console attach/,/# --- end KrakenSDR/d' "$RUN_HOME/.bash_profile" 2>/dev/null || true
    sudo systemctl daemon-reload
    ok "Uninstalled."
    exit 0
fi
 
# -------------------------------------------------------------- preflight ----
[[ -f "$RUN_SH" ]] || die "run.sh not found at $RUN_SH (set APP_DIR= to override)"
chmod +x "$RUN_SH"
 
say "User $RUN_USER, app dir $APP_DIR, session '$SESSION'"
sudo apt-get install -y tmux lxterminal >/dev/null
ok "tmux + lxterminal present"
 
# ------------------------------------------------------- USB enumeration ------
# multi-user.target can be reached before all dongles enumerate on a cold boot.
sudo tee /usr/local/bin/kraken-wait-usb >/dev/null <<EOF
#!/bin/bash
WANT="\${KRAKEN_TUNERS:-$KRAKEN_TUNERS}"
for i in \$(seq 1 45); do
    n=\$(lsusb | grep -ciE '0bda:(2838|2832)' || true)
    [ "\$n" -ge "\$WANT" ] && exit 0
    sleep 1
done
echo "only \$n of \$WANT tuners enumerated; starting anyway" >&2
exit 0
EOF
sudo chmod +x /usr/local/bin/kraken-wait-usb
 
# ------------------------------------------------------------ systemd unit ---
sudo tee "$SVC" >/dev/null <<EOF
[Unit]
Description=KrakenSDR Suite
After=network-online.target
Wants=network-online.target
 
[Service]
Type=forking
GuessMainPID=yes
User=$RUN_USER
Group=$RUN_USER
WorkingDirectory=$APP_DIR
Environment=HOME=$RUN_HOME
Environment=TMUX_TMPDIR=/tmp
Environment=TMUX_SESSION=$SESSION
Environment=NO_ATTACH=1
Environment=WAIT_TIMEOUT=$WAIT_TIMEOUT
Environment=KRAKEN_TUNERS=$KRAKEN_TUNERS
Environment=PYTHONUNBUFFERED=1
ExecStartPre=/usr/local/bin/kraken-wait-usb
ExecStart=$RUN_SH $VARIANT_FLAGS
ExecStop=$RUN_SH stop
Restart=on-failure
RestartSec=15
TimeoutStartSec=120
TimeoutStopSec=30
 
[Install]
WantedBy=multi-user.target
EOF
ok "unit written"
 
# ------------------------------------------------------- viewer (one only) ---
# flock covers the race where two launchers fire before either has attached;
# list-clients covers the ordinary case; attach -d is the final backstop.
sudo tee /usr/local/bin/kraken-term >/dev/null <<EOF
#!/bin/bash
export TMUX_TMPDIR=/tmp
SESSION="\${TMUX_SESSION:-$SESSION}"
 
exec 9>/tmp/kraken-term.lock
flock -n 9 || exit 0
 
for i in \$(seq 1 90); do
    tmux has-session -t "\$SESSION" 2>/dev/null && break
    sleep 1
done
tmux has-session -t "\$SESSION" 2>/dev/null || { echo "no '\$SESSION' session"; exit 1; }
 
tmux list-clients -t "\$SESSION" 2>/dev/null | grep -q . && exit 0
 
lxterminal --title="KrakenSDR" --geometry=150x45 -e "tmux attach -d -t \$SESSION" &
sleep 3
EOF
sudo chmod +x /usr/local/bin/kraken-term
 
# ------------------------------------------------------ autostart: pick ONE ---
# Bookworm labwc reads BOTH ~/.config/labwc/autostart and the XDG dir, so
# installing to every mechanism opens two windows on the same session.
asuser mkdir -p "$RUN_HOME/.config"
HAS_DESKTOP=0
dpkg -s raspberrypi-ui-mods >/dev/null 2>&1 && HAS_DESKTOP=1
[[ -d "$RUN_HOME/.config/labwc" || -f "$RUN_HOME/.config/wayfire.ini" ]] && HAS_DESKTOP=1
 
rm -f "$RUN_HOME/.config/autostart/kraken-term.desktop"
sed -i '/kraken-term/d' "$RUN_HOME/.config/labwc/autostart" 2>/dev/null || true
sed -i '/kraken-term/d' "$RUN_HOME/.config/wayfire.ini" 2>/dev/null || true
 
if [[ "$HAS_DESKTOP" == "1" ]]; then
    if [[ -d "$RUN_HOME/.config/labwc" ]] || command -v labwc >/dev/null 2>&1; then
        asuser mkdir -p "$RUN_HOME/.config/labwc"
        L="$RUN_HOME/.config/labwc/autostart"
        asuser touch "$L"
        echo "/usr/local/bin/kraken-term &" | asuser tee -a "$L" >/dev/null
        asuser chmod +x "$L"
        ok "autostart: labwc"
    elif [[ -f "$RUN_HOME/.config/wayfire.ini" ]]; then
        W="$RUN_HOME/.config/wayfire.ini"
        grep -q '^\[autostart\]' "$W" \
            && asuser sed -i '/^\[autostart\]/a kraken = /usr/local/bin/kraken-term' "$W" \
            || printf '\n[autostart]\nkraken = /usr/local/bin/kraken-term\n' | asuser tee -a "$W" >/dev/null
        ok "autostart: wayfire"
    else
        asuser mkdir -p "$RUN_HOME/.config/autostart"
        asuser tee "$RUN_HOME/.config/autostart/kraken-term.desktop" >/dev/null <<'EOF'
[Desktop Entry]
Type=Application
Name=KrakenSDR Terminal
Exec=/usr/local/bin/kraken-term
Terminal=false
EOF
        ok "autostart: XDG"
    fi
fi
 
# ------------------------------------------------ console fallback (no GUI) ---
P="$RUN_HOME/.bash_profile"
asuser touch "$P"
sed -i '/# --- KrakenSDR console attach/,/# --- end KrakenSDR/d' "$P" 2>/dev/null || true
if [[ "$HAS_DESKTOP" == "0" ]]; then
    asuser tee -a "$P" >/dev/null <<EOF
 
# --- KrakenSDR console attach
if [ "\$(tty)" = "/dev/tty1" ] && [ -z "\${TMUX:-}" ]; then
    export TMUX_TMPDIR=/tmp
    for i in \$(seq 1 90); do tmux has-session -t $SESSION 2>/dev/null && break; sleep 1; done
    tmux attach -d -t $SESSION
fi
# --- end KrakenSDR
EOF
    ok "console fallback on tty1"
fi
 
# ------------------------------------------------------------- tmux config ---
T="$RUN_HOME/.tmux.conf"
asuser touch "$T"
grep -q aggressive-resize "$T" || echo "set -g aggressive-resize on" | asuser tee -a "$T" >/dev/null
 
# ----------------------------------------------------------------- autologin --
if [[ "$HAS_DESKTOP" == "1" ]]; then
    sudo raspi-config nonint do_boot_behaviour B4 2>/dev/null || warn "set desktop autologin manually"
else
    sudo raspi-config nonint do_boot_behaviour B2 2>/dev/null || warn "set console autologin manually"
fi
 
# ---------------------------------------------------------------- Pi 5 HDMI ---
CMD=/boot/firmware/cmdline.txt
[[ -f "$CMD" ]] || CMD=/boot/cmdline.txt
if [[ -f "$CMD" ]] && ! grep -q 'video=HDMI-A-1' "$CMD"; then
    warn "No forced HDMI mode. On a headless boot the desktop has no output, so"
    warn "a later-plugged monitor or VNC shows nothing. To force it:"
    warn "  sudo sed -i '1s|\$| video=HDMI-A-1:1920x1080@60D|' $CMD"
fi
 
sudo systemctl daemon-reload
sudo systemctl enable krakensdr.service >/dev/null
ok "service enabled"
 
cat <<EOF
 
  Installed. Reboot to start on boot:
 
    sudo reboot
 
  Check after reboot:
    systemctl status krakensdr           # active (running)
    journalctl -u krakensdr -b
 
  Operate:
    sudo systemctl {start,stop,restart} krakensdr
    kraken-term                          # open a viewer window
    tmux attach -d -t $SESSION      # attach from SSH
 
  While the service is running, use systemctl / kraken-term rather than
  running run.sh by hand.
 
  Remove with:  ./install-service.sh --uninstall
 
EOF
