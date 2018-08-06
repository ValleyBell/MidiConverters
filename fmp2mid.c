// FMP3 -> Midi Converter
// ----------------------
// Written by Valley Bell, 27 August 2017
// based on Twinkle Soft -> Midi Converter

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
	UINT16 remLen;
} RUN_NOTE;

typedef struct _track_info
{
	UINT16 StartOfs;
	UINT16 LoopOfs;
	UINT32 TickCnt;
	UINT32 LoopTick;
	UINT16 LoopTimes;
} TRK_INFO;

typedef struct _event_list
{
	UINT32 evtAlloc;
	UINT32 evtCount;
	UINT32* data[2];	// data[][0] = tick time, data[][1] = event data
} EVENT_LIST;


UINT8 Fmp2Mid(UINT16 SongLen, const UINT8* SongData);
static void PreparseFmp(UINT32 SongLen, const UINT8* SongData, TRK_INFO* TrkInf);
static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf);
static void CheckRunningNotes(FILE_INF* fInf, UINT32* delay);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static void FlushRunningNotes(FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);

static UINT16 ReadLE16(const UINT8* data);
static UINT32 ReadLE24(const UINT8* data);
static UINT32 ReadLE32(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT8 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

static UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;

static UINT8 MIDI_MODE = 0x02;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("FMP3 -> Midi Converter\n----------------------\n");
	if (argc < 3)
	{
		printf("Usage: Fmp2Mid.exe input.bin output.mid\n");
		printf("Verified games: V.G. 1/2, Briganty, Steam-Heart's\n");
		return 0;
	}
	
	MidiDelayCallback = MidiDelayHandler;
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! _stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}
		else if (! _stricmp(argv[argbase] + 1, "NoLpExt"))
			NO_LOOP_EXT = 1;
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
	if (ROMLen > 0xFFFF)	// 64 KB
		ROMLen = 0xFFFF;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	retVal = Fmp2Mid(ROMLen, ROMData);
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

