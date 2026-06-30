#!/system/bin/sh
MODDIR=${0%/*}

# Wait for full boot
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 10
done

# One-shot: runs, applies, exits. No daemon.
"$MODDIR/bin/yay_apply" --boot

# Optional: start inotify watcher only if user explicitly enables it
# via sentinel file. Watcher sleeps in inotify_wait — near-zero overhead.
if [ -f "$MODDIR/enable_watch" ]; then
    "$MODDIR/bin/yay_watch" &
fi

exit 0
