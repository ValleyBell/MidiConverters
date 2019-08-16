// Wolfteam MF -> Midi Converter
// -----------------------------
// Written by Valley Bell, 06 August 2019
// based on TGL FMP -> Midi Converter


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


typedef struct running_note
{
	UINT8 midChn;
	UINT8 note;
	UINT32 remLen;
} RUN_NOTE;

typedef struct _track_info
{
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 midChn;
} TRK_INFO;


UINT8 WtM2Mid(UINT16 songLen, const UINT8* songData);
static void PreparseWtMTrk(UINT32 songLen, const UINT8* songData, TRK_INFO* trkInf);
static void GuessLoopTimes(UINT8 trkCnt, TRK_INFO* trkInf);
static void CheckRunningNotes(FILE_INF* fInf, UINT32* delay);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static void FlushRunningNotes(FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(UINT16 bpm);

INLINE UINT16 ReadBE16(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be plenty, the driver can only store up to 8 simultaneous notes
static UINT8 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

static UINT16 MIDI_RES = 24;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 DRV_BUGS = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("Wolfteam MF -> Midi Converter\n-----------------------------\n");
	if (argc < 3)
	{
		printf("Usage: wtmf2mid.exe [options] input.bin output.mid\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: %u)\n", NUM_LOOPS);
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -TpQ n      convert with n ticks per quarter. (default: %u)\n", MIDI_RES);
		printf("    -Bugs       Replicate sound driver bugs.\n");
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
	
	retVal = WtM2Mid(ROMLen, ROMData);
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

UINT8 WtM2Mid(UINT16 songLen, const UINT8* songData)
{
	TRK_INFO trkInf[32];
	TRK_INFO* tempTInf;
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT16 segPos;
	UINT16 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 songTempo;
	UINT8 trkEnd;
	UINT8 rhythmMode;
	UINT8 measureID;
	UINT8 loopIdx;
	UINT8 loopMeasure[0x10];
	UINT16 loopCount[0x10];
	UINT16 loopSegPos[0x10];
	UINT8 curCmd;
	UINT8 curCmdType;
	UINT8 curDelay;
	UINT8 curNote;
	UINT8 curNoteLen;
	UINT8 curNoteVol;
	UINT8 earlyNoteOff;
	UINT16 pbVal;
	UINT32 tempLng;
	//UINT16 tempSht;
	UINT8 tempByt;
	UINT8 tempArr[4];
	
	if (memcmp(&songData[0x00], "MF", 0x02))
	{
		printf("Not a Wolfteam MF file!\n");
		MidData = NULL;
		MidLen = 0x00;
		return 0x80;
	}
	// TODO: sub-songs??
	songData += 0x08;	songLen -= 0x08;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = 0x00;
	songTempo = songData[inPos + 0x06];
	trkCnt = songData[inPos + 0x07];
	inPos += 0x08;
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x04)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = ReadBE16(&songData[inPos + 0x00]);
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTimes = NUM_LOOPS;
		tempTInf->loopTick = 0;
		tempTInf->midChn = songData[inPos + 0x02];
		
		PreparseWtMTrk(songLen, songData, tempTInf);
	}
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	if (! NO_LOOP_EXT)
		GuessLoopTimes(trkCnt, trkInf);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (curTrk == 0)
		{
			tempLng = Tempo2Mid(songTempo);
			WriteBE32(tempArr, tempLng);
			WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
		}
		
		if (tempTInf->midChn == 0xFF)
		{
			// disabled track (the driver skips processing this track)
			trkEnd = 1;
		}
		else
		{
			trkEnd = 0;
			MTS.midChn = tempTInf->midChn & 0x0F;
			WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
		}
		rhythmMode = tempTInf->midChn & 0x80;
		
		segPos = tempTInf->startOfs;
		measureID = 1;
		loopIdx = 0;
		inPos = 0x0000;
		earlyNoteOff = 0;	// verified with driver disassembly
		curNoteVol = 0x7F;	// verified with driver disassembly
		RunNoteCnt = 0;
		
		while(! trkEnd && inPos < songLen)
		{
			if (! inPos)
			{
				inPos = ReadBE16(&songData[segPos]);
				segPos += 0x02;
				if (inPos < 0x0010)
				{
					switch(inPos)
					{
					case 0:	// track end
					default:	// fallback case - confirmed via disassembly
						trkEnd = 1;
						break;
					case 1:	// master loop start
						if (loopIdx >= 0x10)
						{
							printf("Error: Too many nested loops!\n");
							trkEnd = 1;
							break;
						}
						loopCount[loopIdx] = 0;
						loopMeasure[loopIdx] = measureID;
						loopSegPos[loopIdx] = segPos;
						loopIdx = 0;
						if (segPos == tempTInf->loopOfs)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCount[loopIdx]);
						break;
					case 2:	// master loop end
						{
							UINT16 numLoops;
							
							numLoops = ReadBE16(&songData[segPos]);
							segPos += 0x02;
							if (loopIdx == 0)	// the driver actually checks this
							{
								trkEnd = 1;
								break;
							}
							loopIdx --;
							loopCount[loopIdx] ++;
							
							if (! numLoops)
							{
								numLoops = tempTInf->loopTimes;
								if (loopCount[loopIdx] < 0x80)
									WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCount[loopIdx]);
							}
							if (loopCount[loopIdx] < numLoops)
							{
								measureID = loopMeasure[loopIdx];
								segPos = loopSegPos[loopIdx];
								loopIdx ++;
							}
							
							// not done by the driver, but fixes drum track in Arcus Odyssey 1B.BIN
							if (! DRV_BUGS)
								earlyNoteOff = 0;
						}
						break;
					}
					inPos = 0x0000;
				}
				continue;
			}
			
			curCmd = songData[inPos];	inPos ++;
			if (curCmd < 0x80)
			{
				curNote = curCmd;
				curDelay = songData[inPos];	inPos ++;
				if (rhythmMode)
				{
					curNoteLen = (curDelay > 0) ? curDelay : 1;
				}
				else
				{
					if (! curDelay)
					{
						curNoteLen = songData[inPos];	inPos ++;
						// Note: notes with curNoteLen==0 are completely ignored
					}
					else
					{
						// possible overflow intended (confirmed via driver disassembly)
						curNoteLen = curDelay - earlyNoteOff;
					}
				}
				CheckRunningNotes(&midFileInf, &MTS.curDly);
				
				for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
				{
					if (RunNotes[tempByt].note == curNote)
					{
						// I confirmed via disassembly, that the actual driver *sets*
						// the note length. (It doesn't just add the ticks to the previous length.)
						RunNotes[tempByt].remLen = MTS.curDly + curNoteLen;
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
				
				MTS.curDly += curDelay;
			}
			else
			{
				// I should proably turn this if-else block into a LUT, but... can't be bothered.
				if (curCmd >= 0x80 && curCmd <= 0xAF)
				{
					curCmdType = 0x00 + ((curCmd & 0x78) >> 3);
					curDelay = ((curCmd & 0x07) == 0x07) ? 0xFF : (curCmd & 0x07);
				}
				else if (curCmd >= 0xB0 && curCmd <= 0xDF)
				{
					curCmdType = 0x06 - 0x03 + ((curCmd & 0x70) >> 4);
					curDelay = ((curCmd & 0x0F) == 0x0F) ? 0xFF : (curCmd & 0x0F);
				}
				else if (curCmd >= 0xE0 && curCmd <= 0xE7)
				{
					curCmdType = 0x09 + ((curCmd & 0x06) >> 1);
					curDelay = ((curCmd & 0x01) == 0x01) ? 0xFF : (curCmd & 0x0F);
				}
				else
				{
					curCmdType = curCmd;
					curDelay = 0;
				}
				if (curDelay == 0xFF)
				{
					curDelay = songData[inPos];
					inPos ++;
				}
				
				switch(curCmdType)
				{
				case 0x00:	// Modulation
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, songData[inPos]);
					inPos ++;
					break;
				case 0x01:	// Volume
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, songData[inPos]);
					inPos ++;
					break;
				case 0x02:	// Pan
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, songData[inPos]);
					inPos ++;
					break;
				case 0x03:	// Expression
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, songData[inPos]);
					inPos ++;
					break;
				case 0x04:	// Instrument
					WriteEvent(&midFileInf, &MTS, 0xC0, songData[inPos], 0x00);
					inPos ++;
					break;
				case 0x05:	// Control Change
					WriteEvent(&midFileInf, &MTS, 0xB0, songData[inPos + 0x00], songData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x06:	// Pitch Bend (8-bit)
					pbVal = 0x2000 + songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xE0, (pbVal >> 0) & 0x7F, (pbVal >> 7) & 0x7F);
					break;
				case 0x07:	// Pitch Bend (16-bit)
					pbVal = 0x2000 + ReadBE16(&songData[inPos]);	inPos += 0x02;
					WriteEvent(&midFileInf, &MTS, 0xE0, (pbVal >> 0) & 0x7F, (pbVal >> 7) & 0x7F);
					break;
				case 0x08:	// set Early Note Stop
					if ((curCmd & 0x0F) == 0xFF)
						earlyNoteOff = curDelay;	// use parameter byte directly
					else
						earlyNoteOff = curDelay - 1;	// overflow from 0x00 to 0xFF intended and in original driver
					curDelay = 0;
					break;
				case 0x09:	// Tempo
					printf("Tempo Change: %u, %u\n", songData[inPos + 0x00], songData[inPos + 0x01]);
					tempByt = songData[inPos + 0x00];
					inPos += 0x02;	// 2nd byte unknown
					
					tempLng = Tempo2Mid(tempByt);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					break;
				case 0x0A:	// set MIDI channel
					tempByt = songData[inPos];	inPos ++;
					if (tempByt == 0xFF)
					{
						trkEnd = 1;	// confirmed via driver disassembly
					}
					else
					{
						MTS.midChn = tempByt & 0x0F;
						WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
					}
					break;
				case 0x0B:	// set Channel Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xD0, songData[inPos], 0x00);
					inPos ++;
					break;
				case 0x0C:	// set Note Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xA0, songData[inPos + 0x00], songData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xEC:	// Pan Centre
				case 0xED:	// Pan Right
				case 0xEE:	// Pan Left
				case 0xEF:	// Pan Centre
					if (curCmd == 0xED)
						tempByt = 0x00;
					else if (curCmd == 0xEE)
						tempByt = 0x7F;
					else
						tempByt = 0x40;
					if (! DRV_BUGS)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					else
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x10, tempByt);	// yes, it *really* does this (typo: 10 vs $10)
					break;
				case 0xF0:	// delay
					curDelay = songData[inPos];
					if (curDelay > 0 && DRV_BUGS)
					{
						// The driver stops *all* running notes before executing a delay > 0.
						// However, most of the time this doesn't seem to be intentional, as it cuts
						// notes that just started to play.
						// e.g. Arcus Odyssey 15.BIN (ch 9), Ryu: Naki No Ryu Yori 08.BIN (ch 11)
						UINT32 maxLen = 0;
						
						for (curNote = 0; curNote < RunNoteCnt; curNote ++)
						{
							if (RunNotes[curNote].remLen > maxLen)
								maxLen = RunNotes[curNote].remLen;
						}
						if (maxLen > MTS.curDly)
						{
							//printf("Notes still playing at delay on track %u / chn %u!\n", curTrk, MTS.midChn);
							//WriteEvent(&midFileInf, &MTS, 0xB0, 113, 0x00);
							for (curNote = 0; curNote < RunNoteCnt; curNote ++)
							{
								if (RunNotes[curNote].remLen > MTS.curDly)
									RunNotes[curNote].remLen = MTS.curDly;
							}
						}
					}
					inPos ++;
					break;
				case 0xF1:	// set Note Velocity
					curNoteVol = songData[inPos];
					inPos ++;
					break;
				case 0xFC:	// delay + more?
					// The sound driver ignores all parameters but this one.
					curDelay = songData[inPos + 0x01];
					inPos += 0x04;
					break;
				case 0xFD:	// increment measure ID
					measureID ++;
					break;
				case 0xFE:	// measure quit
					// load new measure pointer
					inPos = 0x0000;
					break;
				case 0xFF:	// measure end
					// increment measure ID + load new measure pointer
					measureID ++;
					inPos = 0x0000;
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
				MTS.curDly += curDelay;
			}
		}
		FlushRunningNotes(&midFileInf, &MTS);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseWtMTrk(UINT32 songLen, const UINT8* songData, TRK_INFO* trkInf)
{
	UINT16 segPos;
	UINT16 inPos;
	UINT8 cmdDelay;
	UINT8 trkEnd;
	UINT8 rhythmMode;
	UINT8 curCmd;
	UINT8 curCmdType;
	UINT8 loopIdx;
	UINT16 loopCount[0x10];
	UINT16 loopSegPos[0x10];
	UINT32 loopTick[0x10];
	
	trkEnd = (trkInf->midChn == 0xFF);
	loopIdx = 0x00;
	trkInf->loopOfs = 0x0000;
	rhythmMode = trkInf->midChn & 0x80;
	segPos = trkInf->startOfs;
	inPos = 0x0000;
	while(! trkEnd && inPos < songLen)
	{
		if (! inPos)
		{
			inPos = ReadBE16(&songData[segPos]);
			segPos += 0x02;
			if (inPos < 0x0010)
			{
				switch(inPos)
				{
				case 0:	// track end
				default:
					trkEnd = 1;
					break;
				case 1:	// master loop start
					if (loopIdx >= 0x10)
					{
						trkEnd = 1;
						break;
					}
					loopCount[loopIdx] = 0;
					loopSegPos[loopIdx] = segPos;
					loopTick[loopIdx] = trkInf->tickCnt;
					loopIdx = 0;
					break;
				case 2:	// master loop end
					{
						UINT16 numLoops;
						
						numLoops = ReadBE16(&songData[segPos]);
						segPos += 0x02;
						if (loopIdx == 0)
						{
							trkEnd = 1;
							break;
						}
						loopIdx --;
						loopCount[loopIdx] ++;
						
						if (! numLoops)
						{
							// master loop
							if (! trkInf->loopOfs)
								trkInf->loopOfs = inPos;
						}
						if (loopCount[loopIdx] < numLoops)
						{
							segPos = loopSegPos[loopIdx];
							loopIdx ++;
						}
					}
					break;
				}
				inPos = 0x0000;
			}
			continue;
		}
			
		curCmd = songData[inPos];
		if (curCmd < 0x80)
		{
			cmdDelay = songData[inPos + 0x01];
			inPos += 0x02;
			if (cmdDelay == 0 && ! rhythmMode)
				inPos ++;
		}
		else
		{
			inPos ++;
			if (curCmd >= 0x80 && curCmd <= 0xAF)
			{
				curCmdType = 0x00 + ((curCmd & 0x78) >> 3);
				cmdDelay = ((curCmd & 0x07) == 0x07) ? 0xFF : (curCmd & 0x07);
			}
			else if (curCmd >= 0xB0 && curCmd <= 0xDF)
			{
				curCmdType = 0x06 - 0x03 + ((curCmd & 0x70) >> 4);
				cmdDelay = ((curCmd & 0x0F) == 0x0F) ? 0xFF : (curCmd & 0x0F);
			}
			else if (curCmd >= 0xE0 && curCmd <= 0xE7)
			{
				curCmdType = 0x09 + ((curCmd & 0x06) >> 1);
				cmdDelay = ((curCmd & 0x01) == 0x01) ? 0xFF : (curCmd & 0x0F);
			}
			else
			{
				curCmdType = curCmd;
				cmdDelay = 0;
			}
			if (cmdDelay == 0xFF)
			{
				cmdDelay = songData[inPos];
				inPos ++;
			}
			switch(curCmdType)
			{
			case 0x08:	// set Early Note Stop
			case 0xEC:	// Pan Centre
			case 0xED:	// Pan Right
			case 0xEE:	// Pan Left
			case 0xEF:	// Pan Centre
				break;
			case 0x00:	// Modulation
			case 0x01:	// Volume
			case 0x02:	// Pan
			case 0x03:	// Expression
			case 0x04:	// Instrument
			case 0x06:	// Pitch Bend (8-bit)
			case 0x0B:	// set Channel Aftertouch
			case 0xF1:	// set Note Velocity
				inPos ++;
				break;
			case 0x05:	// Control Change
			case 0x07:	// Pitch Bend (16-bit)
			case 0x09:	// Tempo
			case 0x0C:	// set Note Aftertouch
				inPos += 0x02;
				break;
			case 0x0A:	// set MIDI channel
				if (songData[inPos] == 0xFF)
					trkEnd = 1;	// confirmed via driver disassembly
				inPos ++;
				break;
			case 0xF0:	// delay
				cmdDelay = songData[inPos];
				inPos ++;
				break;
			case 0xFC:	// delay + more?
				cmdDelay = songData[inPos + 0x01];
				inPos += 0x04;
				break;
			case 0xFD:	// increment measure ID
				//measureID ++;
				break;
			case 0xFE:	// measure quit
				// load new measure pointer
				inPos = 0x0000;
				break;
			case 0xFF:	// measure end
				// increment measure ID + load new measure pointer
				//measureID ++;
				inPos = 0x0000;
				break;
			default:
				trkEnd = 1;
				break;
			}
		}
		trkInf->tickCnt += cmdDelay;
	}
	
	return;
}

