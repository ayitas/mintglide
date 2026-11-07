#!/usr/bin/env bash
set -euo pipefail

RULE_FILE="/etc/udev/rules.d/70-mintglide-trackpad.rules"
SOURCE_RULE="$(dirname -- "$0")/../udev/70-mintglide-trackpad.rules"
VENDOR_RULE=""
LEGACY_RULES=(
    "/etc/udev/rules.d/99-mintglide-trackpad.rules"
    "/etc/udev/rules.d/99-mac-trackpad.rules"
)

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run this script with sudo: sudo $0" >&2
    exit 1
fi

for candidate in \
    "/usr/lib/udev/rules.d/70-mintglide-trackpad.rules" \
    "/usr/local/lib/udev/rules.d/70-mintglide-trackpad.rules"; do
    if [[ -f "${candidate}" ]]; then
        VENDOR_RULE="${candidate}"
        break
    fi
done

for legacy_rule in "${LEGACY_RULES[@]}"; do
    if [[ -f "${legacy_rule}" ]]; then
        rm -- "${legacy_rule}"
    fi
done

if [[ -f "${SOURCE_RULE}" ]]; then
    install -D -m 0644 "${SOURCE_RULE}" "${RULE_FILE}"
    echo "Installed ${RULE_FILE}."
elif [[ -n "${VENDOR_RULE}" ]]; then
    rm -f -- "${RULE_FILE}"
    echo "Using installed Mintglide udev rule: ${VENDOR_RULE}."
else
    echo "Cannot find the Mintglide udev rule next to this script." >&2
    echo "Reinstall Mintglide or run this script from the source tree." >&2
    exit 1
fi

udevadm control --reload-rules
udevadm trigger --subsystem-match=input

echo "Access is granted to the active local desktop user by logind uaccess."
echo "Log out and back in, or reconnect the trackpad, if access is not available yet."
