#!/bin/sh

# Network fetcher for Hyperion (SFNNv16)
# Copyright (C) 2026 Claudio M. Camacho <claudiomkd@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

NET_FILE="nn-1a298aa575a0.nnue"
if [ -n "$1" ]; then
  NET_FILE="$1"
fi

wget_or_curl=$( (command -v curl >/dev/null 2>&1 && echo "curl -skL --max-time 300") || \
  (command -v wget >/dev/null 2>&1 && echo "wget -qO- --timeout=300 --tries=1") )

sha256sum=$( (command -v shasum >/dev/null 2>&1 && echo "shasum -a 256") || \
  (command -v sha256sum >/dev/null 2>&1 && echo "sha256sum") )

validate_network() {
  if [ -n "$sha256sum" ] && [ -f "$1" ]; then
    hash_prefix=$($sha256sum "$1" | cut -c 1-12)
    expected_prefix=$(echo "$1" | sed -n 's/.*nn-\([0-9a-f]\{12\}\)\.nnue/\1/p')
    if [ -n "$expected_prefix" ] && [ "$hash_prefix" != "$expected_prefix" ]; then
      echo "Checksum mismatch for $1: got $hash_prefix, expected $expected_prefix"
      rm -f "$1"
      return 1
    fi
  fi
  return 0
}

if [ -f "$NET_FILE" ]; then
  if validate_network "$NET_FILE"; then
    echo "NNUE network $NET_FILE validated, skipping download."
    exit 0
  else
    echo "Removed corrupted $NET_FILE, re-downloading..."
  fi
fi

if [ -z "$wget_or_curl" ]; then
  echo "Error: neither curl nor wget found in PATH. Please install curl or wget." >&2
  exit 1
fi

for url in \
  "https://tests.stockfishchess.org/api/nn/$NET_FILE" \
  "https://github.com/official-stockfish/networks/raw/master/$NET_FILE"; do
  echo "Downloading $NET_FILE from $url ..."
  if $wget_or_curl "$url" > "$NET_FILE"; then
    if validate_network "$NET_FILE"; then
      echo "Successfully downloaded and validated $NET_FILE"
      exit 0
    else
      rm -f "$NET_FILE"
    fi
  else
    rm -f "$NET_FILE"
  fi
done

echo "Error: failed to download $NET_FILE" >&2
exit 1
