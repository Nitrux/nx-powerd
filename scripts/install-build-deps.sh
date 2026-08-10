#!/usr/bin/env bash
set -e
if [ "$EUID" -ne 0 ]; then APT_COMMAND="sudo apt"; else APT_COMMAND="apt"; fi
$APT_COMMAND update -q
$APT_COMMAND install -y --no-install-recommends build-essential cmake dpkg-dev
