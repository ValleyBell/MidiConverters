// M.M.D. -> Midi Converter
// ------------------------
// Written by Valley Bell, 06 January 2022
// Wolfteam MMU -> Midi Converter
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

typedef struct MMD_INFO
{
	UINT8 tempoBPM;
	UINT16 trkCnt;
	const UINT8* usrSyx[8];
	const char* songTitle;
} MMD_INFO;

typedef struct _track_info
{
	UINT32 startOfs;
	UINT32 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 transp;
	UINT8 channel;
} TRK_INF;

#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);

UINT8 Mmd2Mid(UINT32 songLen, const UINT8* songData);
static UINT8 MmdTrk2MidTrk(UINT32 songLen, const UINT8* songData, const MMD_INFO* mmdInf,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 trkID);
static UINT8 PreparseMmdTrack(UINT32 songLen, const UINT8* songData, const MMD_INFO* mmdInf, TRK_INF* trkInf);
static UINT16 ProcessRcpSysEx(UINT16 syxMaxLen, const UINT8* syxData, UINT8* syxBuffer,
								UINT8 param1, UINT8 param2, UINT8 midChn);
static UINT32 GetSyxSize(UINT32 songLen, const UINT8* songData, UINT32 startPos);
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
	
	printf("M.M.D. -> Midi Converter\n------------------------\n");
	if (argc < 3)
	{
		printf("Usage: Mmd2Mid.exe [options] input.bin output.mid\n");
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
	
	retVal = Mmd2Mid(ROMLen, ROMData);
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


UINT8 Mmd2Mid(UINT32 songLen, const UINT8* songData)
{
	TRK_INF trkInf[18];
	TRK_INF* tempTInf;
	UINT8 tempArr[0x20];
	MMD_INFO mmdInf;
	UINT8 curTrk;
	UINT32 inPos;
	UINT32 tempLng;
	UINT8 retVal;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	
	midFInf.alloc = 0x20000;	// 128 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	mmdInf.tempoBPM = songData[0x00];
	mmdInf.trkCnt = 18;	// track count is fixed
	inPos = 0x02;
	for (curTrk = 0; curTrk < mmdInf.trkCnt; curTrk ++, inPos += 0x04)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = ReadLE16(&songData[inPos + 0x00]);
		tempTInf->transp = songData[inPos + 0x02];
		tempTInf->channel = songData[inPos + 0x03];
	}
	inPos = ReadLE16(&songData[0x4A]);
	if (inPos && inPos < songLen)
	{
		for (curTrk = 0; curTrk < 8; curTrk ++, inPos += 0x02)
		{
			UINT16 syxPos = ReadLE16(&songData[inPos]);
			mmdInf.usrSyx[curTrk] = &songData[syxPos];
		}
	}
	else
	{
		for (curTrk = 0; curTrk < 8; curTrk ++)
			mmdInf.usrSyx[curTrk] = NULL;
	}
	if (trkInf[curTrk].startOfs > 0x50)
		mmdInf.songTitle = (const char*)&songData[0x50];
	else
		mmdInf.songTitle = NULL;
	
	for (curTrk = 0; curTrk < mmdInf.trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		
		PreparseMmdTrack(songLen, songData, &mmdInf, tempTInf);
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(mmdInf.trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFInf, 0x0001, mmdInf.trkCnt, MIDI_RES);
	
	retVal = 0x00;
	for (curTrk = 0; curTrk < mmdInf.trkCnt; curTrk ++)
	{
		WriteMidiTrackStart(&midFInf, &MTS);
		
		if (curTrk == 0)
		{
			if (mmdInf.songTitle != NULL && mmdInf.songTitle[0] != '\0')
				WriteMetaEvent(&midFInf, &MTS, 0x03, strlen(mmdInf.songTitle), mmdInf.songTitle);
			
			tempLng = Tempo2Mid(mmdInf.tempoBPM, 0x40);
			WriteBE32(tempArr, tempLng);
			WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
		}
		
		retVal = MmdTrk2MidTrk(songLen, songData, &mmdInf, &trkInf[curTrk], &midFInf, &MTS, curTrk);
		
		WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFInf, &MTS);
		
		if (retVal)
		{
			printf("Early EOF when trying to read track %u!\n", 1 + curTrk);
			break;
		}
	}
	
	MidData = midFInf.data;
	MidLen = midFInf.pos;
	
	midFInf.pos = 0x00;
	WriteMidiHeader(&midFInf, 0x0001, curTrk, MIDI_RES);
	
	return retVal;
}

