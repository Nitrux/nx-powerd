# nx-powerd

A background daemon that unifies Nitrux dynamic power-profile management and battery notifications. It installs as `/usr/bin/nx-powerd`.

Build and package it with `scripts/build-deb.sh`. The generated build tree also provides `cmake --build build --target uninstall`.

The x86_64 build enables `-march=x86-64-v3`; other host architectures remain portable.
