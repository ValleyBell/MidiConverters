// MPSMUSIC -> Midi Converter
// --------------------------
// Written by Valley Bell, 02/03 January 2022
// based on EA Steve Hayes -> Midi Converter

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "stdtype.h"

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
	UINT8 chnID;
	UINT32 startPos;
	UINT32 endPos;
	UINT32 tickCnt;
} TRK_INFO;
#define FORMAT_MIDI		0
#define FORMAT_ADLIB	1
typedef struct _song_info
{
	UINT8 fmtVer;
	UINT8 songTempo;
	UINT8 fmInsCnt;
	UINT8 macroCnt;
	UINT8 trkCnt;
	UINT32 fmInsTblPos;
	UINT32 macroPos[0x100];
	TRK_INFO tracks[0x10];
	UINT32 loopTick;
} SONG_INFO;


UINT8 Mps2Mid(UINT32 songLen, const UINT8* songData);
static void PreparseMps(UINT32 songLen, const UINT8* songData, SONG_INFO* songInf, TRK_INFO* trkInf);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(UINT16 bpm);

INLINE UINT32 ReadLE32(const UINT8* data);


static const UINT8 INS_VEL_SCALE[0x80] =
{
	0x64, 0x64, 0x64, 0x64, 0x64, 0x5A, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x5A, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x55, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x5A, 0x5A, 0x6E, 0x50,
	0x64, 0x64, 0x64, 0x5A, 0x46, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x5A, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x78, 0x64, 0x64, 0x64, 0x78, 0x64, 0x7F,
	0x64, 0x64, 0x5A, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x5F, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x73,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
	0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64,
};

static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MIDI_RES	24	// hardcoded in the driver
//static UINT16 NUM_LOOPS = 1;
static UINT8 DRV_BUGS = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("MPSMUSIC -> Midi Converter\n--------------------------\n");
	if (argc < 3)
	{
		printf("Usage: %s [options] input.bin output.mid\n", argv[0]);
		printf("Options:\n");
		//printf("    -Loops n    loop song n times (default: %u)\n", NUM_LOOPS);
		printf("    -Bugs       Replicate sound driver bugs/oddities.\n");
		return 0;
	}
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		/*if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}*/
		if (! stricmp(argv[argbase] + 1, "Bugs"))
			DRV_BUGS = 1;
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 2)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
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
	
	retVal = Mps2Mid(ROMLen, ROMData);
	if (! retVal)
		WriteFileData(MidLen, MidData, argv[argbase + 1]);
	free(MidData);	MidData = NULL;
	
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	//getchar();
#endif
	
	return 0;
}

