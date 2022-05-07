// MsDRV -> Midi Converter
// -----------------------
// Written by Valley Bell, 26 February 2017, 10 March 2017
// based on Wolf Team MegaDrive -> Midi Converter
// Updated to support MsDrv v4 on 25 February 2018, 27 February 2018
// Updated to support MsDrv v1 in April 2022
//
// known games and driver versions:
//	- Ambition: MsDrv v2 [FM]
//	- Artemis: MsDrv v1a [FM]
//	- Beast 2: Inkyu Bastard: MsDrv v1b [MIDI] / MsDrv v2 [FM]
//	- Beast 3: MsDrv v2 [FM] (only 9 tracks!)
//	- Birdy World (PC-98): MsDrv v1b [MIDI] / MsDrv v2 [FM]
//	- Birdy World (X68000): MsDrv v2 [FM]
//	- Bunretsu Shugyo Shin Twinkle Star: MsDrv v2
//	- Ekudorado - Kingdom Inside of a Mirror: MsDrv v4.5a light
//	- Flash Point: MsDrv v1a [FM/MIDI]
//	- Flash Point 2: MsDrv v1b [FM]
//	- Frontier: MsDrv v4.3a
//	- Get!: MsDrv v4.5a light
//	- Irium: MsDrv v2
//	- Joker: MsDrv v1b [FM/MIDI]
//	- Joker 2 (PC-98): MsDrv v1b [MIDI] / MsDrv v2 [FM]
//	- Joker 2 (X68000): MsDrv v2 [FM]
//	- Kara no Naka no Kotori: MsDrv v4.4b
//	- Kagami - Mirror: MsDrv v1c [MIDI] / MsDrv v2 [FM]
//	- Merry Go Round: MsDrv v4.4b light
//	- Mirage: MsDrv v1b [MIDI] / MsDrv v2 [FM]
//	- Miwaku no Chousho: MsDrv v4.4a
//	- My Eyes! (PC-98): MsDrv v1c [MIDI] / MsDrv v2 [FM]
//	- My Eyes! (X68000): MsDrv v2 [FM] (only 9 tracks!)
//	- Pleria - The Royal Emblem: MsDrv v1c [MIDI] / MsDrv v2 [FM]
//	- Present: MsDrv v1a [FM/MIDI]
//	- Present 2: MsDrv v1c [MIDI] / MsDrv v2 [FM]
//	- Red: MsDrv v1b [FM/MIDI]
//	- Rekiai: MsDrv v4.5a light
//	- Rondo: MsDrv v4.1a
//	- Street Mahjong 2: MsDrv v4.4a light
//	- Strush: MsDrv v1b [MIDI] / MsDrv v2 [FM]
//	- Sweet Emotion: MsDrv v1a [FM/MIDI]
//	- Urban Soldier: MsDrv v2
//
// Note:
//  Games using MsDrv v1 contain fully separate FM and MIDI sound drivers.
//  In later versions, the FM and MIDI sound drivers were combined and share some code.
//  Games running on the X68000 keep the Little Endian byte order for pointers in music data.
//
// An easy way to distinguish between MsDrv v1 versions (MIDI driver only):
//  - Search for 80 44 ## 60 E9 ## ## - these 7 bytes form the block that sets the delay.
//    (Searching for "80 44" often immediately locates the block already.)
//  - Set the hex editor to 7 bytes per row.
//  - If all lines begin with 80 44 06, it is v1c.
//  - If all lines begin with 80 44 05, it is v1a or v1b.
//    - If the block of "80 44 xx" lines begins with "80 44 05 60" and ends with "80 44 05 20", it is v1a.
//    - If the block of "80 44 xx" lines begins with "80 44 05 C0" and ends with "80 44 05 01", it is v1b.

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
	UINT32 startOfs;
	UINT32 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 trkID;
} TRK_INF;


#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


UINT8 MsDrv2Mid(UINT32 SongLen, const UINT8* SongData);
UINT8 MsDrv2Mid_v1(UINT32 SongLen, const UINT8* SongData);
static UINT8 CacheSysExData(UINT16 sxBufSize, UINT8* sxBuf, UINT8* sxBufPos, UINT8 data,
							FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 curCmd, UINT8 curTrk, UINT16 cmdPos);
static void PreparseMsDrvTrack(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, UINT8 Mode);
static void PreparseMsDrvTrack_v1(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, UINT8 fileVer, UINT8 Mode);
static void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, INT16 bend);
static UINT8 NeedPBRangeFix(UINT8* curPBRange, INT16 PBend);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);

static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);
static double OPN2DB(UINT8 TL);
static UINT8 DB2Mid(double DB);
static UINT8 PanBits2MidiPan(UINT8 Pan);
static UINT8 CalcGSChecksum(UINT16 DataSize, const UINT8* Data);
INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale);

INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT32 ReadLE32(const UINT8* Data);


#define FILEVER_V1A		0x10	// v1a: 9 tracks, 24 ticks/beat (Present, Sweet Emotion)
#define FILEVER_V1B		0x11	// v1b: 9 tracks, 48 ticks/beat (Mirage 1, Red)
#define FILEVER_V1C		0x12	// v1c: 9 tracks, 48 ticks/beat, has more commands than v1b (Mirror)
#define FILEVER_V2		0x20	// v2: 10 tracks (Bunretsu Shugo Shin Twinkle Star)
#define FILEVER_V4		0x40	// v4: 24 tracks, 4-byte padding (MsDRV 4.4)
#define FILEVER_V4L		0x41	// v4 light: 24 tracks, no padding (MsDRV 4.5 light)

#define IS_MAJOR_VER(a, b)	((a & 0xF0) == (b & 0xF0))

// MsDrv v1 delay table
static const UINT8 DELAY_LUT[2][0x10] = {
	{192, 96, 48, 32, 24, 16, 12, 8, 6, 144, 72, 36, 18, 4, 2, 1},	// mirage_98, resolution 48
	{96, 48, 24, 16, 12, 8, 6, 4, 3, 72, 36, 18, 9, 2, 1, 32},	// present_98/sweet_e_98 only, resolution 24
};


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT16 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];
static UINT8 fileVer;

static UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 FixVolume = 0;
static UINT8 DebugCtrls = 0;
static UINT8 forcedFileVer = 0xFF;
static UINT8 DEFAULT_FM_MODE = 0;

static UINT8 tempoChgTrk = 0xFF;
static UINT32 tempoChgTick = 0;
static UINT32 tempoChgPos = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("MsDRV -> Midi Converter\n-----------------------\n");
	if (argc < 3)
	{
		printf("Usage: msdrv2mid.exe [options] input.bin output.mid\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: %u)\n", NUM_LOOPS);
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -Debug      write debug MIDI controllers\n");
		printf("    -VolFix     OPN/OPL: convert db levels to logarithmic MIDI\n");
		printf("    -ForceVer x enforce a file format version x (can be: v1a, v1c, v2, v4, v4l)\n");
		printf("    -FM         MsDRV v1: assume FM/SSG channels (defaults to MIDI)\n");
		printf("\n");
		printf("Supported/verified games: \n");
		printf("    MsDRV v1: Sweet Emotion, Mirage, Kagami - Mirror\n");
		printf("    MsDRV v2: Bunretsu Shugo Shin Twinkle Star\n");
		printf("    MsDRV v4.4: Miwaku no Chousho, Kara no Naka no Kotori\n");
		printf("    MsDRV v4.5 light: Ekudorado: Kingdom Inside of a Mirror\n");
		return 0;
	}
	
	MidiDelayCallback = MidiDelayHandler;
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "NoLpExt"))
			NO_LOOP_EXT = 1;
		else if (! stricmp(argv[argbase] + 1, "Debug"))
			DebugCtrls = 1;
		else if (! stricmp(argv[argbase] + 1, "VolFix"))
			FixVolume = 1;
		else if (! stricmp(argv[argbase] + 1, "ForceVer"))
		{
			UINT8 ver = 0xFF;
			argbase ++;
			if (argbase < argc)
			{
				if (! stricmp(argv[argbase], "v1a"))
					ver = FILEVER_V1A;
				else if (! stricmp(argv[argbase], "v1b"))
					ver = FILEVER_V1B;	// It's not like this is any different from v1c, but let's just have it.
				else if (! stricmp(argv[argbase], "v1c"))
					ver = FILEVER_V1C;
				else if (! stricmp(argv[argbase], "v2"))
					ver = FILEVER_V2;
				else if (! stricmp(argv[argbase], "v4"))
					ver = FILEVER_V4;
				else if (! stricmp(argv[argbase], "v4l"))
					ver = FILEVER_V4L;
			}
			if (ver == 0xFF)
			{
				printf("Invalid version specifier!\n");
				return 2;
			}
			forcedFileVer = ver;
		}
		else if (! stricmp(argv[argbase] + 1, "FM"))
			DEFAULT_FM_MODE = 1;
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 2)
	{
		printf("Not enough arguments.\n");
		return 2;
	}
	
	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	ROMLen = ftell(hFile);
	if (ROMLen > 0x10000)	// 64 KB
		ROMLen = 0x10000;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	retVal = MsDrv2Mid(ROMLen, ROMData);
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

