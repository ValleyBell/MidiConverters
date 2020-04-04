// EA Steve Hayes -> Midi Converter
// --------------------------------
// Written by Valley Bell, 07 September 2019
// based on Wolfteam MF -> Midi Converter
//
// Notes:
// - MegaDrive games seem to only use the commands 8x/9x/Cx/FC.
// - Apple IIgs games use 8x/9x/Bx/Cx/Ex/FC and the special delay byte F8

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <stdtype.h>

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE	static __inline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif
#endif	// INLINE

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


#include "midi_funcs.h"


typedef struct _track_info
{
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 midChn;
} TRK_INFO;


UINT8 EaSH2Mid(UINT32 songLen, const UINT8* songData, UINT32 songPos);
static void PreparseEaSH(UINT32 songLen, const UINT8* songData, TRK_INFO* trkInf);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(UINT16 bpm);

INLINE UINT32 ReadBE32(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 30;
static UINT16 NUM_LOOPS = 2;
static UINT8 FILE_VER = 1;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	UINT32 songOfs;
	
	printf("EA Steve Hayes -> Midi Converter\n--------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: eash2mid.exe [options] ROM.bin/song.bin output.mid songOffset\n");
		printf("Options:\n");
		printf("    -Ver n      file format version (default: %u)\n", FILE_VER);
		printf("                1 - with note velocity\n");
		printf("                2 - without note velocity (used by some MegaDrive games)\n");
		printf("    -Loops n    loop song n times (default: %u)\n", NUM_LOOPS);
		printf("    -TpQ n      convert with n ticks per quarter (default: %u)\n", MIDI_RES);
		printf("For songs extracted from Apple IIgs games, use version 1 and offset 4.\n");
		printf("\n");
		printf("Examples:\n");
		printf("    eash2mid -Ver 1 \"King's Bounty (U).bin\" KB_01.mid 0x07F7C0\n");
		printf("    eash2mid -Ver 2 \"Might+Magic2(U,Rev01).bin\" MM2_01.mid 0x0AF59C\n");
		printf("    eash2mid -Ver 2 \"Might+Magic3(U).bin\" MM3_01.mid 0x0C2E44\n");
		printf("    eash2mid -Ver 1 snd.24 KQ4GS_24.mid 4\n");
		return 0;
	}
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Ver"))
		{
			argbase ++;
			if (argbase < argc)
			{
				FILE_VER = (UINT8)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					FILE_VER = 1;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				MIDI_RES = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! MIDI_RES)
					MIDI_RES = 30;
			}
		}
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 3)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	songOfs = (UINT32)strtoul(argv[argbase + 2], NULL, 0);
	
	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	ROMLen = ftell(hFile);
	if (ROMLen > 0xFFFFF)	// 1 MB
		ROMLen = 0xFFFFF;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	retVal = EaSH2Mid(ROMLen, ROMData, songOfs);
	if (! retVal)
		WriteFileData(MidLen, MidData, argv[argbase + 1]);
	free(MidData);	MidData = NULL;
	
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

UINT8 EaSH2Mid(UINT32 songLen, const UINT8* songData, UINT32 songPos)
{
	UINT32 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT32 tempLng;
	UINT8 tempByt;
	UINT8 tempArr[4];
	UINT16 loopCnt;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = songPos;
	
	WriteMidiHeader(&midFileInf, 0x0000, 1, MIDI_RES);
	
	WriteMidiTrackStart(&midFileInf, &MTS);
	
	tempLng = Tempo2Mid(150);
	WriteBE32(tempArr, tempLng);
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
	
	loopCnt = 0;
	trkEnd = 0;
	MTS.midChn = 0x00;
	while(! trkEnd && inPos < songLen)
	{
		// delays are always 1 byte with value F8 being special and
		// causing another delay byte to be followed
		do
		{
			tempByt = songData[inPos];	inPos ++;
			MTS.curDly += tempByt;
		} while(tempByt == 0xF8);
		
		if (songData[inPos] & 0x80)	// the driver remembers the last MIDI command
		{
			curCmd = songData[inPos];
			inPos ++;
		}
		switch(curCmd & 0xF0)
		{
		case 0x80:	// Note Off
			if (FILE_VER == 2)
			{
				WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x40);
				inPos += 0x01;
			}
			else
			{
				WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], songData[inPos + 0x01]);
				inPos += 0x02;
			}
			break;
		case 0x90:	// Note On
			if (FILE_VER == 2)
			{
				WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x7F);
				inPos += 0x01;
			}
			else
			{
				WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], songData[inPos + 0x01]);
				inPos += 0x02;
			}
			break;
		case 0xB0:	// Control Change
		case 0xE0:	// Pitch Bend
			WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0xC0:	// Instrument
			WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x00);
			inPos += 0x01;
			break;
		case 0xF0:	// special
			if (curCmd == 0xFC)
			{
				// Note: Sometimes in files from KQ4 (Apple IIgs), there is an additional
				//       "initialization" block after the first FC marker.
				if (inPos < songLen)
				{
					tempByt = songData[inPos + 0x00];
					inPos += 0x01;
				}
				else
				{
					tempByt = 0x00;
				}
				if (tempByt == 0x80)
				{
					// loop back
					loopCnt ++;
					if (loopCnt < 0x80)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCnt);
					if (loopCnt < NUM_LOOPS)
						inPos = songPos;
					else
						trkEnd = 1;
				}
				else
				{
					// track end
					trkEnd = 1;
				}
				break;
			}
			// fall through
		//case 0xA0:	// Note Aftertouch
		//case 0xD0:	// Channel Aftertouch
		default:	// unknown events cause the driver to enter an infinite loop
			printf("Unknown event %02X at %06X\n", curCmd, inPos);
			WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
			inPos += 0x01;
			trkEnd = 1;
			break;
		}
	}
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseEaSH(UINT32 songLen, const UINT8* songData, TRK_INFO* trkInf)
{
	UINT16 inPos;
	UINT8 trkEnd;
	UINT8 curCmd;
	
	trkEnd = 0;
	trkInf->loopOfs = 0x0000;
	inPos = 0x0000;
	while(! trkEnd && inPos < songLen)
	{
		curCmd = songData[inPos];
		trkInf->tickCnt += 1;
		break;
	}
	
	return;
}

static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName)
{
	FILE* hFile;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(data, 0x01, dataLen, hFile);
	fclose(hFile);
	
	return 0;
}

INLINE UINT32 Tempo2Mid(UINT16 bpm)
{
	// formula: (60 000 000 / bpm) * (MIDI_RES / 24)
	return 2500000 * MIDI_RES / bpm;
}


INLINE UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}
