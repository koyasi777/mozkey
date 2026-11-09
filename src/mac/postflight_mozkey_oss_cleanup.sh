# Mozkey OSS-only postinstall tail. Appended after upstream postflight.sh.
# Remove only the known non-packaged arm64 fastbuild renderer_style.inc left by
# earlier development builds. Keep the Mozc.app path and its IME registration.
# Quarantine first; verify the actual installed app signature before committing.

_mozkey_app='/Library/Input Methods/Mozc.app'
_mozkey_renderer="$_mozkey_app/Contents/Resources/MozcRenderer.app"
_mozkey_res="$_mozkey_renderer/Contents/Resources"
_mozkey_old_tag='darwin_arm64-fastbuild-macos-arm64-min12.0-ST-fea79f7fcfbc'
_mozkey_old_dir="$_mozkey_res/bazel-out/$_mozkey_old_tag"
_mozkey_old_file="$_mozkey_old_dir/bin/renderer/renderer_style.inc"

if [ -e "$_mozkey_old_file" ] || [ -L "$_mozkey_old_file" ]; then
  # Do not follow symlinks anywhere along a root-operated removal path.
  for _mozkey_path in \
    "$_mozkey_app" "$_mozkey_app/Contents" \
    "$_mozkey_app/Contents/Resources" "$_mozkey_renderer" \
    "$_mozkey_renderer/Contents" "$_mozkey_res" \
    "$_mozkey_res/bazel-out" "$_mozkey_old_dir" \
    "$_mozkey_old_dir/bin" "$_mozkey_old_dir/bin/renderer"; do
    if [ -L "$_mozkey_path" ] || [ ! -d "$_mozkey_path" ]; then
      echo "Mozkey postinstall: unexpected directory or symlink: $_mozkey_path" >&2
      exit 1
    fi
  done
  if [ ! -f "$_mozkey_old_file" ] || [ -L "$_mozkey_old_file" ]; then
    echo 'Mozkey postinstall: unexpected stale file type' >&2
    exit 1
  fi

  _mozkey_renderer_id=$(/usr/libexec/PlistBuddy \
    -c 'Print :CFBundleIdentifier' \
    "$_mozkey_renderer/Contents/Info.plist" 2>/dev/null) || exit 1
  if [ "$_mozkey_renderer_id" != 'org.mozc.inputmethod.Japanese.Renderer' ]; then
    echo 'Mozkey postinstall: renderer identifier mismatch' >&2
    exit 1
  fi

  # A legitimately signed future payload can include this file. Do nothing
  # in that case, and check the enclosing app at the end as usual.
  if ! /usr/bin/codesign --verify --deep --strict "$_mozkey_app" >/dev/null 2>&1; then
    # Fail closed on any unexpected additional contents within the legacy tag.
    _mozkey_actual_tree=$(/usr/bin/find "$_mozkey_old_dir" -print | /usr/bin/sort) || exit 1
    _mozkey_expected_tree=$(/usr/bin/printf '%s\n' \
      "$_mozkey_old_dir" "$_mozkey_old_dir/bin" \
      "$_mozkey_old_dir/bin/renderer" "$_mozkey_old_file" | /usr/bin/sort)
    if [ "$_mozkey_actual_tree" != "$_mozkey_expected_tree" ]; then
      echo 'Mozkey postinstall: unexpected contents in legacy build directory' >&2
      exit 1
    fi

    _mozkey_quarantine_dir=$(/usr/bin/mktemp -d '/private/tmp/mozkey-renderer-cleanup.XXXXXXXX') || exit 1
    _mozkey_quarantine_file="$_mozkey_quarantine_dir/renderer_style.inc"
    _mozkey_recovery_pending=yes

    _mozkey_restore_on_exit() {
      if [ "$_mozkey_recovery_pending" = yes ]; then
        if [ -f "$_mozkey_quarantine_file" ] && [ ! -L "$_mozkey_quarantine_file" ]; then
          if [ -e "$_mozkey_old_file" ] || [ -L "$_mozkey_old_file" ]; then
            echo 'Mozkey postinstall: CRITICAL: cannot restore stale file; destination occupied' >&2
          elif /bin/mkdir -p "$_mozkey_old_dir/bin/renderer" && \
              /bin/mv "$_mozkey_quarantine_file" "$_mozkey_old_file"; then
            echo 'Mozkey postinstall: signature not recovered; legacy file restored' >&2
          else
            echo "Mozkey postinstall: CRITICAL: restore failed; quarantined file at $_mozkey_quarantine_file" >&2
          fi
        elif [ ! -f "$_mozkey_old_file" ]; then
          echo 'Mozkey postinstall: CRITICAL: stale file missing from both locations' >&2
        fi
      fi
      /bin/rmdir "$_mozkey_quarantine_dir" 2>/dev/null || true
    }

    # Restoration applies to command failures and interrupted execution.
    trap '_mozkey_restore_on_exit' 0
    trap 'exit 1' 1 2 3 15

    /bin/mv "$_mozkey_old_file" "$_mozkey_quarantine_file" || exit 1
    /bin/rmdir "$_mozkey_old_dir/bin/renderer" || exit 1
    /bin/rmdir "$_mozkey_old_dir/bin" || exit 1
    /bin/rmdir "$_mozkey_old_dir" || exit 1

    # The signature outcome, not codesign's diagnostic text, is authoritative.
    # Verify the entire installed app, including the nested renderer.
    if ! /usr/bin/codesign --verify --deep --strict "$_mozkey_app"; then
      echo 'Mozkey postinstall: signature still invalid after quarantine' >&2
      exit 1
    fi

    # Installed app is valid without the legacy file; commit the migration.
    _mozkey_recovery_pending=no
    trap - 0 1 2 3 15
    /bin/rm "$_mozkey_quarantine_file" || exit 1
    /bin/rmdir "$_mozkey_quarantine_dir" || exit 1
    echo 'Mozkey postinstall: one known legacy renderer resource removed; signature valid'
  fi
fi

# Do not report a successful install if any other stale content breaks signing.
if ! /usr/bin/codesign --verify --deep --strict "$_mozkey_app"; then
  echo 'Mozkey postinstall: installed Mozc.app failed codesign verification' >&2
  exit 1
fi

# Enable the newly installed input source only after the app signature passes.
# Do not prompt or switch input sources during CLI installs or upgrades where
# Mozkey is already enabled (the executable checks this via TIS).
# An unavailable desktop session must not fail the package installation.
if [ "${COMMAND_LINE_INSTALL:-}" != '1' ]; then
  _mozkey_console_user=$(/usr/bin/stat -f%Su /dev/console 2>/dev/null) || _mozkey_console_user=''
  if [ -n "$_mozkey_console_user" ] && [ "$_mozkey_console_user" != 'root' ] && \
     [ "$_mozkey_console_user" != 'loginwindow' ]; then
    /usr/bin/sudo -u "$_mozkey_console_user" \
      "$_mozkey_app/Contents/MacOS/Mozc" --register_input_source >/dev/null 2>&1 || true
  fi
fi
exit 0