static UINT8 MmdTrk2MidTrk(UINT32 songLen, const UINT8* songData, const MMD_INFO* mmdInf,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 trkID)
{
	UINT32 inPos;
	UINT8 midiDev;
	UINT8 midChn;
	UINT8 transp;
	UINT8 tempArr[0x40];
	UINT16 curBar;
	UINT8 trkEnd;
	UINT8 cmdMem[4];
	UINT8 cmdType;
	UINT8 cmdDelay;
	UINT8 tempByt;
	UINT8 loopIdx;
	UINT8 loopCMem[0x10][4];
	UINT32 loopPos[0x10];
	UINT16 loopCnt[0x10];
	UINT8 gsParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT32 txtBufSize;
	UINT8* txtBuffer;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	
	inPos = trkInf->startOfs;
	
	if (trkInf->channel == 0xFF)
	{
		// When the KeepDummyCh option is off, prevent events from being
		// written to the MIDI by setting midiDev to 0xFF.
		midiDev = KEEP_DUMMY_CH ? 0x00 : 0xFF;
		trkInf->channel = 0x00;
	}
	else
	{
		midiDev = trkInf->channel >> 4;	// the driver doesn't support multiple devices, but let's keep it because RCP can do it
		midChn = trkInf->channel & 0x0F;
	}
	transp = trkInf->transp;	// transposition
	
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
	
	txtBufSize = 0x00;
	txtBuffer = NULL;
	
	memset(gsParams, 0x00, 6);
	trkEnd = 0;
	RunNoteCnt = 0x00;
	MTS->midChn = midChn;
	MTS->curDly = 0;
	memset(cmdMem, 0x00, 4);
	loopIdx = 0x00;
	curBar = 0;
	
	while(inPos < songLen && ! trkEnd)
	{
		UINT32 prevPos = inPos;
		
		cmdType = songData[inPos];
		if (cmdType >= 0x80 && cmdType <= 0x8F)
		{
			UINT8 cmdMask = cmdType & 0x0F;
			inPos ++;
			for (tempByt = 0; tempByt < 4; tempByt ++, cmdMask <<= 1)
			{
				if (cmdMask & 0x08)
				{
					cmdMem[tempByt] = songData[inPos];
					inPos ++;
				}
			}
		}
		else
		{
			for (tempByt = 0; tempByt < 4; tempByt ++)
				cmdMem[tempByt] = songData[inPos + tempByt];
			inPos += 0x04;
		}
		
		cmdType = cmdMem[0];
		cmdDelay = (cmdType >= 0xF0) ? 0 : cmdMem[1];
		if (cmdType < 0x80)
		{
			UINT8 curNote;
			UINT8 curRN;
			UINT8 noteDur = cmdMem[2];
			
			CheckRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes);
			
			curNote = (cmdType + transp) & 0x7F;
			for (curRN = 0; curRN < RunNoteCnt; curRN ++)
			{
				if (RunNotes[curRN].note == curNote)
				{
					// note already playing - set new length
					RunNotes[curRN].remLen = (UINT16)MTS->curDly + noteDur;
					noteDur = 0;	// prevent adding note below
					break;
				}
			}
			
			// duration == 0 -> no note
			if (noteDur > 0 && midiDev != 0xFF)
			{
				WriteEvent(fInf, MTS, 0x90, curNote, cmdMem[3]);
				AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
								MTS->midChn, curNote, 0x80, noteDur);	// The sound driver sends 9# note 00.
			}
		}
		else switch(cmdType)
		{
		case 0x90: case 0x91: case 0x92: case 0x93:	// send User SysEx (defined via header)
		case 0x94: case 0x95: case 0x96: case 0x97:
			if (midiDev == 0xFF)
				break;
			{
				const UINT8* usrSyx = mmdInf->usrSyx[cmdType & 0x07];
				UINT16 syxLen = ProcessRcpSysEx(sizeof(tempArr), usrSyx, tempArr, cmdMem[2], cmdMem[3], midChn);
				if (syxLen > 1)	// length 1 == contains only F7 byte
					WriteLongEvent(fInf, MTS, 0xF0, syxLen, tempArr);
				printf("Using User Syx %u!\n", cmdType & 0x07);
			}
			break;
		case 0x98:	// send SysEx
			{
				UINT32 cmdLen = GetSyxSize(songLen, songData, inPos);
				if (txtBufSize < cmdLen)
				{
					txtBufSize = (cmdLen + 0x0F) & ~0x0F;	// round up to 0x10
					txtBuffer = (UINT8*)realloc(txtBuffer, txtBufSize);
				}
				if (midiDev != 0xFF)
				{
					UINT32 syxLen = ProcessRcpSysEx(cmdLen, &songData[inPos], txtBuffer, cmdMem[2], cmdMem[3], midChn);
					WriteLongEvent(fInf, MTS, 0xF0, syxLen, txtBuffer);
				}
				inPos += cmdLen;
			}
			break;
		case 0xC0:	// DX7 Function
		case 0xC1:	// DX Parameter
		case 0xC2:	// DX RERF
		case 0xC3:	// TX Function
		case 0xC7:	// TX81Z V VCED
		case 0xC8:	// TX81Z A ACED
		case 0xC9:	// TX81Z P PCED
		case 0xCC:	// DX7-2 R Remote SW
		case 0xCD:	// DX7-2 A ACED
		case 0xCE:	// DX7-2 P PCED
			if (midiDev == 0xFF)
				break;
			{
				static const UINT8 DX_PARAM[0x10] = {
					0x08, 0x00, 0x04, 0x11, 0xFF, 0x15, 0xFF, 0x12,
					0x13, 0x10, 0xFF, 0xFF, 0x1B, 0x18, 0x19, 0x1A,
				};
				tempArr[0] = 0x43;	// YAMAHA ID
				tempArr[1] = 0x10 | MTS->midChn;
				tempArr[2] = DX_PARAM[cmdType & 0x0F] | (cmdMem[2] >> 7);
				tempArr[3] = cmdMem[2];
				tempArr[4] = cmdMem[3];
				tempArr[5] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 6, tempArr);
			}
			break;
		case 0xC5:	// FB-01 P Parameter
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x10 | MTS->midChn;
			tempArr[2] = 0x15;
			tempArr[3] = cmdMem[2];
			if (cmdMem[2] < 0x40)
			{
				tempArr[4] = cmdMem[3];
				tempArr[5] = cmdMem[3] & 0x0F;
				tempArr[6] = cmdMem[3] >> 4;
				tempArr[7] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 8, tempArr);
			}
			else
			{
				tempArr[4] = cmdMem[3] & 0x0F;
				tempArr[5] = cmdMem[3] >> 4;
				tempArr[6] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 7, tempArr);
			}
			break;
		case 0xC6:	// FB-01 S System
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x75;
			tempArr[2] = MTS->midChn;
			tempArr[3] = 0x10;
			tempArr[4] = cmdMem[2];
			tempArr[5] = cmdMem[3];
			tempArr[6] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 7, tempArr);
			break;
		case 0xCA:	// TX81Z S System
		case 0xCB:	// TX81Z E EFFECT
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x10 | MTS->midChn;
			tempArr[2] = 0x7B + (cmdType - 0xCA);	// command CA -> param = 7B, command CB -> param = 7C
			tempArr[3] = cmdMem[2];
			tempArr[4] = cmdMem[3];
			tempArr[5] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 6, tempArr);
			break;
		case 0xCF:	// TX802 P PCED
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x10 | MTS->midChn;
			tempArr[2] = 0x1A;
			tempArr[3] = cmdMem[2];
			if (cmdMem[2] < 0x1B || cmdMem[2] >= 0x60)
			{
				tempArr[4] = cmdMem[3];
				tempArr[5] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 6, tempArr);
			}
			else
			{
				tempArr[4] = cmdMem[3] >> 7;
				tempArr[5] = cmdMem[3] & 0x7F;
				tempArr[6] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 7, tempArr);
			}
			break;
		//case 0xD0:	// YAMAHA Base Address
		//case 0xD1:	// YAMAHA Device Data
		//case 0xD2:	// YAMAHA Address / Parameter
		//case 0xD3:	// YAMAHA XG Address / Parameter
			// D0..D3 is not supported by MMD 2.2
		case 0xDC:	// MKS-7
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x41;	// Roland ID
			tempArr[1] = 0x32;
			tempArr[2] = MTS->midChn;
			tempArr[3] = cmdMem[2];
			tempArr[4] = cmdMem[3];
			tempArr[5] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 6, tempArr);
			break;
		case 0xDD:	// Roland Base Address
			gsParams[2] = cmdMem[2];
			gsParams[3] = cmdMem[3];
			break;
		case 0xDE:	// Roland Parameter
			gsParams[4] = cmdMem[2];
			gsParams[5] = cmdMem[3];
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
			gsParams[0] = cmdMem[2];
			gsParams[1] = cmdMem[3];
			break;
		case 0xE2:	// set GS instrument
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xB0, 0x00, cmdMem[3]);
			WriteEvent(fInf, MTS, 0xB0, 0x20, 0x00);
			WriteEvent(fInf, MTS, 0xC0, cmdMem[2], 0x00);
			break;
		case 0xE6:	// MIDI channel
			tempByt = cmdMem[2] - 1;	// It's same as in the track header, except 1 added.
			if (tempByt == 0xFF)
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
				midiDev = tempByt >> 4;	// port ID
				midChn = tempByt & 0x0F;	// channel ID
				WriteMetaEvent(fInf, MTS, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
				WriteMetaEvent(fInf, MTS, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
			}
			MTS->midChn = midChn;
			break;
		case 0xE7:	// Tempo Modifier
			{
				UINT32 tempoVal;
				
				if (cmdMem[3] == 1)
					printf("Warning Track %u: Interpolated Tempo Change at 0x%04X!\n", trkID, prevPos);
				tempoVal = Tempo2Mid(mmdInf->tempoBPM, cmdMem[2]);
				WriteBE32(tempArr, tempoVal);
				WriteMetaEvent(fInf, MTS, 0x51, 0x03, &tempArr[0x01]);
			}
			break;
		case 0xEA:	// Channel Aftertouch
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xD0, cmdMem[2], 0x00);
			break;
		case 0xEB:	// Control Change
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xB0, cmdMem[2], cmdMem[3]);
			break;
		case 0xEC:	// Instrument
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xC0, cmdMem[2], 0x00);
			break;
		case 0xED:	// Note Aftertouch
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xA0, cmdMem[2], cmdMem[3]);
			break;
		case 0xEE:	// Pitch Bend
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xE0, cmdMem[2], cmdMem[3]);
			break;
			// I have not found any F6/F7 commands in MMD files yet. (They all seem to be stripped off.)
		//case 0xF6:	// comment
		//	break;
		//case 0xF7:	// continuation of previous command
		//	printf("Warning Track %u: Unexpected continuation command at 0x%04X!\n", trkID, prevPos);
		//	break;
		case 0xF8:	// Loop End
			if (loopIdx == 0)
			{
				printf("Warning Track %u: Loop End without Loop Start at 0x%04X!\n", trkID, prevPos);
			}
			else
			{
				UINT8 takeLoop = 0;
				loopIdx --;
				loopCnt[loopIdx] ++;
				if (cmdMem[1] == 0 || cmdMem[1] >= 0x7F)
				{
					// infinite loop
					if (loopCnt[loopIdx] < 0x80 && midiDev != 0xFF)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)loopCnt[loopIdx]);
					
					if (loopCnt[loopIdx] < trkInf->loopTimes)
						takeLoop = 1;
				}
				else
				{
					if (loopCnt[loopIdx] < cmdMem[1])
						takeLoop = 1;
				}
				if (takeLoop)
				{
					memcpy(cmdMem, loopCMem[loopIdx], 4);
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			break;
		case 0xF9:	// Loop Start
			if (loopIdx >= 0x10)
			{
				printf("Error Track %u: Trying to do more than 8 nested loops at 0x%04X!\n", trkID, prevPos);
			}
			else
			{
				if (inPos == trkInf->loopOfs && midiDev != 0xFF)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, 0);
				
				memcpy(loopCMem[loopIdx], cmdMem, 4);
				loopPos[loopIdx] = inPos;
				loopCnt[loopIdx] = 0;
				if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
					loopIdx --;	// ignore bad loop command (for safety)
				loopIdx ++;
			}
			break;
		case 0xFE:	// track end
		case 0xFF:	// also does this
			trkEnd = 1;
			break;
		default:
			printf("Warning Track %u: Unhandled MMD command 0x%02X at position 0x%04X!\n", trkID, cmdType, prevPos);
			break;
		}	// end if (cmdType >= 0x80) / switch(cmdType)
		MTS->curDly += cmdDelay;
	}	// end while(! trkEnd)
	FlushRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes, 0);
	
	free(txtBuffer);
	if (midiDev == 0xFF)
		MTS->curDly = 0;
	
	return 0x00;
}

