#!/usr/bin/python3
# AI-assisted with Amp. SPDX-License-Identifier: LGPL-2.1-or-later
"""Capture the dedicated 24-bit Xvfb QA screen, without desktop portals."""
import ctypes as c
import struct
import sys
import zlib


class Image(c.Structure):
    _fields_ = [(name, kind) for name, kind in (
        ("width", c.c_int), ("height", c.c_int), ("xoffset", c.c_int),
        ("format", c.c_int), ("data", c.c_void_p), ("byte_order", c.c_int),
        ("bitmap_unit", c.c_int), ("bitmap_bit_order", c.c_int),
        ("bitmap_pad", c.c_int), ("depth", c.c_int), ("bytes_per_line", c.c_int),
        ("bits_per_pixel", c.c_int), ("red_mask", c.c_ulong),
        ("green_mask", c.c_ulong), ("blue_mask", c.c_ulong))]


x = c.CDLL("libX11.so.6")
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
x.XDefaultRootWindow.argtypes = [c.c_void_p]
x.XDefaultRootWindow.restype = c.c_ulong
x.XGetImage.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int,
                       c.c_uint, c.c_uint, c.c_ulong, c.c_int]
x.XGetImage.restype = c.POINTER(Image)
x.XDestroyImage.argtypes = [c.POINTER(Image)]
x.XCloseDisplay.argtypes = [c.c_void_p]
display = x.XOpenDisplay(None)
if not display:
    raise SystemExit("Cannot open the isolated QA X display")
width, height = 1280, 800
image = x.XGetImage(display, x.XDefaultRootWindow(display), 0, 0,
                    width, height, c.c_ulong(-1).value, 2)
if not image:
    raise SystemExit("XGetImage failed")
info = image.contents
assert (info.depth, info.bits_per_pixel, info.byte_order) == (24, 32, 0)
assert (info.red_mask, info.green_mask, info.blue_mask) == (0xff0000, 0xff00, 0xff)
pixels = c.string_at(info.data, info.bytes_per_line * height)
rows = bytearray()
for row in range(height):
    rows.append(0)
    offset = row * info.bytes_per_line
    for column in range(width):
        pixel = offset + column * 4
        rows.extend((pixels[pixel + 2], pixels[pixel + 1], pixels[pixel]))
x.XDestroyImage(image)
x.XCloseDisplay(display)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


with open(sys.argv[1], "wb") as file:
    file.write(b"\x89PNG\r\n\x1a\n")
    file.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
    file.write(chunk(b"IDAT", zlib.compress(rows)))
    file.write(chunk(b"IEND", b""))
