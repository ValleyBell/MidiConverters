// M2system sequencer-2 -> Midi Converter
// --------------------------------------
// Written by Valley Bell, 23 July 2025
//
// This converter works with songs for "M2system sequencer-2" (SEQ2.X).
// known supported games:
//	- Final Fight
//	- Pro Tennis World Court
//	- Syvalion
//	- Thunder Blade

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


#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


UINT8 M2sys2Mid(UINT32 songLen, const UINT8* songData);
static void PreparseM2sys(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf, UINT8 pass);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(UINT16 bpm);

INLINE UINT16 ReadBE16(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be plenty, the driver can only store up to 8 simultaneous notes
static UINT16 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

#define MIDI_RES	24	// hardcoded in the driver
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("M2system sequencer-2 -> Midi Converter\n--------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: %s [options] input.sq2 output.mid\n", argv[0]);
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: %u)\n", NUM_LOOPS);
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
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
	
	retVal = M2sys2Mid(ROMLen, ROMData);
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
	TRK_INF trkInf[0x20];
	UINT16 trkCnt;
	UINT32 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT16 songTempo;
	UINT8 curTrk;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 tempBuf[0x100];
	UINT16 mstLoopCnt;
	UINT8 loopIdx;
	UINT8 loopCnt[8];
	UINT32 loopEndPos[8];

	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = 0x00;
	songTempo = ReadBE16(&songData[inPos]);	inPos += 0x02;
	
	for (trkCnt = 0; trkCnt < 0x20; trkCnt ++, inPos += 0x02)
	{
		TRK_INF* tempTInf = &trkInf[trkCnt];
		tempTInf->startOfs = ReadBE16(&songData[inPos]);
		if (tempTInf->startOfs == 0)
			break;	// a null-pointer terminates the track list
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		
		PreparseM2sys(songLen, songData, tempTInf, 0);
		PreparseM2sys(songLen, songData, tempTInf, 1);	// loop via GoTo - parse track again to get loop tick
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}

	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);

	WriteMidiHeader(&midFileInf, 0x0001, 1 + trkCnt, MIDI_RES);
	
	// write tempo track
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.midChn = 0x00;
	WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
	
	WriteBE32(tempBuf, Tempo2Mid(songTempo));
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempBuf[0x01]);
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		TRK_INF* tempTInf = &trkInf[curTrk];
		
		inPos = tempTInf->startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		MTS.midChn = songData[inPos] & 0x0F;	inPos ++;
		WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
		
		mstLoopCnt = 0;
		loopIdx = 0;
		RunNoteCnt = 0;
		trkEnd = 0;
		while(inPos < songLen && ! trkEnd)
		{
			if (inPos == tempTInf->loopOfs && mstLoopCnt == 0)
			{
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
				mstLoopCnt ++;
			}
			
			curCmd = songData[inPos];	inPos ++;
			if (curCmd < 0x80)
			{
				// Note On
				UINT8 noteVel = songData[inPos + 0x00];
				UINT16 noteLen;
				if (songData[inPos + 0x01] & 0x80)
				{
					noteLen = ReadBE16(&songData[inPos + 0x01]) & 0x7FFF;
					inPos += 0x03;
				}
				else
				{
					noteLen = songData[inPos + 0x01];
					inPos += 0x02;
				}
				
				WriteEvent(&midFileInf, &MTS, 0x90, curCmd, noteVel);
				AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
					MTS.midChn, curCmd, 0x80, noteLen);	// The sound driver sends 9# note 00.
			}
			else switch(curCmd & 0xF0)
			{
			case 0x80:	// Delay
			{
				UINT16 delay = ((curCmd & 0x0F) << 8) | (songData[inPos] << 0);
				inPos ++;
				MTS.curDly += delay;
				break;
			}
			case 0x90:	// special commands
				switch(curCmd)
				{
				case 0x90:	// Jump (for looping)
					if (mstLoopCnt < 0x80)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
					if (mstLoopCnt < tempTInf->loopTimes)
					{
						INT16 destOfs = ReadBE16(&songData[inPos]);
						inPos += destOfs;
						mstLoopCnt ++;
					}
					else
					{
						inPos += 0x02;
						trkEnd = 1;
					}
					break;
				case 0x91:	// Loop
				{
					INT16 destOfs = ReadBE16(&songData[inPos + 0x00]);
					// Note: The sound driver uses the current song pointer (inPos) to identify nested loops.
					if (loopIdx == 0 || loopEndPos[loopIdx - 1] != inPos)
					{
						if (loopIdx >= 8)
						{
							printf("Warning: Nested loop at %06X exceeds stack size - ignored.\n", inPos - 0x01);
							inPos += 0x03;
							break;
						}
						// start new loop
						loopCnt[loopIdx] = songData[inPos + 0x02];
						loopEndPos[loopIdx] = inPos;
						loopIdx ++;
					}
					loopIdx --;
					
					loopCnt[loopIdx] --;
					if (loopCnt[loopIdx] > 0)
					{
						// loop back
						inPos += destOfs;
						loopIdx ++;
					}
					else
					{
						// exit loop
						inPos += 0x03;
					}
					break;
				}
				case 0x9E:	// Tempo Change
				{
					UINT16 newTempo = ReadBE16(&songData[inPos]);	inPos += 0x02;
					WriteBE32(tempBuf, Tempo2Mid(newTempo));
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempBuf[0x01]);
					break;
				}
				case 0x9F:	// Track End
					trkEnd = 1;
					break;
				default:	// unknown events cause the driver to enter an infinite loop
					printf("Unknown event %02X at %06X\n", curCmd, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
				break;
			case 0xB0:	// Control Change
			case 0xE0:	// Pitch Bend
				WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], songData[inPos + 0x01]);
				inPos += 0x02;
				break;
			case 0xC0:	// Instrument Change
				WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x00);
				inPos += 0x01;
				break;
			case 0xF0:	// SysEx
			{
				UINT16 syxLen = songData[inPos];	inPos ++;
				memcpy(tempBuf, &songData[inPos], syxLen);
				tempBuf[syxLen] = 0xF7;	// The data does not include the terminating F7 byte
				// buffer size note: syxLen = 0x00..0xFF, so 0x100 bytes are enough
				
				WriteLongEvent(&midFileInf, &MTS, curCmd, syxLen + 1, tempBuf);
				inPos += syxLen;
				break;
			}
			default:	// unknown events cause the driver to enter an infinite loop
				// The sound driver does not support Note Aftertouch (0xA0) or Channel Aftertouch (0xD0).
				printf("Unknown event %02X at %06X\n", curCmd, inPos);
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
				inPos += 0x01;
				trkEnd = 1;
				break;
			}
		}
		
		FlushRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes, 0);
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
	UINT8 loopIdx;
	UINT8 loopCnt[8];
	UINT32 loopEndPos[8];

	trkEnd = 0;
	loopIdx = 0;
	if (pass == 0)
	{
		trkInf->tickCnt = 0;
		trkInf->loopOfs = 0x0000;
	}
	trkInf->loopTick = 0;
	inPos = trkInf->startOfs + 0x01;
	while(inPos < songLen && ! trkEnd)
	{
		if (pass == 1 && inPos == trkInf->loopOfs)
			break;
		
		curCmd = songData[inPos];	inPos ++;
		if (curCmd < 0x80)
		{
			// Note On
			if (songData[inPos + 0x01] & 0x80)
				inPos += 0x03;	// 2-byte note length
			else
				inPos += 0x02;	// 1-byte note length
		}
		else switch(curCmd & 0xF0)
		{
		case 0x80:	// Delay
		{
			UINT16 delay = ((curCmd & 0x0F) << 8) | (songData[inPos] << 0);
			inPos ++;
			if (pass == 0)
				trkInf->tickCnt += delay;
			else
				trkInf->loopTick += delay;
			break;
		}
		case 0x90:	// special commands
			switch(curCmd)
			{
			case 0x90:	// Jump (for looping)
				{
					INT16 destOfs = ReadBE16(&songData[inPos]);
					trkInf->loopOfs = inPos + destOfs;
					trkInf->loopTick = (UINT32)-1;
					trkEnd = 1;
				}
				break;
			case 0x91:	// Loop
				{
					INT16 destOfs = ReadBE16(&songData[inPos + 0x00]);
					if (loopIdx == 0 || loopEndPos[loopIdx - 1] != inPos)
					{
						if (loopIdx >= 8)
						{
							inPos += 0x03;
							break;
						}
						loopCnt[loopIdx] = songData[inPos + 0x02];
						loopEndPos[loopIdx] = inPos;
						loopIdx ++;
					}
					loopIdx --;
				
					loopCnt[loopIdx] --;
					if (loopCnt[loopIdx] > 0)
					{
						inPos += destOfs;
						loopIdx ++;
					}
					else
					{
						inPos += 0x03;
					}
				}
				break;
			case 0x9E:	// Tempo Change
				inPos += 0x02;
				break;
			case 0x9F:	// Track End
			default:
				trkEnd = 1;
				break;
			}
			break;
		case 0xB0:	// Control Change
		case 0xE0:	// Pitch Bend
			inPos += 0x02;
			break;
		case 0xC0:	// Instrument Change
			inPos += 0x01;
			break;
		case 0xF0:	// SysEx
		{
			UINT16 syxLen = songData[inPos];	inPos ++;
			inPos += syxLen;
			break;
		}
		default:
			trkEnd = 1;
			break;
		}
	}
	
	return;
}

static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay)
{
	CheckRunningNotes(fInf, delay, &RunNoteCnt, RunNotes);
	if (*delay)
	{
		UINT16 curNote;
		
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= (UINT16)*delay;
	}
	
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