static void GuessLoopTimes(UINT8 trkCnt, TRK_INFO* trkInf)
{
	UINT8 curTrk;
	TRK_INFO* tempTInf;
	UINT32 trkLen;
	UINT32 trkLoopLen;
	UINT32 maxTrkLen;
	
	maxTrkLen = 0x00;
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		if (tempTInf->loopOfs)
			trkLoopLen = tempTInf->tickCnt - tempTInf->loopTick;
		else
			trkLoopLen = 0x00;
		
		trkLen = tempTInf->tickCnt + trkLoopLen * (tempTInf->loopTimes - 1);
		if (maxTrkLen < trkLen)
			maxTrkLen = trkLen;
	}
	
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		if (tempTInf->loopOfs)
			trkLoopLen = tempTInf->tickCnt - tempTInf->loopTick;
		else
			trkLoopLen = 0x00;
		if (trkLoopLen < 0x20)
			continue;
		
		trkLen = tempTInf->tickCnt + trkLoopLen * (tempTInf->loopTimes - 1);
		if (trkLen * 5 / 4 < maxTrkLen)
		{
			// trkLen = desired length of the loop
			trkLen = maxTrkLen - tempTInf->loopTick;
			
			tempTInf->loopTimes = (UINT16)((trkLen + trkLoopLen / 3) / trkLoopLen);
			printf("Trk %u: Extended loop to %u times\n", curTrk, tempTInf->loopTimes);
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
			RunNotes[curNote].remLen -= tempDly;
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
				// The sound driver actually sends 8c nn 00, but I prefer it this way.
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

INLINE UINT32 Tempo2Mid(UINT16 bpm)
{
	// formula: (60 000 000 / bpm) * (MIDI_RES / 48)
	return 1250000 * MIDI_RES / bpm;
}


INLINE UINT16 ReadBE16(const UINT8* data)
{
	return (data[0x00] << 8) | (data[0x01] << 0);
}
