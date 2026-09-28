#!/usr/bin/env bash
#
# Security boundary scan (spec §14 and the project's hard security rules).
#
# Fails the build if the first-party sources (src/, excluding third_party/) use
# any API that would cross a boundary the product promises never to cross:
#   * network / cloud / telemetry
#   * input injection into other apps
#   * code injection into other processes
#   * privilege escalation
#   * loading external code
#
# This is a guard rail, not a substitute for review: it makes an accidental
# regression (someone adds a socket) fail CI loudly.
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

# Regexes for forbidden API usage. Word-boundaried where sensible.
patterns=(
  # Networking
  'WSAStartup' '\bsocket\s*\(' '\bconnect\s*\(' '\bsend\s*\(' '\brecv\s*\('
  'gethostby' 'getaddrinfo' 'InternetOpen' 'InternetConnect' 'HttpOpenRequest'
  'HttpSendRequest' 'WinHttp' 'URLDownloadToFile' 'URLOpenStream'
  'winsock' 'ws2_32' 'wininet' 'winhttp'
  # Input injection into other apps
  '\bSendInput\s*\(' '\bkeybd_event\s*\(' '\bmouse_event\s*\('
  # Code injection into other processes
  'CreateRemoteThread' 'WriteProcessMemory' 'VirtualAllocEx' 'QueueUserAPC'
  'SetThreadContext' 'NtMapViewOfSection'
  # Privilege escalation
  'AdjustTokenPrivileges' 'requireAdministrator' 'highestAvailable'
  'ShellExecute.*runas' 'SeDebugPrivilege'
  # Loading external code
  'LoadLibrary' 'GetProcAddress' 'CoCreateInstance'
)

fail=0
for p in "${patterns[@]}"; do
  # Search only first-party source; skip the vendored amalgamation.
  if matches="$(grep -rnE "$p" src/ 2>/dev/null)"; then
    echo "FORBIDDEN pattern '$p' found:" >&2
    echo "$matches" >&2
    fail=1
  fi
done

if [[ "$fail" -ne 0 ]]; then
  echo "" >&2
  echo "Security scan FAILED: first-party code must not cross these boundaries." >&2
  exit 1
fi

echo "Security scan passed: no forbidden network/injection/elevation APIs in src/."
