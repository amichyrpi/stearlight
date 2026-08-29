#!/usr/bin/env bash
set -euo pipefail

if [[ $(id -u) -ne 0 ]]; then
  echo "Run this on the Raspberry Pi as root." >&2
  exit 1
fi
command -v rpi-eeprom-config >/dev/null 2>&1 || {
  echo "rpi-eeprom-config is required; install the Raspberry Pi rpi-eeprom package." >&2
  exit 1
}

config_file=$(mktemp)
trap 'rm -f "$config_file"' EXIT
rpi-eeprom-config > "$config_file"

set_option() {
  local key=$1 value=$2
  if grep -qE "^[#[:space:]]*${key}=" "$config_file"; then
    sed -i -E "s|^[#[:space:]]*${key}=.*|${key}=${value}|" "$config_file"
  else
    printf '%s=%s\n' "$key" "$value" >> "$config_file"
  fi
}

set_option DISABLE_HDMI 1
set_option BOOT_UART 0
echo "Scheduling this EEPROM configuration for the next reboot:"
grep -E '^(DISABLE_HDMI|BOOT_UART)=' "$config_file"
rpi-eeprom-config --apply "$config_file"
echo "EEPROM update scheduled. Reboot once before changing the OS image."
