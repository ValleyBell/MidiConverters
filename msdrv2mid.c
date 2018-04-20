// MsDRV -> Midi Converter
// -----------------------
// Written by Valley Bell, 26 February 2017, 10 March 2017
// based on Wolf Team MegaDrive -> Midi Converter
// Updated to support MsDRV4 on 25 February 2018, 27 February 2018

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


#include "midi_funcs.h"


typedef struct running_note
{
	UINT8 midChn;
	UINT8 note;
	UINT32 remLen;
} RUN_NOTE;

typedef struct _track_info
{
	UINT32 StartOfs;
	UINT32 LoopOfs;
	UINT32 TickCnt;
	UINT32 LoopTick;
	UINT16 LoopTimes;
} TRK_INFO;


UINT8 MsDrv2Mid(UINT32 SongLen, const UINT8* SongData);
static UINT8 CacheSysExData(UINT16 sxBufSize, UINT8* sxBuf, UINT8* sxBufPos, UINT8 data,
							FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 curCmd, UINT8 curTrk, UINT16 cmdPos);
static void PreparseMsDrvTrack(UINT32 SongLen, const UINT8* SongData, TRK_INFO* trkInf, UINT8 Mode);
static UINT8 NeedPBRangeFix(UINT8* curPBRange, INT16 PBend);
static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf);
static void CheckRunningNotes(FILE_INF* fInf, UINT32* Delay);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static void FlushRunningNotes(FILE_INF* fInf, MID_TRK_STATE* MTS);

static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);
static double OPN2DB(UINT8 TL);
static UINT8 DB2Mid(double DB);
static UINT8 PanBits2MidiPan(UINT8 Pan);
static UINT8 CalcGSChecksum(UINT16 DataSize, const UINT8* Data);

INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT32 ReadLE32(const UINT8* Data);


#define FILEVER_OLD		0x01	// 10 tracks, no padding (Bunretsu Shugo Shin Twinkle Star)
#define FILEVER_V4		0x40	// 24 tracks, 4-byte padding (MsDRV 4.4)
#define FILEVER_V4L		0x41	// 24 tracks, no padding (MsDRV 4.5 light)

static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT8 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];
static UINT8 FixVolume;
static UINT8 DebugCtrls;
static UINT8 fileVer;

static UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	char* StrPtr;
	UINT8 retVal;
	
	printf("MsDRV -> Midi Converter\n-----------------------\n");
	if (argc < 3)
	{
		printf("Usage: msdrv2mid.exe Options input.bin output.mid\n");
		printf("Options: (options can be combined, default setting is 'r')\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    v   fix Volume (convert db levels to logarithmic MIDI, OPN(A) only)\n");
		printf("    d   write debug MIDI controllers\n");
		printf("Supported/verified games: \n");
		printf("    MsDRV v1: Bunretsu Shugo Shin Twinkle Star\n");
		printf("    MsDRV 4.4: Miwaku no Chousho, Kara no Naka no Kotori\n");
		printf("    MsDRV 4.5 light: Ekudorado: Kingdom Inside of a Mirror\n");
		return 0;
	}
	
	MidiDelayCallback = MidiDelayHandler;
	
	FixVolume = 0;
	DebugCtrls = 0;
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			FixVolume = 0;
			break;
		case 'V':
			FixVolume = 1;
			break;
		case 'D':
			DebugCtrls = 1;
			break;
		}
		StrPtr ++;
	}
	argbase = 2;
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
	if (ROMLen > 0xFFFF)	// 64 KB
		ROMLen = 0xFFFF;
	
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
	getchar();
#endif
	
	return 0;
}