UINT8 Fmp2Mid(UINT16 SongLen, const UINT8* SongData)
{
	TRK_INFO trkInf[20];
	TRK_INFO* tempTInf;
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT16 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 LoopIdx;
	UINT16 mstLoopCount;
	UINT16 LoopCount[8];
	UINT16 LoopPos[8];
	UINT8 tickMult;
	
	UINT32 tempLng;
	//UINT16 tempSht;
	UINT8 tempByt;
	UINT8 tempArr[4];
	
	UINT8 curNote;
	UINT16 curNoteLen;
	UINT8 curNoteVol;
	
	UINT32 sysExAlloc;
	UINT32 sysExLen;
	UINT8* sysExData;
	
	if (SongData[0x00] != 0x02)
	{
		printf("Unsupported FMP format!\n");
		MidData = NULL;
		MidLen = 0x00;
		return 0x80;
	}
	
	trkCnt = 20;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	sysExAlloc = 0x20;
	sysExLen = 0x00;
	sysExData = (UINT8*)malloc(sysExAlloc);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	inPos = 0x04;
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++, inPos += 0x02)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->StartOfs = ReadLE16(&SongData[inPos]);
		tempTInf->LoopOfs = 0x0000;
		tempTInf->TickCnt = 0;
		tempTInf->LoopTimes = NUM_LOOPS;
		tempTInf->LoopTick = 0;
		
		PreparseFmp(SongLen, SongData, tempTInf);
	}
	
	if (! NO_LOOP_EXT)
		GuessLoopTimes(trkCnt, trkInf);
	
	tickMult = 1;
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++)
	{
		inPos = trkInf[curTrk].StartOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		trkEnd = 0;
		LoopIdx = 0x00;
		mstLoopCount = 0;
		MTS.midChn = curTrk;
		curNoteVol = 0x7F;
		RunNoteCnt = 0;
		
		while(inPos < SongLen)
		{
			curCmd = SongData[inPos];
			if (curCmd < 0x80)
			{
				curNoteLen = (UINT16)SongData[inPos + 0x01] * tickMult;
				inPos += 0x02;
				
				curNote = curCmd;
				if (! curNoteLen)	// length == 0 -> rest (confirmed with MIDI log of sound driver)
					curNote = 0xFF;
				
				CheckRunningNotes(&midFileInf, &MTS.curDly);
				for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
				{
					if (RunNotes[tempByt].note == curNote)
					{
						RunNotes[tempByt].remLen = (UINT16)MTS.curDly + curNoteLen;
						break;
					}
				}
				if (tempByt >= RunNoteCnt && curNote != 0xFF)
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
			}
			else
			{
				switch(curCmd)
				{
				case 0x80:	// Set Instrument
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					inPos += 0x02;
					break;
				case 0x81:	// Set Volume
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					inPos += 0x02;
					break;
				case 0x82:	// Tempo
					if (0)	// FMP v3
					{
						tempLng = ReadLE24(&SongData[inPos + 0x01]);
						tempLng = (UINT32)((UINT64)500000 * tempLng / 0x32F000);
						inPos += 0x06;
					}
					else	// FMP v2
					{
						tempLng = ReadLE16(&SongData[inPos + 0x01]);
						tempLng = (UINT32)((UINT64)500000 * tempLng / 0x32F0);
						inPos += 0x05;
					}
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					break;
				case 0x83:	// Set Note Velocity
					tempByt = SongData[inPos + 0x01];
					curNoteVol = tempByt;
					inPos += 0x02;
					break;
				case 0x84:	// Set Modulation
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, tempByt);
					inPos += 0x02;
					break;
				case 0x85:	// Pitch Bend
					WriteEvent(&midFileInf, &MTS, 0xE0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0x86:	// Sustain Pedal On
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x40, 0x40);	// value verified with MIDI log
					inPos += 0x01;
					break;
				case 0x87:	// Sustain Pedal Off
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x40, 0x00);
					inPos += 0x01;
					break;
				case 0x88:	// Loop Start
					if (0)	// FMP v3
					{
						tempByt = SongData[inPos + 0x03];
						inPos += 0x04;
					}
					else	// FMP v2
					{
						tempByt = SongData[inPos + 0x01];
						inPos += 0x02;
					}
					
					LoopPos[LoopIdx] = inPos;
					LoopCount[LoopIdx] = tempByt;
					if (LoopCount[LoopIdx] == 0x00)
					{
						mstLoopCount = 0;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
					}
					LoopIdx ++;
					break;
				case 0x89:	// Loop End
					if (! LoopIdx)
					{
						printf("Warning: Loop End without Loop Start!\n");
						trkEnd = 1;
						break;
					}
					inPos += 0x01;
					
					LoopIdx --;
					if (! LoopCount[LoopIdx])
					{
						// master loop
						mstLoopCount ++;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
						if (mstLoopCount >= trkInf[curTrk].LoopTimes)
							break;
					}
					else
					{
						LoopCount[LoopIdx] --;
						if (! LoopCount[LoopIdx])
							break;
					}
					// loop back
					inPos = LoopPos[LoopIdx];
					LoopIdx ++;
					break;
				case 0x8B:	// Set Pan
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0x8C:	// Send SysEx Data
					sysExData[0x00] = 0x41;			// Roland ID
					for (sysExLen = 0x01; inPos + sysExLen < SongLen; sysExLen ++)
					{
						if (sysExAlloc <= sysExLen)
						{
							sysExAlloc *= 2;
							sysExData = (UINT8*)realloc(sysExData, sysExAlloc);
						}
						sysExData[sysExLen] = SongData[inPos + sysExLen];
						if (sysExData[sysExLen] == 0xF7)
							break;
					}
					if (sysExData[sysExLen] == 0xF7)
					{
						sysExLen ++;	// count end SysEx End command
						tempByt = 1;
						if (1)
						{
							// comfort option: ignore SysEx messages that don't fit the device
							// The actual driver sends them regardless.
							if (MIDI_MODE == 0x01 && (sysExData[0x02] & 0xF0) != 0x10)	// check for MT-32 ID (0x16)
								tempByt = 0;
							else if (MIDI_MODE == 0x02 && (sysExData[0x02] & 0xF0) != 0x40)	// check for SC-55 ID (0x42/0x45)
								tempByt = 0;
						}
						if (tempByt)
							WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen, sysExData);
					}
					inPos += sysExLen;
					break;
				case 0x8E:	// set MIDI Channel
					if (SongData[inPos + 0x01] != 0xFF)
						MTS.midChn = SongData[inPos + 0x01] & 0x0F;
					inPos += 0x02;
					break;
				case 0x8F:	// Set Expression
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, tempByt);
					inPos += 0x02;
					break;
				case 0x90:	// MIDI Controller
					WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xB6:	// Tempo/Timing?
					// TODO: read [inPos + 0x02] to get actual base timing
					tempLng = ReadLE24(&SongData[inPos + 0x03]);
					tempLng = (UINT32)((UINT64)500000 * tempLng / 0x32F000);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					inPos += 0x08;
					break;
				case 0xB7:	// ?Timing related? (set OPN Timer A?)
					inPos += 0x04;
					break;
				case 0xFF:	// Track End
					trkEnd = 1;
					inPos += 0x01;
					break;
				case 0x93:	// MT-32 MIDI channel
					if (MIDI_MODE == 0x02)
						MTS.midChn = SongData[inPos + 0x01] & 0x0F;
					inPos += 0x02;
					break;
				case 0x95:	// SC-55 MIDI channel
					if (MIDI_MODE == 0x02)
						MTS.midChn = SongData[inPos + 0x01] & 0x0F;
					inPos += 0x02;
					break;
				case 0x96:	// MT-32 Expression
					if (MIDI_MODE == 0x01)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x98:	// SC-55 Expression
					if (MIDI_MODE == 0x02)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x99:	// MT-32 Volume
					if (MIDI_MODE == 0x01)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x9B:	// SC-55 Volume
					if (MIDI_MODE == 0x02)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x9C:	// MT-32 Stop Track
					printf("Event %02X on track %X at %04X\n", SongData[inPos + 0x00], curTrk, inPos);
					if (MIDI_MODE == 0x01)
						trkEnd = 1;
					inPos += 0x01;
					break;
				case 0x9E:	// SC-55 Stop Track
					printf("Event %02X on track %X at %04X\n", SongData[inPos + 0x00], curTrk, inPos);
					if (MIDI_MODE == 0x02)
						trkEnd = 1;
					inPos += 0x01;
					break;
				case 0xA1:	// SC-55 MIDI controller
					//printf("Event %02X on track %X at %04X\n", SongData[inPos + 0x00], curTrk, inPos);
					if (MIDI_MODE == 0x02)
						WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xA2:	// MT-32 instrument
					if (MIDI_MODE == 0x01)
						WriteEvent(&midFileInf, &MTS, 0xC0, SongData[inPos + 0x01], 0x00);
					inPos += 0x02;
					break;
				case 0xA4:	// SC-55 instrument
					if (MIDI_MODE == 0x02)
						WriteEvent(&midFileInf, &MTS, 0xC0, SongData[inPos + 0x01], 0x00);
					inPos += 0x02;
					break;
				case 0xA5:	// MT-32 Pan
					if (MIDI_MODE == 0x01)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xA7:	// SC-55 Pan
					if (MIDI_MODE == 0x02)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xA8:	// Send MT-32 SysEx Data
				case 0xAA:	// Send MT-32 SysEx Data
					sysExData[0x00] = 0x41;			// Roland ID
					for (sysExLen = 0x01; inPos + sysExLen < SongLen; sysExLen ++)
					{
						if (sysExAlloc <= sysExLen)
						{
							sysExAlloc *= 2;
							sysExData = (UINT8*)realloc(sysExData, sysExAlloc);
						}
						sysExData[sysExLen] = SongData[inPos + sysExLen];
						if (sysExData[sysExLen] == 0xF7)
							break;
					}
					if (sysExData[sysExLen] == 0xF7)
					{
						sysExLen ++;	// count end SysEx End command
						if ((curCmd == 0xA8 && MIDI_MODE == 0x01) || (curCmd == 0xAA && MIDI_MODE == 0x02))
							WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen, sysExData);
					}
					inPos += sysExLen;
					break;
				case 0xB0:	// MT-32 Pitch Bend
					if (MIDI_MODE == 0x01)
						WriteEvent(&midFileInf, &MTS, 0xE0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xB2:	// SC-55 Pitch Bend
					if (MIDI_MODE == 0x02)
						WriteEvent(&midFileInf, &MTS, 0xE0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xB3:
					printf("Event %02X on track %X at %04X\n", SongData[inPos + 0x00], curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x02;
					break;
				case 0xAD:	// set global Tick Multiplier
					tickMult = SongData[inPos + 0x01];
					printf("Track %X at %04X: Set Tick Multiplier = %u\n", curTrk, inPos, tickMult);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", SongData[inPos + 0x00], curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
			}
			if (trkEnd)
				break;
			
			tempByt = SongData[inPos];	inPos ++;
			MTS.curDly += (UINT16)tempByt * tickMult;
		}
		FlushRunningNotes(&midFileInf, &MTS);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	free(sysExData);	sysExData = NULL;
	
	return 0x00;
}

