// Wolfteam MMU -> Midi Converter
// ------------------------------
// Written by Valley Bell
// based on RCP -> Midi Converter
// TODO: Add an option to convert ZAN3.MTN, which is a stripped-down .CM6 file
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

typedef struct mmu_info
{
	UINT8 fileVer;
	UINT16 trkCnt;
	UINT16 tickRes;
	UINT16 tempoBPM;
	UINT8 gblTransp;
} MMU_INFO;

typedef struct _track_info
{
	UINT32 startOfs;
	UINT32 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
} TRK_INF;

#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


#define MCMD_INI_EXCLUDE	0x00	// exclude initial command
#define MCMD_INI_INCLUDE	0x01	// include initial command
#define MCMD_RET_CMDCOUNT	0x00	// return number of commands
#define MCMD_RET_DATASIZE	0x02	// return number of data bytes


static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);

UINT8 Mmu2Mid(UINT32 songLen, const UINT8* songData);
static UINT8 MmuTrk2MidTrk(UINT32 songLen, const UINT8* songData, const MMU_INFO* mmuInf,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 PreparseMmuTrack(UINT32 songLen, const UINT8* songData, const MMU_INFO* mmuInf, TRK_INF* trkInf);
static UINT16 GetMultiCmdDataSize(UINT32 songLen, const UINT8* songData, UINT32 startPos, UINT8 flags);
static UINT16 ReadMultiCmdData(UINT32 songLen, const UINT8* songData,
								UINT32* mmuInPos, UINT32 bufSize, UINT8* buffer, UINT8 flags);
static UINT16 GetTrimmedLength(UINT16 dataLen, const char* data, char trimChar, UINT8 leaveLast);
INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static UINT16 ReadLE16(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT16 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

static UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 KEEP_DUMMY_CH = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("Wolfteam MMU -> Midi Converter\n------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: mmu2mid.exe [options] input.bin output.mid\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -KeepDummyCh convert data with MIDI channel set to -1\n");
		printf("                channel -1 is invalid, which can be used for muting\n");
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
		else if (! stricmp(argv[argbase] + 1, "KeepDummyCh"))
			KEEP_DUMMY_CH = 1;
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
	
	retVal = Mmu2Mid(ROMLen, ROMData);
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


UINT8 Mmu2Mid(UINT32 songLen, const UINT8* songData)
{
	TRK_INF trkInf[18];
	TRK_INF* tempTInf;
	UINT8 tempArr[0x20];
	MMU_INFO mmuInf;
	UINT8 curTrk;
	UINT32 inPos;
	UINT32 tempLng;
	UINT8 retVal;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	
	if (memcmp(&songData[0x00], "MMU1", 0x04))
	{
		printf("Not a Wolfteam MF file!\n");
		return 0x80;
	}
	
	midFInf.alloc = 0x20000;	// 128 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	mmuInf.tempoBPM = songData[0x04];
	mmuInf.gblTransp = songData[0x05];
	mmuInf.trkCnt = 18;	// track count is fixed
	
	inPos = 0x10;
	for (curTrk = 0; curTrk < mmuInf.trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = inPos;
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		
		if (inPos < songLen)
		{
			tempLng = ReadLE16(&songData[inPos]);
			PreparseMmuTrack(songLen, songData, &mmuInf, tempTInf);
			inPos += tempLng;
		}
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(mmuInf.trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFInf, 0x0001, mmuInf.trkCnt, MIDI_RES);
	
	retVal = 0x00;
	for (curTrk = 0; curTrk < mmuInf.trkCnt; curTrk ++)
	{
		WriteMidiTrackStart(&midFInf, &MTS);
		
		if (curTrk == 0)
		{
			tempLng = Tempo2Mid(mmuInf.tempoBPM, 0x40);
			WriteBE32(tempArr, tempLng);
			WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
		}
		
		retVal = MmuTrk2MidTrk(songLen, songData, &mmuInf, &trkInf[curTrk], &midFInf, &MTS);
		
		WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFInf, &MTS);
		
		if (retVal)
		{
			if (retVal == 0x01)
			{
				printf("Early EOF when trying to read track %u!\n", 1 + curTrk);
				retVal = 0x00;	// assume that early EOF is not an error (trkCnt may be wrong)
			}
			break;
		}
	}
	
	MidData = midFInf.data;
	MidLen = midFInf.pos;
	
	midFInf.pos = 0x00;
	WriteMidiHeader(&midFInf, 0x0001, curTrk, MIDI_RES);
	
	return retVal;
}

static UINT8 MmuTrk2MidTrk(UINT32 songLen, const UINT8* songData, const MMU_INFO* mmuInf,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT32 inPos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT16 repMeasure;
	UINT8 trkID;
	UINT8 midiDev;
	UINT8 midChn;
	UINT8 transp;
	UINT8 startTick;
	UINT8 trkMute;
	UINT8 tempArr[0x40];
	UINT16 curBar;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT8 cmdP1;
	UINT8 cmdP2;
	UINT8 cmdP0Delay;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT16 loopCnt[8];
	UINT8 gsParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT32 txtBufSize;
	UINT8* txtBuffer;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	
	inPos = trkInf->startOfs;
	trkLen = ReadLE16(&songData[inPos]);
	trkEndPos = inPos + trkLen;
	if (trkEndPos > songLen)
		trkEndPos = songLen;
	if (inPos + 0x08 > songLen)
		return 0x01;	// not enough bytes to read the header
	
	trkID = songData[inPos + 0x02];		// track ID
	midChn = songData[inPos + 0x04];	// MIDI channel
	if (midChn == 0xFF)
	{
		// When the KeepDummyCh option is off, prevent events from being
		// written to the MIDI by setting midiDev to 0xFF.
		midiDev = KEEP_DUMMY_CH ? 0x00 : 0xFF;
		midChn = 0x00;
	}
	else
	{
		midiDev = midChn >> 4;
		midChn &= 0x0F;
	}
	transp = songData[inPos + 0x05];	// transposition
	startTick = songData[inPos + 0x06];	// start tick (unsigned 8-bit according to driver disassembly)
	trkMute = songData[inPos + 0x07] & 0x01;	// mute
	inPos += 0x08;
	
	if (midiDev != 0xFF)
	{
		WriteMetaEvent(fInf, MTS, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
		WriteMetaEvent(fInf, MTS, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
	}
	if (transp > 0x80)
	{
		// known values are: 0x00..0x3F (+0 .. +63), 0x40..0x7F (-64 .. -1), 0x80 (drums)
		printf("Warning Track %u: Key 0x%02X!\n", trkID, transp);
		transp = 0x00;
	}
	if (startTick != 0)
		printf("Warning Track %u: Start Tick %+d!\n", trkID, startTick);
	
	txtBufSize = 0x00;
	txtBuffer = NULL;
	
	memset(gsParams, 0x00, 6);
	trkEnd = 0;
	parentPos = 0x00;
	repMeasure = 0xFFFF;
	RunNoteCnt = 0x00;
	MTS->midChn = midChn;
	MTS->curDly = startTick;
	loopIdx = 0x00;
	curBar = 0;
	
	while(inPos < trkEndPos && ! trkEnd)
	{
		UINT32 prevPos = inPos;
		
		cmdType = songData[inPos + 0x00];
		cmdP0Delay = songData[inPos + 0x01];
		cmdP1 = songData[inPos + 0x02];
		cmdP2 = songData[inPos + 0x03];
		inPos += 0x04;
		
		if (cmdType < 0x80)
		{
			UINT8 curNote;
			UINT8 curRN;
			
			CheckRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes);
			
			curNote = (cmdType + mmuInf->gblTransp + transp) & 0x7F;
			for (curRN = 0; curRN < RunNoteCnt; curRN ++)
			{
				if (RunNotes[curRN].note == curNote)
				{
					// note already playing - set new length
					RunNotes[curRN].remLen = (UINT16)MTS->curDly + cmdP1;
					cmdP1 = 0;	// prevent adding note below
					break;
				}
			}
			
			// duration == 0 -> no note
			if (cmdP1 > 0 && midiDev != 0xFF)
			{
				WriteEvent(fInf, MTS, 0x90, curNote, cmdP2);
				AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
								MTS->midChn, curNote, 0x80, cmdP1);	// The sound driver sends 9# note 00.
			}
		}
		else switch(cmdType)
		{
		case 0xDD:	// Roland Base Address
			gsParams[2] = cmdP1;
			gsParams[3] = cmdP2;
			break;
		case 0xDE:	// Roland Parameter
			gsParams[4] = cmdP1;
			gsParams[5] = cmdP2;
			if (midiDev == 0xFF)
				break;
			
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
				WriteLongEvent(fInf, MTS, 0xF0, 10, tempArr);
			}
			break;
		case 0xDF:	// Roland Device
			gsParams[0] = cmdP1;
			gsParams[1] = cmdP2;
			break;
		case 0xE6:	// MIDI channel
			//printf("Warning Track %u: Set MIDI Channel command found at 0x%04X\n", trkID, prevPos);
			cmdP1 --;	// It's same as in the track header, except 1 added.
			if (cmdP1 == 0xFF)
			{
				// When the KeepDummyCh option is off, ignore the event.
				// Else set midiDev to 0xFF to prevent events from being written.
				if (! KEEP_DUMMY_CH)
				{
					midiDev = 0xFF;
					midChn = 0x00;
				}
			}
			else
			{
				midiDev = cmdP1 >> 4;	// port ID
				midChn = cmdP1 & 0x0F;	// channel ID
				WriteMetaEvent(fInf, MTS, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
				WriteMetaEvent(fInf, MTS, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
			}
			MTS->midChn = midChn;
			break;
		case 0xE7:	// Tempo Modifier
			{
				UINT32 tempoVal;
				
				if (cmdP2 == 1)
				{
					printf("Warning Track %u: Interpolated Tempo Change at 0x%04X!\n", trkID, prevPos);
					break;	// the driver just ignores it in this case
				}
				tempoVal = (UINT32)(60000000.0 / (mmuInf->tempoBPM * cmdP1 / 64.0) + 0.5);
				tempArr[0] = (tempoVal >> 16) & 0xFF;
				tempArr[1] = (tempoVal >>  8) & 0xFF;
				tempArr[2] = (tempoVal >>  0) & 0xFF;
				WriteMetaEvent(fInf, MTS, 0x51, 0x03, tempArr);
			}
			break;
		case 0xEA:	// Channel Aftertouch
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xD0, cmdP1, 0x00);
			break;
		case 0xEB:	// Control Change
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xB0, cmdP1, cmdP2);
			break;
		case 0xEC:	// Instrument
			if (midiDev == 0xFF)
				break;
			if (cmdP1 < 0x80)
				WriteEvent(fInf, MTS, 0xC0, cmdP1, 0x00);
			break;
		case 0xED:	// Note Aftertouch
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xA0, cmdP1, cmdP2);
			break;
		case 0xEE:	// Pitch Bend
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xE0, cmdP1, cmdP2);
			break;
		case 0xF6:	// comment
			// Note: Not actively supported by the driver, but used by some files.
			//       The driver just skips all unknown commands.
			{
				UINT16 txtLen;
				
				// at first, determine the size of the required buffer
				txtLen = GetMultiCmdDataSize(songLen, songData, inPos, MCMD_INI_INCLUDE | MCMD_RET_DATASIZE);
				if (txtBufSize < txtLen)
				{
					txtBufSize = (txtLen + 0x0F) & ~0x0F;	// round up to 0x10
					txtBuffer = (UINT8*)realloc(txtBuffer, txtBufSize);
				}
				// then read input data
				txtLen = ReadMultiCmdData(songLen, songData, &inPos, txtBufSize, txtBuffer, MCMD_INI_INCLUDE);
				txtLen = GetTrimmedLength(txtLen, (char*)txtBuffer, ' ', 0);
				WriteMetaEvent(fInf, MTS, 0x01, txtLen, txtBuffer);
			}
			cmdP0Delay = 0;
			break;
		case 0xF7:	// continuation of previous command
			printf("Warning Track %u: Unexpected continuation command at 0x%04X!\n", trkID, prevPos);
			cmdP0Delay = 0;
			break;
		case 0xF8:	// Loop End
			if (loopIdx == 0)
			{
				printf("Warning Track %u: Loop End without Loop Start at 0x%04X!\n", trkID, prevPos);
			}
			else
			{
				UINT8 takeLoop;
				
				takeLoop = 0;
				loopIdx --;
				loopCnt[loopIdx] ++;
				if (cmdP0Delay == 0)
				{
					// infinite loop
					if (loopCnt[loopIdx] < 0x80 && midiDev != 0xFF)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)loopCnt[loopIdx]);
					
					if (loopCnt[loopIdx] < trkInf->loopTimes)
						takeLoop = 1;
				}
				else
				{
					if (loopCnt[loopIdx] < cmdP0Delay)
						takeLoop = 1;
				}
				if (takeLoop)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			cmdP0Delay = 0;
			break;
		case 0xF9:	// Loop Start
			if (loopIdx >= 8)
			{
				printf("Error Track %u: Trying to do more than 8 nested loops at 0x%04X!\n", trkID, prevPos);
			}
			else
			{
				if (inPos == trkInf->loopOfs && midiDev != 0xFF)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, 0);
				
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopCnt[loopIdx] = 0;
				if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
					loopIdx --;	// ignore bad loop command (for safety)
				loopIdx ++;
			}
			cmdP0Delay = 0;
			break;
		case 0xFC:	// repeat previous measure
			// I didn't see any song using this, even though the driver supports it.
			printf("Warning Track %u: Measure Repetition command [untested] at position 0x%04X!\n", trkID, cmdType, prevPos);
			{
				UINT16 measureID;
				UINT16 repeatPos;
				
				measureID = cmdP0Delay;
				repeatPos = (cmdP2 << 8) | (cmdP1 << 0);
				cmdP0Delay = 0;
				
				if (measureID == repMeasure)
					break;	// prevent recursion (just for safety)
				
				if (! parentPos)
					parentPos = inPos;
				repMeasure = measureID;
				inPos = trkInf->startOfs + repeatPos - 0x24;	// This is what the driver does.
			}
			break;
		case 0xFD:	// measure end
			// Some files (e.g. Z05MD.MMU and Z16MD.MMU) have the parameters set to non-zero values.
			// However the driver completely ignores them.
			if (curBar >= 0x8000)	// prevent infinite loops
			{
				trkEnd = 1;
				break;
			}
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
				repMeasure = 0xFFFF;
			}
			curBar ++;
			cmdP0Delay = 0;
			break;
		case 0xFE:	// track end
		case 0xFF:	// also does this
			trkEnd = 1;
			cmdP0Delay = 0;
			break;
		default:
			if (cmdType >= 0xF0)
				cmdP0Delay = 0;
			printf("Warning Track %u: Unhandled MMU command 0x%02X at position 0x%04X!\n", trkID, cmdType, prevPos);
			break;
		}	// end if (cmdType >= 0x80) / switch(cmdType)
		MTS->curDly += cmdP0Delay;
	}	// end while(! trkEnd)
	FlushRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes, 0);
	
	free(txtBuffer);
	if (midiDev == 0xFF)
		MTS->curDly = 0;
	
	return 0x00;
}

