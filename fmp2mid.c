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
} TRK_INFO;


UINT8 Fmp2Mid(UINT16 SongLen, const UINT8* SongData);
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
	
	UINT32 tempLng;
	UINT16 tempSht;
	UINT8 tempByt;
	UINT8 tempArr[4];
	
	UINT8 curNote;
	UINT8 curNoteLen;
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
		trkInf[curTrk].StartOfs = ReadLE16(&SongData[inPos]);
	
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++)
	{
		inPos = trkInf[curTrk].StartOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		trkEnd = 0;
		LoopIdx = 0x00;
		mstLoopCount = 0;
		MTS.midChn = curTrk;
		curNoteVol = 0x7F;
		RunNoteCnt = 0x00;
		
		while(inPos < SongLen)
		{
			curCmd = SongData[inPos];
			if (curCmd < 0x80)
			{
				curNoteLen = SongData[inPos + 0x01];
				inPos += 0x02;
				
				curNote = curCmd;
				if (! curNoteLen)	// length == 0 -> rest (confirmed with MIDI log of sound driver)
					curNote = 0xFF;
				
				CheckRunningNotes(&midFileInf, &MTS.curDly);
				for (tempByt = 0x00; tempByt < RunNoteCnt; tempByt ++)
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
					tempLng = ReadLE24(&SongData[inPos + 0x01]);
					tempLng = (UINT32)((UINT64)500000 * tempLng / 0x32F000);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					inPos += 0x06;
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
					tempByt = SongData[inPos + 0x03];
					inPos += 0x04;
					
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
						if (mstLoopCount >= NUM_LOOPS)
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
						WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen, sysExData);
					}
					inPos += sysExLen;
					break;
				case 0x8E:	// set MIDI Channel
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
				case 0xA1:
					printf("Unknown event %02X on track %X at %04X\n", SongData[inPos + 0x00], curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
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
			MTS.curDly += tempByt;
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
			RunNotes[curNote].remLen -= (UINT16)tempDly;
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
			RunNotes[curNote].remLen -= (UINT16)*delay;
	}
	
	return 0x00;
}

static void FlushRunningNotes(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT8 curNote;
	
	for (curNote = 0x00; curNote < RunNoteCnt; curNote ++)
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
