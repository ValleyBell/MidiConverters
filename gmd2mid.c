// GMD -> Midi Converter
// ---------------------
// Written by Valley Bell
// based on MMU -> Midi Converter
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

typedef struct _gmd_chunk
{
	UINT16 itemCnt;
	UINT8 itemMode;
	UINT8 itemSize;
	const UINT8* data;
} GMD_CHUNK;

typedef struct _gmd_info
{
	UINT8 verMinor;
	UINT8 verMajor;
	UINT8 gblTransp;
	UINT16 tempoBPM;
	UINT8 timeSigNum;
	UINT8 timeSigDen;
	UINT16 tickRes;
	GMD_CHUNK songTitle;
	GMD_CHUNK fmIns;
	GMD_CHUNK ssgIns;
	UINT16 trkCnt;
	UINT32 trkDataPos;
} GMD_INFO;

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

static void ReadGMDChunk(UINT32 songLen, const UINT8* songData, GMD_CHUNK* gmdChk, UINT32* pos);
static UINT32 GetSongTitleLen(UINT32 txtLen, const char* txtData);
UINT8 Gmd2Mid(UINT32 songLen, const UINT8* songData);
static void WriteRPN(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8* rpnCache,
	UINT8 mode, UINT8 msb, UINT8 lsb, UINT8 value);
static UINT8 GmdTrk2MidTrk(UINT32 songLen, const UINT8* songData, const GMD_INFO* gmdInf,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 PreparseGmdTrack(UINT32 songLen, const UINT8* songData, const GMD_INFO* gmdInf, TRK_INF* trkInf);
static void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT16 pbBase, INT16 pbDetune);
INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT16 scale);
INLINE void RcpTimeSig2Mid(UINT8 buffer[4], UINT8 beatNum, UINT8 beatDen);
static UINT8 val2shift(UINT32 value);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
INLINE UINT16 ReadLE16(const UINT8* data);
INLINE INT8 Read7BitSigned(UINT8 value);
INLINE UINT16 PitchBendAddClamp(UINT16 baseVal, INT16 add);