static UINT8 PreparseMmuTrack(UINT32 songLen, const UINT8* songData, const MMU_INFO* mmuInf, TRK_INF* trkInf)
{
	UINT32 inPos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT16 repMeasure;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT8 cmdP1;
	UINT8 cmdP2;
	UINT8 cmdP0Delay;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT32 loopTick[8];
	UINT16 loopCnt[8];
	
	trkInf->loopOfs = 0x00;
	trkInf->tickCnt = 0;
	trkInf->loopTick = 0;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	
	inPos = trkInf->startOfs;
	trkLen = ReadLE16(&songData[inPos]);
	trkEndPos = inPos + trkLen;
	if (trkEndPos > songLen)
		trkEndPos = songLen;
	if (inPos + 0x08 > songLen)
		return 0x01;	// not enough bytes to read the header
	
	inPos += 0x08;
	
	trkEnd = 0;
	parentPos = 0x00;
	loopIdx = 0x00;
	repMeasure = 0xFFFF;
	
	while(inPos < trkEndPos && ! trkEnd)
	{
		cmdType = songData[inPos + 0x00];
		cmdP0Delay = songData[inPos + 0x01];
		cmdP1 = songData[inPos + 0x02];
		cmdP2 = songData[inPos + 0x03];
		inPos += 0x04;
		
		switch(cmdType)
		{
		case 0xF8:	// Loop End
			if (loopIdx > 0)
			{
				loopIdx --;
				loopCnt[loopIdx] ++;
				if (cmdP0Delay == 0)
				{
					trkInf->loopOfs = loopPos[loopIdx];
					trkInf->loopTick = loopTick[loopIdx];
					trkEnd = 1;
				}
				else if (loopCnt[loopIdx] < cmdP0Delay)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			cmdP0Delay = 0;
			break;
		case 0xF9:	// Loop Start
			if (loopIdx < 8)
			{
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopCnt[loopIdx] = 0;
				if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
					loopIdx --;	// ignore loop command
				loopIdx ++;
			}
			cmdP0Delay = 0;
			break;
		case 0xFC:	// repeat previous measure
			{
				UINT16 measureID = cmdP0Delay;
				UINT16 repeatPos = (cmdP2 << 8) | (cmdP1 << 0);
				
				cmdP0Delay = 0;
				if (measureID == repMeasure)
					break;	// prevent recursion (just for safety)
				
				if (! parentPos)
					parentPos = inPos;
				repMeasure = measureID;
				inPos = trkInf->startOfs + repeatPos - 0x24;	// This is what the driver does.
			}
			break;
		case 0xFD:	// measure end
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
				repMeasure = 0xFFFF;
			}
			cmdP0Delay = 0;
			break;
		case 0xFE:	// track end
			trkEnd = 1;
			cmdP0Delay = 0;
			break;
		}	// end switch(cmdType)
		
		trkInf->tickCnt += cmdP0Delay;
	}	// end while(! trkEnd)
	
	return 0x00;
}