static void PreparseFmp(UINT32 SongLen, const UINT8* SongData, TRK_INFO* TrkInf)
{
	UINT16 inPos;
	UINT16 cmdLen;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 LoopIdx;
	UINT16 LoopCount[8];
	UINT16 LoopPos[8];
	UINT8 tempByt;
	
	trkEnd = 0;
	LoopIdx = 0x00;
	TrkInf->LoopOfs = 0x0000;
	inPos = TrkInf->StartOfs;
	while(inPos < SongLen)
	{
		curCmd = SongData[inPos];
		if (curCmd < 0x80)
		{
			cmdLen = 0x02;
		}
		else
		{
			cmdLen = 0x00;
			switch(curCmd)
			{
			case 0x86:	// Sustain Pedal On
			case 0x87:	// Sustain Pedal Off
				cmdLen = 0x01;
				break;
			case 0x80:	// Set Instrument
			case 0x81:	// Set Volume
			case 0x83:	// Set Note Velocity
			case 0x84:	// Set Modulation
			case 0x8B:	// Set Pan
			case 0x8E:	// set MIDI Channel
			case 0x8F:	// Set Expression
				cmdLen = 0x02;
				break;
			case 0x85:	// Pitch Bend
			case 0x90:	// MIDI Controller
			case 0xA1:
				cmdLen = 0x03;
				break;
			case 0xB7:	// ?Timing related?
				cmdLen = 0x04;
				break;
			case 0x82:	// Tempo
				cmdLen = 0x06;
				break;
			case 0xB6:	// Tempo/Timing?
				cmdLen = 0x08;
				break;
			case 0x88:	// Loop Start
				tempByt = SongData[inPos + 0x03];
				cmdLen = 0x04;
				
				LoopPos[LoopIdx] = inPos + cmdLen;
				LoopCount[LoopIdx] = tempByt;
				if (LoopCount[LoopIdx] == 0x00)
					TrkInf->LoopOfs = inPos;
				LoopIdx ++;
				break;
			case 0x89:	// Loop End
				if (! LoopIdx)
				{
					trkEnd = 1;
					break;
				}
				cmdLen = 0x01;
				
				LoopIdx --;
				if (LoopCount[LoopIdx])
					LoopCount[LoopIdx] --;
				if (LoopCount[LoopIdx])
				{
					// loop back
					inPos = LoopPos[LoopIdx];
					cmdLen = 0x00;
					LoopIdx ++;
				}
				break;
			case 0x8C:	// Send SysEx Data
				cmdLen = 0x01;
				for (cmdLen = 0x01; inPos + cmdLen < SongLen; cmdLen ++)
				{
					if (SongData[inPos + cmdLen] == 0xF7)
					{
						cmdLen ++;	// count end SysEx End command
						break;
					}
				}
				break;
			case 0xFF:	// Track End
				trkEnd = 1;
				cmdLen = 0x01;
				break;
			default:
				return;
			}
		}
		inPos += cmdLen;
		if (trkEnd)
			break;
		
		tempByt = SongData[inPos];
		inPos ++;
		TrkInf->TickCnt += tempByt;
		if (! TrkInf->LoopOfs)
			TrkInf->LoopTick += tempByt;
	}
	
	return;
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
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
		{
			tempNote = &RunNotes[curNote];
			if (tempNote->remLen < tempDly)
				tempDly = tempNote->remLen;
		}
		if (tempDly > *delay)
			break;	// not beyond the timeout - do the event
		
		// 2. advance all notes by X ticks
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= (UINT16)tempDly;
		(*delay) -= tempDly;
		
		// 3. send NoteOff for expired notes
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
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
			RunNotes[curNote].remLen -= (UINT16)*delay;
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


static UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

static UINT32 ReadLE24(const UINT8* data)
{
	return (data[0x02] << 16) | (data[0x01] <<  8) | (data[0x00] <<  0);
}

static UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}