static const UINT8 GS_RESET[0x0A] = {0xF0, 0x41, 0x10, 0x42, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};


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
static UINT8 DRIVER_BUGS = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("GMD -> Midi Converter\n---------------------\n");
	if (argc < 3)
	{
		printf("Usage: gmd2mid.exe [options] input.bin output.mid\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -DriverBugs include oddities and bugs from the sound driver\n");
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
		else if (! stricmp(argv[argbase] + 1, "DriverBugs"))
			DRIVER_BUGS = 1;
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
	
	retVal = Gmd2Mid(ROMLen, ROMData);
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


static void ReadGMDChunk(UINT32 songLen, const UINT8* songData, GMD_CHUNK* gmdChk, UINT32* pos)
{
	GMD_CHUNK temp;
	
	if (gmdChk == NULL)
		gmdChk = &temp;
	gmdChk->itemCnt = 0;
	gmdChk->itemMode = 0;
	gmdChk->itemSize = 0;
	gmdChk->data = NULL;
	if (*pos >= songLen)
		return;
	
	gmdChk->itemCnt = ReadLE16(&songData[*pos]);
	*pos += 0x02;
	if (! gmdChk->itemCnt)
		return;
	
	gmdChk->itemMode = songData[*pos + 0x00];
	gmdChk->itemSize = songData[*pos + 0x01];
	gmdChk->data = &songData[*pos + 0x02];
	
	if (gmdChk->itemMode == 0)
		*pos += 0x02 + gmdChk->itemCnt * gmdChk->itemSize;
	else
		*pos += gmdChk->itemCnt;
	return;
}

static UINT32 GetSongTitleLen(UINT32 txtLen, const char* txtData)
{
	// Step 1: get "real" End-Of-String (first '\0' character OR buffer end)
	// Note: The buffer size is aligned to 2-byte words and includes the
	//       terminating \0. If that results in an odd buffer size, it is
	//       padded with garbage data.
	const char* txtEnd = (const char*)memchr(txtData, '\0', txtLen);
	if (txtEnd == NULL)
		txtEnd = txtData + txtLen;
	
	// Step 2: trim off trailing spaces
	// needed by hinadori_98
	while(txtEnd > txtData)
	{
		if (txtEnd[-1] != ' ')
			break;
		txtEnd --;
	}
	
	return (UINT32)(txtEnd - txtData);
}

UINT8 Gmd2Mid(UINT32 songLen, const UINT8* songData)
{
	TRK_INF trkInf[18];
	TRK_INF* tempTInf;
	UINT8 tempArr[0x20];
	GMD_INFO gmdInf;
	UINT8 curTrk;
	UINT32 inPos;
	UINT32 tempLng;
	UINT8 retVal;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	
	if (memcmp(&songData[0x00], "GMD0", 0x04))
	{
		printf("Not a GMDx file!\n");
		return 0x80;
	}
	
	midFInf.alloc = 0x20000;	// 128 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	gmdInf.verMinor = songData[0x04];
	gmdInf.verMajor = songData[0x05];
	gmdInf.gblTransp = 0;
	gmdInf.tempoBPM = ReadLE16(&songData[0x0A]);
	gmdInf.timeSigNum = songData[0x0C];
	gmdInf.timeSigDen = songData[0x0D];
	gmdInf.tickRes = ReadLE16(&songData[0x0E]);
	inPos = 0x20;
	ReadGMDChunk(songLen, songData, &gmdInf.songTitle, &inPos);
	ReadGMDChunk(songLen, songData, &gmdInf.fmIns, &inPos);
	ReadGMDChunk(songLen, songData, &gmdInf.ssgIns, &inPos);
	ReadGMDChunk(songLen, songData, NULL, &inPos);	// ignored by the sound driver
	ReadGMDChunk(songLen, songData, NULL, &inPos);	// ignored by the sound driver
	ReadGMDChunk(songLen, songData, NULL, &inPos);	// ignored by the sound driver
	ReadGMDChunk(songLen, songData, NULL, &inPos);	// TODO: the sound driver does something here
	
	gmdInf.trkCnt = ReadLE16(&songData[inPos]);
	inPos += 0x02;
	gmdInf.trkDataPos = inPos;
	
	for (curTrk = 0; curTrk < gmdInf.trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = inPos;
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		
		if (inPos < songLen)
		{
			tempLng = ReadLE16(&songData[inPos]);
			PreparseGmdTrack(songLen, songData, &gmdInf, tempTInf);
			inPos += tempLng;
		}
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(gmdInf.trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFInf, 0x0001, gmdInf.trkCnt, MIDI_RES);
	
	inPos = gmdInf.trkDataPos;
	retVal = 0x00;
	for (curTrk = 0; curTrk < gmdInf.trkCnt; curTrk ++)
	{
		WriteMidiTrackStart(&midFInf, &MTS);
		
		if (curTrk == 0)
		{
			// first track: write global song information
			if (gmdInf.songTitle.itemCnt >= 2)	// 2 bytes are part of the chunk header
			{
				const char* title = (const char*)gmdInf.songTitle.data;
				UINT32 tLen = GetSongTitleLen(gmdInf.songTitle.itemCnt - 2, title);
				WriteMetaEvent(&midFInf, &MTS, 0x03, tLen, title);
			}
			
			tempLng = Tempo2Mid(gmdInf.tempoBPM, 0x40);
			WriteBE32(tempArr, tempLng);
			WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
			
			RcpTimeSig2Mid(tempArr, gmdInf.timeSigNum, gmdInf.timeSigDen);
			WriteMetaEvent(&midFInf, &MTS, 0x58, 0x04, tempArr);
		}
		
		retVal = GmdTrk2MidTrk(songLen, songData, &gmdInf, &trkInf[curTrk], &midFInf, &MTS);
		
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

static void WriteRPN(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8* rpnCache,
	UINT8 mode, UINT8 msb, UINT8 lsb, UINT8 value)
{
	UINT8 ctrlMSB = (mode & 0x01) ? 0x63 : 0x65;
	UINT8 ctrlLSB = (mode & 0x01) ? 0x62 : 0x64;
	UINT8 skipRPN = 0x00;
	
	if (rpnCache != NULL)
	{
		if (DRIVER_BUGS)
		{
			// original driver behaviour
			// bug: writing RPN/NRPN may go missing when alternating between the same RPN and NRPN settings
			if (rpnCache[ctrlMSB - 0x62] != msb)
				rpnCache[ctrlMSB - 0x62] = msb;
			else
				skipRPN |= 0x01;
			if (rpnCache[ctrlLSB - 0x62] != lsb)
				rpnCache[ctrlLSB - 0x62] = lsb;
			else
				skipRPN |= 0x02;
		}
		else
		{
			// fixed behaviour
			UINT8 msbC = msb | ((mode & 0x01) << 7);
			UINT8 lsbC = lsb | ((mode & 0x01) << 7);
			if (rpnCache[1] == msbC && rpnCache[0] == lsbC)
			{
				skipRPN |= 0x03;
			}
			else
			{
				rpnCache[1] = msbC;
				rpnCache[0] = lsbC;
			}
		}
	}
	
	if (! (skipRPN & 0x01))
		WriteEvent(fInf, MTS, 0xB0, ctrlMSB, msb);
	if (! (skipRPN & 0x02))
		WriteEvent(fInf, MTS, 0xB0, ctrlLSB, lsb);
	WriteEvent(fInf, MTS, 0xB0, 0x06, value);
	
	return;
}

static UINT8 GmdTrk2MidTrk(UINT32 songLen, const UINT8* songData, const GMD_INFO* gmdInf,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT32 inPos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT32 trkTick;
	UINT8 trkID;
	UINT8 chnMode;
	UINT8 noteMode;
	UINT8 transp;
	UINT8 startTick;
	UINT16 curBar;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT8 noteVel;
	UINT8 noteVelAcc;
	UINT8 chnVol;
	UINT8 chnVolAcc;
	UINT8 chnPan;
	UINT8 modStrength;
	UINT8 modDelay;
	UINT8 susPedState;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT16 loopMax[8];	// total loop count
	UINT16 loopCnt[8];	// remaining loops
	UINT8 syxHdr[2];	// 0 device ID, 1 model ID
	UINT32 syxBufSize;
	UINT8* syxBuffer;
	UINT8 rpnCache[4];
	UINT16 songTempo;
	UINT16 curTempo;
	UINT16 curTempoMod;
	UINT8 noteLenMul;
	UINT8 noteLenSub;
	INT16 trkDetune;
	INT16 pbDetune;
	UINT16 pbValue;
	UINT8 swModEnable;
	UINT8 swModDelay;
	INT16 swModStrength;
	UINT16 destTempo;
	UINT16 destTempoMod;
	UINT32 tempoSldNextTick;
	UINT8 tempoSldStpSize;
	INT8 tempoSldDir;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	
	inPos = trkInf->startOfs;
	trkLen = ReadLE16(&songData[inPos]);
	trkEndPos = inPos + trkLen;
	if (trkEndPos > songLen)
		trkEndPos = songLen;
	if (inPos + 0x10 > songLen)
		return 0x01;	// not enough bytes to read the header
	
	trkID = songData[inPos + 0x02];		// track ID
	transp = songData[inPos + 0x04];	// transposition?? (ignored by driver)
	startTick = songData[inPos + 0x05];	// start tick (unsigned 8-bit according to driver disassembly)
	inPos += 0x10;
	
	if (transp != 0)
	{
		// known values are: 0x00..0x3F (+0 .. +63), 0x40..0x7F (-64 .. -1), 0x80 (drums)
		printf("Warning Track %u: transpose 0x%02X!\n", trkID, transp);
		transp = 0;
	}
	if (startTick != 0)
		printf("Warning Track %u: Start Tick %+d!\n", trkID, startTick);
	
	syxBufSize = 0x00;
	syxBuffer = NULL;
	
	syxHdr[0] = 0x10;	syxHdr[1] = 0x42;	// default according to driver disassembly
	memset(rpnCache, 0xFF, 4);
	trkEnd = 0;
	chnMode = 0x00;
	noteMode = 0x00;
	parentPos = 0x00;
	RunNoteCnt = 0x00;
	MTS->midChn = 0x00;
	MTS->curDly = startTick;
	songTempo = gmdInf->tempoBPM;
	curTempoMod = 0x40;
	curTempo = songTempo;
	noteVel = 100;	// confirmed via driver disassembly
	noteVelAcc = 6;
	chnVol = 100;	// confirmed via driver disassembly
	chnVolAcc = 6;
	chnPan = 0x40;
	modDelay = 0;
	modStrength = 0;
	noteLenMul = 0x10;
	noteLenSub = 2;
	trkDetune = 0x0000;
	pbDetune = 0x0000;
	pbValue = 0x2000;
	swModEnable = 0;
	swModDelay = 0;
	swModStrength = 0x0000;
	susPedState = 0x00;
	loopIdx = 0x00;
	curBar = 0;
	tempoSldStpSize = 0;
	tempoSldNextTick = (UINT32)-1;
	tempoSldDir = 0;
	destTempoMod = curTempoMod;
	destTempo = curTempo;
	
	trkTick = MTS->curDly;
	while(inPos < trkEndPos && ! trkEnd)
	{
		UINT32 prevPos = inPos;
		
		if (tempoSldStpSize > 0)
		{
			// handle tempo slides
			// used by hinadori_98/MD2_55_21.DAT
			while(trkTick >= tempoSldNextTick && tempoSldDir != 0)
			{
				UINT32 tempoVal;
				UINT8 tempArr[4];
				UINT32 tempDly;
				
				tempDly = trkTick - tempoSldNextTick;
				MTS->curDly -= tempDly;
				
				// tempo slide algorithm according to sound driver disassembly
				// After applying increment/decrement, the driver actually does some
				// clamping that is omitted here.
				// The lower limit is 17 BPM, the upper limit is 300 BPM (CSCP) / 340 BPM (SSCP).
				curTempo += tempoSldDir;
				tempoSldNextTick += tempoSldStpSize;
				if (curTempo == destTempo)
				{
					tempoSldStpSize = 0;
					tempoSldNextTick = (UINT32)-1;
					tempoSldDir = 0;
					curTempoMod = destTempoMod;
				}
				
				tempoVal = Tempo2Mid(curTempo, 0x40);
				if (curTempo == destTempo && ! DRIVER_BUGS)
				{
					// Try to get a slightly more accurate "end" tempo by
					// recalculating it using the Tempo Modifier value.
					destTempo = songTempo * destTempoMod / 0x40;
					curTempo = destTempo;
					tempoVal = Tempo2Mid(songTempo, destTempoMod);
				}
				WriteBE32(tempArr, tempoVal);
				WriteMetaEvent(fInf, MTS, 0x51, 0x03, &tempArr[0x01]);
				MTS->curDly += tempDly;
			}
		}
		
		cmdType = songData[inPos];
		if (cmdType < 0x80)
		{
			UINT8 curNote;
			UINT8 noteDelay;
			UINT8 noteLen;
			UINT8 curNoteVel;
			UINT8 curRN;
			
			noteDelay = songData[inPos + 0x01];
			noteLen = noteDelay;
			curNoteVel = noteVel;
			if (noteMode == 0)
			{
				noteLen = songData[inPos + 0x02];
				inPos += 0x03;
			}
			else if (noteMode == 1)
			{
				curNoteVel = songData[inPos + 0x02];
				noteLen = 1;
				inPos += 0x03;
			}
			else if (noteMode == 2)
			{
				inPos += 0x02;
				if (songData[inPos] == 0xEF)	// note tie?
				{
					inPos += 0x01;	// skip it
					noteLen ++;
				}
				else
				{
					if (noteLenMul == 0x10)
						noteLen -= noteLenSub;
					else if (noteLenMul != 0)
						noteLen = noteLen * noteLenMul / 0x10;
				}
			}
			else if (noteMode == 3)
			{
				noteLen = songData[inPos + 0x02];
				curNoteVel = songData[inPos + 0x03];
				inPos += 0x04;
			}
			if (curNoteVel & 0x80)
			{
				INT16 vel16 = noteVel + Read7BitSigned(curNoteVel & 0x7F);
				if (curNoteVel != 0x80)
					printf("Relative note velocity.\n");
				// The driver does proper clamping.
				if (vel16 < 0x00)
					vel16 = 0x00;
				else if (vel16 > 0x7F)
					vel16 = 0x7F;
				curNoteVel = (UINT8)vel16;
			}
			
			CheckRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes);
			
			curNote = (cmdType + gmdInf->gblTransp + transp) & 0x7F;
			for (curRN = 0; curRN < RunNoteCnt; curRN ++)
			{
				if (RunNotes[curRN].note == curNote)
				{
					// note already playing - set new length
					RunNotes[curRN].remLen = (UINT16)MTS->curDly + noteLen;
					noteLen = 0;	// prevent adding note below
					break;
				}
			}
			
			if (noteLen > 0)
			{
				WriteEvent(fInf, MTS, 0x90, curNote, curNoteVel);
				AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
								MTS->midChn, curNote, 0x80, noteLen);	// The sound driver sends 9# note 00.
			}
			
			MTS->curDly += noteDelay;
			trkTick += noteDelay;
		}
		else switch(cmdType)
		{
		case 0x80:	// rest
			{
				UINT8 delayVal = songData[inPos + 0x01];
				inPos += 0x02;
				
				CheckRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes);
				if (noteMode == 0 || noteMode >= 3)
				{
					UINT8 cutNotes = songData[inPos];
					inPos += 0x01;
					if (cutNotes > 0)
					{
						// force all notes to play for a remaining X ticks
						UINT8 curRN;
						//printf("Cut %u notes to %u ticks\n", RunNoteCnt, cutNotes);
						for (curRN = 0; curRN < RunNoteCnt; curRN ++)
						{
							UINT8 noteLen = cutNotes;
							if (noteMode == 2)
							{
								if (songData[inPos] == 0xEF)	// note tie?
								{
									inPos += 0x01;	// skip it
									noteLen ++;
								}
								else
								{
									if (noteLenMul == 0x10)
										noteLen -= noteLenSub;
									else if (noteLenMul != 0)
										noteLen = noteLen * noteLenMul / 0x10;
								}
							}
							RunNotes[curRN].remLen = (UINT16)MTS->curDly + noteLen;
						}
					}
				}
				MTS->curDly += delayVal;
				trkTick += delayVal;
			}
			break;
		case 0x81:	// delay
			MTS->curDly += songData[inPos + 0x01];
			trkTick += songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0x82:	// Note Off
			printf("Warning Track %u: Explicit Note Off (untested)\n", trkID);
			inPos ++;
			{
				UINT8 curNote;
				do
				{
					cmdType = songData[inPos];
					curNote = (cmdType + gmdInf->gblTransp + transp) & 0x7F;
					WriteEvent(fInf, MTS, 0x90, curNote, 0x00);
					inPos ++;
				} while(! (cmdType & 0x80));
			}
			break;
		case 0x83:	// Note On
			printf("Warning Track %u: Explicit Note On (untested)\n", trkID);
			inPos ++;
			{
				UINT8 curNote;
				do
				{
					cmdType = songData[inPos + 0x00];
					curNote = (cmdType + gmdInf->gblTransp + transp) & 0x7F;
					WriteEvent(fInf, MTS, 0x90, curNote, songData[inPos + 0x01]);
					inPos += 0x02;
				} while(! (cmdType & 0x80));
			}
			break;
		case 0x84:	// Note Length Multiplicator
			noteLenMul = songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0x85:	// Note Length Subtraction
			noteLenSub = songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0x86:	// Detune Set
			transp -= (INT8)(trkDetune >> 8);
			trkDetune = ReadLE16(&songData[inPos + 0x01]);
			inPos += 0x03;
			
			transp += (INT8)(trkDetune >> 8);
			if (trkDetune < 0 && (trkDetune & 0x00FF) == 0x00)
				transp ++;	// xx01..xxFF -> round up
			pbDetune = (((trkDetune < 0) ? -0x100 : 0x00) | (trkDetune & 0x00FF)) << 1;
			WritePitchBend(fInf, MTS, pbValue, pbDetune);
			break;
		case 0x87:	// Detune Add
			transp -= (INT8)(trkDetune >> 8);
			trkDetune += ReadLE16(&songData[inPos + 0x01]);
			inPos += 0x03;
			
			transp += (INT8)(trkDetune >> 8);
			if (trkDetune < 0 && (trkDetune & 0x00FF) == 0x00)
				transp ++;	// xx01..xxFF -> round up
			pbDetune = (((trkDetune < 0) ? -0x100 : 0x00) | (trkDetune & 0x00FF)) << 1;
			WritePitchBend(fInf, MTS, pbValue, pbDetune);
			break;
		case 0x88:	// Transposition Set
			transp = songData[inPos + 0x01];
			inPos += 0x02;
			
			transp += (INT8)(trkDetune >> 8);
			if (trkDetune < 0 && (trkDetune & 0x00FF) == 0x00)
				transp ++;	// xx01..xxFF -> round up
			break;
		case 0x89:	// Transposition Add
			transp += songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0x8A:	// Pitch Bend Reset
			pbValue = 0x2000;
			WritePitchBend(fInf, MTS, pbValue, pbDetune);
			inPos += 0x01;
			break;
		case 0x8B:	// Pitch Bend
			pbValue = ReadLE16(&songData[inPos + 0x01]);
			WritePitchBend(fInf, MTS, pbValue, pbDetune);
			inPos += 0x03;
			break;
		case 0x8C:	// Pitch Bend Add
			pbValue += ReadLE16(&songData[inPos + 0x01]);
			WritePitchBend(fInf, MTS, pbValue, pbDetune);
			inPos += 0x03;
			break;
		case 0x8D:	// Portamento Up
		case 0x8E:	// Portamento Down
			printf("Warning Track %u: Portamento %s at position 0x%04X [not implemented]\n",
					trkID, (cmdType == 0x8D) ? "Up" : "Down", inPos);
			inPos += 0x03;
			break;
		case 0x8F:	// Sustain Pedal
			susPedState = songData[inPos + 0x01];
			WriteEvent(fInf, MTS, 0xB0, 0x40, susPedState);
			inPos += 0x02;
			break;
		case 0x90:	// Channel Volume
			chnVol = songData[inPos + 0x01];
			WriteEvent(fInf, MTS, 0xB0, 0x07, chnVol);
			inPos += 0x02;
			break;
		case 0x91:	// Channel Volume Accumulation
			{
				INT16 vol16;
				UINT8 accSign = songData[inPos + 0x01] & 0x80;
				UINT8 volAcc = songData[inPos + 0x01] & 0x7F;	// accumulation value
				if (! volAcc)
					volAcc = chnVolAcc;	// value 0 - get cached value
				else
					chnVolAcc = volAcc;
				if (! accSign)
				{
					vol16 = chnVol + volAcc;
					if (vol16 > 0x7F)
						vol16 = 0x7F;
				}
				else
				{
					vol16 = chnVol - volAcc;
					if (vol16 < 0x00)
						vol16 = 0x00;
				}
				chnVol = (UINT8)vol16;
				inPos += 0x02;
			}
			WriteEvent(fInf, MTS, 0xB0, 0x07, chnVol);
			break;
		case 0x92:	// Note Velocity
			noteVel = songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0x93:	// Note Velocity Accumulation
			{
				INT16 vol16;
				UINT8 accSign = songData[inPos + 0x01] & 0x80;
				UINT8 volAcc = songData[inPos + 0x01] & 0x7F;	// accumulation value
				if (! volAcc)
					volAcc = noteVelAcc;	// value 0 - get cached value
				else
					noteVelAcc = volAcc;
				if (! accSign)
				{
					vol16 = noteVel + volAcc;
					if (vol16 > 0x7F)
						vol16 = 0x7F;
				}
				else
				{
					vol16 = noteVel - volAcc;
					if (vol16 < 0x00)
						vol16 = 0x00;
				}
				noteVel = (UINT8)vol16;
				inPos += 0x02;
			}
			break;
		case 0x98:	// Song Tempo
			{
				UINT32 tempoVal;
				UINT8 tempArr[4];
				
				printf("Warning Track %u: Song Tempo change! [may break tempo modifier]\n", trkID);
				songTempo = ReadLE16(&songData[inPos + 0x01]);
				// Note: CSCP (valkyrie_98) clamps the tempo to 300 BPM, SSCP to 340 BPM
				// I'm omitting this here.
				curTempoMod = 0x40;
				curTempo = songTempo;
				destTempoMod = curTempoMod;
				destTempo = curTempo;
				tempoSldDir = 0;
				inPos += 0x03;
				
				tempoVal = Tempo2Mid(songTempo, curTempoMod);
				WriteBE32(tempArr, tempoVal);
				WriteMetaEvent(fInf, MTS, 0x51, 0x03, &tempArr[0x01]);
			}
			break;
		case 0x9A:	// Tempo Modifier
			{
				UINT32 tempoVal;
				UINT8 tempArr[4];
				
				destTempoMod = songData[inPos + 0x01];
				if (! destTempoMod)
					destTempoMod = 0x100;	// confirmed using disassembly
				// the driver does integer arithmetic without rounding here
				destTempo = songTempo * destTempoMod / 0x40;
				// Note: The sound driver does the same clamping as with command 0x98 here.
				
				tempoSldStpSize = songData[inPos + 0x02];	// number of ticks between each tempo slide event
				inPos += 0x03;
				
				if (destTempo == curTempo)
					tempoSldStpSize = 0;
				if (tempoSldStpSize == 0)
				{
					curTempoMod = destTempoMod;
					curTempo = destTempo;
					tempoSldDir = 0;
					if (DRIVER_BUGS)
						tempoVal = Tempo2Mid(curTempo, 0x40);	// not really a "bug", but mimic the actual driver implementation
					else
						tempoVal = Tempo2Mid(songTempo, curTempoMod);
					WriteBE32(tempArr, tempoVal);
					WriteMetaEvent(fInf, MTS, 0x51, 0x03, &tempArr[0x01]);
				}
				else
				{
					tempoSldNextTick = trkTick + tempoSldStpSize;
					if (destTempo < curTempo)
						tempoSldDir = -1;
					else //if (destTempo > curTempo)
						tempoSldDir = +1;
					
					if (! DRIVER_BUGS && tempoSldDir > 0)
					{
						// Try to get to the specified tempo incl. fraction.
						// This may involve an additional tempo event, so we have to recalculate
						// the destination tempo with integer ceil().
						destTempo = (songTempo * destTempoMod + 0x3F) / 0x40;
					}
				}
			}
			break;
		case 0x9C:	// Instrument
			if (DRIVER_BUGS)	// original driver behaviour
			{
				WriteEvent(fInf, MTS, 0xB0, 0x40, 0x00);
				susPedState = 0x00;
				FlushRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes, 1);
				WriteEvent(fInf, MTS, 0xB0, 0x7B, 0x00);
			}
			else
			{
				if (susPedState & 0x40)
				{
					printf("Warning Track %u: Sustain Off due to new instrument at 0x%04X!\n", trkID, inPos);
					WriteEvent(fInf, MTS, 0xB0, 0x40, 0x00);
				}
				susPedState = 0x00;
				FlushRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes, 1);
			}
			WriteEvent(fInf, MTS, 0xC0, songData[inPos + 0x01], 0x00);
			inPos += 0x02;
			break;
		case 0x9D:	// banked Instrument
			WriteEvent(fInf, MTS, 0xB0, 0x00, songData[inPos + 0x01]);
			WriteEvent(fInf, MTS, 0xC0, songData[inPos + 0x02], 0x00);
			inPos += 0x03;
			break;
		case 0x9E:	// Note Aftertouch
			WriteEvent(fInf, MTS, 0xA0, songData[inPos + 0x01], songData[inPos + 0x02]);
			inPos += 0x03;
			break;
		case 0x9F:	// Channel Aftertouch
			WriteEvent(fInf, MTS, 0xD0, songData[inPos + 0x01], 0x00);
			inPos += 0x02;
			break;
		case 0xA0:	// Pan (via index)
			{
				static const UINT8 PAN_LUT[0x04] = {0x40, 0x01, 0x7F, 0x40};
				UINT8 panIdx = songData[inPos + 0x01];
				inPos += 0x02;
				
				if (panIdx >= 0x04)
					break;	// actually causes invalid values to be read
				WriteEvent(fInf, MTS, 0xB0, 0x0A, PAN_LUT[panIdx]);
			}
			break;
		case 0xA1:	// Pan
			chnPan = songData[inPos + 0x01];
			inPos += 0x02;
			WriteEvent(fInf, MTS, 0xB0, 0x0A, chnPan);
			break;
		case 0xA2:	// Pan Add
			{
				INT16 pan16;
				UINT8 accSign = songData[inPos + 0x01] & 0x80;
				UINT8 panAcc = songData[inPos + 0x01] & 0x7F;	// accumulation value
				if (! accSign)
				{
					pan16 = chnPan + panAcc;
					if (pan16 > 0x7F)
						pan16 = 0x7F;
				}
				else
				{
					pan16 = chnPan - panAcc;
					if (pan16 < 0x00)	// clamping according to sound driver
						pan16 = 0x01;
				}
				chnPan = (UINT8)pan16;
				inPos += 0x02;
			}
			WriteEvent(fInf, MTS, 0xB0, 0x0A, chnPan);
			break;
		case 0xA4:	// Modulation
			{
				modStrength = songData[inPos + 0x01];
				inPos += 0x02;
				if (modStrength & 0x80)
				{
					printf("Warning Track %u: Modulation Delay used! [not implemented]\n", trkID);
					modStrength &= 0x7F;
					modDelay = songData[inPos];	inPos ++;
				}
				WriteEvent(fInf, MTS, 0xB0, 0x01, modStrength);
			}
			break;
		case 0xA5:	// Software Modulation Enable
			{
				UINT8 modEnable = songData[inPos + 0x01];
				UINT8 oldSwModEn = swModEnable;
				
				//printf("Warning Track %u: Command %02X at position 0x%04X\n", trkID, cmdType, inPos);
				swModEnable = modEnable & 0x7F;
				inPos += 0x02;
				if (modEnable & 0x80)
				{
					UINT8 modDelay = songData[inPos];
					inPos ++;
				}
				
				if (oldSwModEn != swModEnable)
				{
					UINT8 modVal;
					if (swModEnable)
						modVal = (swModStrength > 0x7F) ? 0x7F : (UINT8)swModStrength;
					else
						modVal = 0x00;
					WriteEvent(fInf, MTS, 0xB0, 0x01, modVal);
				}
			}
			break;
		case 0xA7:	// Software Modulation Settings
			{
				UINT8 modMode = songData[inPos + 0x01];
				UINT16 dInvCounter = ReadLE16(&songData[inPos + 0x02]);
				INT16 freqDelta = (INT16)ReadLE16(&songData[inPos + 0x04]);
				//printf("Warning Track %u: Command %02X at position 0x%04X\n", trkID, cmdType, inPos);
				inPos += 0x06;
				
				if (freqDelta < 0)
					freqDelta *= -1;
				swModStrength = (dInvCounter * freqDelta) / 0x10;
				if (swModEnable)
				{
					UINT8 modVal = (swModStrength > 0x7F) ? 0x7F : (UINT8)swModStrength;
					WriteEvent(fInf, MTS, 0xB0, 0x01, modVal);
				}
			}
			break;
		case 0xAC:	// FM register write
			{
				UINT8 reg = songData[inPos + 0x01];
				UINT8 data = songData[inPos + 0x02];
				printf("Warning Track %u: FM Register Write (Reg 0x%02X, Data %02X) position 0x%04X\n",
						trkID, reg, data, inPos);
				inPos += 0x03;
			}
			break;
		case 0xAD:	// FM channel register write
			{
				UINT8 reg = songData[inPos + 0x01];
				UINT8 data = songData[inPos + 0x02];
				printf("Warning Track %u: FM Chn Reg Write (Reg 0x%02X, Data %02X) position 0x%04X on track %u\n",
						trkID, reg, data, inPos);
				inPos += 0x03;
			}
			break;
		case 0xAE:	// Control Change
			{
				UINT8 ctrl = songData[inPos + 0x01];
				UINT8 value = songData[inPos + 0x02];
				if (ctrl >= 0x62 && ctrl <= 0x65)
				{
					// just like the original driver, keep track of (N)RPN settings
					if (DRIVER_BUGS)	// original driver behaviour
						rpnCache[ctrl - 0x62] = value;
					else
						rpnCache[ctrl & 0x01] = (ctrl >= 0x64) ? value : (0x80 | value);
				}
				WriteEvent(fInf, MTS, 0xB0, ctrl, value);
				inPos += 0x03;
			}
			break;
		case 0xAF:	// SysEx command
			{
				UINT32 syxPos;
				UINT32 dataLen;
				
				for (syxPos = inPos + 0x01; syxPos < songLen; syxPos ++)
				{
					if (songData[syxPos] & 0x80)
						break;	// The last data byte has bit 7 set.
				}
				dataLen = syxPos - inPos + 0x01;	// 1 byte footer
				if (syxBufSize < dataLen)
				{
					syxBufSize = (dataLen + 0x0F) & ~0x0F;	// round up to 0x10
					syxBuffer = (UINT8*)realloc(syxBuffer, syxBufSize);
				}
				dataLen -= 0x01;
				
				memcpy(&syxBuffer[0], &songData[inPos + 0x01], dataLen);
				syxBuffer[dataLen - 0x01] &= 0x7F;
				syxBuffer[dataLen + 0x00] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, dataLen + 0x01, syxBuffer);
				inPos += 0x01 + dataLen;
			}
			break;
		case 0xB0:	// set Pitch Bend Range
			printf("Warning Track %u: Set PB Range (untested)\n", trkID);
			WriteRPN(fInf, MTS, rpnCache, 0x00, 0x00, 0x00, songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0xB1:	// set RPN Parameter
			printf("Warning Track %u: RPN (untested)\n", trkID);
			WriteRPN(fInf, MTS, rpnCache, 0x00,
					songData[inPos + 0x01], songData[inPos + 0x02], songData[inPos + 0x03]);
			inPos += 0x04;
			break;
		case 0xB3:	// set NRPN Parameter
			printf("Warning Track %u: NRPN (untested)\n", trkID);
			WriteRPN(fInf, MTS, rpnCache, 0x01,
					songData[inPos + 0x01], songData[inPos + 0x02], songData[inPos + 0x03]);
			inPos += 0x04;
			break;
		case 0xB5:	// Roland Device
			syxHdr[0] = songData[inPos + 0x01];
			syxHdr[1] = songData[inPos + 0x02];
			inPos += 0x03;
			break;
		case 0xB6:	// Roland SysEx
			{
				UINT32 syxPos;
				UINT32 dataLen;
				UINT8 chkSum;
				
				for (syxPos = inPos + 0x01; syxPos < songLen; syxPos ++)
				{
					if (songData[syxPos] & 0x80)
						break;	// The last data byte has bit 7 set.
				}
				dataLen = syxPos - inPos + 0x06;	// 4 bytes header + 2 bytes footer
				if (syxBufSize < dataLen)
				{
					syxBufSize = (dataLen + 0x0F) & ~0x0F;	// round up to 0x10
					syxBuffer = (UINT8*)realloc(syxBuffer, syxBufSize);
				}
				dataLen -= 0x06;
				
				syxBuffer[0] = 0x41;	// Roland ID
				syxBuffer[1] = syxHdr[0];
				syxBuffer[2] = syxHdr[1];
				syxBuffer[3] = 0x12;
				
				chkSum = 0x00;	// initialize checksum
				for (syxPos = 0x00; syxPos < dataLen; syxPos ++)
				{
					syxBuffer[0x04 + syxPos] = songData[inPos + 0x01 + syxPos] & 0x7F;
					chkSum += syxBuffer[0x04 + syxPos];	// add to checksum
				}
				syxBuffer[syxPos + 0x04] = (0x100 - chkSum) & 0x7F;
				syxBuffer[syxPos + 0x05] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, dataLen + 0x06, syxBuffer);
				inPos += 0x01 + dataLen;
			}
			break;
		case 0xB7:	// GS Reset
			printf("Warning Track %u: GS Reset (untested)\n", trkID);
			WriteLongEvent(fInf, MTS, GS_RESET[0], sizeof(GS_RESET) - 1, &GS_RESET[1]);
			inPos += 0x01;
			break;
		case 0xB8:	// Expression
			WriteEvent(fInf, MTS, 0xB0, 0x0B, songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0xE0:	// channel mode
			//printf("Warning Track %u: Set MIDI Channel command found at 0x%04X\n", trkID, inPos);
			chnMode = songData[inPos + 0x01];
			if (chnMode == 0x00)	// FM 1..6
				MTS->midChn = songData[inPos + 0x02] & 0x0F;
			else if (chnMode == 0x01)	// SSG 1..3
				MTS->midChn = (0x0A + songData[inPos + 0x02]) & 0x0F;
			else if (chnMode == 0x06)	// FM3 Multi-Frequency mode?
				MTS->midChn = (0x06 + songData[inPos + 0x02]) & 0x0F;
			else if (chnMode == 0x07)	// OPNA Rhythm
				MTS->midChn = 0x09;
			else //if (chnMode == 0x10)	// MIDI
				MTS->midChn = songData[inPos + 0x02] & 0x0F;
			//WriteMetaEvent(fInf, MTS, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
			inPos += 0x03;
			break;
		case 0xE1:	// note mode
			noteMode = songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0xE5:	// repeat previous measure
			if (parentPos)
			{
				// exit measure repetition (prevents recursion)
				inPos = parentPos;
				break;
			}
			
			parentPos = inPos + 0x03;
			do
			{
				UINT32 repeatPos = trkInf->startOfs + ReadLE16(&songData[inPos + 0x01]);
				if (repeatPos == inPos)
					break;
				inPos = repeatPos;
			} while(songData[inPos] == 0xE5);
			break;
		case 0xE6:	// Loop Start (A)
			{
				UINT8 loopTimes = songData[inPos + 0x01];
				inPos += 0x02;
				
				if (loopIdx >= 8)
				{
					printf("Error Track %u: Trying to do more than 8 nested loops at 0x%04X!\n", trkID, prevPos);
					break;
				}
				if (inPos == trkInf->loopOfs)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, 0);
				
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopMax[loopIdx] = loopTimes;
				loopCnt[loopIdx] = 0;
				loopIdx ++;
			}
			break;
		case 0xE7:	// Loop End (A)
			{
				UINT8 takeLoop = 0;
				inPos += 0x01;
				
				if (loopIdx == 0)
				{
					printf("Error Track %u: Loop End without Loop Start at 0x%04X!\n", trkID, prevPos);
					break;
				}
				loopIdx --;
				
				loopCnt[loopIdx] ++;
				if (loopMax[loopIdx] == 0)
				{
					// infinite loop
					if (loopMax[loopIdx] < 0x80)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)loopCnt[loopIdx]);
					
					if (loopCnt[loopIdx] < trkInf->loopTimes)
						takeLoop = 1;
				}
				else if (loopCnt[loopIdx] < loopMax[loopIdx])
				{
					takeLoop = 1;
				}
				if (takeLoop)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			break;
		case 0xE8:	// Loop Start (B)
			inPos += 0x01;
			if (loopIdx >= 8)
			{
				printf("Error Track %u: Trying to do more than 8 nested loops at 0x%04X!\n", trkID, prevPos);
				break;
			}
			if (inPos == trkInf->loopOfs)
				WriteEvent(fInf, MTS, 0xB0, 0x6F, 0);
				
			loopPPos[loopIdx] = parentPos;
			loopPos[loopIdx] = inPos;
			loopMax[loopIdx] = 0;
			loopCnt[loopIdx] = 0;
			loopIdx ++;
			break;
		case 0xE9:	// Loop End (B)
			{
				UINT8 takeLoop = 0;
				UINT8 loopTimes = songData[inPos + 0x01];
				inPos += 0x02;
				
				if (loopIdx == 0)
				{
					printf("Error Track %u: Loop End without Loop Start at 0x%04X!\n", trkID, prevPos);
					break;
				}
				loopIdx --;
				
				if (loopMax[loopIdx] == 0)
					loopMax[loopIdx] = loopTimes;
				loopCnt[loopIdx] ++;
				if (loopMax[loopIdx] == 0)
				{
					// infinite loop
					if (loopCnt[loopIdx] < 0x80)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)loopCnt[loopIdx]);
					
					if (loopCnt[loopIdx] < trkInf->loopTimes)
						takeLoop = 1;
				}
				else
				{
					if (loopCnt[loopIdx] < loopMax[loopIdx])
						takeLoop = 1;
				}
				if (takeLoop)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			break;
		case 0xEA:	// Loop Exit
			printf("Warning Track %u: Loop Exit (untested)\n", trkID);
			{
				UINT16 exitOfs = ReadLE16(&songData[inPos + 0x01]);
				inPos += 0x03;
				
				if (loopIdx == 0)
				{
					printf("Error Track %u: Loop End without Loop Start at 0x%04X!\n", trkID, prevPos);
					break;
				}
				
				loopIdx --;
				if (loopCnt[loopIdx] == loopMax[loopIdx] - 1)
					inPos = trkInf->startOfs + exitOfs;
				else
					loopIdx ++;
			}
			break;
		//case 0xEB:	// special loop thing??
		case 0xEC:	// GoTo
			printf("Warning Track %u: Jump Command (untested)\n", trkID);
			{
				UINT16 exitOfs = ReadLE16(&songData[inPos + 0x01]);
				inPos = trkInf->startOfs + exitOfs;
			}
			break;
		case 0xED:	//
			{
				UINT8 val = songData[inPos + 0x01];
				printf("Warning Track %u: Command %02X %02X at position 0x%04X\n", trkID, cmdType, val, inPos);
				if (val < 2)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, val);
			}
			inPos += 0x02;
			break;
		case 0xEE:	// no effect
			inPos += 0x01;
			break;
		case 0xEF:	// note tie
			inPos += 0x01;
			break;
		case 0xF7:	// Fade Out parameters
			// Note: invalid in CSCP.BIN (valkyrie_98), valid/used in SSCP.BIN
		//	printf("Warning Track %u: Fade Out Parameters: %02X %02X %02X\n",
		//		trkID, cmdType, songData[inPos + 0x01], songData[inPos + 0x02], songData[inPos + 0x03]);
			inPos += 0x04;
			break;
		case 0xF8:	// Marker Set
			WriteEvent(fInf, MTS, 0xB0, 0x70, songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0xF9:	// Marker Add
			WriteEvent(fInf, MTS, 0xB0, 0x71, songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0xFA:	// measure end / set measure counter
			inPos += 0x02;
			// fall through
		case 0xFB:	// measure end / increment measure counter
			inPos += 0x01;
			if (curBar >= 0x8000)	// prevent infinite loops
			{
				trkEnd = 1;
				break;
			}
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
			}
			curBar ++;
			break;
		case 0xFC:	// comment
			inPos ++;
			printf("Warning Track %u: Comment found (untested)\n", trkID);
			{
				UINT32 txtLen = (UINT32)strlen((const char*)&songData[inPos]);
				WriteMetaEvent(fInf, MTS, 0x01, txtLen, &songData[inPos]);
				inPos += txtLen + 1;	// length + '\0'
			}
			break;
		case 0xFD:	// no effect
			inPos += 0x01;
			break;
		case 0xFE:	// unknown, apparently no effect for sound playback
			if (songData[inPos + 0x01] > 0)
				printf("Warning Track %u: Command %02X at position 0x%04X\n", trkID, cmdType, inPos);
			inPos += 0x02;
			break;
		case 0xFF:	// track end
			trkEnd = 1;
			break;
		default:
			printf("Error Track %u: Unhandled GMD command 0x%02X at position 0x%04X!\n", trkID, cmdType, prevPos);
			trkEnd = 1;
#ifdef _DEBUG
			getchar();
#endif
			break;
		}	// end if (cmdType >= 0x80) / switch(cmdType)
	}	// end while(! trkEnd)
	FlushRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes, 0);
	
	free(syxBuffer);
	
	return 0x00;
}

