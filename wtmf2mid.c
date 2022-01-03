// Wolfteam MF -> Midi Converter
// -----------------------------
// Written by Valley Bell, 06 August 2019
// Updated with Little Endian support on 24 October 2019
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


typedef struct _track_info
{
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 midChn;
} TRK_INF;


#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


static UINT8 ReadMFHeader(UINT32 songLen, const UINT8* songData);
UINT8 WtM2Mid(UINT16 songLen, const UINT8* songData);
static void PreparseWtMTrk(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale);

INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT16 ReadBE16(const UINT8* data);
INLINE UINT16 ReadUInt16(const UINT8* data);
INLINE UINT32 ReadLE32(const UINT8* data);
INLINE UINT32 ReadBE32(const UINT8* data);
INLINE UINT32 ReadUInt32(const UINT8* data);
static const char* GetLastDirSepPos(const char* fileName);
INLINE const char* GetFileTitle(const char* fileName);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT32 MidAlloc;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be plenty, the driver can only store up to 8 simultaneous notes
static UINT16 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

static UINT16 MIDI_RES = 0;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 DRV_BUGS = 0;

// MF sub-song flags
//	bit 0 (01): Endianess (0 - little, 1 - big)
//	others are unknown, but the X68000 sound drivers enforce 0x28
static UINT8 mfFlags = 0x00;
static UINT8 mfSongs = 0;
static UINT32 mfSize = 0;

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
		printf("    -TpQ n      convert with n ticks per quarter.\n");
		printf("                default: 48 (PC-98) / 24 (X68000)\n");
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
				MIDI_RES = (UINT16)strtoul(argv[argbase], NULL, 0);
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
	if (ROMLen > 0x100000)	// 1 MB
		ROMLen = 0x100000;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	retVal = ReadMFHeader(ROMLen, ROMData);
	if (! retVal)
	{
		const char* fileName = argv[argbase + 1];
		const char* fileExt;
		char* outName;
		char* outExt;
		UINT32 curPos;
		UINT8 curSong;
		UINT16 songLen;
		
		fileExt = strrchr(GetFileTitle(fileName), '.');
		if (fileExt == NULL)
			fileExt = fileName + strlen(fileName);
		outName = (char*)malloc(strlen(fileName) + 0x10);
		strcpy(outName, fileName);
		outExt = outName + (fileExt - fileName);
		
		if (mfSongs == 0)
			printf("Info: The file contains no songs.\n");
		else if (mfSongs > 1)
			printf("Info: Converting %u songs.\n", mfSongs);
		if (mfSize > ROMLen)
			mfSize = ROMLen;	// just for safety
		
		MidAlloc = 0x20000;	// 128 KB should be enough
		MidData = (UINT8*)malloc(MidAlloc);
		curPos = 0x08;
		// I haven't seen any files that contain multiple songs so far.
		// But the format and sound driver both support it.
		for (curSong = 0; curSong < mfSongs; curSong ++)
		{
			if (curPos >= mfSize)
				break;
			
			songLen = ReadUInt16(&ROMData[curPos]);
			if ((UINT32)songLen > mfSize - curPos)
				songLen = (UINT16)(mfSize - curPos);
			
			retVal = WtM2Mid(songLen, &ROMData[curPos]);
			if (! retVal)
			{
				// generate file name(ABC.ext -> ABC_00.ext)
				if (mfSongs > 1)
					sprintf(outExt, "_%02X%s", curSong, fileExt);
				WriteFileData(MidLen, MidData, outName);
			}
			curPos += songLen;
		}
		free(MidData);	MidData = NULL;
		free(outName);	outName = NULL;
	}
	
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	//getchar();
#endif
	
	return 0;
}

static UINT8 ReadMFHeader(UINT32 songLen, const UINT8* songData)
{
	UINT32 valLE;
	UINT32 valBE;
	
	if (memcmp(&songData[0x00], "MF", 0x02) && memcmp(&songData[0x00], "MU", 0x02))
	{
		printf("Not a Wolfteam MF file!\n");
		return 0x80;
	}
	if (songData[0x02] > 2 || songData[0x03] > 0)
	{
		printf("Not a Wolfteam MF file!\n");
		return 0x80;
	}
	valLE = ReadLE32(&songData[0x04]);
	valBE = ReadBE32(&songData[0x04]);
	mfFlags = (valLE < valBE) ? 0x00 : 0x01;	// bit 0 = Endianess
	
	mfSongs = songData[0x02];
	// 0x03 is unknown (always 0)
	mfSize = ReadUInt32(&songData[0x04]);	// archive size
	
	return 0x00;
}