static UINT16 GetMultiCmdDataSize(UINT32 songLen, const UINT8* songData, UINT32 startPos, UINT8 flags)
{
	UINT32 inPos;
	UINT16 cmdCount;
	
	cmdCount = (flags & MCMD_INI_INCLUDE) ? 1 : 0;
	for (inPos = startPos; inPos < songLen && songData[inPos] == 0xF7; inPos += 0x04)
		cmdCount ++;
	if (flags & MCMD_RET_DATASIZE)
		cmdCount *= 2;	// 2 data bytes per command
	return cmdCount;
}

static UINT16 ReadMultiCmdData(UINT32 songLen, const UINT8* songData,
								UINT32* mmuInPos, UINT32 bufSize, UINT8* buffer, UINT8 flags)
{
	UINT32 inPos;
	UINT32 bufPos;
	
	bufPos = 0x00;
	inPos = *mmuInPos;
	if (flags & MCMD_INI_INCLUDE)
	{
		if (bufPos + 0x02 > bufSize)
			return 0x00;
		buffer[bufPos + 0x00] = songData[inPos - 0x02];
		buffer[bufPos + 0x01] = songData[inPos - 0x01];
		bufPos += 0x02;
	}
	for (; inPos < songLen && songData[inPos] == 0xF7; inPos += 0x04)
	{
		if (bufPos + 0x02 > bufSize)
			break;
		buffer[bufPos + 0x00] = songData[inPos + 0x02];
		buffer[bufPos + 0x01] = songData[inPos + 0x03];
		bufPos += 0x02;
	}
	
	*mmuInPos = inPos;
	return (UINT16)bufPos;
}

static UINT16 GetTrimmedLength(UINT16 dataLen, const char* data, char trimChar, UINT8 leaveLast)
{
	UINT16 trimLen;
	
	for (trimLen = dataLen; trimLen > 0; trimLen --)
	{
		if (data[trimLen - 1] != trimChar)
			break;
	}
	if (leaveLast && trimLen < dataLen)
		trimLen ++;
	return trimLen;
}

INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale)
{
	// formula: (60 000 000.0 / bpm) * (scale / 64.0)
	UINT32 div = bpm * scale;
	// I like rounding, but doing so make most MIDI programs display e.g. "144.99 BPM".
	return 60000000U * 64U / div;
	//return (60000000U * 64U + div / 2) / div;
}

static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay)
{
	CheckRunningNotes(fInf, delay, &RunNoteCnt, RunNotes);
	if (*delay)
	{
		UINT8 curNote;
		
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= (UINT16)*delay;
	}
	
	return 0x00;
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}
