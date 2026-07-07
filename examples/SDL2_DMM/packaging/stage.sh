#!/usr/bin/env bash
set -euo pipefail

install -D -m 0755 olb/psu_app "$STAGE$APP_INSTALL_DIR/psu_app"

# Launcher wrapper:
#  1. Pick the SDL video driver (Wayland -> kmsdrm -> offscreen), same probe
#     as the other SDL2 examples (see SDL2_HelloWorld for rationale).
#  2. Run Open LabBench with a DMM driver + the 320x170 dmm-compact view.
#     Defaults to the synthetic demo driver so it works with no hardware.
#     Override via env to talk to a real meter, e.g.:
#       DMM_DRIVER=owon-xdm       DMM_PORT=/dev/ttyUSB0
#       DMM_DRIVER=keysight-34461a DMM_PORT=usbtmc:/dev/usbtmc0
#       DMM_DRIVER=hp-3458a        DMM_PORT=prologix:/dev/ttyUSB0:22
cat >"$STAGE$INSTALL_PREFIX/bin/$PKG_NAME" <<EOF
#!/bin/sh
LOG=/tmp/$PKG_NAME.log
: >"\$LOG" 2>/dev/null || LOG=/dev/null

if [ -z "\${XDG_RUNTIME_DIR:-}" ]; then
    _uid=\$(id -u 2>/dev/null || echo 1000)
    if [ -d "/run/user/\$_uid" ]; then
        XDG_RUNTIME_DIR="/run/user/\$_uid"
    elif [ -d "/run/user/1000" ]; then
        XDG_RUNTIME_DIR="/run/user/1000"
    fi
    [ -n "\$XDG_RUNTIME_DIR" ] && export XDG_RUNTIME_DIR
fi

_wl_ok=0
if [ -n "\${WAYLAND_DISPLAY:-}" ] && [ -n "\${XDG_RUNTIME_DIR:-}" ] && \\
   [ -S "\$XDG_RUNTIME_DIR/\$WAYLAND_DISPLAY" ]; then
    _wl_ok=1
elif [ -n "\${XDG_RUNTIME_DIR:-}" ]; then
    for _c in wayland-0 wayland-1; do
        if [ -S "\$XDG_RUNTIME_DIR/\$_c" ]; then
            WAYLAND_DISPLAY=\$_c
            export WAYLAND_DISPLAY
            _wl_ok=1
            break
        fi
    done
fi

if [ -z "\${SDL_VIDEODRIVER:-}" ]; then
    if [ "\$_wl_ok" = 1 ]; then
        SDL_VIDEODRIVER=wayland
    elif [ -e /dev/dri/card0 ]; then
        SDL_VIDEODRIVER=kmsdrm
    else
        SDL_VIDEODRIVER=offscreen
    fi
    export SDL_VIDEODRIVER
fi

# DMM selection (env-overridable; defaults to the synthetic demo driver).
DRIVER="\${DMM_DRIVER:-dmm-demo}"
VIEW="\${DMM_VIEW:-dmm-compact}"
PORT="\${DMM_PORT:--}"
set -- --driver="\$DRIVER" --view="\$VIEW" --port="\$PORT"
[ -n "\${DMM_BAUD:-}" ] && set -- "\$@" --baud="\$DMM_BAUD"

echo "[$PKG_NAME] driver=\$SDL_VIDEODRIVER dmm=\$DRIVER view=\$VIEW port=\$PORT uid=\$(id -u)" >>"\$LOG" 2>&1
exec $APP_INSTALL_DIR/psu_app "\$@" >>"\$LOG" 2>&1
EOF
chmod 0755 "$STAGE$INSTALL_PREFIX/bin/$PKG_NAME"
