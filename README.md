# MIDI Converters
various <game format> to MIDI converters I wrote over the years.

## AKAO2MID
This tool converts PSX AKAO v1 and v2 to MIDI. It is based on the information [here](http://wiki.qhimm.com/view/FF7/PSX/Sound/AKAO_frames), but I did a lot of additional reverse engineering.

The original version had everything hardcoded, but thanks a request by FlyingGrayson you can now compile an .exe file and drop files on it.  
The tool is still pretty alpha though.

Features I should mention:
* AKAO v1 and v2 support (the latter is slightly worse than the former)
* understandable tempo calculations
* portamento handling (portamentos are indicated by MIDI controller 5, with its value being the portamento length)
* full volume slide support
* AKAO "drum mode" is mapped to MIDI channel 10

### AKAO_Filter
This is a small tool that I used to extract PSX AKAO files from uncompressed PSF files.

I used the tool to get AKAO files for AKAO2MID. It uses hardocded paths, so you need to use it from the VB6 IDE directly.

## cotton2mid
This tool converts songs from the game "Cotton: Fantastic Night Dreams" for Sega System 16B to MIDI.

It extracts and converts the songs from the sound ROM, usually called opr-13893.a11.

## de2mid
This tool converts songs from MegaDrive games developed by Data East to MIDI.

It reads MegaDrive ROMs in .bin format. There is a [page on Sega Retro](http://segaretro.org/Data%20East%20Music%20Hacking/Music%20Pointers) that lists all known games that use this sound driver.
It also lists the offset of the song list.

## de2mid_SMS
Despite its name, this tool converts songs from Sonic Spinball on the Sega Master System/Game Gear to MIDI.

I did a quick hack of de2mid to support Sonic Spinball's sound format, which is the cause of the name. I kept the tool for historical purposes. You should use smps2mid if you want to convert songs from Sonic Spinball (8-bit) to MIDI.

## ffmq2mid
This tool converts SPC files from Final Fantasy: Mystic Quest (and other games that use the SNES AKAO format) to MIDI.

The tool was developed to convert songs from FFMQ, but later I added support for Final Fantasy 5 and Live A Live. The tool uses a few hardcoded instrument mappings, which don't work with all songs.

Nowadways you shouldn't use ffmq2mid. loveemu wrote a tool called akaospc with the help of disassemblies, so its SFC-AKAO support is more complete and it supports more games. (I reverse-engineered the format just by looking at the SPC700's RAM and the raw sequence data.)  
I recommend VGMTrans to convert Squaresoft SPCs to MIDI. It uses an improved version of akaospc to do the conversion.

## fmp2mid
This tool converts FMP songs to MIDI.

I developed the tool using songs from Variable Geo and other GIGA games that use FMP v3. I also tested it with the TGL game "Edge", which uses FMP v2. FMP v1 sequences don't work yet.

## gems2mid
This tool converts songs from the GEMS sound driver to MIDI.

The tool was developed with the help of the source code of the original GEMS driver, which was included with the GEMS development kit.  

The tool can not just convert GEMS sequences to MIDI, but it can also dump DAC samples (to .raw files) and export YM2612 instruments to .gyb.
It processes sequence, sample and instrument libraries.

For sequence data, the tool tries to detect the pointer format, which is different between GEMS 2.0-2.5 and 2.8.

The GEMS sound driver is commonly found in MegaDrive games developed in the U.S.

## grc2mid
This tool converts songs from MegaDrive games that use the GRC sound driver to MIDI.

Initially I wrote this tool to get MIDIs from songs of Decap Attack, which only used FM channels. The current version supports FM, PSG and DAC channels.  
The tool is written based on a sound driver disassembly of Decap Attack and Socket.

## HMI2MID
This is a quick and dirty Visual Basic 6 tool to convert HMI files to standard MIDIs.

I used it to convert HMIs from Lemmings 3. Everything is hardcoded though.

I recommend kode54's [2mid](https://github.com/kode54/tomid) for proper HMI -> MID conversions.

## hum2mid
This tool converts songs from NES Hummer Team games to MIDI.

I initially wrote this tool to convert Somari songs to MIDI, but it should be able to convert songs from almost every Hummer Team game.

The tool converts the songs directly from NES or NSF files.
It was written based on a sound driver disassembly of Somari.

## ims2mid
This converts IMS AdLib songs to MIDI.

The IMS was very popular in the Korean AdLib scene and IMS songs were commonly used in Korean DOS games.

The tool should also work with AdLib MIDI files (used e.g. in Vinyl Goddess From Mars), because this format is very similar to IMS.

## it2mid
This tool converts IT modules to MIDI.

I wasn't satisfied with OpenMPT's MIDI exporting function, because it didn't preserve volume and panning information, so I wrote this. (There was also something I didn't like about LifeAmp's conversion.)

## konami2mid
This tool converts songs from TMNT: Hyperstone Heist to MIDI. It probably works with other MegaDrive Konami games as well.

The tool can export MIDIs and dump instruments to GYB files.  
It was written based on a sound driver disassembly of TMNT: Hyperstone Heist.

## Lem3DMid
This converts songs from Lemmings 3D to MIDI.

There are two versions - an old Visual Basic 6 .bas module (that uses hardcoded paths) and C version (which is a port of the VB6 version) with a proper commandline interface.

## M2MidiDec
This tool converts songs from games for Sega Model 2 to MIDI.

This tool can convert the sequences from Model 2 sound ROMs to MIDI and export the samples to WAV or SF2 soundfonts.  
It was written based on a sound driver disassembly of Sonic the Fighters.

So far only Sonic the Fighters and Fighting Vipers are supported. You need to select between those games by enabling a #define at compile time. It will then try to read the respective ROM files from the current directory. Byteswapped ROMs are detected and fixed automatically.

Notes:
* The sequences contain no tempo information, so don't expect the MIDIs to be printable.
* The speed of the exported sequences might be a bit off. I have no reference to compare it against, because emulation is pretty inaccurate in that regard as well.
* The MIDI/SF2 combo doesn't always produce perfect results. I still have trouble with certain instruments in Fighting Vipers.

**Compilation note:** Needs to be linked to Soundfont.c.

## MI1-Midi
This tool converts MT-32 sound data from early SCUMM games (like Monkey Island 1) to MIDI.

The tool needs "RO" files ripped from ScummVM's RAM as I couldn't figure out the format of the "WA" files that you can extract from the actual game.
The "RO" signature must be at byte 0x0C. (I apparently ripped some bytes from the memory block header.)  
MI1-Midi_Data.7z contains the files that I used with the converter.

## msdrv2mid
This tool converts songs from PC-98 games that use the MsDRV sound driver to MIDI.

The tool was originally called twinkle2mid and was written to convert the songs from Bunretsu Shugo Shin Twinkle Star, which uses an early version of MsDRV. It was since updated to work with more versions of the sound format.

Early versions of MsDRV have the code embedded in the main executable. Later version come with a separate sound driver executable that is usually called "MSDRV4.EXE" or "MSDRV4L.EXE". In any case you can find a copyright notice by the author "KENJI" in side the executable.

Common file extensions for MsDRV music files are MF1, MF2, and MUS.
Bunretsu Shugo Shin Twinkle Star has one .MF2 archive per song. Eacho .MF2 archive contains all variants (OPN, OPNA, MT-32, SC-55) of a song and you need to unpack/decompress it to get the actual music file.

## mucom2mid
This converts songs in PC-8801 Mucom format to MIDI.

Initially written to convert music from Misty Blue (for NEC PC-8801), I also did a few tests with other songs from an archive with Mucom88 music, so I think it should work with the majority of Mucom88 songs.

The main test case for this tool were the OPN and OPNA versions of Misty Blue's Opening Theme, because this song rocks. (And I wanted to make an 8-bit version of it.)

## ngp2mid
This tool converts songs from Neo Geo Pocket games to MIDI.

The tool supports two versions of the NGP sound driver: The one used by the NGP BIOS (0x9B9 bytes large) and Sonic Pocket Adventure (0x9F5). These two formats should cover a majority of all NGP and NGPC games.
Unfortunately you need to choose the driver version at compile time by setting `DrvVer` to the respective value.

I initially wrote the tool to convert songs from Sonic Pocket Adventure. It was written based on a disassembly of its sound driver. (There's no way I could reverse-engineer a format that is based on a bitstream just by looking at bytes in a hex editor.)
Later I disassembled the NGP BIOS sound driver as well. It turned out to have a few major differences, so I added support for its version of the format.

## pmd2mid
This tool converts P.M.D. songs to MIDI.

P.M.D. is the Professional Music Driver written by Masahiro Kajihara. It is commonly found in games for the NEC PC-9801, but versions for the PC-8801 and IBM PC exist as well.

The tool was made with the help of the PMDWin 0.36 source code. But due to the huge number of features P.M.D. supports, not all sequence commands are supported by pmd2mid currently.  
During the development process the songs from PC-9801 Rusty served as a main test case. I used the IBM PC version of Rusty to quickly test how PMD-IBM works, so the tool can deal with PMD-IBM songs as well.

## sbm52mid
This tool converts SPCs from Super Bomberman 5 to MIDI.

There isn't a lot to this tool, actually. I was asked by Varion to make this converter and because I liked how the game sounds I quickly reverse-engineered the format and wrote this tool. If you look at the source code you'll quickly notice it's based on ffmq2mid.

## seq2mid
This tool converts PSX SEQ files to MIDI.

There are two versions. The Visual Basic 6 module (.bas) is obsolete and uses hardcoded paths and hardcoded instrument mappings.  
The C version instead allows for advanced options, including volume conversion and instrument remapping via external .ini files.

For example instrument maps, look at the .ini files in the seq2mid_data folder.  
The first line is the PSX VAB file to use, all following lines describe the instrument mapping.
Columns are separated by tabs and have the following meaning:
1. column: instrument number (in hex) in SEQ file, optionally followed by "-" and a hex number to select the "region" item (can be 0 to F)
2. column: instrument number (in hex) in resulting MIDI file OR drum note ('D' + hex number)
3. column: note transposition (for instruments) OR base drum note (for drums, in hex)
4. column: comment

The converter will use NRPNs to adjust the pitch of drum notes, so setting a correct "base drum note" is important.

## sg2mid
This converts SPC files from Speedy Gonzales: Los Gatos Bandidos to MIDI.

I learned about this Speedy Gonzales game (and its music) due to the bootleg version of the game that replaces Speedy with Sonic. And although the music isn't amazing, there are a few very nice tunes in the game, IMO.

The game uploads "banks" to the sound RAM, so one SPC file may contain multiple songs, which are all exported.

## srmp4-midi
This tool converts the one and only song present in Super Real Mahjong PIV (Arcade version) to MIDI.

I wrote this tool just for fun and because the format they used already looked quite MIDI-ish.

The game uses "SIFF" containers (like to RIFF with Big Endian values) for sound stuff. The SIFF/tone file contains the sample tables for instruments, the SIFF/seqs file contains the music. The tool can convert the latter.

## Sys32MidiDec
This tool converts songs from games for Sega System 18/32 to MIDI.

This tool can convert the sequences from System 18 and System 32 sound ROMs to MIDI.  
It is a slightly modified version of M2MidiDec.

So far only SegaSonic the Hedgehog and OutRunners are supported. You need to select between those games by enabling a #define at compile time. It will then try to read the respective ROM files from the current directory.

## TG100_DemoSongDump
This tool can be used to dump the TG100 demo song from its firmware ROM.

Note that the song itself has no tempo information, so the beats aren't aligned. The tempo of the resulting MIDI might also be a bit off.

## top2mid
This tool converts SPC files from SFC Tales of Phantasia and Star Ocean to MIDI.

I really love the music from those two games (especially ToP's), which is the reason I wrote this tool.

The tool allows for advanced instrument remapping. The format of the .ini files used for defining instrument maps is mostly the same as seq2mid, aside from the lack of a PSX VAB file name.
NRPNs are used to change drum pitches.

## top2mid_Arc
This is top2mid, but with support for Arcus Spirits hacked in.

It sort of worked, but I think Arcus Spirits' format is more similar to what Wolfteam used on most other platforms.

## toutrun2mid
This tool converts songs from Turbo OutRun (Arcade) to MIDI.

The tool is written based on a sound driver disassembly of Turbo OutRun.

## wtmd2mid
This tool converts songs from MegaDrive Wolfteam games to MIDI.

I wrote this tool to convert music from late MegaDrive Wolfteam games. (The ones with PCM support.) But it can also convert songs from X68000 Wolfteam games.  
Right now the tool uses hardcoded instrument mappings - which interestingly worked across all MegaDrive and X68000 games I tested.

For Wolfteam MegaDrive games with PCM drums, it can autodetect the song list and will batch-convert all songs. (compile with `PLMode = 0x01;` to enable it)

## yong2mid
This tool converts songs from Yong Yong games to MIDI.

Yes, it's the result of reverse-engineering the sound driver of the Game Boy games Sonic 3D Blast 5 and Sonic Adventure 7. It works with the raw ROMs.

The sound engine is very basic and has only very few features. It still doesn't justify the terrible sound of those unlicensed games, though.

## ys2mid
This tool converts songs from Ys II (PC-8801 version) to MIDI.

I only wrote the tool to work with Ys II. I haven't check what other games it works with so far. (The chance for it to work with Ys I and III is probably high.)

Now what song do you think was the main test case for the tool? Right, it's *To Make The End of Battle*. I wanted to make an 8-bit version of the song and vgm2mid didn't satisfy me.


# Libraries

## Midi1to0.c
This is a tiny library that allows you to convert MIDIs from format 1 to format 0.

When I did format 1 to 0 conversions the first time, I merged track 1 into track 0, then track 2 into track 0, etc., but I think that didn't perform well with large files.
The approach I use here is to merge all tracks simultaneously. This might perform a bit worse for short songs with a huge number of tracks, but I prefer this solution.

## midi_funcs.h
This is a small header-only library that allows you to easily write MIDI files. It automatically resizes the data buffer if it gets too small.

## Soundfont.c/.h
This library can help you to generate SF2 soundfont files. It only does the chunk management and file writing though, so you still need to do most of the work by yourself.

The soundfont library is used by M2MidiDec.