UINT8 MsDrv2Mid(UINT32 SongLen, const UINT8* SongData)
{
	TRK_INF trkInf[0x20];
	TRK_INF* tempTInf;
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT32 inPos;
	UINT32 trkTick;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	// Bit 0 - raw MIDI mode (don't do any fixes)
	// Bit 1 - 3-byte note mode
	// Bit 7 - track end
	UINT8 chnMode;
	UINT8 trkFlags;
	UINT8 curCmd;
	UINT8 pbRange;
	UINT8 curBPM;
	UINT8 tempoMod;
	
	UINT32 subEndOfs;	// subroutine end file offset
	UINT32 subRetOfs;	// subroutine return file offset
	
	UINT8 loopIdx;
	UINT16 loopCount[8];
	UINT32 loopPos[8];
	
	UINT32 tempLng;
	UINT16 tempSht;
	INT16 tempSSht;
	UINT8 tempByt;
	UINT8 retVal;
	UINT8 tempArr[0x10];
	char tempStr[0x20];
	UINT16 syxPos;
	
	UINT8 curNote;
	UINT8 curNoteVol;
	INT8 curNoteMove;
	UINT8 lastNote;
	
	INT16 curPBend;	// trkRAM+2F/30
	INT16 pSldCur;	// trkRAM+31/32
	INT16 pSldDelta;	// trkRAM+33/34
	INT16 pSldTarget;	// trkRAM+35/36
	UINT32 pSldNextTick;
	
	UINT8 sysExHdr[2];
	UINT8 sysExData[4];
	UINT8 sysExBuf[0x80];
	UINT8 sysExBPos;
	UINT8 sysExChkSum;
	
	tempSht = ReadLE16(&SongData[0x00]);
	if (forcedFileVer != 0xFF)
	{
		if (IS_MAJOR_VER(forcedFileVer, FILEVER_V1A))
			return MsDrv2Mid_v1(SongLen, SongData);
		else if (IS_MAJOR_VER(forcedFileVer, FILEVER_V2))
			trkCnt = (UINT8)tempSht / 2;
		else
			trkCnt = 24;
		fileVer = forcedFileVer;
		if (fileVer == FILEVER_V2)
			printf("Format: %s, %u tracks\n", "v2", trkCnt);
		else
			printf("Format: %s, %u tracks (padding: %s)\n", "v4", trkCnt,
					(fileVer & 0x01) ? "no" : "yes");
	}
	else if (tempSht == 0x0010 || tempSht == 0x0012)
	{
		return MsDrv2Mid_v1(SongLen, SongData);
	}
	else if (tempSht == 0x0014)
	{
		fileVer = FILEVER_V2;
		trkCnt = (UINT8)tempSht / 2;
		printf("Detected format: %s, %u tracks\n", "v2", trkCnt);
	}
	else if (tempSht == 0x00A0)
	{
		tempLng = 0x00;
		for (inPos = 0x00; inPos < 0x90; inPos += 0x04)	// actual track offsets go until 0x90 only
			tempLng |= SongData[inPos];
		// If all start offsets are aligned to 4 bytes, it's V4 (with padding).
		// If not, then it's V4 light (no padding).
		fileVer = (tempLng & 0x03) ? FILEVER_V4L : FILEVER_V4;
		trkCnt = 24;
		printf("Detected format: %s, %u tracks (padding: %s)\n", "v4", trkCnt,
				(fileVer & 0x01) ? "no" : "yes");
	}
	else
	{
		printf("Unable to detect format version!\n");
		return 0x80;	// unknown version
	}
	if (trkCnt > sizeof(trkInf) / sizeof(trkInf[0]))
		trkCnt = (UINT8)(sizeof(trkInf) / sizeof(trkInf[0]));
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	if (IS_MAJOR_VER(fileVer, FILEVER_V2))
	{
		inPos = 0x00;
		for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
			trkInf[curTrk].startOfs = ReadLE16(&SongData[inPos]);
	}
	else //if (IS_MAJOR_VER(fileVer, FILEVER_V4))
	{
		inPos = 0x00;
		for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x04)
			trkInf[curTrk].startOfs = ReadLE32(&SongData[inPos]);
	}
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		// set the "length" to the next track's offset - makes handling command 00 easier
		UINT32 nextTrkOfs = (curTrk + 1 < trkCnt) ? trkInf[curTrk+1].startOfs : SongLen;
		nextTrkOfs = (nextTrkOfs >= trkInf[curTrk].startOfs && nextTrkOfs <= SongLen) ? nextTrkOfs : SongLen;
		
		tempTInf = &trkInf[curTrk];
		tempTInf->loopOfs = 0x00;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		tempTInf->trkID = curTrk;
		
		PreparseMsDrvTrack(nextTrkOfs, SongData, tempTInf, 0);
		if (tempTInf->loopOfs)
			PreparseMsDrvTrack(nextTrkOfs, SongData, tempTInf, 1);	// pass #2 to count the actual loop length
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	curBPM = 120;
	tempoMod = 0x40;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		inPos = tempTInf->startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (fileVer == FILEVER_V2)
		{
			if (curTrk < 3)
				chnMode = 0x40 + curTrk;
			else
				chnMode = 0x50 + (curTrk - 3);
		}
		else
		{
			chnMode = 0xFF;
		}
		
		loopIdx = 0;
		trkFlags = 0x00;
		if (! inPos)
			trkFlags |= 0x80;
		MTS.midChn = curTrk;
		curNoteVol = 0x7F;
		if ((chnMode & 0xF0) == 0x40)
			curNoteMove = +24;	// SSG channel
		else
			curNoteMove = 0;	// MIDI/FM channel
		
		pbRange = 0;
		curPBend = 0;
		pSldCur = 0;
		pSldDelta = 0;
		pSldTarget = 0;
		pSldNextTick = (UINT32)-1;
		
		sysExBPos = 0x00;
		sysExChkSum = 0x00;
		RunNoteCnt = 0;
		subEndOfs = 0x00;
		subRetOfs = 0x00;
		if (fileVer == FILEVER_V2)
			trkFlags |= 0x02;	// default to 3-byte note mode
		lastNote = 48;
		
		trkTick = MTS.curDly;
		while(! (trkFlags & 0x80) && inPos < SongLen)
		{
			UINT8 evtDly = 0;
			if (pSldDelta != 0)
			{
				// handle pitch slides
				// used by stjan2_98/ZAKOH12.BM2 and ZAKOPLAY.BM2
				while(trkTick > pSldNextTick && pSldCur != pSldTarget)
				{
					UINT32 tempDly = trkTick - pSldNextTick;
					MTS.curDly -= tempDly;
					pSldNextTick ++;
					
					if (pSldCur < pSldTarget)
					{
						pSldCur += pSldDelta;
						if (pSldCur > pSldTarget)
							pSldCur = pSldTarget;
					}
					else if (pSldCur > pSldTarget)
					{
						pSldCur -= pSldDelta;
						if (pSldCur < pSldTarget)
							pSldCur = pSldTarget;
					}
					
					tempSSht = curPBend + pSldCur;
					if (pbRange != 0xFF && tempSSht != 0)
						tempSSht = tempSSht * 8192 / pbRange / 256;
					WritePitchBend(&midFileInf, &MTS, tempSSht);
					MTS.curDly += tempDly;
				}
				if (pSldCur == pSldTarget)
					pSldNextTick = (UINT32)-1;
			}
			
			if (subEndOfs && inPos >= subEndOfs)
			{
				subEndOfs = 0x00;
				inPos = subRetOfs;
			}
			
			curCmd = SongData[inPos];
			if (curCmd < 0x80)
			{
				UINT8 curNoteLen;
				UINT8 curNoteDly;
				
				if (trkFlags & 0x02)
				{
					curCmd = SongData[inPos + 0x00];
					curNoteDly = SongData[inPos + 0x01];
					curNoteLen = SongData[inPos + 0x02];
					inPos += 0x03;
				}
				else
				{
					curCmd = SongData[inPos + 0x00];
					curNoteDly = SongData[inPos + 0x01];
					curNoteLen = SongData[inPos + 0x02];
					curNoteVol = SongData[inPos + 0x03];
					inPos += 0x04;
				}
				
				curNote = curCmd;
				if (! (trkFlags & 0x01))
				{
					if (curNote < curNoteMove)
						curNote = 0x00;
					else if (curNote + curNoteMove > 0x7F)
						curNote = 0x7F;
					else
						curNote += curNoteMove;
				}
				if (! curNoteLen)	// length == 0 -> rest (confirmed with MIDI log of sound driver)
					curNote = 0x00;
				
				CheckRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes);
				for (tempByt = 0x00; tempByt < RunNoteCnt; tempByt ++)
				{
					if (RunNotes[tempByt].note == curNote)
					{
						RunNotes[tempByt].remLen = MTS.curDly + curNoteLen;
						break;
					}
				}
				if (tempByt >= RunNoteCnt && curNote > 0x00)
				{
					WriteEvent(&midFileInf, &MTS, 0x90, curNote, curNoteVol);
					AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
									MTS.midChn, curNote, 0x80, curNoteLen);	// The sound driver sends 9# note 00.
				}
				
				evtDly = curNoteDly;
			}
			else
			{
				switch(curCmd)
				{
				case 0x80:	// set Tick resolution
					printf("Warning Track %u: Sequence tries to change tick resolution at %04X\n", curTrk, inPos);
					tempSht = ReadLE16(&SongData[inPos + 0x01]);
					inPos += 0x03;
					break;
				case 0x81:	// OPL register write
					printf("Track %u: OPL write at %04X: mode %02X reg %02X data %02X\n", curTrk, inPos,
							SongData[inPos + 0x01], SongData[inPos + 0x02], SongData[inPos + 0x03]);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x04;
					break;
				case 0x82:	// Set Instrument
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					inPos += 0x02;
					break;
				case 0x83:	// Subroutine (repeat previous part)
					if (fileVer == FILEVER_V2)
					{
						printf("Track %u: Ignored unknown command %02X at %04X\n", curTrk, curCmd, inPos);
						if (DebugCtrls)
						{
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
						}
						inPos += 0x02;
					}
					else
					{
						UINT32 startPos;
						UINT32 endPos;
						
						inPos += 0x01;
						startPos = ReadLE32(&SongData[inPos]);	inPos += 0x04;
						endPos = ReadLE32(&SongData[inPos]);	inPos += 0x04;
						if (startPos > endPos)
							printf("Warning Track %u: Subroutine StartOfs %04X > EndOfs %04X (at %04X)\n",
									curTrk, startPos, endPos, curCmd, inPos);
						if (subEndOfs)
						{
							printf("Error Track %u: Nested subroutines!\n", curTrk);
							break;	// just ignore them
						}
						subRetOfs = inPos;
						inPos = tempTInf->startOfs + startPos;
						subEndOfs = tempTInf->startOfs + endPos;
						// In MsDRV v4, it works like this:
						//  - enable "subroutine mode" + save old offset
						//  - jump to subroutine offset
						//  - make a backup of the byte at "subroutine end offset"
						//  - overwrite the byte at "subroutine end offset" with command 0x84
						// When reaching command 0x84:
						//  - leave "subroutine mode"
						//  - restore the overwritten byte from the backup
						//  - return to saved offset
					}
					break;
				case 0x84:	// Return / GoTo
					if (fileVer == FILEVER_V2)	// GoTo
					{
						tempSSht = (INT16)ReadLE16(&SongData[inPos + 0x01]);
						if (tempSSht < 0)
						{
							printf("Warning Track %u: Stopping on backwards jump at %04X\n", curTrk, inPos);
							trkFlags |= 0x80;
						}
						inPos += tempSSht;
					}
					else	// Subroutine Return
					{
						printf("Error Track %u: Invalid Subroutine Return command at %04X\n", curTrk, inPos);
						trkFlags |= 0x80;
					}
					break;
				case 0x85:	// set Volume
					if (FixVolume && ! (trkFlags & 0x01))
						tempByt = DB2Mid(OPN2DB(SongData[inPos + 0x01] ^ 0x7F));
					else
						tempByt = SongData[inPos + 0x01];
					// Note: Velocity 0 results in a rest in the sound driver as well
					curNoteVol = tempByt;
					//WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					inPos += 0x02;
					break;
				case 0x8A:	// Tempo in BPM
					curBPM = SongData[inPos + 0x01];
					tempLng = Tempo2Mid(curBPM, tempoMod);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					inPos += 0x02;
					break;
				case 0x8B:	// switch note format (3/4 bytes)
					if (fileVer == FILEVER_V2)
					{
						inPos += 0x01;
						break;
					}
					tempByt = SongData[inPos + 0x01];
					if (tempByt == 0x01)
						trkFlags |= 0x02;	// set 3-byte note format
					else
					{
						printf("Error Track %u: Command %02X with unexpected parameter %02X at %04X\n",
								curTrk, curCmd, tempByt, inPos);
						getchar();
					}
					inPos += 0x02;
					break;
				case 0x8C:	// Data 1 Set Byte
				case 0x8E:	// Data 2 Set Byte
					inPos += 0x04;
					break;
				case 0x8D:	// Data 1 Block Copy
				case 0x8F:	// Data 2 Block Copy
					inPos += 0x04 + SongData[inPos + 0x03];
					break;
				case 0x91:	// unknown
					printf("Warning Track %u: Ignored unknown command %02X at %04X\n", curTrk, curCmd, inPos);
					if (DebugCtrls)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x01;
					break;
				case 0x94:	// OPN register write
					printf("Track %u: OPN write at %04X: reg %02X data %02X\n", curTrk, inPos,
							SongData[inPos + 0x01], SongData[inPos + 0x02]);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x03;
					break;
				case 0x96:	// ignored
					if (SongData[inPos + 0x01] || SongData[inPos + 0x02])
					{
						printf("Warning Track %u: Ignored unknown command %02X %02X %02X at %04X\n", curTrk,
							SongData[inPos + 0x00], SongData[inPos + 0x01], SongData[inPos + 0x02], inPos);
						if (DebugCtrls)
						{
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
						}
					}
					inPos += 0x03;
					break;
				case 0x9B:	// Loop End
					if (! loopIdx)
					{
						printf("Warning Track %u: Loop End without Loop Start at 0x%04X - ignoring!\n", curTrk, inPos);
						if (DebugCtrls)
						{
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x7F);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
						}
						//trkFlags |= 0x80;
						inPos += 0x02;	// The driver just ignores the loop then.
						break;
					}
					loopIdx --;
					loopCount[loopIdx] ++;
					tempSht = SongData[inPos + 0x01];
					if (! tempSht || tempSht >= 0xF0)	// infinite loop
					{
						if (loopCount[loopIdx] <= 0x7F)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCount[loopIdx]);
						tempSht = tempTInf->loopTimes;
					}
					if (loopCount[loopIdx] < tempSht)
					{
						// loop back
						inPos = loopPos[loopIdx];
						loopIdx ++;
					}
					else
					{
						// finish loop
						inPos += 0x02;
					}
					break;
				case 0x9C:	// Loop Start
					if (inPos == tempTInf->loopOfs)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, 0);
					inPos += 0x01;
					loopPos[loopIdx] = inPos;
					loopCount[loopIdx] = 0;
					loopIdx ++;
					break;
				case 0x9D:	// Detune
					tempSSht = (INT8)SongData[inPos + 0x01];
					tempSSht *= 8;
					WritePitchBend(&midFileInf, &MTS, tempSSht);
					inPos += 0x02;
					break;
				case 0x9E:	// ignored (used for padding)
					inPos += 0x01;
					break;
				case 0x9F:	// set Pan
					if (fileVer == FILEVER_V2)
						tempByt = PanBits2MidiPan(SongData[inPos + 0x01]);
					else
						tempByt = (SongData[inPos + 0x01] ^ 0x80) >> 1;	// 80..FF,00..7F -> 00..3F,40..7F
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0xA4:	// Pitch Bend
					// Note: MSB = semitone, LSB = fraction
					curPBend = ReadLE16(&SongData[inPos + 0x01]);
					tempSSht = curPBend + pSldCur;
					if (pbRange != 0xFF && tempSSht != 0)
					{
						if (NeedPBRangeFix(&pbRange, tempSSht))
						{
							// write Pitch Bend Range
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x65, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x64, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, pbRange);
						}
						tempSSht = tempSSht * 8192 / pbRange / 256;
					}
					WritePitchBend(&midFileInf, &MTS, tempSSht);
					inPos += 0x03;
					break;
				case 0xA5:	// set OPN/OPNA mode
					// Note: actually invalid in MsDrv v4
					tempByt = SongData[inPos + 0x01];
					printf("Track %u: OPNA mode enable = %u at %04X\n", curTrk, tempByt, inPos);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x02;
					break;
				case 0xA6:	// set OPNA LFO speed
					tempByt = SongData[inPos + 0x01];
					//Reg022_data = tempByt ? (0x07 + tempByt) : 0x00;
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x02;
					break;
				case 0xA7:	// ??
					inPos += 0x03;
					break;
				case 0xA8:	// ??
					inPos += 0x02;
					break;
				case 0xA9:	// Special FM3 frequency mode enable
					tempByt = SongData[inPos + 0x01];
					printf("Track %u: Special FM3 mode enable = %u at %04X\n", curTrk, tempByt, inPos);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x02;
					break;
				case 0xAA:	// set Special FM3 key on operator mask
					tempByt = SongData[inPos + 0x01];
					printf("Track %u: Special FM3 mode enable = %u at %04X\n", curTrk, tempByt, inPos);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x02;
					break;
				case 0xAB:	// ?? (modulation-related?)
					inPos += 0x03;
					break;
				case 0xAC:	// ?? (modulation-related?)
					inPos += 0x03;
					break;
				case 0xAD:	// set Pitch Slide step
					pSldDelta = (INT16)ReadLE16(&SongData[inPos + 0x01]);
					//printf("Track %u: Pitch Slide step = %d at %04X\n", curTrk, pSldDelta, inPos);
					pSldNextTick = trkTick;
					inPos += 0x03;
					break;
				case 0xAE:	// set Pitch Slide destination
					pSldTarget = ReadLE16(&SongData[inPos + 0x01]);
					//printf("Track %u: Pitch Slide target = %+d at %04X\n", curTrk, pSldTarget, inPos);
					pSldNextTick = trkTick;
					
					if (pbRange != 0xFF && pSldTarget != 0)
					{
						if (NeedPBRangeFix(&pbRange, curPBend + pSldTarget))
						{
							// write Pitch Bend Range
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x65, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x64, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, pbRange);
						}
					}
					inPos += 0x03;
					break;
				case 0xAF:	// set Pitch Slide state (current value)
					pSldCur = (INT16)ReadLE16(&SongData[inPos + 0x01]);
					pSldNextTick = trkTick;
					
					tempSSht = curPBend + pSldCur;
					if (pbRange != 0xFF && tempSSht != 0)
					{
						if (NeedPBRangeFix(&pbRange, tempSSht))
						{
							// write Pitch Bend Range
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x65, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x64, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, pbRange);
						}
						tempSSht = tempSSht * 8192 / pbRange / 256;
					}
					WritePitchBend(&midFileInf, &MTS, tempSSht);
					//if (pSldCur != 0)
					//	printf("Track %u: Pitch slide state = +%d at %04X\n", curTrk, pSldCur, inPos);
					inPos += 0x03;
					break;
				case 0xB0:	// set Pan Left
				case 0xB1:	// set Pan Right
					if (curCmd & 0x01)
						tempByt = 0x40 + (SongData[inPos + 0x01] >> 1);	// 00..7F -> 40..7F
					else
						tempByt = 0x40 - (SongData[inPos + 0x01] >> 1);	// 00..7F -> 40..01
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0xC1:	// set Communication value
					tempSht = ReadLE16(&SongData[inPos + 0x01]);
					printf("Track %u: Set Marker = %u at position 0x%04X\n", curTrk, tempSht, inPos);
					sprintf(tempStr, "Marker = %u", tempSht);
					WriteMetaEvent(&midFileInf, &MTS, 0x06, strlen(tempStr), tempStr);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, tempSht & 0x7F);
					inPos += 0x03;
					break;
				case 0xC2:	// reset GS checksum state
					sysExChkSum = 0x00;
					inPos += 0x01;
					break;
				case 0xC3:	// send 1 byte of SysEx data
					tempByt = SongData[inPos + 0x01];
					retVal = CacheSysExData(0x80, sysExBuf, &sysExBPos, tempByt,
											&midFileInf, &MTS, curCmd, curTrk, inPos);
					sysExChkSum += tempByt;
					inPos += 0x02;
					break;
				case 0xC4:	// send Roland SysEx checksum
					if (sysExBPos >= 0x80)
					{
						printf("Warning Track %u: Event %02X buffer overflow at %04X!\n",
								curTrk, curCmd, inPos);
						sysExBPos --;
					}
					else
					{
						sysExBuf[sysExBPos] = -sysExChkSum & 0x7F;
						sysExBPos ++;
					}
					inPos += 0x01;
					break;
				case 0xC5:	// send multiple bytes of SysEx data
					tempSht = ReadLE16(&SongData[inPos + 0x01]);
					for (syxPos = 0x00; syxPos < tempSht; syxPos ++)
					{
						tempByt = SongData[inPos + 0x03 + syxPos];
						retVal = CacheSysExData(0x80, sysExBuf, &sysExBPos, tempByt,
												&midFileInf, &MTS, curCmd, curTrk, inPos);
						sysExChkSum += tempByt;
					}
					inPos += 0x03 + tempSht;
					break;
				case 0xD0:	// set OPNA Rhythm Mask
					evtDly = SongData[inPos + 0x01];
					// I don't print a warning here, as it may spam the console.
					if (DebugCtrls)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x3F, SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xD1:	// set OPNA Rhythm channel 1 volume
				case 0xD2:	// set OPNA Rhythm channel 2 volume
				case 0xD3:	// set OPNA Rhythm channel 3 volume
				case 0xD4:	// set OPNA Rhythm channel 4 volume
				case 0xD5:	// set OPNA Rhythm channel 5 volume
				case 0xD6:	// set OPNA Rhythm channel 6 volume
					tempByt = curCmd - 0xD1;	// rhythm channel ID
					//printf("Warning Track %u: Unimplemented: Setting OPNA rhythm ch %u volume at %04X\n", curTrk, tempByt, inPos);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x02;
					break;
				case 0xDD:	// set SysEx Offset high/mid
					sysExData[0] = SongData[inPos + 0x02];
					sysExData[1] = SongData[inPos + 0x03];
					if (fileVer != FILEVER_V2)	// skip for correct timing in urban_98
						evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xDE:	// set SysEx Offset low + Data, send SysEx
					sysExData[2] = SongData[inPos + 0x02];
					sysExData[3] = SongData[inPos + 0x03];
					// Generate Roland GS SysEx command
					tempArr[0x00] = 0x41;			// Roland ID
					tempArr[0x01] = sysExHdr[0];	// Device ID
					tempArr[0x02] = sysExHdr[1];	// Model ID (0x42 == GS)
					tempArr[0x03] = 0x12;			// Command ID (0x12 == DT1)
					memcpy(&tempArr[0x04], sysExData, 0x04);
					tempArr[0x08] = CalcGSChecksum(0x04, sysExData);
					tempArr[0x09] = 0xF7;
					tempByt = 0x0A;	// SysEx data size
					
					WriteLongEvent(&midFileInf, &MTS, 0xF0, tempByt, tempArr);
					if (fileVer != FILEVER_V2)	// skip for correct timing in urban_98
						evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xDF:	// set SysEx Device ID + Model ID
					sysExHdr[0] = SongData[inPos + 0x02];	// Device ID
					sysExHdr[1] = SongData[inPos + 0x03];	// Model ID
					if (fileVer != FILEVER_V2)	// skip for correct timing in urban_98
						evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xE2:	// set MIDI instrument with Bank MSB/LSB
					tempByt = SongData[inPos + 0x02];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x00, SongData[inPos + 0x03]);	// Bank MSB
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x20, 0x00);	// Bank LSB (fixed to 0)
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xE6:	// set MIDI Channel
					chnMode = SongData[inPos + 0x02];
					MTS.midChn = chnMode & 0x0F;
					curNoteMove = 0;
					if (chnMode < 0x20)	// 32 MIDI channels (2 parts, 16 channels each)
					{
						trkFlags |= 0x01;
						pbRange = 0xFF;
						WriteEvent(&midFileInf, &MTS, 0xFF, 0x21, 0x01);
						midFileInf.data[midFileInf.pos] = chnMode >> 4;
						midFileInf.pos ++;
						WriteEvent(&midFileInf, &MTS, 0xFF, 0x20, 0x01);
						midFileInf.data[midFileInf.pos] = MTS.midChn;
						midFileInf.pos ++;
					}
					else
					{
						// used by later version only?
						if ((chnMode & 0xF0) == 0x40)
						{
							curNoteMove = +24;	// SSG channel
							MTS.midChn += 10;
						}
						else if ((chnMode & 0xF0) == 0x50)
						{
							curNoteMove = 0;	// FM/OPN channel
						}
						else if ((chnMode & 0xF8) == 0xF0)
						{
							curNoteMove = 0;	// FM/OPN FM3 channel
							MTS.midChn += 6;
						}
						else if ((chnMode & 0xF0) == 0x70 || (chnMode & 0xF0) == 0x80)
						{
							if (chnMode >= 0x7F)
								MTS.midChn = chnMode - 0x7C;
							curNoteMove = 0;	// FM/OPL channel
						}
					}
					evtDly = SongData[inPos + 0x01];
					inPos += 0x03;
					break;
				case 0xE7:	// Tempo Modifier
					if (tempoChgTrk > curTrk || tempoChgPos > inPos)
						printf("Warning Track %u: Tempo Modifier at %04X\n", curTrk, inPos);
					tempoMod = SongData[inPos + 0x02];
					// I've only seen the second parameter byte to be 00 and the driver ignores it.
					// The RCP format has it as tempo slide speed.
					if (SongData[inPos + 0x03] != 0x00)
					{
						printf("Warning Track %u: Unsupported tempo slide speed 0x%02X at %04X\n",
								curTrk, curCmd, SongData[inPos + 0x03], inPos);
						getchar();
					}
					if (! tempoMod)
						tempoMod = 0x40;	// just for safety
					if (fileVer == FILEVER_V2)
					{
						// The old driver simply ignores the command.
						if (DebugCtrls)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x03, tempoMod & 0x7F);
					}
					else
					{
						// TODO: This goes horribly wrong when a song should split 8A and E7 commands over multiple tracks.
						// I haven't seen this in any song though.
						tempLng = Tempo2Mid(curBPM, tempoMod);
						WriteBE32(tempArr, tempLng);
						WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					}
					evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xEA:	// MIDI Channel Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xD0, SongData[inPos + 0x02], 0x00);
					evtDly = SongData[inPos + 0x01];
					inPos += 0x03;
					break;
				case 0xEB:	// MIDI Controller
					WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x02], SongData[inPos + 0x03]);
					evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xEC:	// MIDI Instrument
					tempByt = SongData[inPos + 0x02];
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					evtDly = SongData[inPos + 0x01];
					inPos += 0x03;
					break;
				case 0xED:	// MIDI Note Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xA0, SongData[inPos + 0x02], SongData[inPos + 0x03]);
					evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xEE:	// Pitch Bend with Delay
					curPBend = ReadLE16(&SongData[inPos + 0x02]);
					tempSSht = curPBend + pSldCur;
					if (pbRange != 0xFF && tempSSht != 0)
					{
						if (NeedPBRangeFix(&pbRange, tempSSht))
						{
							// write Pitch Bend Range
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x65, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x64, 0x00);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, pbRange);
						}
						tempSSht = tempSSht * 8192 / pbRange / 256;
					}
					WritePitchBend(&midFileInf, &MTS, tempSSht);
					evtDly = SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xFE:	// Track End
				case 0xFF:	// Song End (stops all tracks)
					trkFlags |= 0x80;
					inPos += 0x01;
					break;
				default:
					printf("Error Track %u: Unknown command 0x%02X at position 0x%04X!\n", curTrk, curCmd, inPos);
					if (DebugCtrls)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkFlags |= 0x80;
					break;
				}
			}
			MTS.curDly += evtDly;
			trkTick += evtDly;
			if (fileVer == FILEVER_V4)
				inPos = (inPos + 0x03) & ~0x03;	// 4-byte padding
		}
		if (inPos >= SongLen && ! (trkFlags & 0x80))
			printf("Warning: Reached EOF early on track %u!\n", curTrk);
		FlushRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes, 0);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