UINT8 WtM2Mid(UINT16 songLen, const UINT8* songData)
{
	TRK_INF trkInf[32];
	TRK_INF* tempTInf;
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
	UINT8 tempArr[10];
	UINT8 gsParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	
	midFileInf.alloc = MidAlloc;
	midFileInf.data = MidData;
	midFileInf.pos = 0x00;
	
	inPos = 0x00;
	mfFlags = songData[0x05];	// read flags first
	segPos = ReadUInt16(&songData[0x00]);
	if (songLen > segPos)
		songLen = segPos;
	//tempSht = ReadUInt16(&songData[0x02]);	// unknown
	//tempByt = songData[0x04];	// unknown
	songTempo = songData[0x06];
	trkCnt = songData[0x07];
	
	inPos = 0x08;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x04)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = ReadUInt16(&songData[inPos + 0x00]);
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		tempTInf->midChn = songData[inPos + 0x02];
		
		PreparseWtMTrk(songLen, songData, tempTInf);
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! MIDI_RES)
		MIDI_RES = (mfFlags & 0x01) ? 24 : 48;	// different default based on Endianess flag
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (curTrk == 0)
		{
			tempLng = Tempo2Mid(songTempo, 0x40);
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
				inPos = ReadUInt16(&songData[segPos]);
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
						if (segPos == tempTInf->loopOfs)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCount[loopIdx]);
						loopIdx ++;
						break;
					case 2:	// master loop end
						{
							UINT16 numLoops;
							
							numLoops = ReadUInt16(&songData[segPos]);
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
						// possible underflow intended (confirmed via driver disassembly)
						curNoteLen = curDelay - earlyNoteOff;
					}
				}
				
				CheckRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes);
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
					AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
									MTS.midChn, curNote, 0x00, curNoteLen);	// The sound driver sends 8# note 00.
				}
				
				MTS.curDly += curDelay;
			}
			else
			{
				// I should proably turn this if-else block into a LUT, but... can't be bothered.
				if (curCmd >= 0x80 && curCmd <= 0xAF)
				{
					curCmdType = curCmd & ~0x07;
					curDelay = ((curCmd & 0x07) == 0x07) ? 0xFF : (curCmd & 0x07);
				}
				else if (curCmd >= 0xB0 && curCmd <= 0xDF)
				{
					curCmdType = curCmd & ~0x0F;
					curDelay = ((curCmd & 0x0F) == 0x0F) ? 0xFF : (curCmd & 0x0F);
				}
				else if (curCmd >= 0xE0 && curCmd <= 0xE7)
				{
					curCmdType = curCmd & ~0x01;
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
				case 0x80:	// Modulation
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, songData[inPos]);
					inPos ++;
					break;
				case 0x88:	// Volume
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, songData[inPos]);
					inPos ++;
					break;
				case 0x90:	// Pan
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, songData[inPos]);
					inPos ++;
					break;
				case 0x98:	// Expression
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, songData[inPos]);
					inPos ++;
					break;
				case 0xA0:	// Instrument
					WriteEvent(&midFileInf, &MTS, 0xC0, songData[inPos], 0x00);
					inPos ++;
					break;
				case 0xA8:	// Control Change
					WriteEvent(&midFileInf, &MTS, 0xB0, songData[inPos + 0x00], songData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xB0:	// Pitch Bend (8-bit)
					pbVal = 0x2000 + songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xE0, (pbVal >> 0) & 0x7F, (pbVal >> 7) & 0x7F);
					break;
				case 0xC0:	// Pitch Bend (16-bit)
					pbVal = 0x2000 + ReadUInt16(&songData[inPos]);	inPos += 0x02;
					WriteEvent(&midFileInf, &MTS, 0xE0, (pbVal >> 0) & 0x7F, (pbVal >> 7) & 0x7F);
					break;
				case 0xD0:	// set Early Note Stop
					if ((curCmd & 0x0F) == 0x0F)
						earlyNoteOff = curDelay;	// use parameter byte directly
					else
						earlyNoteOff = curDelay - 1;	// underflow from 0x00 to 0xFF intended and in original driver
					curDelay = 0;
					break;
				case 0xE0:	// Tempo
					//printf("Tempo Change: %u, %u\n", songData[inPos + 0x00], songData[inPos + 0x01]);
					tempByt = songData[inPos + 0x00];
					inPos += 0x02;	// 2nd byte is unknown and ignored by all drivers
					
					// Note: Some songs in spanof98 (e.g. 0EE_MI03 and 0EF_MI04) use this command.
					// Its first parameter is a 2.6 fixed point scale factor.
					// (i.e. 0x40 = 100%, 0x20 = 50%, just like in RCP files)
					//
					// However, in the PC-9801 and X68000 sound drivers I checked, they just store the
					// parameter into the "song tempo" value - and then don't touch it again.
					// So the command has no effect.
					//
					// P.S.: It is implemented properly in MFD.COM
					// P.S.2: in MFD.COM, the formula is: songTempo = baseSongTempo + tempByt - 0x40;
					if (! DRV_BUGS)
					{
						tempLng = Tempo2Mid(songTempo, tempByt);
						WriteBE32(tempArr, tempLng);
						WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					}
					break;
				case 0xE2:	// set MIDI channel
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
				case 0xE4:	// set Channel Aftertouch
					WriteEvent(&midFileInf, &MTS, 0xD0, songData[inPos], 0x00);
					inPos ++;
					break;
				case 0xE6:	// set Note Aftertouch
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
					else	// typo bug present in X68000 driver (good in PC-98 version)
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
				case 0xFC:	// raw RCP command
					// The Wolfteam sound driver ignores all parameters but the delay.
					// However, MFD.COM actually implements this (with RCP commands DD/DE/DF only)
					// and e.g. night_s_98 uses it for SysEx commands.
					curCmd = songData[inPos + 0x00];
					curDelay = songData[inPos + 0x01];
					switch(curCmd)
					{
					case 0xDD:	// Roland Base Address
						gsParams[2] = songData[inPos + 0x02];
						gsParams[3] = songData[inPos + 0x03];
						break;
					case 0xDE:	// Roland Parameter
						gsParams[4] = songData[inPos + 0x02];
						gsParams[5] = songData[inPos + 0x03];
						{
							UINT8 chkSum;
							UINT8 curParam;
							
							tempArr[0] = 0x41;	// Roland ID
							tempArr[1] = gsParams[0];
							tempArr[2] = gsParams[1];
							tempArr[3] = 0x12;
							chkSum = 0x00;	// initialize checksum
							for (curParam = 0; curParam < 4; curParam ++)
							{
								tempArr[4 + curParam] = gsParams[2 + curParam];
								chkSum += gsParams[2 + curParam];	// add to checksum
							}
							tempArr[8] = (0x100 - chkSum) & 0x7F;
							tempArr[9] = 0xF7;
							WriteLongEvent(&midFileInf, &MTS, 0xF0, 10, tempArr);
						}
						break;
					case 0xDF:	// Roland Device
						gsParams[0] = songData[inPos + 0x02];
						gsParams[1] = songData[inPos + 0x03];
						break;
					case 0xF6:	// comment
						{
							UINT32 curPos;
							UINT32 allDelay;
							UINT16 txtLen;
							UINT16 txtPos;
							char* txtBuf;
							
							txtLen = 2;
							for (curPos = inPos + 0x05; inPos < songLen; curPos += 0x05, txtLen += 2)
							{
								if (! (songData[curPos - 0x01] == 0xFC && songData[curPos + 0x00] == 0xF7))
									break;
							}
							
							// I know constantly doing malloc() is inefficient ... but I don't care,
							// as comments aren't used a lot in the files.
							txtBuf = (char*)malloc(txtLen);
							allDelay = 0;
							for (curPos = inPos, txtPos = 0; txtPos < txtLen; curPos += 0x05, txtPos += 2)
							{
								allDelay += songData[curPos + 0x01];
								txtBuf[txtPos + 0x00] = songData[curPos + 0x02];
								txtBuf[txtPos + 0x01] = songData[curPos + 0x03];
							}
							while(txtLen > 0 && txtBuf[txtLen - 1] == ' ')
								txtLen --;	// trim off trailing spaces
							WriteMetaEvent(&midFileInf, &MTS, 0x01, txtLen, txtBuf);
							free(txtBuf);
							
							curDelay = 0;
							MTS.curDly += allDelay;
							inPos = curPos - 0x05;
						}
						break;
					case 0xF7:	// continuation of previous command
						printf("Warning Track %u: Unexpected RCP continuation command at 0x%04X!\n", curTrk, inPos);
						break;
					default:
						printf("Warning Track %u: Unhandled RCP command %02X at 0x%04X!\n", curTrk, curCmd, inPos);
						break;
					}
					inPos += 0x04;
					break;
				case 0xFD:	// increment measure ID
					// I haven't found any file that uses this.
					//printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					measureID ++;
					break;
				case 0xFE:	// measure quit
					// This command is often used to "split" a measure in order to set a loop point.
					//printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					// load new measure pointer
					inPos = 0x0000;
					break;
				case 0xFF:	// measure end
					// increment measure ID + load new measure pointer
					measureID ++;
					inPos = 0x0000;
					break;
				default:
					printf("Unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
				MTS.curDly += curDelay;
			}
		}
		FlushRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes, 0);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidAlloc = midFileInf.alloc;
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseWtMTrk(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf)
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
			inPos = ReadUInt16(&songData[segPos]);
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
					loopIdx ++;
					break;
				case 2:	// master loop end
					{
						UINT16 numLoops;
						
						numLoops = ReadUInt16(&songData[segPos]);
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
							{
								trkInf->loopOfs = loopSegPos[loopIdx];
								trkInf->loopTick = loopTick[loopIdx];
							}
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
				curCmdType = curCmd & ~0x07;
				cmdDelay = ((curCmd & 0x07) == 0x07) ? 0xFF : (curCmd & 0x07);
			}
			else if (curCmd >= 0xB0 && curCmd <= 0xDF)
			{
				curCmdType = curCmd & ~0x0F;
				cmdDelay = ((curCmd & 0x0F) == 0x0F) ? 0xFF : (curCmd & 0x0F);
			}
			else if (curCmd >= 0xE0 && curCmd <= 0xE7)
			{
				curCmdType = curCmd & ~0x01;
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
			case 0xD0:	// set Early Note Stop
				// the value used as delay for all other events is the actual parameter here
				cmdDelay = 0;
				break;
			case 0xEC:	// Pan Centre
			case 0xED:	// Pan Right
			case 0xEE:	// Pan Left
			case 0xEF:	// Pan Centre
				break;
			case 0x80:	// Modulation
			case 0x88:	// Volume
			case 0x90:	// Pan
			case 0x98:	// Expression
			case 0xA0:	// Instrument
			case 0xB0:	// Pitch Bend (8-bit)
			case 0xE4:	// set Channel Aftertouch
			case 0xF1:	// set Note Velocity
				inPos ++;
				break;
			case 0xA8:	// Control Change
			case 0xC0:	// Pitch Bend (16-bit)
			case 0xE0:	// Tempo
			case 0xE6:	// set Note Aftertouch
				inPos += 0x02;
				break;
			case 0xE2:	// set MIDI channel
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

INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale)
{
	// formula: (60 000 000 / bpm) * (64 / scale) * (MIDI_RES / 48)
	UINT32 div = bpm * scale;
	return (UINT32)((UINT64)80000000 * MIDI_RES / div);
}



