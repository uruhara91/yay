#!/system/bin/sh
MODDIR=${0%/*}

# Runs early, before userspace. Keep it fast.
"$MODDIR/bin/yay_apply" --post

exit 0