UINT8 MsDrv2Mid_v1(UINT32 SongLen, const UINT8* SongData)
{
	TRK_INF trkInf[0x10];
	TRK_INF* tempTInf;
	UINT8 DELAY_MODE;
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT32 inPos;
	UINT32 trkTick;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 chnMode;	// Bit 7 - track end
	UINT8 trkFlags;
	UINT8 tieFlag;
	UINT8 curCmd;
	UINT8 pbRange;
	UINT8 curBPM;
	
	UINT8 loopIdx;
	UINT16 loopCount[8];
	UINT32 loopPos[8];
	
	UINT32 tempLng;
	UINT16 tempSht;
	UINT8 tempByt;
	UINT8 tempArr[0x10];
	
	UINT8 curOct;
	UINT8 curNote;
	UINT8 curNoteVol;
	UINT8 curNoteLen;
	INT8 curNoteMove;
	UINT8 noteLenMod;
	UINT8 lastNote;
	
	tempSht = ReadLE16(&SongData[0x00]);
	if (forcedFileVer != 0xFF)
	{
		const char* fmtStr;
		fileVer = forcedFileVer;
		fmtStr = (fileVer == FILEVER_V1A) ? "v1a" : "v1c";
		trkCnt = (UINT8)tempSht / 2;
		tempByt = trkCnt;
		if (!DebugCtrls)
		{
			if (trkCnt > 8)
				trkCnt = 8;	// Only the first 8 tracks are processed and others often contains garbage.
		}
		printf("Format: %s, %u tracks\n", fmtStr, trkCnt);
	}
	else
	{
		const char* fmtStr;
		fileVer = FILEVER_V1C;	// we default to v1c
		fmtStr = (fileVer == FILEVER_V1A) ? "v1a" : "v1c";
		trkCnt = (UINT8)tempSht / 2;
		tempByt = trkCnt;
		if (!DebugCtrls)
		{
			if (trkCnt > 8)
				trkCnt = 8;	// Only the first 8 tracks are processed and others often contains garbage.
		}
		if (trkCnt == tempByt)
			printf("Detected format: %s, %u tracks\n", fmtStr, trkCnt);
		else
			printf("Detected format: %s, %u pointers/%u tracks\n", fmtStr, tempByt, trkCnt);
	}
	if (trkCnt > sizeof(trkInf) / sizeof(trkInf[0]))
		trkCnt = (UINT8)(sizeof(trkInf) / sizeof(trkInf[0]));
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	{
		inPos = 0x00;
		for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
			trkInf[curTrk].startOfs = ReadLE16(&SongData[inPos]);
	}
	if (fileVer == FILEVER_V1A)
	{
		DELAY_MODE = 1;
		MIDI_RES = 24;	// This driver runs with half the rate.
	}
	else
	{
		DELAY_MODE = 0;
		MIDI_RES = 48;
	}
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		// set the "length" to the next track's offset - makes handling command 00 easier
		UINT32 nextTrkOfs = (curTrk + 1 < trkCnt) ? trkInf[curTrk+1].startOfs : SongLen;
		nextTrkOfs = (nextTrkOfs >= trkInf[curTrk].startOfs && nextTrkOfs <= SongLen) ? nextTrkOfs : SongLen;
		
		tempTInf = &trkInf[curTrk];
		tempTInf->loopOfs = 0x00;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		tempTInf->trkID = curTrk;
		
		PreparseMsDrvTrack_v1(nextTrkOfs, SongData, tempTInf, fileVer, 0);
		if (tempTInf->loopOfs)
			PreparseMsDrvTrack_v1(nextTrkOfs, SongData, tempTInf, fileVer, 1);	// pass #2 to count the actual loop length
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	curBPM = 120;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		inPos = tempTInf->startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (DEFAULT_FM_MODE)
		{
			if (curTrk < 3)
				chnMode = 0x40 + curTrk;
			else
				chnMode = 0x50 + (curTrk - 3);
		}
		else
		{
			chnMode = curTrk;
		}
		
		loopIdx = 0;
		trkFlags = 0x00;
		if (! inPos)
			trkFlags |= 0x80;
		if ((chnMode & 0xF0) == 0x00)
			MTS.midChn = 0x02 + curTrk;
		else
			MTS.midChn = curTrk;
		curNoteVol = 0x7F;
		if ((chnMode & 0xF0) == 0x40)
			curNoteMove = +24;	// SSG channel
		else
			curNoteMove = 0;	// MIDI/FM channel
		
		pbRange = 0;
		
		curOct = 4;
		lastNote = 0xFF;
		curNoteLen = 48;
		noteLenMod = 8;
		tieFlag = 0x00;
		
		trkTick = MTS.curDly;
		while(! (trkFlags & 0x80) && inPos < SongLen)
		{
			UINT8 evtDly = 0;
			
			curCmd = SongData[inPos];
			if (curCmd >= 0x01 && curCmd <= 0x0D)
			{
				UINT8 realNoteLen;
				
				if (curCmd == 0x0D)	// 0D - rest
					curNote = 0xFF;
				else	// 01..0C - notes
					curNote = (curOct + 1) * 12 + (curCmd - 1);
				inPos ++;
				
				realNoteLen = curNoteLen * noteLenMod / 8;	// the driver does NOT do any rounding
				
				if ((tieFlag & 0x02) && lastNote == curNote)
				{
					// do nothing - keep previous note playing
				}
				else
				{
					if (lastNote != 0xFF)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
						lastNote = 0xFF;
					}
					if (curNote != 0xFF && realNoteLen > 0)	// note length 0 produces no notes
					{
						WriteEvent(&midFileInf, &MTS, 0x90, curNote, curNoteVol);
						lastNote = curNote;
					}
				}
				evtDly = curNoteLen;
				tieFlag <<= 1;
				
				if (realNoteLen < curNoteLen)
				{
					MTS.curDly += realNoteLen;
					trkTick += realNoteLen;
					evtDly -= realNoteLen;
					if (lastNote != 0xFF)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
						lastNote = 0xFF;
					}
				}
			}
			else if ((curCmd & 0xF0) == 0xC0 || (curCmd & 0xF0) == 0xE0)
			{
				inPos ++;
				//if ((curCmd & 0x0F) == 0x0F)	// Some tracks in sweet_e_98 rely on this being 32 ticks.
				//	printf("Track %u: Using last delay (cmd %02X) at %04X\n", curTrk, curCmd, inPos);
				if (curCmd & 0x20)
					curNoteLen = DELAY_LUT[DELAY_MODE][curCmd & 0x0F];	// E0..EF - set length
				else
					curNoteLen += DELAY_LUT[DELAY_MODE][curCmd & 0x0F];	// C0..CF - add to length
			}
			else
			{
				switch(curCmd)
				{
				case 0x81:	// set current octave
					curOct = SongData[inPos + 0x01];
					if (curOct > 8)
						curOct = 8;	// not done by the driver, done here for safety (and ML12.MD1)
					inPos += 0x02;
					break;
				case 0x82:	// Set Instrument
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					inPos += 0x02;
					break;
				case 0x83:	// set MIDI Channel
					chnMode = SongData[inPos + 0x01];
					MTS.midChn = chnMode & 0x0F;
					curNoteMove = 0;
					{
						trkFlags |= 0x01;
						pbRange = 0xFF;
						WriteEvent(&midFileInf, &MTS, 0xFF, 0x20, 0x01);
						midFileInf.data[midFileInf.pos] = MTS.midChn;
						midFileInf.pos ++;
					}
					inPos += 0x02;
					break;
				case 0x84:	// GoTo
					// I haven't yet seen any song that uses this command.
					printf("Track %u: GoTo at %04X\n", curTrk, inPos);
					getchar();
					{
						tempSht = ReadLE16(&SongData[inPos + 0x01]);
						if (tempSht < inPos)
						{
							printf("Warning Track %u: Stopping on backwards jump at %04X\n", curTrk, inPos);
							trkFlags |= 0x80;
						}
						inPos = tempSht;
					}
					break;
				case 0x85:	// set Volume
					if (FixVolume && ! (trkFlags & 0x01))
						tempByt = DB2Mid(OPN2DB(SongData[inPos + 0x01] ^ 0x7F));
					else
						tempByt = SongData[inPos + 0x01];
					// Note: Velocity 0 results in a rest in the sound driver as well
					curNoteVol = tempByt;
					//WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					inPos += 0x02;
					break;
				case 0x86:	// ignored
				case 0x87:	// ignored
					printf("Warning Track %u: Ignored unknown command %02X %02X at %04X\n",
						curTrk, curCmd, SongData[inPos + 0x01], inPos);
					inPos += 0x02;
					break;
				case 0x88:	// octave up
					if (curOct < 7)
						curOct ++;
					inPos += 0x01;
					break;
				case 0x89:	// octave down
					if (curOct > 0)
						curOct --;
					inPos += 0x01;
					break;
				case 0x8A:	// Tempo in BPM
					curBPM = SongData[inPos + 0x01];
					tempLng = Tempo2Mid(curBPM, 0x40);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					inPos += 0x02;
					break;
				case 0x8C:	// ignored
				case 0x8D:	// ignored
				case 0x8E:	// ignored
					printf("Warning Track %u: Ignored unknown command %02X %02X at %04X\n",
						curTrk, curCmd, SongData[inPos + 0x01], inPos);
					inPos += 0x02;
					break;
				case 0x94:	// Pitch Bend
					WriteEvent(&midFileInf, &MTS, 0xE0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0x95:	// Note Tie
					tieFlag |= 0x01;
					inPos += 0x01;
					break;
				case 0x96:	// Bar ID
					// Some songs seem to use this command to indicate the current bar.
					//printf("Warning Track %u: Ignored unknown command %02X %02X at %04X\n",
					//	curTrk, curCmd, SongData[inPos + 0x01], inPos);
					inPos += 0x02;
					break;
				case 0x97:	// set volume or instrument
					tempByt = SongData[inPos + 0x02];
					inPos += 0x02;
					switch(tempByt)
					{
					case 0x00:	// set volume
						if (FixVolume && ! (trkFlags & 0x01))
							tempByt = DB2Mid(OPN2DB(SongData[inPos] ^ 0x7F));
						else
							tempByt = SongData[inPos];
						// Note: Velocity 0 results in a rest in the sound driver as well
						curNoteVol = tempByt;
						//WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
						inPos ++;
						break;
					case 0x01:	// set instrument
						WriteEvent(&midFileInf, &MTS, 0xC0, SongData[inPos], 0x00);
						inPos ++;
						break;
					}
					break;
				case 0x98:	// set delay ticks
					curNoteLen = SongData[inPos + 0x02];
					inPos += 0x02;
					break;
				case 0x99:	// note length modifier
					noteLenMod = SongData[inPos + 0x01];
					inPos += 0x02;
					break;
				case 0x9A:	// Return
					// This command is for driver-internal use of handling the note length modifier.
					printf("Error Track %u: Invalid Return command at %04X\n", curTrk, inPos);
					trkFlags |= 0x80;
					break;
				case 0x9B:	// Loop End
					if (! loopIdx)
					{
						printf("Warning Track %u: Loop End without Loop Start at 0x%04X - ignoring!\n", curTrk, inPos);
						if (DebugCtrls)
						{
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x7F);
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
						}
						//trkFlags |= 0x80;
						inPos += 0x02;	// The driver just ignores the loop then.
						// pleria_98/PLE17 (CM/64/SC) have invalid Loop End commands that need to be ignored.
						// The songs work properly with this behaviour.
						break;
					}
					loopIdx --;
					loopCount[loopIdx] ++;
					tempSht = SongData[inPos + 0x01];
					if (! tempSht || tempSht >= 0xF0)	// infinite loop
					{
						if (loopCount[loopIdx] <= 0x7F)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCount[loopIdx]);
						tempSht = tempTInf->loopTimes;
					}
					if (loopCount[loopIdx] < tempSht)
					{
						// loop back
						inPos = loopPos[loopIdx];
						loopIdx ++;
					}
					else
					{
						// finish loop
						inPos += 0x02;
					}
					break;
				case 0x9C:	// Loop Start
					if (inPos == tempTInf->loopOfs)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, 0);
					inPos += 0x01;
					loopPos[loopIdx] = inPos;
					loopCount[loopIdx] = 0;
					loopIdx ++;
					break;
				case 0x9D:	// ignored
					printf("Warning Track %u: Ignored unknown command %02X %02X at %04X\n",
						curTrk, curCmd, SongData[inPos + 0x01], inPos);
					inPos += 0x02;
					break;
				case 0x9F:	// Set Pan
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xFE:	// Track End
				case 0xFF:	// Song End
					trkFlags |= 0x80;
					inPos += 0x01;
					break;
				default:
					printf("Error Track %u: Unknown command 0x%02X at position 0x%04X!\n", curTrk, curCmd, inPos);
					if (DebugCtrls)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkFlags |= 0x80;
					break;
				}
			}
			MTS.curDly += evtDly;
			trkTick += evtDly;
		}
		if (lastNote != 0xFF)
			WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
		if (inPos >= SongLen && ! (trkFlags & 0x80))
			printf("Warning: Reached EOF early on track %u!\n", curTrk);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static UINT8 CacheSysExData(UINT16 sxBufSize, UINT8* sxBuffer, UINT8* sxBufPos, UINT8 data,
							FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 curCmd, UINT8 curTrk, UINT16 cmdPos)
{
	UINT8 resVal;
	
	resVal = 0x00;
	if ((data & 0x80) && data != 0xF7)
	{
		// flush previous data
		if (*sxBufPos)
		{
			if (sxBuffer[0x00] == 0xF0 || sxBuffer[0x00] == 0xF7)
			{
				WriteLongEvent(fInf, MTS, sxBuffer[0x00], *sxBufPos - 0x01, &sxBuffer[0x01]);
			}
			else
			{
				printf("Event %02X Warning: MIDI event %02X in raw buffer! (detected in track %u at %04X)\n",
					curCmd, sxBuffer[0x00], curTrk, cmdPos);
			}
		}
		*sxBufPos = 0x00;
		resVal |= 0x01;	// did flush
		//printf("Info: sending raw MIDI data. (track %u at %04X)\n", curTrk, cmdPos);
	}
	if (*sxBufPos >= 0x80)
	{
		printf("Event %02X Warning: buffer overflow! (track %u at %04X)\n",
				curCmd, curTrk, cmdPos);
		*sxBufPos --;
	}
	sxBuffer[*sxBufPos] = data;
	(*sxBufPos) ++;
	if (*sxBufPos > 0x00 && data == 0xF7)
	{
		// on SysEx end command, flush data
		WriteLongEvent(fInf, MTS, sxBuffer[0x00], *sxBufPos - 0x01, &sxBuffer[0x01]);
		*sxBufPos = 0x00;
		resVal |= 0x01;	// did flush
	}
	
	return resVal;
}