INLINE UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x00] << 0) | (data[0x01] << 8);
}

INLINE UINT16 ReadBE16(const UINT8* data)
{
	return	(data[0x00] << 8) | (data[0x01] << 0);
}

INLINE UINT16 ReadUInt16(const UINT8* data)
{
	return (mfFlags & 0x01) ? ReadBE16(data) : ReadLE16(data);
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x00] <<  0) | (data[0x01] <<  8) |
			(data[0x02] << 16) | (data[0x03] << 24);
}

INLINE UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}

INLINE UINT32 ReadUInt32(const UINT8* data)
{
	return (mfFlags & 0x01) ? ReadBE32(data) : ReadLE32(data);
}

static const char* GetLastDirSepPos(const char* fileName)
{
	const char* sepPos;
	const char* wSepPos;	// Windows separator
	
	sepPos = strrchr(fileName, '/');
	wSepPos = strrchr(fileName, '\\');
	if (wSepPos == NULL)
		return sepPos;
	else if (sepPos == NULL)
		return wSepPos;
	return (wSepPos > sepPos) ? wSepPos : sepPos;
}

INLINE const char* GetFileTitle(const char* fileName)
{
	const char* sepPos = GetLastDirSepPos(fileName);
	return (sepPos == NULL) ? fileName : (sepPos + 1);
}