UINT8 MsDrv2Mid(UINT32 SongLen, const UINT8* SongData)
{
	TRK_INFO trkInf[0x20];
	TRK_INFO* tempTInf;
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT32 inPos;
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
	UINT16 tempoMod;
	
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
	UINT16 syxPos;
	
	UINT8 curNote;
	UINT32 curNoteLen;
	UINT32 curNoteDly;
	UINT8 curNoteVol;
	INT8 curNoteMove;
	
	UINT8 sysExHdr[2];
	UINT8 sysExData[4];
	UINT8 sysExBuf[0x80];
	UINT8 sysExBPos;
	UINT8 sysExChkSum;
	
	tempSht = ReadLE16(&SongData[0x00]);
	if (tempSht == 0x0014)
	{
		fileVer = FILEVER_OLD;
		trkCnt = 10;
		printf("Detected format: %s, %u tracks\n", "v1", trkCnt);
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
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	if (fileVer == FILEVER_OLD)
	{
		inPos = 0x00;
		for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
			trkInf[curTrk].StartOfs = ReadLE16(&SongData[inPos]);
	}
	else //if ((fileVer & 0xF0) == FILEVER_V4)
	{
		inPos = 0x00;
		for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x04)
			trkInf[curTrk].StartOfs = ReadLE32(&SongData[inPos]);
	}
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->LoopOfs = 0x00;
		tempTInf->TickCnt = 0;
		tempTInf->LoopTimes = NUM_LOOPS;
		tempTInf->LoopTick = 0;
		
		PreparseMsDrvTrack(SongLen, SongData, tempTInf, 0);
		if (tempTInf->LoopOfs)
			PreparseMsDrvTrack(SongLen, SongData, tempTInf, 1);
	}
	
	if (! NO_LOOP_EXT)
		GuessLoopTimes(trkCnt, trkInf);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	curBPM = 120;
	tempoMod = 0x0040;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		inPos = tempTInf->StartOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (fileVer == FILEVER_OLD)
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
		
		loopIdx = 0x00;
		trkFlags = 0x00;
		MTS.midChn = curTrk;
		curNoteVol = 0x7F;
		if ((chnMode & 0xF0) == 0x40)
			curNoteMove = +24;	// SSG channel
		else
			curNoteMove = 0;	// MIDI/FM channel
		pbRange = 0;
		sysExBPos = 0x00;
		sysExChkSum = 0x00;
		RunNoteCnt = 0x00;
		subEndOfs = 0x00;
		subRetOfs = 0x00;
		if (fileVer == FILEVER_OLD)
			trkFlags |= 0x02;	// default to 3-byte note mode
		
		while(! (trkFlags & 0x80))
		{
			if (subEndOfs && inPos >= subEndOfs)
			{
				subEndOfs = 0x00;
				inPos = subRetOfs;
			}
			
			curCmd = SongData[inPos];
			if (curCmd < 0x80)
			{
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
				if (fileVer == FILEVER_OLD)
				{
					// These fixes are required for TWED.MF2 (OPN/OPNA).
					// I can't say why it behaves like that though, even after disassembling the sound driver.
					if (curCmd == 0)
						curCmd = 0x30;
					if (curNoteDly == 0 && curNoteLen == 0)
						curNoteDly = curNoteLen = 48;
				}
				// MC_11_GS.MS confirms that, for MsDRV 4 versions, delay == 0 doesn't make a note
				
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
				
				CheckRunningNotes(&midFileInf, &MTS.curDly);
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
					if (RunNoteCnt < MAX_RUN_NOTES)
					{
						RunNotes[RunNoteCnt].midChn = MTS.midChn;
						RunNotes[RunNoteCnt].note = curNote;
						RunNotes[RunNoteCnt].remLen = curNoteLen;
						RunNoteCnt ++;
					}
				}
				
				MTS.curDly += curNoteDly;
			}
			else
			{
				switch(curCmd)
				{
				case 0x80:	// set Tick resolution
					printf("Warning: Sequence tries to change tick resolution on track %u at %04X\n", curTrk, inPos);
					tempSht = ReadLE16(&SongData[inPos + 0x01]);
					inPos += 0x03;
					break;
				case 0x81:	// OPL register write
					printf("OPL write track %u at %04X: mode %02X reg %02X data %02X\n", curTrk, inPos,
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
					if (fileVer == FILEVER_OLD)
					{
						printf("Ignored unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
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
							printf("Subroutine warning: StartOfs %04X > EndOfs %04X (track %u at %04X)\n",
									startPos, endPos, curCmd, curTrk, inPos);
						if (subEndOfs)
						{
							printf("Error: Nested subroutines!\n");
							break;	// just ignore them
						}
						subRetOfs = inPos;
						inPos = tempTInf->StartOfs + startPos;
						subEndOfs = tempTInf->StartOfs + endPos;
					}
					break;
				case 0x84:	// Return / GoTo
					if (fileVer == FILEVER_OLD)	// GoTo
					{
						tempSSht = (INT16)ReadLE16(&SongData[inPos + 0x01]);
						if (tempSSht < 0)
						{
							printf("Stopping on backwards jump on track %u at %04X\n", curTrk, inPos);
							trkFlags |= 0x80;
						}
						inPos += tempSSht;
					}
					else	// Subroutine Return
					{
						printf("Invalid Subroutine Return command on track %u at %04X\n", curTrk, inPos);
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
					tempLng = (UINT32)(60000000.0 / (curBPM * tempoMod / 64.0) + 0.5);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					inPos += 0x02;
					break;
				case 0x8B:	// switch note format (3/4 bytes)
					if (fileVer == FILEVER_OLD)
					{
						inPos += 0x01;
						break;
					}
					tempByt = SongData[inPos + 0x01];
					if (tempByt == 0x01)
						trkFlags |= 0x02;	// set 3-byte note format
					else
					{
						printf("Event %02X: Unexpected parameter %02X on track %u at %04X\n",
								curCmd, tempByt, curTrk, inPos);
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
					printf("Ignored unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					if (DebugCtrls)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x01;
					break;
				case 0x94:	// OPN register write
					printf("OPN write track %u at %04X: reg %02X data %02X\n", curTrk, inPos,
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
						printf("Ignored unknown event %02X %02X %02X on track %u at %04X\n",
							SongData[inPos + 0x00], SongData[inPos + 0x01], SongData[inPos + 0x02],
							curTrk, inPos);
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
						printf("Warning: Loop End without Loop Start!\n");
						trkFlags |= 0x80;
						break;
					}
					loopIdx --;
					loopCount[loopIdx] ++;
					tempSht = SongData[inPos + 0x01];
					if (! tempSht || tempSht >= 0xF0)	// infinite loop
					{
						if (loopCount[loopIdx] <= 0x7F)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCount[loopIdx]);
						tempSht = tempTInf->LoopTimes;
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
					if (inPos == tempTInf->LoopOfs)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, 0);
					inPos += 0x01;
					loopPos[loopIdx] = inPos;
					loopCount[loopIdx] = 0;
					loopIdx ++;
					break;
				case 0x9D:	// Detune
					tempSht = (INT8)SongData[inPos + 0x01];
					tempSht *= 8;
					tempSht += 0x2000;
					WriteEvent(&midFileInf, &MTS, 0xE0, tempSht & 0x7F, tempSht >> 7);
					inPos += 0x02;
					break;
				case 0x9E:	// ignored (used for padding)
					inPos += 0x01;
					break;
				case 0x9F:	// set Pan
					if (fileVer == FILEVER_OLD)
						tempByt = PanBits2MidiPan(SongData[inPos + 0x01]);
					else
						tempByt = (SongData[inPos + 0x01] ^ 0x80) >> 1;	// 80..FF,00..7F -> 00..3F,40..7F
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0xA4:	// Pitch Bend
					// Note: MSB = semitone, LSB = fraction
					tempSSht = ReadLE16(&SongData[inPos + 0x01]);
					if (pbRange != 0xFF)
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
					tempSSht += 0x2000;
					WriteEvent(&midFileInf, &MTS, 0xE0, tempSSht & 0x7F, tempSSht >> 7);
					inPos += 0x03;
					break;
				case 0xA5:	// set OPN/OPNA mode
					// Note: actually invalid in MsDrv v4
					tempByt = SongData[inPos + 0x01];
					printf("OPNA mode enable = %u on track %u at %04X\n", tempByt, curTrk, inPos);
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
					printf("Special FM3 mode enable = %u on track %u at %04X\n", tempByt, curTrk, inPos);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
					inPos += 0x02;
					break;
				case 0xAA:	// set Special FM3 key on operator mask
					tempByt = SongData[inPos + 0x01];
					printf("Special FM3 mode enable = %u on track %u at %04X\n", tempByt, curTrk, inPos);
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
					printf("Unimplemented: 'Pitch Slide step' command on track %u at %04X\n", curTrk, inPos);
					tempSSht = (INT16)ReadLE16(&SongData[inPos + 0x01]);
					inPos += 0x03;
					break;
				case 0xAE:	// set Pitch Slide destination
					printf("Unimplemented: 'Pitch Slide destination' command on track %u at %04X\n", curTrk, inPos);
					tempSht = ReadLE16(&SongData[inPos + 0x01]);
					inPos += 0x03;
					break;
				case 0xAF:	// set Pitch Slide state (current value)
					tempSSht = (INT16)ReadLE16(&SongData[inPos + 0x01]);
					if (tempSSht != 0)
						printf("Unimplemented: Non-zero pitch slide state set on track %u at %04X\n", curTrk, inPos);
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
					printf("Ignored unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					if (DebugCtrls)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					}
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
						printf("Event %02X Warning: buffer overflow! (track %u at %04X)\n",
								curCmd, curTrk, inPos);
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
					MTS.curDly += SongData[inPos + 0x01];
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
					printf("Unimplemented: Setting OPNA rhythm volume track %u at %04X\n", curTrk, inPos);
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
					MTS.curDly += SongData[inPos + 0x01];
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
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xDF:	// set SysEx Device ID + Model ID
					sysExHdr[0] = SongData[inPos + 0x02];	// Device ID
					sysExHdr[1] = SongData[inPos + 0x03];	// Model ID
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xE2:	// set MIDI instrument with Bank MSB/LSB
					tempByt = SongData[inPos + 0x02];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x00, SongData[inPos + 0x03]);	// Bank MSB
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x20, 0x00);	// Bank LSB (fixed to 0)
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					MTS.curDly += SongData[inPos + 0x01];
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
						WriteEvent(&midFileInf, &MTS, 0xFF, 0x21, 0x00);
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
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x03;
					break;
				case 0xE7:	// Tempo Modifier
					printf("Info: Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					tempoMod = ReadLE16(&SongData[inPos + 0x02]);
					// I've only seen the second parameter byte to be 00 and the driver ignores it.
					if (tempoMod & 0xFF00)
					{
						printf("Event %02X: Unexpected parameter %04X on track %u at %04X\n",
								curCmd, tempoMod, curTrk, inPos);
						getchar();
						tempoMod &= 0x00FF;
					}
					if (! tempoMod)
						tempoMod = 0x0040;	// just for safety
					if (fileVer == FILEVER_OLD)
					{
						// The old driver simply ignores the command.
						if (DebugCtrls)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x03, tempoMod & 0x7F);
					}
					else
					{
						// TODO: This goes horribly wrong when a song splits 8A and E7 commands over multiple tracks.
						tempLng = (UINT32)(60000000.0 / (curBPM * tempoMod / 64.0) + 0.5);
						WriteBE32(tempArr, tempLng);
						WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					}
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xEA:	// MIDI Channel Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xD0, SongData[inPos + 0x02], 0x00);
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x03;
					break;
				case 0xEB:	// MIDI Controller
					WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x02], SongData[inPos + 0x03]);
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xEC:	// MIDI Instrument
					tempByt = SongData[inPos + 0x02];
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x03;
					break;
				case 0xED:	// MIDI Note Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xA0, SongData[inPos + 0x02], SongData[inPos + 0x03]);
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xEE:	// Pitch Bend with Delay
					tempSSht = ReadLE16(&SongData[inPos + 0x02]);
					if (pbRange != 0xFF)
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
					tempSSht += 0x2000;
					WriteEvent(&midFileInf, &MTS, 0xE0, tempSSht & 0x7F, tempSSht >> 7);
					MTS.curDly += SongData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xFE:	// Track End
				case 0xFF:	// Song End (stops all tracks)
					trkFlags |= 0x80;
					inPos += 0x01;
					break;
				default:
					printf("Unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					if (DebugCtrls)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkFlags |= 0x80;
					break;
				}
			}
			if (fileVer == FILEVER_V4)
				inPos = (inPos + 0x03) & ~0x03;	// 4-byte padding
		}
		FlushRunningNotes(&midFileInf, &MTS);
		
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

static void PreparseMsDrvTrack(UINT32 SongLen, const UINT8* SongData, TRK_INFO* trkInf, UINT8 Mode)
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
	
	inPos = trkInf->StartOfs;
	if (! Mode)
		trkInf->LoopOfs = 0x0000;
	
	loopIdx = 0x00;
	trkFlags = 0x00;
	subEndOfs = 0x00;
	subRetOfs = 0x00;
	if (fileVer == FILEVER_OLD)
		trkFlags |= 0x02;	// default to 3-byte note mode
	while(inPos < SongLen)
	{
		if (subEndOfs && inPos >= subEndOfs)
		{
			subEndOfs = 0x00;
			inPos = subRetOfs;
		}
		
		if (Mode && inPos == trkInf->LoopOfs)
			return;
		
		curCmd = SongData[inPos];
		if (curCmd < 0x80)
		{
			UINT32 curNoteDly;
			
			curNoteDly = SongData[inPos + 0x01];
			if (fileVer == FILEVER_OLD)
			{
				if (curNoteDly == 0 && SongData[inPos + 0x02] == 0)
					curNoteDly = 48;	// fix for TWED.MF2 (OPN/OPNA)
			}
			if (trkFlags & 0x02)
				inPos += 0x03;
			else
				inPos += 0x04;
			if (! Mode)
				trkInf->TickCnt += curNoteDly;
			else
				trkInf->LoopTick += curNoteDly;
		}
		else
		{
			if (curCmd >= 0xDD && curCmd <= 0xEE)
			{
				if (! Mode)
					trkInf->TickCnt += SongData[inPos + 0x01];
				else
					trkInf->LoopTick += SongData[inPos + 0x01];
			}
			switch(curCmd)
			{
			case 0x9B:	// Loop End
				if (! loopIdx)
					return;
				loopIdx --;
				loopCount[loopIdx] ++;
				if (! SongData[inPos + 0x01])	// infinite loop
				{
					trkInf->LoopOfs = loopPos[loopIdx] - 0x01;
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
				if (fileVer == FILEVER_OLD)
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
					inPos = trkInf->StartOfs + startPos;
					subEndOfs = trkInf->StartOfs + endPos;
				}
				break;
			case 0x84:	// Return / GoTo
				if (fileVer == FILEVER_OLD)	// GoTo
				{
					INT16 jumpPos;
					
					jumpPos = (INT16)ReadLE16(&SongData[inPos + 0x01]);
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
				if (fileVer == FILEVER_OLD)
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
			case 0x91:	// unknown
			case 0x9E:	// ignored
			case 0xC2:	// checksum start marker
			case 0xC4:	// send Roland SysEx checksum
				inPos += 0x01;
				break;
			case 0x82:	// Set Instrument
			case 0x85:	// Set Volume
			case 0x8A:	// Tempo in BPM
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
					trkInf->TickCnt += SongData[inPos + 0x01];
				else
					trkInf->LoopTick += SongData[inPos + 0x01];
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

static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf)
{
	UINT8 CurTrk;
	TRK_INFO* TempTInf;
	UINT32 TrkLen;
	UINT32 TrkLoopLen;
	UINT32 MaxTrkLen;
	
	MaxTrkLen = 0x00;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		if (TempTInf->LoopOfs)
			TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		else
			TrkLoopLen = 0x00;
		
		TrkLen = TempTInf->TickCnt + TrkLoopLen * (TempTInf->LoopTimes - 1);
		if (MaxTrkLen < TrkLen)
			MaxTrkLen = TrkLen;
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		if (TempTInf->LoopOfs)
			TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		else
			TrkLoopLen = 0x00;
		if (TrkLoopLen < 0x20)
			continue;
		
		TrkLen = TempTInf->TickCnt + TrkLoopLen * (TempTInf->LoopTimes - 1);
		if (TrkLen * 5 / 4 < MaxTrkLen)
		{
			// TrkLen = desired length of the loop
			TrkLen = MaxTrkLen - TempTInf->LoopTick;
			
			TempTInf->LoopTimes = (UINT16)((TrkLen + TrkLoopLen / 3) / TrkLoopLen);
			printf("Trk %u: Extended loop to %u times\n", CurTrk, TempTInf->LoopTimes);
		}
	}
	
	return;
}

static void CheckRunningNotes(FILE_INF* fInf, UINT32* delay)
{
	UINT8 curNote;
	UINT32 tempDly;
	RUN_NOTE* tempNote;
	
	while(RunNoteCnt)
	{
		// 1. Check if we're going beyond a note's timeout.
		tempDly = *delay + 1;
		for (curNote = 0x00; curNote < RunNoteCnt; curNote ++)
		{
			tempNote = &RunNotes[curNote];
			if (tempNote->remLen < tempDly)
				tempDly = tempNote->remLen;
		}
		if (tempDly > *delay)
			break;	// not beyond the timeout - do the event
		
		// 2. advance all notes by X ticks
		for (curNote = 0x00; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= tempDly;
		(*delay) -= tempDly;
		
		// 3. send NoteOff for expired notes
		for (curNote = 0x00; curNote < RunNoteCnt; curNote ++)
		{
			tempNote = &RunNotes[curNote];
			if (! tempNote->remLen)	// turn note off, it going beyond the Timeout
			{
				WriteMidiValue(fInf, tempDly);
				tempDly = 0;
				
				File_CheckRealloc(fInf, 0x03);
				fInf->data[fInf->pos + 0x00] = 0x90 | tempNote->midChn;
				fInf->data[fInf->pos + 0x01] = tempNote->note;
				fInf->data[fInf->pos + 0x02] = 0x00;
				fInf->pos += 0x03;
				
				RunNoteCnt --;
				if (RunNoteCnt)
					*tempNote = RunNotes[RunNoteCnt];
				curNote --;
			}
		}
	}
	
	return;
}

static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay)
{
	CheckRunningNotes(fInf, delay);
	if (*delay)
	{
		UINT8 curNote;
		
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= *delay;
	}
	
	return 0x00;
}

static void FlushRunningNotes(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT8 curNote;
	
	for (curNote = 0; curNote < RunNoteCnt; curNote ++)
	{
		if (RunNotes[curNote].remLen > MTS->curDly)
			MTS->curDly = RunNotes[curNote].remLen;
	}
	CheckRunningNotes(fInf, &MTS->curDly);
	
	return;
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


INLINE UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

INLINE UINT32 ReadLE32(const UINT8* Data)
{
	return	(Data[0x03] << 24) | (Data[0x02] << 16) |
			(Data[0x01] <<  8) | (Data[0x00] <<  0);
}