UINT8 Mps2Mid(UINT32 songLen, const UINT8* songData)
{
	SONG_INFO songInf;
	TRK_INFO* trkInf;
	UINT32 inPos;
	UINT32 retPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 curTrk;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT32 tempLng;
	UINT8 tempByt;
	UINT8 tempArr[4];
	UINT8 newRS;
	UINT8 chnIns[0x10];
	//UINT16 loopCnt;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = 0x00;
	songInf.songTempo = songData[inPos];	inPos ++;
	
	songInf.fmtVer = FORMAT_MIDI;
	{
		// -- AdLib vs. MIDI format autodetection --
		UINT32 tempPos = inPos;
		tempByt = songData[tempPos];	tempPos ++;
		tempPos += tempByt * 0x18;	// skip FM data
		if (tempPos + 0x05 < songLen)
		{
			// assume that at this file offset, the macro part starts
			// e.g. [1 byte macro count], then [4 bytes macro size]
			// Check that the high 2 bytes of the macro size are 0.
			tempPos ++;
			tempLng = ReadLE32(&songData[tempPos]);
			if (tempLng < 0x10000 && tempPos + tempLng < songLen)
			{
				// The track must be terminated with FD byte.
				if (songData[tempPos + tempLng - 1] == 0xFD)
					songInf.fmtVer = FORMAT_ADLIB;
			}
		}
	}
	printf("Format: %s\n", (songInf.fmtVer == FORMAT_ADLIB) ? "AdLib" : "MIDI");
	
	if (songInf.fmtVer == FORMAT_ADLIB)
	{
		songInf.fmInsCnt = songData[inPos];	inPos ++;
		songInf.fmInsTblPos = inPos;
		inPos += 0x18 * songInf.fmInsCnt;
	}
	
	songInf.macroCnt = songData[inPos];	inPos ++;
	for (curTrk = 0; curTrk < songInf.macroCnt; curTrk ++)
	{
		songInf.macroPos[curTrk] = inPos + 0x04;
		inPos += ReadLE32(&songData[inPos]);
	}
	
	songInf.trkCnt = songData[inPos];	inPos ++;
	if (songInf.trkCnt > 0x10)
		songInf.trkCnt = 0x10;
	songInf.loopTick = 0;
	for (curTrk = 0; curTrk < songInf.trkCnt; curTrk ++)
	{
		trkInf = &songInf.tracks[curTrk];
		
		if (songInf.fmtVer == FORMAT_MIDI)
		{
			trkInf->chnID = songData[inPos];	inPos ++;
			trkInf->startPos = inPos + 0x04;
			inPos += ReadLE32(&songData[inPos]);
		}
		else //if (songInf.fmtVer == FORMAT_ADLIB)
		{
			trkInf->chnID = songData[inPos + 0x04];
			trkInf->startPos = inPos + 0x05;
			inPos += ReadLE32(&songData[inPos]);
		}
		trkInf->endPos = inPos;
		PreparseMps(songLen, songData, &songInf, trkInf);
	}
	
	WriteMidiHeader(&midFileInf, 0x0001, 1 + songInf.trkCnt, MIDI_RES);
	
	// write tempo track
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.midChn = 0x00;
	WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
	
	tempLng = Tempo2Mid(songInf.songTempo);
	WriteBE32(tempArr, tempLng);
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
	
	if (songInf.loopTick)
	{
		const char* text;
		text = "loopStart";
		WriteMetaEvent(&midFileInf, &MTS, 0x06, strlen(text), text);
		
		MTS.curDly += songInf.loopTick;
		text = "loopEnd";
		WriteMetaEvent(&midFileInf, &MTS, 0x06, strlen(text), text);
	}
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	memset(chnIns, 0x00, 0x10);
	for (curTrk = 0; curTrk < songInf.trkCnt; curTrk ++)
	{
		trkInf = &songInf.tracks[curTrk];
		
		inPos = trkInf->startPos;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		MTS.midChn = trkInf->chnID & 0x0F;
		WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
		
		//loopCnt = 0;
		trkEnd = 0;
		curCmd = 0x00;
		retPos = 0x00;
		while(! trkEnd && inPos < trkInf->endPos)
		{
			tempLng = 0x00;
			while(songData[inPos] & 0x80)
			{
				tempLng |= (songData[inPos] & 0x7F);	inPos ++;
				tempLng <<= 7;
			}
			tempLng |= songData[inPos];	inPos ++;
			MTS.curDly += tempLng;
			
			// the driver supports the MIDI "running status"
			newRS = (songData[inPos] & 0x80);
			if (newRS)
			{
				curCmd = songData[inPos];
				inPos ++;
			}
			switch(curCmd & 0xF0)
			{
			case 0x80:	// Note Off
			case 0x90:	// Note On
				// The sound driver also applies additional scaling on the velocities that we don't do here.
				// According to the MID files from the Windows 95 version of Transport Tycoon Deluxe,
				// the unpatched data matches the source songs.
				// ... except that all velocities are rounded down to multiples of 8 for whatever reason.
				if (DRV_BUGS)
				{
					UINT8 vel = songData[inPos + 0x01];
					if (trkInf->chnID != 9)
						vel = (UINT8)(vel * INS_VEL_SCALE[chnIns[trkInf->chnID]] / 0x80);	// melodic channel velocity scaling
					else
						vel = (UINT8)(vel * 0x50 / 0x80);	// drum channel velocity scaling
					WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], vel);
				}
				else
				{
					WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], songData[inPos + 0x01]);
				}
				inPos += 0x02;
				break;
			case 0xA0:	// Note Aftertouch (not in the driver)
				WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], songData[inPos + 0x01]);
				inPos += 0x02;
				break;
			case 0xB0:	// Control Change
				// Notes:
				//  - The sound driver hardcodes CC 91 (Reverb) to 30.
				//    Unlike the driver I'm also checking for a value of 0 here.
				//    This is to prevent overwriting intentional reverb settings. (most sequences have it set to 0)
				//  - The driver prevents CC 126 (Mono Mode On) from being sent.
				//  - Additional scaling is applied to CC 7 (Main Volume) by the driver:
				//    final volume = ((value * global music volume / 0x80) * fade volume / 0x80)
				//  - CC 0 is not being sent. Instead it seems to control the song tempo.
				if (songData[inPos + 0x00] == 0x00)
				{
					printf("Warning: Tempo Change (untested) on Track %u at %06X\n", curTrk, inPos);
					tempLng = Tempo2Mid(songData[inPos + 0x01] * 2);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
				}
				else
				{
					if (songData[inPos + 0x00] == 0x5B && songData[inPos + 0x01] == 0)
						WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], 30);
					else
						WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], songData[inPos + 0x01]);
				}
				inPos += 0x02;
				break;
			case 0xC0:	// Instrument
				tempByt = songData[inPos + 0x00];
				inPos += 0x01;
				if (tempByt == 0x7E)
				{
					// Patch 127 (0x7E) is used as "loop end" marker and is not being sent.
					WriteEvent2(&midFileInf, &MTS, !newRS, 0xB0, 0x6E, 0x00);
					break;
				}
				chnIns[trkInf->chnID] = tempByt;
				if (DRV_BUGS)
				{
					// weird patching done by the driver
					if (tempByt == 0x57)
						tempByt = 0x3E;
					else if (tempByt == 0x3F)
						tempByt = 0x3E;
				}
				WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, tempByt, 0x00);
				break;
			case 0xD0:	// Channel Aftertouch (not in the driver)
				WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], 0x00);
				inPos += 0x01;
				break;
			case 0xE0:	// Pitch Bend
				if (songInf.fmtVer == FORMAT_MIDI)
				{
					WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, songData[inPos + 0x00], songData[inPos + 0x01]);
					inPos += 0x02;
				}
				else //if (songInf.fmtVer == FORMAT_ADLIB)
				{
					INT16 pbVal = (INT8)songData[inPos + 0x00] - 0x10;
					pbVal *= 0x200;	// scale from -0x10 .. +0x0F to -0x2000 .. +0x1E00
					if (pbVal < -0x2000)
						pbVal = -0x2000;
					else if (pbVal > +0x1FFF)
						pbVal = +0x1FFF;
					pbVal += 0x2000;
					WriteEvent2(&midFileInf, &MTS, !newRS, curCmd & 0xF0, (pbVal >> 0) & 0x7F, (pbVal >> 7) & 0x7F);
					inPos += 0x01;
				}
				break;
			case 0xF0:	// special
				if (curCmd == 0xFD)
				{
					// macro track end
					inPos = retPos;
					retPos = 0x00;
					if (!inPos)
						trkEnd = 1;
					break;
				}
				else if (curCmd == 0xFE)
				{
					// call macro
					tempByt = songData[inPos];	inPos ++;
					if (tempByt >= songInf.macroCnt)
						break;
					if (!retPos)
						retPos = inPos;
					inPos = songInf.macroPos[tempByt];
					break;
				}
				else if (curCmd == 0xFF)
				{
					if (songData[inPos] == 0x2F)
					{
						// normal track end
						trkEnd = 1;
						break;
					}
				}
				// fall through
			default:	// unknown events cause the driver to enter an infinite loop
				printf("Unknown event %02X at %06X\n", curCmd, inPos);
				WriteEvent2(&midFileInf, &MTS, !newRS, 0xB0, 0x6E, curCmd & 0x7F);
				inPos += 0x01;
				trkEnd = 1;
				break;
			}
		}
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseMps(UINT32 songLen, const UINT8* songData, SONG_INFO* songInf, TRK_INFO* trkInf)
{
	UINT32 inPos;
	UINT32 retPos;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT32 tempLng;
	UINT8 tempByt;
	
	inPos = trkInf->startPos;
	
	trkInf->tickCnt = 0;
	trkEnd = 0;
	curCmd = 0x00;
	retPos = 0x00;
	while(! trkEnd && inPos < trkInf->endPos)
	{
		tempLng = 0x00;
		while(songData[inPos] & 0x80)
		{
			tempLng |= (songData[inPos] & 0x7F);	inPos ++;
			tempLng <<= 7;
		}
		tempLng |= songData[inPos];	inPos ++;
		trkInf->tickCnt += tempLng;
		
		if (songData[inPos] & 0x80)	// the driver remembers the last MIDI command
		{
			curCmd = songData[inPos];
			inPos ++;
		}
		switch(curCmd & 0xF0)
		{
		case 0x80:	// Note Off
		case 0x90:	// Note On
		case 0xA0:	// Note Aftertouch (not in the driver)
		case 0xB0:	// Control Change
			inPos += 0x02;
			break;
		case 0xC0:	// Instrument
			if (songData[inPos + 0x00] == 0x7E)	// loop marker
			{
				// DRV_BUGS == 0 -> use last marker (not the first one)
				if (!songInf->loopTick || !DRV_BUGS)
					songInf->loopTick = trkInf->tickCnt;
			}
			inPos += 0x01;
			break;
		case 0xD0:	// Channel Aftertouch (not in the driver)
			inPos += 0x01;
			break;
		case 0xE0:	// Pitch Bend
			if (songInf->fmtVer == FORMAT_MIDI)
				inPos += 0x02;
			else //if (songInf.fmtVer == FORMAT_ADLIB)
				inPos += 0x01;
			break;
		case 0xF0:	// special
			if (curCmd == 0xFD)
			{
				// macro track end
				inPos = retPos;
				retPos = 0x00;
				if (!inPos)
					trkEnd = 1;
				break;
			}
			else if (curCmd == 0xFE)
			{
				// call macro
				tempByt = songData[inPos];	inPos ++;
				if (tempByt >= songInf->macroCnt)
					break;
				if (! retPos)
					retPos = inPos;
				inPos = songInf->macroPos[tempByt];
				break;
			}
			else if (curCmd == 0xFF)
			{
				// normal track end
				trkEnd = 1;
				break;
			}
			// fall through
		default:	// unknown events cause the driver to enter an infinite loop
			trkEnd = 1;
			break;
		}
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

INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x00] <<  0) | (data[0x01] <<  8) |
			(data[0x02] << 16) | (data[0x03] << 24);
}
