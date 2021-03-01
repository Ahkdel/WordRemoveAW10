#!/bin/sh
PKG_CONFIG_SYSROOT_DIR=/opt/fslc-framebuffer/2.5/sysroots/armv7at2hf-neon-fslc-linux-gnueabi
export PKG_CONFIG_SYSROOT_DIR
PKG_CONFIG_LIBDIR=/opt/fslc-framebuffer/2.5/sysroots/armv7at2hf-neon-fslc-linux-gnueabi/usr/lib/pkgconfig:/opt/fslc-framebuffer/2.5/sysroots/armv7at2hf-neon-fslc-linux-gnueabi/usr/share/pkgconfig:/opt/fslc-framebuffer/2.5/sysroots/armv7at2hf-neon-fslc-linux-gnueabi/usr/lib/arm-fslc-linux-gnueabi/pkgconfig
export PKG_CONFIG_LIBDIR
exec pkg-config "$@"