static void PreparseMsDrvTrack(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, UINT8 Mode)
{
	// This function detects the offset of the master loop and counts the total + loop length.
	UINT32 inPos;
	UINT8 curCmd;
	UINT8 trkFlags;
	
	UINT32 subEndOfs;
	UINT32 subRetOfs;
	
	UINT8 loopIdx;
	UINT8 loopCount[8];
	UINT32 loopPos[8];
	
	inPos = trkInf->startOfs;
	if (! Mode)
		trkInf->loopOfs = 0x0000;
	
	loopIdx = 0;
	trkFlags = 0x00;
	subEndOfs = 0x00;
	subRetOfs = 0x00;
	if (fileVer == FILEVER_V2)
		trkFlags |= 0x02;	// default to 3-byte note mode
	while(inPos < SongLen)
	{
		if (subEndOfs && inPos >= subEndOfs)
		{
			subEndOfs = 0x00;
			inPos = subRetOfs;
		}
		
		if (Mode && inPos == trkInf->loopOfs)
			return;
		
		curCmd = SongData[inPos];
		if (curCmd < 0x80)
		{
			UINT32 curNoteDly = SongData[inPos + 0x01];
			if (fileVer == FILEVER_V2)
			{
				if (curNoteDly == 0 && SongData[inPos + 0x02] == 0)
					curNoteDly = 48;	// fix for TWED.MF2 (OPN/OPNA)
			}
			if (trkFlags & 0x02)
				inPos += 0x03;
			else
				inPos += 0x04;
			if (! Mode)
				trkInf->tickCnt += curNoteDly;
			else
				trkInf->loopTick += curNoteDly;
		}
		else
		{
			if (curCmd >= 0xDD && curCmd <= 0xEE)
			{
				if (! Mode)
					trkInf->tickCnt += SongData[inPos + 0x01];
				else
					trkInf->loopTick += SongData[inPos + 0x01];
			}
			switch(curCmd)
			{
			case 0x9B:	// Loop End
				if (! loopIdx)
				{
					inPos += 0x02;
					break;
				}
				loopIdx --;
				loopCount[loopIdx] ++;
				if (! SongData[inPos + 0x01] || SongData[inPos + 0x01] >= 0xF0)	// infinite loop
				{
					trkInf->loopOfs = loopPos[loopIdx] - 0x01;
					return;
				}
				if (loopCount[loopIdx] < SongData[inPos + 0x01])
				{
					// loop back
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
				else
				{
					// finish loop
					inPos += 0x02;
				}
				break;
			case 0x9C:	// Loop Start
				inPos += 0x01;
				loopPos[loopIdx] = inPos;
				loopCount[loopIdx] = 0;
				loopIdx ++;
				break;
			case 0x83:	// Subroutine (repeat previous part)
				if (fileVer == FILEVER_V2)
				{
					inPos += 0x02;
				}
				else
				{
					UINT32 startPos;
					UINT32 endPos;
					
					inPos += 0x01;
					startPos = ReadLE32(&SongData[inPos]);	inPos += 0x04;
					endPos = ReadLE32(&SongData[inPos]);	inPos += 0x04;
					if (subEndOfs)
						break;	// ignore nested subroutines
					subRetOfs = inPos;
					inPos = trkInf->startOfs + startPos;
					subEndOfs = trkInf->startOfs + endPos;
				}
				break;
			case 0x84:	// Return / GoTo
				if (fileVer == FILEVER_V2)	// GoTo
				{
					INT16 jumpPos = (INT16)ReadLE16(&SongData[inPos + 0x01]);
					if (jumpPos < 0)
						return;
					inPos += jumpPos;
				}
				else	// Subroutine Return
				{
					return;
				}
				break;
			case 0x8B:	// switch note format (3/4 bytes)
				if (fileVer == FILEVER_V2)
				{
					inPos += 0x01;
					break;
				}
				if (SongData[inPos + 0x01] == 0x01)
					trkFlags |= 0x02;	// set 3-byte note format
				else
					trkFlags &= ~0x02;	// set 4-byte note format
				inPos += 0x02;
				break;
			case 0x8A:	// Tempo in BPM
				if (tempoChgTick <= trkInf->tickCnt)
				{
					tempoChgTrk = trkInf->trkID;
					tempoChgPos = inPos;
					tempoChgTick = trkInf->tickCnt;
				}
				inPos += 0x02;
				break;
			case 0x91:	// unknown
			case 0x9E:	// ignored
			case 0xC2:	// checksum start marker
			case 0xC4:	// send Roland SysEx checksum
				inPos += 0x01;
				break;
			case 0x82:	// Set Instrument
			case 0x85:	// Set Volume
			case 0x9D:	// Detune
			case 0x9F:	// Set Pan
			case 0xA5:	// set OPN/OPNA mode
			case 0xA6:	// set OPNA LFO speed
			case 0xA8:	// ??
			case 0xA9:	// Special FM3 frequency mode enable
			case 0xAA:	// set Special FM3 key on operator mask
			case 0xB0:	// set Pan Left
			case 0xB1:	// set Pan Right
			case 0xC3:	// send 1 byte of SysEx data
			case 0xD1:	// set OPNA Rhythm channel 1 volume
			case 0xD2:	// set OPNA Rhythm channel 2 volume
			case 0xD3:	// set OPNA Rhythm channel 3 volume
			case 0xD4:	// set OPNA Rhythm channel 4 volume
			case 0xD5:	// set OPNA Rhythm channel 5 volume
			case 0xD6:	// set OPNA Rhythm channel 6 volume
				inPos += 0x02;
				break;
			case 0x94:	// OPN register write
			case 0x96:	// ignored
			case 0xA4:	// Pitch Bend
			case 0xA7:	// ??
			case 0xAB:	// ?? (modulation-related?)
			case 0xAC:	// ?? (modulation-related?)
			case 0xAD:	// set Pitch Slide step
			case 0xAE:	// set Pitch Slide destination
			case 0xAF:	// set Pitch Slide state (current value)
			case 0xC1:	// set Communication value
			case 0xE6:	// set MIDI Channel
			case 0xEA:	// MIDI Channel Aftertouch
			case 0xEC:	// MIDI Instrument
				inPos += 0x03;
				break;
			case 0x81:	// OPL register write
			case 0x8C:	// Data 1 Set Byte
			case 0x8E:	// Data 2 Set Byte
			case 0xDD:	// set SysEx Data 1
			case 0xDE:	// set SysEx Data 2 + send
			case 0xDF:	// set SysEx Device ID + Model ID
			case 0xE2:	// set MIDI instrument with Bank MSB/LSB
			case 0xE7:	// Tempo Modifier
			case 0xEB:	// MIDI Controller
			case 0xED:	// MIDI Note Aftertouch
			case 0xEE:	// Pitch Bend with Delay
				inPos += 0x04;
				break;
			case 0xC5:	// send multiple bytes of SysEx data
				inPos += 0x03 + ReadLE16(&SongData[inPos + 0x01]);
				break;
			case 0x8D:	// Data 1 Block Copy
			case 0x8F:	// Data 2 Block Copy
				inPos += 0x04 + SongData[inPos + 0x03];
				break;
			case 0xD0:	// set OPNA Rhythm Mask
				if (! Mode)
					trkInf->tickCnt += SongData[inPos + 0x01];
				else
					trkInf->loopTick += SongData[inPos + 0x01];
				inPos += 0x03;
				break;
			case 0xFE:	// Track End
			case 0xFF:	// Song End
			default:
				inPos += 0x01;
				return;
			}
		}
		if (fileVer == FILEVER_V4)
			inPos = (inPos + 0x03) & ~0x03;	// 4-byte padding
	}
	
	return;
}

static void PreparseMsDrvTrack_v1(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, UINT8 fileVer, UINT8 Mode)
{
	// This function detects the offset of the master loop and counts the total + loop length.
	UINT8 DELAY_MODE = (fileVer == FILEVER_V1A) ? 1 : 0;
	UINT32 inPos;
	UINT8 curCmd;
	UINT8 curNoteLen;
	
	UINT16 tempSht;
	
	UINT8 loopIdx;
	UINT8 loopCount[8];
	UINT32 loopPos[8];
	
	inPos = trkInf->startOfs;
	if (! Mode)
		trkInf->loopOfs = 0x0000;
	
	curNoteLen = 48;
	loopIdx = 0;
	while(inPos < SongLen)
	{
		if (Mode && inPos == trkInf->loopOfs)
			return;
		
		curCmd = SongData[inPos];
		if (curCmd >= 0x01 && curCmd <= 0x0D)
		{
			inPos ++;
			if (! Mode)
				trkInf->tickCnt += curNoteLen;
			else
				trkInf->loopTick += curNoteLen;
		}
		else if ((curCmd & 0xF0) == 0xC0 || (curCmd & 0xF0) == 0xE0)
		{
			inPos ++;
			if (curCmd & 0x20)
				curNoteLen = DELAY_LUT[DELAY_MODE][curCmd & 0x0F];	// E0..EF - set length
			else
				curNoteLen += DELAY_LUT[DELAY_MODE][curCmd & 0x0F];	// C0..CF - add to length
		}
		else
		{
			switch(curCmd)
			{
			case 0x84:	// GoTo
				{
					tempSht = ReadLE16(&SongData[inPos + 0x01]);
					if (tempSht < inPos)
						return;
					inPos = tempSht;
				}
				break;
			case 0x8A:	// Tempo in BPM
				if (tempoChgTick <= trkInf->tickCnt)
				{
					tempoChgTrk = trkInf->trkID;
					tempoChgPos = inPos;
					tempoChgTick = trkInf->tickCnt;
				}
				inPos += 0x02;
				break;
			case 0x98:	// set delay ticks
				curNoteLen = SongData[inPos + 0x02];
				inPos += 0x02;
				break;
			case 0x9A:	// Return
				return;	// invalid for sequences
			case 0x9B:	// Loop End
				if (! loopIdx)
				{
					inPos += 0x02;
					break;
				}
				loopIdx --;
				loopCount[loopIdx] ++;
				if (! SongData[inPos + 0x01] || SongData[inPos + 0x01] >= 0xF0)	// infinite loop
				{
					trkInf->loopOfs = loopPos[loopIdx] - 0x01;
					return;
				}
				if (loopCount[loopIdx] < SongData[inPos + 0x01])
				{
					// loop back
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
				else
				{
					// finish loop
					inPos += 0x02;
				}
				break;
			case 0x9C:	// Loop Start
				inPos += 0x01;
				loopPos[loopIdx] = inPos;
				loopCount[loopIdx] = 0;
				loopIdx ++;
				break;
			case 0x00:
			case 0x88:	// octave up
			case 0x89:	// octave down
			case 0x95:	// Note Tie
				inPos += 0x01;
				break;
			case 0x81:	// set current octave
			case 0x82:	// Set Instrument
			case 0x83:	// set MIDI Channel
			case 0x85:	// Set Volume
			case 0x86:	// ignored
			case 0x87:	// ignored
			case 0x8C:	// ignored
			case 0x8D:	// ignored
			case 0x8E:	// ignored
			case 0x96:	// Bar ID
			case 0x99:	// note length modifier
			case 0x9D:	// ignored
			case 0x9F:	// Set Pan
				inPos += 0x02;
				break;
			case 0x94:	// Pitch Bend
			case 0x97:	// set volume or instrument
				inPos += 0x03;
				break;
			case 0xFE:	// Track End
			case 0xFF:	// Song End
			default:
				inPos += 0x01;
				return;
			}
		}
	}
	
	return;
}

static void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, INT16 bend)
{
	UINT16 bendVal;
	
	// The sound driver does no boundary checks here.
	//if (bend < -0x2000)
	//	bend = -0x2000;
	//else if (bend > +0x1FFF)
	//	bend = +0x1FFF;
	bendVal = 0x2000 + bend;
	WriteEvent(fInf, MTS, 0xE0, (bendVal >> 0) & 0x7F, (bendVal >> 7) & 0x7F);
	
	return;
}

static UINT8 NeedPBRangeFix(UINT8* curPBRange, INT16 PBend)
{
	UINT16 pbAbs;
	UINT8 pbRequired;
	
	pbAbs = (PBend >= 0) ? PBend : -PBend;
	pbRequired = (UINT8)((pbAbs + 0xFF) >> 8);
	if (*curPBRange >= pbRequired)
		return 0;
	
	if (*curPBRange == 0 && pbRequired < 16)
		*curPBRange = 16;
	else
		*curPBRange = (pbRequired + 7) & ~7;	// round up to 8
	
	return 1;
}

static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay)
{
	CheckRunningNotes(fInf, delay, &RunNoteCnt, RunNotes);
	if (*delay)
	{
		UINT8 curNote;
		
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= *delay;
	}
	
	return 0x00;
}

