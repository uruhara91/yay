#!/system/bin/sh
# Runs during Magisk module installation (flash time).
# Sets up directory structure and runs a full first-time apply.

MODDIR="$MODPATH"
YAY_DATA="/data/adb/yay"
CFG_DIR="$YAY_DATA/config"
CACHE_DIR="$YAY_DATA/cache"

ui_print "- yay: setting up data directories..."
mkdir -p "$CFG_DIR" "$CACHE_DIR"
chmod 700 "$YAY_DATA" "$CFG_DIR" "$CACHE_DIR"

# Copy default configs only if not already present (preserve user edits on update)
for f in rules.json game_config.json io_config.json; do
    if [ ! -f "$CFG_DIR/$f" ]; then
        if [ -f "$MODDIR/config/$f" ]; then
            cp "$MODDIR/config/$f" "$CFG_DIR/$f"
            ui_print "  installed default $f"
        fi
    else
        ui_print "  keeping existing $f"
    fi
done

# Mark binary as executable
chmod 755 "$MODDIR/bin/yay_apply"
chmod 755 "$MODDIR/bin/yay_watch"

ui_print "- yay: running full first-time apply..."
"$MODDIR/bin/yay_apply" --full --moddir "$MODDIR"

ui_print "- yay: done."
