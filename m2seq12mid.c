// M2system sequencer-1 -> Midi Converter
// --------------------------------------
// Written by Valley Bell, 24 July 2025
//
// This converter works with songs for "M2system sequencer-1" (M2SEQ.X).
// known supported games:
//	- Ajax
//	- Super Hang-On

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
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
} TRK_INF;


#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


UINT8 M2sys2Mid(UINT32 songLen, const UINT8* songData);
static void PreparseM2sys(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf, UINT8 pass);
UINT8 M2ex2Syx(UINT32 songLen, const UINT8* songData);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(UINT16 bpm);

INLINE UINT16 ReadBE16(const UINT8* data);


#define MODE_MID	0x00
#define MODE_SYX	0x01

static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 24;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 DRV_BUGS = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	UINT8 mode;
	
	printf("M2system sequencer-1 -> Midi Converter\n--------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage songs: %s [options] input.m2s output.mid\n", argv[0]);
		printf("Usage SysEx: %s [options] input.m2x output.syx\n", argv[0]);
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: %u)\n", NUM_LOOPS);
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -TpQ n      Sets the number of Ticks per Quarter to n. (default: %u)\n", MIDI_RES);
		printf("    -Bugs       Replicate sound driver bugs. (hanging note in SPHBGM3.M2S)\n");
		return 0;
	}
	
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
		else if (! stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				MIDI_RES = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! MIDI_RES)
					MIDI_RES = 24;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "Bugs"))
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
	
	mode = MODE_MID;
	{	// format detection
		UINT16 trkCnt = ReadBE16(&ROMData[0x00]);	// M2S: track count, M2X: size of following data
		UINT16 ptr1 = ReadBE16(&ROMData[0x02]);
		UINT16 ptr2 = ReadBE16(&ROMData[0x04]);
		if (trkCnt >= 1 && ptr1 > ROMLen && trkCnt < ROMLen)
			mode = MODE_SYX;
		else if (trkCnt >= 2 && ptr2 > ROMLen)
			mode = MODE_SYX;
	}
	
	if (mode == MODE_MID)
		retVal = M2sys2Mid(ROMLen, ROMData);
	else if (mode == MODE_SYX)
		retVal = M2ex2Syx(ROMLen, ROMData);
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