static UINT8 PreparseGmdTrack(UINT32 songLen, const UINT8* songData, const GMD_INFO* gmdInf, TRK_INF* trkInf)
{
	UINT32 inPos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT8 trkEnd;
	UINT8 noteMode;
	UINT8 cmdType;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT32 loopTick[8];
	UINT16 loopMax[8];
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
	if (inPos + 0x10 > songLen)
		return 0x01;	// not enough bytes to read the header
	
	trkInf->tickCnt = songData[inPos + 0x05];	// start tick
	inPos += 0x10;
	
	trkEnd = 0;
	parentPos = 0x00;
	loopIdx = 0x00;
	noteMode = 0x00;
	
	while(inPos < trkEndPos && ! trkEnd)
	{
		cmdType = songData[inPos];
		if (cmdType < 0x80)
		{
			UINT8 noteDelay = songData[inPos + 0x01];
			if (noteMode == 0)
				inPos += 0x03;
			else if (noteMode == 1)
				inPos += 0x03;
			else if (noteMode == 2)
				inPos += 0x02;
			else if (noteMode == 3)
				inPos += 0x04;
			trkInf->tickCnt += noteDelay;
		}
		else switch(cmdType)
		{
		case 0x80:	// rest
			trkInf->tickCnt += songData[inPos + 0x01];
			inPos += 0x02;
			if (noteMode == 0 || noteMode >= 3)
				inPos += 0x01;
			break;
		case 0x81:	// delay
			trkInf->tickCnt += songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0x8A:	// Pitch Bend Reset
		case 0xB7:	// GS Reset
		case 0xEE:	// no effect
		case 0xEF:	// note tie
		case 0xFD:	// no effect
			inPos += 0x01;
			break;
		case 0x84:	// Note Length Multiplicator
		case 0x85:	// Note Length Subtraction
		case 0x88:
		case 0x89:	// Transposition Add
		case 0x8F:	// Sustain Pedal
		case 0x90:	// Channel Volume
		case 0x91:	// Channel Volume Accumulation
		case 0x92:	// Note Velocity
		case 0x93:	// Note Velocity Accumulation
		case 0x95:
		case 0x96:
		case 0x9C:	// Instrument
		case 0x9F:	// Channel Aftertouch
		case 0xA0:	// Pan (via index)
		case 0xA1:	// Pan
		case 0xA2:	// Pan Add
		case 0xB0:	// set Pitch Bend Range
		case 0xB8:	// Expression
		case 0xE2:
		case 0xE3:
		case 0xED:
		case 0xF8:	// Marker Set
		case 0xF9:	// Marker Add
		case 0xFE:	// unknown
			inPos += 0x02;
			break;
		case 0x86:	// Detune Set
		case 0x87:	// Detune Add
		case 0x8B:	// Pitch Bend
		case 0x8C:	// Pitch Bend Add
		case 0x8D:	// Portamento Up
		case 0x8E:	// Portamento Down
		case 0x9D:	// banked Instrument
		case 0x9E:	// Note Aftertouch
		case 0xAC:	// FM register write
		case 0xAD:	// FM channel register write
		case 0xAE:	// Control Change
		case 0xB5:	// Roland Device
		case 0xE0:	// channel mode
			inPos += 0x03;
			break;
		case 0x97:
		case 0xB1:	// set RPN Parameter
		case 0xB3:	// set NRPN Parameter
		case 0xF7:	// Fade Out parameters
			inPos += 0x04;
			break;
		case 0xA7:	// Software Modulation Settings
			inPos += 0x06;
			break;
		case 0xAF:	// SysEx command
		case 0xB6:	// Roland SysEx
		case 0x82:	// Note Off
			inPos ++;
			while(inPos < songLen && ! (songData[inPos] & 0x80))
				inPos ++;
			inPos ++;
			break;
		case 0x83:	// Note On
			inPos ++;
			while(inPos < songLen && ! (songData[inPos] & 0x80))
				inPos += 0x02;
			inPos += 0x02;
			break;
		case 0x98:	// Song Tempo
			//songTempo = ReadLE16(&songData[inPos + 0x01]);
			inPos += 0x03;
			break;
		case 0x9A:	// Tempo Modifier
			inPos += 0x03;
			break;
		case 0xA4:	// Modulation
		case 0xA5:	// Software Modulation Enable
			inPos += (songData[inPos + 0x01] & 0x80) ? 0x03 : 0x02;
			break;
		case 0xE1:	// note mode
			noteMode = songData[inPos + 0x01];
			inPos += 0x02;
			break;
		case 0xE5:	// repeat previous measure
			if (parentPos)
			{
				inPos = parentPos;
				break;
			}
			
			parentPos = inPos + 0x03;
			do
			{
				UINT32 repeatPos = trkInf->startOfs + ReadLE16(&songData[inPos + 0x01]);
				if (repeatPos == inPos)
					break;
				inPos = repeatPos;
			} while(songData[inPos] == 0xE5);
			break;
		case 0xE6:	// Loop Start (A)
			{
				UINT8 loopTimes = songData[inPos + 0x01];
				inPos += 0x02;
				
				if (loopIdx >= 8)
					break;
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopMax[loopIdx] = loopTimes;
				loopCnt[loopIdx] = 0;
				loopIdx ++;
			}
			break;
		case 0xE7:	// Loop End (A)
			{
				inPos += 0x01;
				
				if (loopIdx == 0)
					break;
				loopIdx --;
				
				loopCnt[loopIdx] ++;
				if (loopMax[loopIdx] == 0)
				{
					trkInf->loopOfs = loopPos[loopIdx];
					trkInf->loopTick = loopTick[loopIdx];
					trkEnd = 1;
				}
				else if (loopCnt[loopIdx] < loopMax[loopIdx])
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			break;
		case 0xE8:	// Loop Start (B)
			{
				inPos += 0x01;
				
				if (loopIdx >= 8)
					break;
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopMax[loopIdx] = 0;
				loopCnt[loopIdx] = 0;
				loopIdx ++;
			}
			break;
		case 0xE9:	// Loop End (B)
			{
				UINT8 loopTimes = songData[inPos + 0x01];
				inPos += 0x02;
				
				if (loopIdx == 0)
					break;
				loopIdx --;
				
				if (loopMax[loopIdx] == 0)
					loopMax[loopIdx] = loopTimes;
				loopCnt[loopIdx] ++;
				if (loopMax[loopIdx] == 0)
				{
					trkInf->loopOfs = loopPos[loopIdx];
					trkInf->loopTick = loopTick[loopIdx];
					trkEnd = 1;
				}
				else if (loopCnt[loopIdx] < loopMax[loopIdx])
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			break;
		case 0xEA:	// Loop Exit
			{
				UINT16 exitOfs = ReadLE16(&songData[inPos + 0x01]);
				inPos += 0x03;
				
				if (loopIdx == 0)
					break;
				loopIdx --;
				
				if (loopCnt[loopIdx] == loopMax[loopIdx] - 1)
					inPos = trkInf->startOfs + exitOfs;
				else
					loopIdx ++;
			}
			break;
		//case 0xEB:	// special loop thing??
		case 0xEC:	// GoTo
			{
				UINT16 exitOfs = ReadLE16(&songData[inPos + 0x01]);
				inPos = trkInf->startOfs + exitOfs;
			}
			break;
		case 0xFA:	// measure end / set measure counter
			inPos += 0x02;
			// fall through
		case 0xFB:	// measure end
			inPos += 0x01;
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
			}
			break;
		case 0xFC:	// comment
			inPos ++;
			while(inPos < songLen && songData[inPos] != '\0')
				inPos ++;
			inPos ++;
			break;
		case 0xFF:	// track end
			trkEnd = 1;
			break;
		default:
			trkEnd = 1;
			break;
		}	// end switch(cmdType)
	}	// end while(! trkEnd)
	
	return 0x00;
}


static void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT16 pbBase, INT16 pbDetune)
{
	INT32 pbFinal = (INT32)pbBase + pbDetune;
	if (pbFinal < 0x0000)
		pbFinal = 0x0000;
	else if (pbFinal > 0x3FFF)
		pbFinal = 0x3FFF;
	
	WriteEvent(fInf, MTS, 0xE0, (pbFinal >> 0) & 0x7F, (pbFinal >> 7) & 0x7F);
	return;
}

INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT16 scale)
{
	// formula: (60 000 000.0 / bpm) * (scale / 64.0)
	UINT32 div = bpm * scale;
	// I like rounding, but doing so make most MIDI programs display e.g. "144.99 BPM".
	return 60000000U * 64U / div;
	//return (60000000U * 64U + div / 2) / div;
}

INLINE void RcpTimeSig2Mid(UINT8 buffer[4], UINT8 beatNum, UINT8 beatDen)
{
	UINT8 den_base2;
	
	den_base2 = val2shift(beatDen);
	buffer[0] = beatNum;			// numerator
	buffer[1] = den_base2;			// log2(denominator)
	buffer[2] = 96 >> den_base2;	// metronome pulse
	buffer[3] = 8;					// 32nd notes per 1/4 note
	
	return;
}

static UINT8 val2shift(UINT32 value)
{
	UINT8 shift;
	
	shift = 0;
	value >>= 1;
	while(value)
	{
		shift ++;
		value >>= 1;
	}
	return shift;
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

INLINE INT8 Read7BitSigned(UINT8 value)
{
	value &= 0x7F;
	return (value & 0x40) ? (-0x80 + value) : value;
}