static UINT8 PreparseMmdTrack(UINT32 songLen, const UINT8* songData, const MMD_INFO* mmdInf, TRK_INF* trkInf)
{
	UINT32 inPos;
	UINT8 trkEnd;
	UINT8 cmdMem[4];
	UINT8 cmdType;
	UINT8 cmdDelay;
	UINT8 tempByt;
	UINT8 loopIdx;
	UINT8 loopCMem[0x10][4];
	UINT32 loopPos[0x10];
	UINT32 loopTick[0x10];
	UINT16 loopCnt[0x10];
	
	trkInf->loopOfs = 0x00;
	trkInf->tickCnt = 0;
	trkInf->loopTick = 0;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	
	inPos = trkInf->startOfs;
	
	trkEnd = 0;
	memset(cmdMem, 0x00, 4);
	loopIdx = 0x00;
	
	while(inPos < songLen && ! trkEnd)
	{
		cmdType = songData[inPos];
		if (cmdType >= 0x80 && cmdType <= 0x8F)
		{
			UINT8 cmdMask = cmdType & 0x0F;
			inPos ++;
			for (tempByt = 0; tempByt < 4; tempByt ++, cmdMask <<= 1)
			{
				if (cmdMask & 0x08)
				{
					cmdMem[tempByt] = songData[inPos];
					inPos ++;
				}
			}
		}
		else
		{
			for (tempByt = 0; tempByt < 4; tempByt ++)
				cmdMem[tempByt] = songData[inPos + tempByt];
			inPos += 0x04;
		}
		
		cmdType = cmdMem[0];
		cmdDelay = (cmdType >= 0xF0) ? 0 : cmdMem[1];
		switch(cmdType)
		{
		case 0x98:	// send SysEx
			inPos += GetSyxSize(songLen, songData, inPos);
			break;
		case 0xF8:	// Loop End
			if (loopIdx > 0)
			{
				loopIdx --;
				loopCnt[loopIdx] ++;
				if (cmdMem[1] == 0 || cmdMem[1] >= 0x7F)
				{
					trkInf->loopOfs = loopPos[loopIdx];
					trkInf->loopTick = loopTick[loopIdx];
					trkEnd = 1;
				}
				else if (loopCnt[loopIdx] < cmdMem[1])
				{
					memcpy(cmdMem, loopCMem[loopIdx], 4);
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			break;
		case 0xF9:	// Loop Start
			if (loopIdx < 0x10)
			{
				memcpy(loopCMem[loopIdx], cmdMem, 4);
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopCnt[loopIdx] = 0;
				if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
					loopIdx --;	// ignore loop command
				loopIdx ++;
			}
			break;
		case 0xFE:	// track end
			trkEnd = 1;
			break;
		}	// end switch(cmdType)
		
		trkInf->tickCnt += cmdDelay;
	}	// end while(! trkEnd)
	
	return 0x00;
}

static UINT16 ProcessRcpSysEx(UINT16 syxMaxLen, const UINT8* syxData, UINT8* syxBuffer,
								UINT8 param1, UINT8 param2, UINT8 midChn)
{
	UINT16 inPos;
	UINT16 outPos;
	UINT8 chkSum;
	
	chkSum = 0x00;
	outPos = 0x00;
	for (inPos = 0x00; inPos < syxMaxLen; inPos ++)
	{
		UINT8 data = syxData[inPos];
		
		if (data & 0x80)
		{
			switch(data)
			{
			case 0x80:	// put data value (cmdMem[2])
				data = param1;
				break;
			case 0x81:	// put data value (cmdMem[3])
				data = param2;
				break;
			case 0x82:	// put data value (midChn)
				data = midChn;
				break;
			case 0x83:	// initialize Roland Checksum
				chkSum = 0x00;
				break;
			case 0x84:	// put Roland Checksum
				data = (0x100 - chkSum) & 0x7F;
				break;
			case 0xF7:	// SysEx end
				syxBuffer[outPos] = data;
				outPos ++;
				return outPos;
			default:
				printf("Unknown SysEx command 0x%02X found in SysEx data!\n", data);
				break;
			}
		}
		
		if (! (data & 0x80))
		{
			syxBuffer[outPos] = data;
			outPos ++;
			chkSum += data;
		}
	}
	
	return outPos;
}

static UINT32 GetSyxSize(UINT32 songLen, const UINT8* songData, UINT32 startPos)
{
	UINT32 curPos = startPos;
	while(curPos < songLen && songData[curPos] != 0xF7)
		curPos ++;
	if (curPos < songLen)
		curPos ++;	// include terminating F7 byte
	return curPos - startPos;
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
