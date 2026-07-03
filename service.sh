#!/system/bin/sh
MODDIR=${0%/*}

# Wait for full boot
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 3
done

# One-shot: runs, applies, exits. No daemon.
"$MODDIR/bin/yay_apply" --boot

# Optional: start inotify watcher only if user explicitly enables it
# via sentinel file. Watcher sleeps in inotify_wait — near-zero overhead.
if [ -f "$MODDIR/enable_watch" ]; then
    "$MODDIR/bin/yay_watch" &
fi

# EEM (Extended Environment Monitor) undervolt offset.
# Backgrounded + delayed well past boot_completed.
# Not persisted anywhere by us — this only pokes a runtime kernel node.
# So a reboot (or module removal) alone reverts it to stock.
#(
#    sleep 60
#    [ -w /proc/eem/EEM_DET_B/eem_offset ]   && echo "-5" > /proc/eem/EEM_DET_B/eem_offset   2>/dev/null
#    [ -w /proc/eem/EEM_DET_L/eem_offset ]   && echo "-5" > /proc/eem/EEM_DET_L/eem_offset   2>/dev/null
#) &
#
exit 0
