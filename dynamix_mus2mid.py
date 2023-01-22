#!/usr/bin/env python3
# Written by Valley Bell, 2023-01-22
import sys
import os
import struct

"""
Dynamix song file
-----------------
4 bytes - sub-song 1 file offset (absolute, Little Endian)
4 bytes - sub-song 2 file offset (absolute, Little Endian)
...

sub-song data:
2 bytes - sub-song size (includes this value, Little Endian)
2 bytes - ??
2 bytes - MIDI resolution (ticks per quarter, Little Endian)
2 bytes - ?? (always FF FF)
rest of the data - MIDI file payload
"""

MID_HEADER = [
	0x4D, 0x54, 0x68, 0x64,	# "MThd"
	0x00, 0x00, 0x00, 0x06,	# header size
	0x00, 0x00,	# format: 0
	0x00, 0x01,	# tracks: 1
	0x00, 0x18,	# resolution: 24 ticks
	0x4D, 0x54, 0x72, 0x6B,	# "MTrk"
	0x00, 0x00, 0x00, 0x00,	# track size
]


if len(sys.argv) < 3:
	print("Dynamix song -> MID converter")
	print("Usage: {} song.rol song.mid".format(sys.argv[0]))
	sys.exit(1)

OUT_PATTERN = os.path.splitext(sys.argv[2])

with open(sys.argv[1], "rb") as hMusFile:
	hMusFile.seek(0, 2)
	musSize = hMusFile.tell()
	hMusFile.seek(0)
	
	# read TOC
	first_song_pos = musSize
	song_offsets = []
	while hMusFile.tell() < first_song_pos:
		fofs = struct.unpack("<I", hMusFile.read(0x04))[0]
		if first_song_pos > fofs:
			first_song_pos = fofs
		song_offsets.append(fofs)
	
	for song_id in range(len(song_offsets)):
		hMusFile.seek(song_offsets[song_id])
		header = struct.unpack("<HHHH", hMusFile.read(0x08))
		
		payload_len = header[0] - 0x08
		mid_hdr = bytes(MID_HEADER)
		# basic MIDI format 0 header [00:0C] + MIDI resolution [0C:0E] + "MTrk" [0E:12] + MIDI track size [12:16]
		mid_hdr = mid_hdr[0:0x0C] + struct.pack(">H", header[2]) + mid_hdr[0x0E:0x12] + struct.pack(">I", payload_len)
		fname = f"{OUT_PATTERN[0]}{song_id}{OUT_PATTERN[1]}"
		print(f"Song {song_id}: offset = 0x{song_offsets[song_id]:04X}, size = 0x{header[0]:04X}, "
			f"val2 = 0x{header[1]:04X}, resolution = {header[2]}, val4 = 0x{header[3]:04X}")
		
		with open(fname, "wb") as hFile:
			hFile.write(mid_hdr)
			hFile.write(hMusFile.read(payload_len))