static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName)
{
	FILE* hFile;
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", FileName);
		return 0xFF;
	}
	
	fwrite(Data, 0x01, DataLen, hFile);
	fclose(hFile);
	
	return 0;
}

static double OPN2DB(UINT8 TL)
{
	return -(TL * 3 / 4.0f);
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

static UINT8 PanBits2MidiPan(UINT8 Pan)
{
	switch(Pan & 0x03)
	{
	case 0x00:	// no sound
		return 0x3F;
	case 0x01:	// Right Channel
		return 0x7F;
	case 0x02:	// Left Channel
		return 0x00;
	case 0x03:	// Center
		return 0x40;
	}
	return 0x3F;
}

static UINT8 CalcGSChecksum(UINT16 DataSize, const UINT8* Data)
{
	UINT8 ChkSum;
	UINT16 CurPos;
	
	ChkSum = 0x00;
	for (CurPos = 0x00; CurPos < DataSize; CurPos ++)
		ChkSum += Data[CurPos];
	return -ChkSum & 0x7F;
}

INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale)
{
	// formula: 60 000 000 / (bpm * (scale / 64.0))
	UINT32 div = bpm * scale;
	return 60000000U * 64U / div;
}


INLINE UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

INLINE UINT32 ReadLE32(const UINT8* Data)
{
	return	(Data[0x03] << 24) | (Data[0x02] << 16) |
			(Data[0x01] <<  8) | (Data[0x00] <<  0);
}