UINT8 M2sys2Mid(UINT32 songLen, const UINT8* songData)
{
	TRK_INF trkInf[0x10];
	UINT16 trkCnt;
	UINT32 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 curTrk;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 tempArr[0x04];
	UINT16 mstLoopCnt;
	UINT8 curNoteVel;
	UINT16 noteLenMod;
	INT8 notePitchMod;
	UINT8 trkFlags;
	UINT8 chordSize;
	UINT16 cmdDelay;
	UINT16 noteLen;
	UINT8 noteListLen;
	UINT8 noteList[8];
	UINT8 loopCnt[3];
	UINT32 loopStPos[3];
	UINT16 callRetPos[2];
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = 0x00;
	trkCnt = ReadBE16(&songData[inPos]);	inPos += 0x02;
	if (trkCnt > 0x10)
		trkCnt = 0x10;
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
	{
		TRK_INF* tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = ReadBE16(&songData[inPos]);
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		
		PreparseM2sys(songLen, songData, tempTInf, 0);
		PreparseM2sys(songLen, songData, tempTInf, 1);	// loop via GoTo - parse track again to get loop tick
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		TRK_INF* tempTInf = &trkInf[curTrk];
		
		inPos = tempTInf->startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		MTS.midChn = songData[inPos] & 0x0F;	inPos ++;
		WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
		
		trkFlags = 0x00;	// verified with actual driver
		curNoteVel = 100;	// verified with actual driver
		noteLenMod = 0x0F;	// verified with actual driver
		notePitchMod = 0;
		chordSize = 1;
		noteLen = 0;
		noteListLen = 0;
		mstLoopCnt = 0;
		loopCnt[0] = loopCnt[1] = loopCnt[2] = 0;
		callRetPos[0] = callRetPos[1] = 0x0000;
		trkEnd = 0;
		while(inPos < songLen && ! trkEnd)
		{
			if (inPos == tempTInf->loopOfs && mstLoopCnt == 0)
			{
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
				mstLoopCnt ++;
			}
			
			cmdDelay = 0;
			curCmd = songData[inPos];	inPos ++;
			if (curCmd == 0x00)	// Delay
			{
				cmdDelay = songData[inPos];	inPos ++;
				noteLen = 0;	// enforce "note off"
			}
			else if (curCmd < 0x80)		// Note On
			{
				UINT8 oldNtCnt;
				UINT8 oldNotes[8];
				UINT8 curNote;
				inPos --;
				
				if (! (trkFlags & 0x01) && noteListLen > 0)
					printf("Warning: Hanging note detected at 0x%04X\n", inPos);
				
				oldNtCnt = 0;
				if (trkFlags & 0x01)
				{
					oldNtCnt = noteListLen;
					for (curNote = 0; curNote < noteListLen; curNote ++)
						oldNotes[curNote] = noteList[curNote];
				}
				for (curNote = 0; curNote < chordSize; curNote ++)
				{
					noteList[curNote] = songData[inPos] + notePitchMod;
					inPos ++;
				}
				noteListLen = chordSize;
				if (! (trkFlags & 0x01))
				{
					for (curNote = 0; curNote < noteListLen; curNote ++)
						WriteEvent(&midFileInf, &MTS, 0x90, noteList[curNote], curNoteVel);
					trkFlags |= 0x02;
				}
				else
				{
					// note "tie" handling
					UINT8 newNtCnt = 0;
					UINT8 newNotes[8];
					
					trkFlags &= ~0x01;
					// based on the "old notes" and "new notes" lists, create lists of:
					//  - old notes not in the new list (by removing new notes from the old list)
					//  - new notes not in the old list (by taking note of new notes not found in the old list)
					for (curNote = 0; curNote < noteListLen; curNote ++)
					{
						UINT8 found = 0;
						UINT8 oldNote;
						for (oldNote = 0; oldNote < oldNtCnt; oldNote ++)
						{
							if (oldNotes[oldNote] == noteList[curNote])
							{
								found = 1;
								break;
							}
						}
						if (found)
						{
							oldNotes[oldNote] = 0xFF;
						}
						else
						{
							newNotes[newNtCnt] = noteList[curNote];
							newNtCnt ++;
						}
					}
					
					// turn off old notes not in the new chord
					for (curNote = 0; curNote < oldNtCnt; curNote ++)
					{
						if (oldNotes[curNote] != 0xFF)
							WriteEvent(&midFileInf, &MTS, 0x90, oldNotes[curNote], 0x00);
					}
					for (curNote = 0; curNote < newNtCnt; curNote ++)
						WriteEvent(&midFileInf, &MTS, 0x90, newNotes[curNote], curNoteVel);
					if (! DRV_BUGS)
						trkFlags |= 0x02;	// BUGFIX: prevents hanging note in SPHBGM3.M2S
				}
				
				cmdDelay = songData[inPos];	inPos ++;
				if (songData[inPos] == 0xFE)
				{
					inPos ++;
					trkFlags |= 0x01;
					noteLen = 0xFFFF;
				}
				else
				{
					if (trkFlags & 0x04)
					{
						noteLen = (cmdDelay < noteLenMod) ? cmdDelay : noteLenMod;
					}
					else if (noteLenMod >= 0x10)
					{
						noteLen = cmdDelay;
					}
					else
					{
						noteLen = (cmdDelay * noteLenMod + 0x08) / 0x10;
						if (noteLen == 0)
							noteLen = 1;
					}
					if (noteLen == 0)
						noteLen = 0xFFFF;	// in the original driver, a length of 0 is infinite
				}
			}
			else if (curCmd >= 0x81 && curCmd <= 0x88)	// chord size
			{
				// not used by any known game
				chordSize = curCmd & 0x0F;
			}
			else switch(curCmd)
			{
			case 0xC0:	// Track End
				trkEnd = 1;
				break;
			case 0xC3:	// Jump (for looping)
			{
				INT16 destOfs = ReadBE16(&songData[inPos]);	inPos += 0x02;
				inPos += destOfs;
				if (inPos == tempTInf->loopOfs)
				{
					if (mstLoopCnt < 0x80)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
					if (mstLoopCnt < tempTInf->loopTimes)
						mstLoopCnt ++;
					else
						trkEnd = 1;
				}
				break;
			}
			case 0xC4:	// Subroutine Call 1
			case 0xC5:	// Subroutine Call 2
			{
				UINT8 subIdx = curCmd & 0x01;
				INT16 destOfs = ReadBE16(&songData[inPos]);	inPos += 0x02;
				callRetPos[subIdx] = inPos;
				inPos += destOfs;
				break;
			}
			case 0xC6:	// Subroutine Return 1
			case 0xC7:	// Subroutine Return 2
			{
				UINT8 subIdx = curCmd & 0x01;
				if (! callRetPos[subIdx])
				{
					// Note: The sound driver explicitly checks for 0 here and ignores it in that case.
					// Tracks in "Ajax" rely on this behaviour.
					//printf("Warning: Ignoring invalid Subroutine Return at %06X\n", inPos);
				}
				else
				{
					inPos = callRetPos[subIdx];
					callRetPos[subIdx] = 0x0000;
				}
				break;
			}
			case 0xC8:	// Loop Start 1
			case 0xCA:	// Loop Start 2
			case 0xCC:	// Loop Start 3
			{
				UINT8 loopIdx = (curCmd & 0x07) / 2;
				loopCnt[loopIdx] = songData[inPos];	inPos ++;
				loopStPos[loopIdx] = inPos;
				break;
			}
			case 0xC9:	// Loop End 1
			case 0xCB:	// Loop End 2
			case 0xCD:	// Loop End 3
			{
				UINT8 loopIdx = (curCmd & 0x07) / 2;
				if (! loopStPos[loopIdx])
				{
					printf("Warning: Loop End without Loop Start at %06X\n", inPos);
					trkEnd = 1;
					break;
				}
				loopCnt[loopIdx] --;
				if (loopCnt[loopIdx])
					inPos = loopStPos[loopIdx];
				else
					loopStPos[loopIdx] = 0x0000;
				break;
			}
			case 0xD0:	// Tempo Change
			{
				UINT16 newTempo = ReadBE16(&songData[inPos]);	inPos += 0x02;
				if (DRV_BUGS && newTempo > 312)
					newTempo = 312;	// That's what the driver does.
				WriteBE32(tempArr, Tempo2Mid(newTempo));
				WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
				break;
			}
			case 0xD1:	// note length modification: fraction of 10h
				noteLenMod = songData[inPos];	inPos ++;
				trkFlags &= ~0x04;
				break;
			case 0xD2:	// note length modification: limit length
				// not used by any known game
				noteLenMod = songData[inPos];	inPos ++;
				trkFlags |= 0x04;
				break;
			case 0xD4:	// set track transposition
				notePitchMod = songData[inPos];	inPos ++;
				break;
			case 0xD5:	// add to track transposition
				notePitchMod += songData[inPos];	inPos ++;
				break;
			case 0xE0:	// set MIDI channel
				MTS.midChn = songData[inPos] & 0x0F;	inPos ++;
				break;
			case 0xE1:	// set note velocity
				curNoteVel = songData[inPos];	inPos ++;
				break;
			case 0xE2:	// set channel volume
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, songData[inPos]);
				inPos ++;
				break;
			case 0xE3:	// Control Change
				WriteEvent(&midFileInf, &MTS, 0xB0, songData[inPos + 0x00], songData[inPos + 0x01]);
				inPos += 0x02;
				break;
			case 0xE4:	// Instrument Change
				WriteEvent(&midFileInf, &MTS, 0xC0, songData[inPos], 0x00);
				inPos ++;
				break;
			case 0xE5:	// Pitch Bend
				WriteEvent(&midFileInf, &MTS, 0xE0, 0x00, songData[inPos]);
				inPos ++;
				break;
			default:	// unknown events cause the driver to enter an infinite loop
				// The sound driver does not support Note Aftertouch (0xA0) or Channel Aftertouch (0xD0).
				printf("Unknown event %02X at %06X\n", curCmd, inPos);
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
				inPos += 0x01;
				trkEnd = 1;
				break;
			}
			if ((trkFlags & 0x02) && noteLen <= cmdDelay)
			{
				UINT8 curNote;
				MTS.curDly += noteLen;
				cmdDelay -= noteLen;
				for (curNote = 0; curNote < noteListLen; curNote ++)
					WriteEvent(&midFileInf, &MTS, 0x90, noteList[curNote], 0x00);
				noteListLen = 0;
				noteLen = 0;
				trkFlags &= ~0x02;
			}
			MTS.curDly += cmdDelay;
		}
		
		if (trkFlags & 0x02)
		{
			UINT8 curNote;
			if (noteLen > 0x100)
				noteLen = 0x100;	// prevent excessive delays for notes hanging at the end
			MTS.curDly += noteLen;
			for (curNote = 0; curNote < noteListLen; curNote ++)
				WriteEvent(&midFileInf, &MTS, 0x90, noteList[curNote], 0x00);
		}
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseM2sys(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf, UINT8 pass)
{
	UINT32 inPos;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 chordSize;
	UINT8 loopCnt[3];
	UINT32 loopStPos[3];
	UINT16 callRetPos[2];
	UINT16 basePos;
	
	trkEnd = 0;
	if (pass == 0)
	{
		trkInf->tickCnt = 0;
		trkInf->loopOfs = 0x0000;
	}
	trkInf->loopTick = 0;
	basePos = trkInf->startOfs + 0x01;
	inPos = basePos;
	chordSize = 1;
	loopCnt[0] = loopCnt[1] = loopCnt[2] = 0;
	callRetPos[0] = callRetPos[1] = 0x0000;
	while(inPos < songLen && ! trkEnd)
	{
		if (pass == 1 && inPos == trkInf->loopOfs)
			break;
		
		curCmd = songData[inPos];	inPos ++;
		if (curCmd == 0x00)
		{
			UINT16 delay = songData[inPos];	inPos ++;
			if (pass == 0)
				trkInf->tickCnt += delay;
			else
				trkInf->loopTick += delay;
		}
		else if (curCmd < 0x80)	// Note On
		{
			UINT16 delay;
			inPos += (chordSize - 1);
			delay = songData[inPos];	inPos ++;
			if (pass == 0)
				trkInf->tickCnt += delay;
			else
				trkInf->loopTick += delay;
			if (songData[inPos] == 0xFE)
				inPos ++;
		}
		else if (curCmd >= 0x81 && curCmd <= 0x88)
		{
			chordSize = curCmd & 0x0F;
			//inPos += 0x00;
		}
		else switch(curCmd)
		{
		case 0xC3:	// Jump (for looping)
		{
			UINT16 newPos;
			INT16 destOfs = ReadBE16(&songData[inPos]);	inPos += 0x02;
			newPos = inPos + destOfs;
			// slightly complicated loop detection, due to shared data
			if (newPos < basePos)
			{
				// jumping beyond beginning of the track - back-jump to data shared with other track
				basePos = newPos;
				inPos = newPos;
			}
			else if (newPos >= inPos)
			{
				// forward jump - definitely not a loop
				inPos = newPos;
			}
			else
			{
				// backward jump on the same track - should be a good loop
				trkInf->loopOfs = newPos;
				trkInf->loopTick = (UINT32)-1;
				trkEnd = 1;
			}
			break;
		}
		case 0xC4:	// Subroutine Call 1
		case 0xC5:	// Subroutine Call 2
		{
			UINT8 subIdx = curCmd & 0x01;
			INT16 destOfs = ReadBE16(&songData[inPos]);	inPos += 0x02;
			callRetPos[subIdx] = inPos;
			inPos += destOfs;
			break;
		}
		case 0xC6:	// Subroutine Return 1
		case 0xC7:	// Subroutine Return 2
		{
			UINT8 subIdx = curCmd & 0x01;
			if (! callRetPos[subIdx])
				break;
			inPos = callRetPos[subIdx];
			callRetPos[subIdx] = 0x0000;
			break;
		}
		case 0xC8:	// Loop Start 1
		case 0xCA:	// Loop Start 2
		case 0xCC:	// Loop Start 3
		{
			UINT8 loopIdx = (curCmd & 0x07) / 2;
			loopCnt[loopIdx] = songData[inPos];	inPos ++;
			loopStPos[loopIdx] = inPos;
			break;
		}
		case 0xC9:	// Loop End 1
		case 0xCB:	// Loop End 2
		case 0xCD:	// Loop End 3
		{
			UINT8 loopIdx = (curCmd & 0x07) / 2;
			if (! loopStPos[loopIdx])
			{
				trkEnd = 1;
				break;
			}
			loopCnt[loopIdx] --;
			if (loopCnt[loopIdx])
				inPos = loopStPos[loopIdx];
			else
				loopStPos[loopIdx] = 0x0000;
			break;
		}
		case 0xD0:	// Tempo Change
		case 0xE3:	// Control Change
			inPos += 0x02;
			break;
		case 0xD1:	// note length modification: fraction of 10h
		case 0xD2:	// note length modification: limit length
		case 0xD4:	// set track transposition
		case 0xD5:	// add to track transposition
		case 0xE0:	// set MIDI channel
		case 0xE1:	// set note velocity
		case 0xE2:	// set channel volume
		case 0xE4:	// Instrument Change
		case 0xE5:	// Pitch Bend
			inPos += 0x01;
			break;
		case 0x9F:	// Track End
		default:
			trkEnd = 1;
			break;
		}
	}
	
	return;
}

UINT8 M2ex2Syx(UINT32 songLen, const UINT8* songData)
{
	UINT32 inPos;
	UINT32 outPos;
	
	MidLen = songLen;
	MidData = (UINT8*)malloc(MidLen);
	
	inPos = 0x00;
	outPos = 0x00;
	while(inPos < songLen)
	{
		UINT16 syxLen = ReadBE16(&songData[inPos]);	inPos += 0x02;
		
		MidData[outPos] = 0xF0;	outPos ++;
		
		memcpy(&MidData[outPos], &songData[inPos], syxLen);
		inPos += syxLen;	outPos += syxLen;
		
		MidData[outPos] = 0xF7;	outPos ++;
	}
	MidLen = outPos;
	
	return 0x00;
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

INLINE UINT16 ReadBE16(const UINT8* data)
{
	return	(data[0x00] << 8) | (data[0x01] << 0);
}
