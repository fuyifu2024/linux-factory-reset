#!/bin/sh
# CI ONLY: private, disposable container; never run on the user's real host.
set -eu
app=$1
fixture=$(mktemp -d)
server=''
cleanup() { [ -z "$server" ] || kill "$server" 2>/dev/null || :; rm -rf "$fixture"; }
trap cleanup EXIT HUP INT TERM
mkdir -p "$fixture/26.04"
printf 'tiny synthetic ISO fixture, never bootable\n' >"$fixture/26.04/ubuntu-26.04.1-desktop-amd64.iso"
(cd "$fixture/26.04" && sha256sum ./*.iso | sed 's@ \./@ @' > SHA256SUMS)
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj '/CN=releases.ubuntu.com' -addext 'subjectAltName=DNS:releases.ubuntu.com' -keyout "$fixture/key" -out "$fixture/cert" >/dev/null 2>&1
cp "$fixture/cert" /usr/local/share/ca-certificates/linux-reset-fixture.crt
update-ca-certificates >/dev/null 2>&1
printf '\n127.0.0.1 releases.ubuntu.com\n' >>/etc/hosts
(cd "$fixture" && exec openssl s_server -quiet -accept 443 -cert cert -key key -WWW) >"$fixture/tls.log" 2>&1 &
server=$!
sleep 1
# Explicitly bypass CI proxy variables: this test MUST reach only the loopback fixture.
HTTPS_PROXY= HTTP_PROXY= ALL_PROXY= https_proxy= http_proxy= all_proxy= "$app" "$fixture"
