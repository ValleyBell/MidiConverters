// Falcom TotalSoundDriver -> Midi Converter
// -----------------------------------------
// Written by Valley Bell, August 2021
// based on GMD -> Midi Converter
// Note: volume scaling for non-MIDI channels is not fully accurate
// TODO:
//	- "improvement" mode with chord support (which the driver lacks)
//	  This mode would make 0-tick notes longer when on a drum track OR the sustain pedal is pressed.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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
	UINT32 startOfs;
	UINT32 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 id;
	UINT8 mode;
	// bit 0: requires pitch bend
	// bit 3/4/5/6/7: sets Pan/Main Volume/Expression/Reverb/Chorus before first note
	// bit 8: sets pitch bend before first note
	UINT16 useFlags;
} TRK_INF;

typedef struct _track_ram
{
	// bit 0: pitch to next note / "no attack" mode
	// bit 1: enable early Note Off
	// bit 4/5/6/7: [frequency effects] enable portamento / vibrato / pitch slide / vibrato during portamento
	// bit 8/9: [volume effects] enable tremolo / envelope
	UINT16 chnFlags;
	
	UINT8 curNote;
	UINT8 lastNote;
	INT8 noteTransp;	// transposition for non-MIDI channels
	UINT8 chnVolScale;
	UINT8 chnVol;
	UINT8 lastVol;
	UINT8 noteVel;	// 42
	UINT8 nVelSingle;	// 43
	UINT8 chnPan;
	INT32 lastPB;
	
	UINT8 loopIdx;
	UINT32 loopPos[8];
	UINT16 loopMax[8];	// total loop count
	UINT16 loopCnt[8];	// remaining loops
	
	UINT16 noteLenMod;	// 0E/0F
	UINT32 noteStartTick;
	UINT32 noteOffTick;	// 0A/0B
	INT16 pbDetune;	// 18/19
	UINT16 portaDurat;	// 2E/2F
	INT32 portaRange;	// 2C/2D
	
	UINT8 vibDelay;	// 25 initial delay
	UINT8 vibCurDly;	// 26 current delay
	INT8 vibStrength;	// 27
	INT8 vibSpeed;	// 28
	UINT8 vibType;	// 29
	INT16 vibPos;	// 2A/2B
	
	UINT8 trmDelay;	// 1E initial delay
	UINT8 trmCurDly;	// 1F current delay
	UINT8 trmStrength;	// 20
	UINT8 trmVolScale;	// 21
	UINT8 trmSpeed;	// 22
	UINT16 trmPos;	// 23/24
	
	UINT8 psldDelay;	// 31
	UINT8 psldCurDly;	// 30
	INT16 psldDelta;	// 32/33
	INT16 psldFreq;	// 34/35
	
	UINT8 vevAtkLvl;	// 36
	UINT8 vevAtkTime;	// 37
	UINT8 vevDecTime;	// 38
	UINT8 vevDecLvl;	// 39
	UINT8 vevSusRate;	// 3A
	UINT8 vevRelTime;	// 3B
	UINT8 vevPhase;	// 3C
	INT16 vevTick;	// 3D
	UINT8 vevVol;	// 3F
} TRK_RAM;

#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);

INLINE INT16 VibratoLUT(UINT8 table, UINT16 index);
INLINE INT16 TremoloLUT(UINT16 index);
UINT8 Tsd2Mid(UINT32 songLen, const UINT8* songData);
static UINT8 TsdTrk2MidTrk(UINT32 songLen, const UINT8* songData,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 LookAheadCommand(UINT32 songLen, const UINT8* songData, UINT32 startPos, UINT8 cmd, UINT8 chnMode);
static UINT8 PreparseTsdTrack(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf, UINT8 mode);
INLINE INT32 NoteFrac2PitchBend(INT16 noteTransp, INT32 noteFrac);
static void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, INT32 pbVal);
INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT16 scale);
INLINE UINT8 Note_OPNARhy2MidiDrum(UINT8 note);
INLINE UINT16 ReadLE16(const UINT8* data);


static const INT16 VIBRATO_TBLS[2][0x20] = {
	{
		// triangle
		  32,  64,  96, 128, 160, 192, 224, 255,
		 224, 192, 160, 128,  96,  64,  32,   0,
		 -32, -64, -96,-128,-160,-192,-224,-255,
		-224,-192,-160,-128, -96, -64, -32,   0,
	},
	{
		// sine
		// formula: val = round(sin(PI * (index + 0.5) / 15) * 256)
		  27,  79, 128, 171, 207, 234, 250, 255,
		 250, 234, 207, 171, 128,  79,  27,   0,
		 -27, -79,-128,-171,-207,-234,-250,-255,
		-250,-234,-207,-171,-128, -79, -27,   0,
	}
};
// high-precision values
static const INT32 VIBRATO_TBLS_HP[2][0x20] = {
	{
		// triangle
		 0x20000, 0x40000, 0x60000, 0x80000, 0xA0000, 0xC0000, 0xE0000, 0x100000,
		 0xE0000, 0xC0000, 0xA0000, 0x80000, 0x60000, 0x40000, 0x20000,  0x00000,
		-0x20000,-0x40000,-0x60000,-0x80000,-0xA0000,-0xC0000,-0xE0000,-0x100000,
		-0xE0000,-0xC0000,-0xA0000,-0x80000,-0x60000,-0x40000,-0x20000,  0x00000,
	},
	{
		// sine
		 0x1AC26, 0x4F1BC, 0x80000, 0xAB4C2, 0xCF1BC, 0xE9DE2, 0xFA67E, 0x100000,
		 0xFA67E, 0xE9DE2, 0xCF1BC, 0xAB4C2, 0x80000, 0x4F1BC, 0x1AC26,  0x00000,
		-0x1AC26,-0x4F1BC,-0x80000,-0xAB4C2,-0xCF1BC,-0xE9DE2,-0xFA67E,-0x100000,
		-0xFA67E,-0xE9DE2,-0xCF1BC,-0xAB4C2,-0x80000,-0x4F1BC,-0x1AC26,  0x00000,
	}
};
static const INT16 SUSTAIN_RATES[0x80] = {
	   1,    2,    3,    4,    5,    6,    8,   10,   12,   14,   16,   18,   20,   24,   28,   30,
	  36,   34,   36,   40,   42,   44,   46,   48,   52,   54,   56,   60,   64,   68,   72,   76,
	  80,   84,   88,   92,   96,  100,  110,  120,  128,  136,  144,  152,  160,  168,  176,  184,
	 188,  192,  216,  240,  264,  288,  312,  336,  360,  384,  408,  432,  456,  480,  504,  528,
	 552,  576,  600,  624,  648,  672,  696,  720,  744,  768,  792,  816,  840,  864,  888,  912,
	 936,  960,  984, 1008, 1032, 1056, 1080, 1104, 1128, 1152, 1176, 1200, 1224, 1248, 1272, 1296,
	1320, 1344, 1368, 1392, 1416, 1440, 1464, 1488, 1512, 1536, 1560, 1584, 1608, 1632, 1656, 1680,
	1704, 1728, 1752, 1776, 1800, 1824, 1848, 1872, 1896, 3000, 5000, 7000,10000,20000,30000,    0,
};

static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define PB_RANGE	12	// hardcoded by the driver
static UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 DRIVER_BUGS = 0;
static UINT8 TIE_LOOKAHEAD = 0;
static UINT8 ED4_MODE = 0;
static UINT8 NO_TRK_NAMES = 0;
static UINT8 HIGH_PREC_PB = 0;
static UINT8 HIGH_PREC_VIB = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("Falcom TotalSoundDriver -> Midi Converter\n-----------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: tsd2mid.exe [options] input.bin output.mid\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -DriverBugs include oddities and bugs from the sound driver\n");
		printf("    -TieLAH     improved look-ahead for note tie (fixes VT_15_MD.M_)\n");
		printf("    -ED4        Legend of Heroes IV mode (enable additional bugs/oddities)\n");
		printf("    -NoTrkNames omit channel/track names\n");
		printf("    -PrecisePB  enable higher-precision pitch bend calculations\n");
		printf("    -PreciseVib enable higher-precision vibrato (requires -PrecisePB)\n");
		printf("                Warning: This may result in slightly stronger vibarto.\n");
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
		else if (! stricmp(argv[argbase] + 1, "DriverBugs"))
			DRIVER_BUGS = 1;
		else if (! stricmp(argv[argbase] + 1, "TieLAH"))
			TIE_LOOKAHEAD = 1;
		else if (! stricmp(argv[argbase] + 1, "ED4"))
			ED4_MODE = 1;
		else if (! stricmp(argv[argbase] + 1, "NoTrkNames"))
			NO_TRK_NAMES = 1;
		else if (! stricmp(argv[argbase] + 1, "PrecisePB"))
			HIGH_PREC_PB = 1;
		else if (! stricmp(argv[argbase] + 1, "PreciseVib"))
			HIGH_PREC_VIB = 1;
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
	
	retVal = Tsd2Mid(ROMLen, ROMData);
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

INLINE INT16 VibratoLUT(UINT8 table, UINT16 index)
{
	INT16 result = VIBRATO_TBLS[table][index & 0x1F];
	if (HIGH_PREC_PB && HIGH_PREC_VIB)	// don't do this in "non-precise vibrato" mode - due to lower precision 255 actually fits better
	{
		// patch 255 to 256, for a nicer triangle shape
		// The code divides by 256 anyway.
		if (result == -255)
			result = -256;
		else if (result == +255)
			result = +256;
	}
	return result;
}

INLINE INT16 TremoloLUT(UINT16 index)
{
	INT16 result = VIBRATO_TBLS[0][0x10 | (index & 0x0F)];
	if (HIGH_PREC_PB && result == -255)
		result = -256;
	return result;
}

UINT8 Tsd2Mid(UINT32 songLen, const UINT8* songData)
{
	TRK_INF trkInf[0x10];
	TRK_INF* tempTInf;
	UINT8 tempArr[0x20];
	UINT8 trkCnt;
	UINT16 trkPtr[0x10];
	UINT16 trkMode[0x10];
	UINT8 curTrk;
	UINT32 inPos;
	UINT32 tempLng;
	UINT8 retVal;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	
	midFInf.alloc = 0x20000;	// 128 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	trkCnt = 0x10;
	inPos = 0x00;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
		trkPtr[curTrk] = ReadLE16(&songData[inPos]);
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
		trkMode[curTrk] = ReadLE16(&songData[inPos]);
	//ReadLE16(&songData[0x40]);	// SSG instrument pointer
	//songData[0x42];	// number of FM instruments
	//songData[0x43];	// number of SSG instruments
	//inPos = 0x50;	// start of GM instruments
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = trkPtr[curTrk];
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		tempTInf->id = curTrk;
		tempTInf->mode = (UINT8)trkMode[curTrk];
		
		if (tempTInf->startOfs)
			PreparseTsdTrack(songLen, songData, tempTInf, 0);
		if (tempTInf->loopOfs)
			PreparseTsdTrack(songLen, songData, tempTInf, 1);	// pass #2 to count the actual loop length
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFInf, 0x0001, trkCnt, MIDI_RES);
	
	retVal = 0x00;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		WriteMidiTrackStart(&midFInf, &MTS);
		
		if (curTrk == 0)
		{
			// first track: write global song information
			tempLng = Tempo2Mid(120, 0x40);
			WriteBE32(tempArr, tempLng);
			WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
		}
		
		if (trkInf[curTrk].startOfs)	// offset 0 == track disabled
			retVal = TsdTrk2MidTrk(songLen, songData, &trkInf[curTrk], &midFInf, &MTS);
		
		WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFInf, &MTS);
		
		if (retVal)
		{
			if (retVal == 0x01)
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

static void ProcessTsdTrkFX(TRK_RAM* trk, FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	// run Effects Processor
	UINT32 evtTicks = MTS->curDly - trk->noteStartTick;
	UINT32 remTicks;
	INT32 portaPos = 0;
	INT16 pitchTransp = (INT16)trk->curNote - trk->lastNote;
	
	MTS->curDly = trk->noteStartTick;
	for (remTicks = evtTicks; remTicks > 0; remTicks --, MTS->curDly ++)
	{
		INT32 noteFreq = 0;
		INT32 tempPB;
		UINT8 tempVol = trk->chnVol;
		UINT8 vibAllow = 1;
		
		// handle portamento
		if ((trk->chnFlags & 0x10) && portaPos < trk->portaDurat)
		{
			INT32 portaOffset = trk->portaRange * portaPos / (INT32)trk->portaDurat;
			noteFreq += portaOffset;
			portaPos ++;
			vibAllow = (trk->chnFlags & 0x80);	// vibrato may be disabled during portamento
		}
		// handle software vibrato
		if ((trk->chnFlags & 0x20) && vibAllow)
		{
			if (trk->vibCurDly > 0)
			{
				trk->vibCurDly --;
			}
			else
			{
				INT16 tblIdx = (INT16)trk->vibSpeed * trk->vibPos / 0x1F;
				INT16 vibVal = VibratoLUT(trk->vibType, tblIdx - 1);	// start with value 0, like the actual driver
				INT32 vibOffset;
				if (! HIGH_PREC_PB)
					vibOffset = (INT32)trk->vibStrength * vibVal / 0x100;	// driver formula
				else if (! HIGH_PREC_VIB)
					vibOffset = ((INT32)trk->vibStrength * vibVal / 0x100) * 0x1000;	// original vibrato precision with accurate PBs
				else
					vibOffset = (INT32)trk->vibStrength * vibVal * 0x10;	// with increased precision
				noteFreq += vibOffset;
				trk->vibPos ++;
				if (! ED4_MODE)
					trk->vibPos &= 0x1FF;	// only done by Brandish VT's driver
			}
		}
		// handle pitch slides
		if (trk->chnFlags & 0x40)
		{
			if (trk->psldCurDly > 0)
				trk->psldCurDly --;
			else
				trk->psldFreq += trk->psldDelta;
		}
		tempPB = trk->pbDetune + trk->psldFreq + NoteFrac2PitchBend(pitchTransp, noteFreq);
		if (tempPB != trk->lastPB)
		{
			trk->lastPB = tempPB;
			WritePitchBend(fInf, MTS, trk->lastPB);
		}
		
		// handle early note off
		// Note: The order of all this stuff is important. (else the Note Off in VT_06_MD.M_ is triggered too late)
		if (trk->chnFlags & 0x02)
		{
			if (remTicks == trk->noteOffTick)
			{
				trk->chnFlags &= ~0x02;
				if ((trk->chnFlags & 0x200) && trk->vevPhase != 5)
				{
					trk->vevPhase = 4;
					trk->vevTick = 0;
				}
				else if (trk->lastNote != 0xFF)
				{
					WriteEvent(fInf, MTS, 0x90, (trk->lastNote + trk->noteTransp) & 0x7F, 0x00);
					trk->lastNote = 0xFF;
				}
			}
		}
		
		// handle software tremolo
		if (trk->chnFlags & 0x200)
		{
			INT16 volDelta = 0;
			INT16 phaseDurat = 0;
			INT16 envVolume = 0;
			UINT8 skip = 0;
			switch(trk->vevPhase)
			{
			case 0:	// attack
				phaseDurat = trk->vevAtkTime;
				envVolume = trk->vevAtkLvl;
				volDelta = 0x7F - trk->vevAtkLvl;
				break;
			case 1:	// decay
				phaseDurat = trk->vevDecTime;
				envVolume = 0x7F;
				volDelta = trk->vevDecLvl - 0x7F;
				break;
			case 2:	// sustain
				phaseDurat = SUSTAIN_RATES[trk->vevSusRate & 0x7F];
				envVolume = trk->vevDecLvl;
				volDelta = 0x00 - trk->vevDecLvl;
				break;
			case 3:	// sustain silent
				skip = 1;
				tempVol = 0x00;
				break;
			case 4:	// release
				phaseDurat = trk->vevRelTime;
				envVolume = trk->vevVol;
				volDelta = 0x00 - trk->vevVol;
				break;
			case 5:	// key off
				skip = 1;
				tempVol = 0x00;
				if (trk->lastNote != 0xFF)
				{
					WriteEvent(fInf, MTS, 0x90, (trk->lastNote + trk->noteTransp) & 0x7F, 0x00);
					trk->lastNote = 0xFF;
				}
				break;
			}
			if (! skip)
			{
				if (phaseDurat != 0)
				{
					volDelta = volDelta * (INT16)trk->vevTick / phaseDurat;
					envVolume += volDelta;
					if (trk->vevTick == phaseDurat)
					{
						trk->vevPhase ++;
						trk->vevTick = 0;
					}
				}
				trk->vevVol = (UINT8)envVolume;
				tempVol = tempVol * envVolume / 0x7F;
				trk->vevTick ++;
			}
		}
		if (trk->chnFlags & 0x100)
		{
			if (trk->trmCurDly > 0)
			{
				trk->trmCurDly --;
			}
			else
			{
				UINT16 tblIdx = trk->trmSpeed * trk->trmPos / 0x20;
				INT16 trmVal = TremoloLUT(tblIdx);
				UINT16 trmVolume;
				INT16 trmOffset;
				tempVol = tempVol * trk->trmVolScale / 0x7F;
				trmVolume = tempVol * trk->trmStrength / 0xFF;
				trmOffset = (INT16)trmVolume * trmVal / 0x100;
				tempVol += trmOffset;
				trk->trmPos ++;
			}
		}
		if (DRIVER_BUGS && trk->lastNote == 0xFF)
			tempVol = trk->lastVol;	// don't output when no note is playing
		if (tempVol != trk->lastVol && ! (trk->chnVolScale & 0x80))
		{
			trk->lastVol = tempVol;
			WriteEvent(fInf, MTS, 0xB0, 0x0B, trk->lastVol * trk->chnVolScale);
		}
		if (trk->lastNote == 0xFF)
			break;	// a Note Off stops all effect processing (see VT_06_MD.M_)
	}
	trk->chnFlags &= ~0x011;
	MTS->curDly += remTicks;
	trk->noteStartTick = MTS->curDly;
	return;
}

static UINT8 TsdTrk2MidTrk(UINT32 songLen, const UINT8* songData,
							TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	static const UINT8 OPNA_PAN_LUT[0x04] = {0x3F, 0x7F, 0x01, 0x40};
	TRK_RAM trkRAM;
	TRK_RAM* trk = &trkRAM;
	UINT32 inPos;
	UINT32 trkTick;
	UINT8 chnMode;
	UINT8 chnID;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT16 mstLoopCnt;
	char tempStr[0x20];
	UINT16 songTempo;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	if (trkInf->mode & 0x01)
		printf("Warning Track %u: Invalid channel mode 0x02X\n", trkInf->id, trkInf->mode);
	
	trkEnd = 0;
	inPos = trkInf->startOfs;
	trk->chnFlags = 0x080;	// "vibrato during portamento" is enabled by default
	songTempo = 120;
	trk->noteTransp = 0;
	trk->chnVolScale = 1;
	trk->curNote = 0xFF;
	trk->lastNote = 0xFF;
	trk->noteVel = 0;	// driver default
	trk->nVelSingle = 0;
	trk->chnVol = 0x00;	// driver default
	trk->chnPan = 0x40;
	trk->lastVol = 0x80 | trk->chnVol;
	trk->lastPB = 0;
	trk->noteLenMod = 0;
	trk->noteOffTick = 0;
	trk->noteStartTick = 0;
	trk->pbDetune = 0;
	trk->vibDelay = 0;
	trk->vibCurDly = 0;
	trk->trmDelay = 0;
	trk->trmCurDly = 0;
	trk->psldDelay = 0;
	trk->psldCurDly = 0;
	trk->psldDelta = 0;
	trk->psldFreq = 0;
	trk->loopIdx = 0x00;
	mstLoopCnt = 0;
	trk->vevPhase = 0;
	MTS->curDly = 0;
	
	if (trkInf->mode < 0x14)	// OPN/OPNA channels
	{
		chnID = trkInf->mode / 2;
		chnMode = chnID / 3;
		chnID %= 3;
		if (chnMode == 0)	// FM 4..6
		{
			chnID += 3;
			MTS->midChn = chnID;
			chnMode = 0;
			sprintf(tempStr, "FM %u", 1 + chnID);
			trk->noteTransp = +1*12;
		}
		else if (chnMode == 1)	 // FM 1..3
		{
			MTS->midChn = chnID;
			chnMode = 0x00;
			sprintf(tempStr, "FM %u", 1 + chnID);
			trk->noteTransp = +1*12;
		}
		else if (chnMode == 2)	// SSG 1..3
		{
			MTS->midChn = 0x0A + chnID;
			chnMode = 0x01;
			sprintf(tempStr, "SSG %u", 1 + chnID);
			trk->noteTransp = +2*12;
			trk->chnVolScale = 8;	// 0x0..0xF -> 0x00..0x7F
		}
		else //if (chnMode == 3)	// Rhythm
		{
			MTS->midChn = 0x09;
			chnMode = 0x02;
			sprintf(tempStr, "Rhythm");
			trk->chnVolScale = 0x80 | 4;	// 0x0..0x1F -> 0x00..0x7F
		}
	}
	else if (trkInf->mode < 0x34)	// MIDI channels
	{
		chnMode = 0x10;
		chnID = (trkInf->mode - 0x14) / 2;
		MTS->midChn = chnID;
		sprintf(tempStr, "MIDI %u", 1 + chnID);
	}
	else if (trkInf->mode < 0x36)	// beeper
	{
		chnMode = 0x20;
		chnID = 0x00;
 		MTS->midChn = chnID;
		sprintf(tempStr, "Beeper");
		trk->chnVol = 0x7F;	// not what the driver does, but the beeper doesn't have volume control anyway
		trk->noteTransp = +4*12;
	}
	if (! NO_TRK_NAMES)
		WriteMetaEvent(fInf, MTS, 0x03, strlen(tempStr), tempStr);
	if (chnMode == 0x01 || chnMode == 0x20)
		WriteEvent(fInf, MTS, 0xC0, 0x50, 0x00);	// square wave
	
	// channel initialization (based on actual driver, but optimized)
	if (trkInf->useFlags & 0x01)
	{
		WriteEvent(fInf, MTS, 0xB0, 0x65, 0x00);
		WriteEvent(fInf, MTS, 0xB0, 0x64, 0x00);
		WriteEvent(fInf, MTS, 0xB0, 0x06, PB_RANGE);
		if (! (trkInf->useFlags & 0x100))
			WritePitchBend(fInf, MTS, trk->lastPB + trk->pbDetune + trk->psldFreq);
	}
	if (! (trkInf->useFlags & 0x10))
		WriteEvent(fInf, MTS, 0xB0, 0x07, 105);
	if (! (trkInf->useFlags & 0x20))
		WriteEvent(fInf, MTS, 0xB0, 0x0A, 0x40);
	if (trk->chnVolScale & 0x80)
	{
		trk->lastVol = 0x80 | 0x7F;
		WriteEvent(fInf, MTS, 0xB0, 0x0B, 0x7F);
	}
	else if (! (trkInf->useFlags & 0x08))
	{
		trk->lastVol = 0x80 | trk->chnVol;
		WriteEvent(fInf, MTS, 0xB0, 0x0B, trk->chnVol * trk->chnVolScale);
	}
	if (! (trkInf->useFlags & 0x40))
		WriteEvent(fInf, MTS, 0xB0, 0x5B, 0);
	if (! (trkInf->useFlags & 0x80))
		WriteEvent(fInf, MTS, 0xB0, 0x5D, 0);
	
	trkTick = MTS->curDly;
	while(inPos < songLen && ! trkEnd)
	{
		UINT32 prevPos = inPos;
		
		if (MTS->curDly > trk->noteStartTick && trk->lastNote != 0xFF)
			ProcessTsdTrkFX(trk, fInf, MTS);
		
		if (inPos == trkInf->loopOfs && mstLoopCnt == 0)
			WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
		
		cmdType = songData[inPos];
		if (cmdType == 0x7F)
		{
			UINT16 noteDelay = songData[inPos + 0x01];
			inPos += 0x02;
			if (noteDelay == 0xFF)
			{
				noteDelay = ReadLE16(&songData[inPos]);
				inPos += 0x02;
			}
			
			if ((trk->chnFlags & 0x200) && trk->vevPhase != 5)
			{
				trk->vevPhase = 4;
				trk->vevTick = 0;
			}
			else
			{
				if (trk->lastNote != 0xFF)
					WriteEvent(fInf, MTS, 0x90, (trk->lastNote + trk->noteTransp) & 0x7F, 0x00);
				trk->lastNote = 0xFF;
				trk->curNote = 0xFF;
			}
			
			trk->noteStartTick = MTS->curDly;
			MTS->curDly += noteDelay;
			trkTick += noteDelay;
		}
		else if (cmdType < 0x80)
		{
			UINT16 noteDelay;
			UINT16 noteLen;
			UINT8 curNoteVel;
			
			noteDelay = songData[inPos + 0x01];
			inPos += 0x02;
			if (noteDelay == 0xFF)
			{
				noteDelay = ReadLE16(&songData[inPos]);
				inPos += 0x02;
			}
			
			trk->curNote = cmdType;
			if (trk->nVelSingle)
			{
				curNoteVel = trk->nVelSingle;
				trk->nVelSingle = 0x00;
			}
			else
			{
				curNoteVel = trk->noteVel;
			}
			if (trk->chnVolScale & 0x80)
				curNoteVel = trk->chnVol * (trk->chnVolScale & 0x7F);	// for OPNA Rhythm, use Channel Volume as note velocity
			else if (chnMode != 0x10)
				curNoteVel = 0x7F;	// note velocity affects only MIDI channels
			
			if (songData[inPos] == 0x89)
			{
				//printf("Track %u: Portamento at position 0x%04X\n", trkInf->id, inPos);
				trk->portaRange = (songData[inPos + 0x01] - trk->curNote) * 0x30;
				if (HIGH_PREC_PB)
					trk->portaRange *= 0x1000;	// Note: considering [pos+2] being 0..100, this overflows for a distance >= 0x6D
				trk->portaRange = trk->portaRange * songData[inPos + 0x02] / 100;
				trk->portaDurat = noteDelay;
				trk->chnFlags |= 0x10;	// enable portamento
				inPos += 0x03;
			}
			if (trk->noteLenMod == 0 || LookAheadCommand(songLen, songData, inPos, 0x83, chnMode))	// yes, the driver looks ahead for command 83 here
			{
				if (trk->noteLenMod > 0 && songData[inPos] != 0x83)
					printf("Track %u: Lookahead found far Tie command at 0x%04X\n", trkInf->id, prevPos);
				trk->chnFlags &= ~0x02;
				noteLen = noteDelay;
				trk->noteOffTick = 0;
			}
			else
			{
				trk->chnFlags |= 0x02;
				if (trk->noteLenMod & 0x8000)
					trk->noteOffTick = (trk->noteLenMod & 0x3FFF);
				else if (trk->noteLenMod & 0x4000)
					trk->noteOffTick = noteDelay - (trk->noteLenMod & 0x3FFF);
				else
				{
					// The driver uses this algorithm.
					UINT16 stopTick = (100 - trk->noteLenMod) * noteDelay / 100;
					{
						// The original code does SUB (for [100-noteLenMod]), then MOV, MUL, DIV [/100], then JNZ.
						// However only the SUB sets the flags, so the JNZ effectively checks the result of the SUB command.
						// So this is how it really works.
						if (trk->noteLenMod == 100)
							stopTick = 1;
					}
					{
						// I assume that it was originally intended to behave a bit differently though.
						// It was probably intended to check the result of the DIV (stopTick) and clamp it to 1.
						// Doing this breaks some songs. (obvious example: ED415.M_: guitar in bar 20)
						// Thanks to Ristar for helping me to find this oddity.
						//if (stopTick == 0)
						//	stopTick = 1;
					}
					trk->noteOffTick = stopTick;
				}
				noteLen = noteDelay - (UINT16)trk->noteOffTick;
			}
			//if (noteLen > noteDelay)	// prevent warnings for chords
			//	printf("Warning Track %u: Bad note length %d at position 0x%04X\n", trkInf->id, (INT16)noteLen, inPos);
			// noteLen > noteDelay is possible and just causes the note to end after noteDelay ticks
			
			
			// do Note Off and re-initialize effect memory for new notes
			if (! (trk->chnFlags & 0x01) && trk->lastNote != 0xFF)
				WriteEvent(fInf, MTS, 0x90, (trk->lastNote + trk->noteTransp) & 0x7F, 0x00);
			
			if (! (trk->chnFlags & 0x01) || trk->curNote != trk->lastNote)
			{
				// This is reached when:
				//	- (chnFlags & 0x01) is not set
				//	- current note != base note
				// This has the effect, that sometimes the values are reset when doing pitch bends
				// to other notes. (The driver does this and VT_04_MD.M_ requires this to work properly.)
				trk->vibCurDly = trk->vibDelay;
				trk->vibPos = 0;
				trk->psldCurDly = trk->psldDelay;
				trk->psldFreq = 0;
				trk->trmCurDly = trk->trmDelay;
				trk->trmPos = 0;
			}
			if (! (trk->chnFlags & 0x01))
			{
				trk->vevPhase = 0;
				trk->vevTick = 0;
			}
			
			if (! (trk->chnFlags & 0x01))
			{
				// Write Channel Volume and Pitch Bend states, so that they get set *BEFORE* Note On.
				// This improves sound especially on Yamaha devices.
				// When we DON'T do a note on, the Effects Processor will set the proper values and we don't need this.
				INT32 tempPB;
				UINT8 tempVol;
				
				tempVol = trk->chnVol;
				if (trk->lastVol == 0x80 && trk->chnVol == 0)	// when volume was set at "init" tick and unchanged, don't resend
					trk->lastVol &= 0x7F;	// required to fix start of ED442.M_ track 11, which was broken by the ED411.M_ fix
				if (trk->chnFlags & 0x200)
					tempVol = tempVol * trk->vevAtkLvl / 0x7F;
				if (tempVol != trk->lastVol && ! (trk->chnVolScale & 0x80))
				{
					trk->lastVol = tempVol;
					WriteEvent(fInf, MTS, 0xB0, 0x0B, trk->lastVol * trk->chnVolScale);
				}
				
				tempPB = trk->pbDetune + trk->psldFreq;
				//if (trk->chnFlags & 0x01)
				//	tempPB += NoteFrac2PitchBend((INT16)trk->curNote - trk->lastNote, 0);
				if (tempPB != trk->lastPB)	// omit the PB when same as before
				{
					trk->lastPB = tempPB;
					WritePitchBend(fInf, MTS, trk->lastPB);
				}
			}
			
			// do actual Note On
			if (! (trk->chnFlags & 0x01) && trk->curNote != 0xFF)
			{
				if (chnMode == 0x02)
					trk->curNote = Note_OPNARhy2MidiDrum(trk->curNote);
				WriteEvent(fInf, MTS, 0x90, (trk->curNote + trk->noteTransp) & 0x7F, curNoteVel);
				trk->lastNote = trk->curNote;
			}
			trk->chnFlags &= ~0x01;
			
			// TODO: optional improvement - support chords when noteDelay == 0 (The actual driver does NOT support chords.)
			trk->noteStartTick = MTS->curDly;
			MTS->curDly += noteDelay;
			trkTick += noteDelay;
		}
		else switch(cmdType)
		{
		case 0x80:	// Loop Start
			{
				if (trk->loopIdx >= 8)
				{
					inPos += 0x03;
					printf("Error Track %u: Trying to do more than 8 nested loops at 0x%04X!\n", trkInf->id, prevPos);
					break;
				}
				
				trk->loopPos[trk->loopIdx] = inPos;
				trk->loopMax[trk->loopIdx] = songData[inPos + 0x01];
				trk->loopCnt[trk->loopIdx] = songData[inPos + 0x02];
				trk->loopIdx ++;
				inPos += 0x03;
			}
			break;
		case 0x81:	// Loop Exit
			{
				INT16 exitOfs = (INT16)ReadLE16(&songData[inPos + 0x01]);
				UINT16 lpEndPos = inPos + exitOfs;
				UINT16 lpStPos = lpEndPos + (INT16)ReadLE16(&songData[lpEndPos + 0x01]) + 0x01;
				UINT8 exitLpIdx;
				inPos += 0x03;
				
				if (songData[lpEndPos] != 0x82)
				{
					// Some tracks in ED4 happen to have the (invalid) sequence 81 00 00.
					printf("Warning Track %u: Loop Exit at 0x%04X not pointing to Loop End!\n", trkInf->id, prevPos);
					break;
				}
				if (trk->loopIdx == 0)
				{
					printf("Error Track %u: Loop Exit without Loop Start at 0x%04X!\n", trkInf->id, prevPos);
					break;
				}
				
				// required in order to allow exiting neseted loops (see ED407.N_, track 8)
				for (exitLpIdx = trk->loopIdx; exitLpIdx > 0; exitLpIdx --)
				{
					if (trk->loopPos[exitLpIdx - 1] == lpStPos)
						break;
				}
				if (exitLpIdx == 0)
				{
					printf("Error Track %u: Unable to find matching Loop Start/End for Loop Exit at 0x%04X!\n", trkInf->id, prevPos);
					break;
				}
				
				exitLpIdx --;
				if (trk->loopCnt[exitLpIdx] == trk->loopMax[exitLpIdx] - 1)
				{
					inPos += exitOfs;
					trk->loopIdx = exitLpIdx;
				}
			}
			break;
		case 0x82:	// Loop End
			{
				UINT8 takeLoop = 0;
				INT16 loopOfs = (INT16)ReadLE16(&songData[inPos + 0x01]);	// points to loopCnt[loopIdx]
				UINT16 lpStPos = inPos + loopOfs + 0x01;
				inPos += 0x03;
				
				if (trk->loopIdx == 0 && songData[lpStPos] == 0x80)	// check for Loop Start command
				{
					// recover loops where the Loop Start command was missed
					// happens in ED407.A/.N and ED408.A/.N
					trk->loopPos[trk->loopIdx] = lpStPos;
					trk->loopMax[trk->loopIdx] = songData[lpStPos + 0x01];
					trk->loopCnt[trk->loopIdx] = songData[lpStPos + 0x02];
					trk->loopIdx ++;
				}
				if (trk->loopIdx == 0)
				{
					printf("Error Track %u: Loop End without Loop Start at 0x%04X!\n", trkInf->id, prevPos);
					break;
				}
				trk->loopIdx --;
				if (lpStPos != trk->loopPos[trk->loopIdx])
				{
					printf("Error Track %u: Loop End at 0x%04X points to Loop Start at 0x%04X, expected %04X!\n",
						trkInf->id, prevPos, lpStPos, trk->loopPos[trk->loopIdx]);
					break;
				}
				
				trk->loopCnt[trk->loopIdx] ++;
				if (trk->loopMax[trk->loopIdx] == 0)
				{
					// infinite loop
					if (trk->loopMax[trk->loopIdx] < 0x80)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)trk->loopCnt[trk->loopIdx]);
					
					if (trk->loopCnt[trk->loopIdx] < trkInf->loopTimes)
						takeLoop = 1;
				}
				else if (trk->loopCnt[trk->loopIdx] < trk->loopMax[trk->loopIdx])
				{
					takeLoop = 1;
				}
				if (takeLoop)
				{
					inPos += loopOfs + 0x01;
					trk->loopIdx ++;
				}
			}
			break;
		case 0x83:	// Pitch Bend / no-attack mode
			trk->chnFlags |= 0x01;	// set Pitch Bend flag
			inPos += 0x01;
			break;
		case 0x85:	// Song Tempo
			{
				UINT32 tempoVal;
				UINT8 tempArr[4];
				
				songTempo = songData[inPos + 0x01];
				tempoVal = Tempo2Mid(songTempo, 0x40);
				WriteBE32(tempArr, tempoVal);
				WriteMetaEvent(fInf, MTS, 0x51, 0x03, &tempArr[0x01]);
				inPos += 0x02;
			}
			break;
		case 0x86:	// Detune Set
			trk->lastPB -= trk->pbDetune;
			trk->pbDetune = (INT16)ReadLE16(&songData[inPos + 0x01]);
			trk->lastPB += trk->pbDetune;
			WritePitchBend(fInf, MTS, trk->lastPB);
			inPos += 0x03;
			break;
		case 0x87:	// Note Duration Modifier
			trk->noteLenMod = ReadLE16(&songData[inPos + 0x01]);
			inPos += 0x03;
			break;
		case 0x88:	// Software Vibrato
			trk->vibDelay = songData[inPos + 0x01];
			trk->vibSpeed = (INT8)songData[inPos + 0x02];
			trk->vibStrength = (INT8)songData[inPos + 0x03];
			trk->vibType = songData[inPos + 0x04];
			trk->chnFlags |= 0x20;	// enable vibrato
			inPos += 0x05;
			break;
		case 0x89:	// Portamento
			trk->portaRange = (songData[inPos + 0x01] - trk->curNote) * 0x30;
			if (HIGH_PREC_PB)
				trk->portaRange *= 0x1000;
			trk->portaRange = trk->portaRange * songData[inPos + 0x02] / 100;
			trk->portaDurat = 0;	// this is what happens when the slide is at the wrong spot
			trk->chnFlags |= 0x10;	// enable portamento
			inPos += 0x03;
			break;
		case 0x8A:	// Effect Disable
			{
				UINT8 fxType = songData[inPos + 0x01];
				UINT8 onOff = songData[inPos + 0x02];
				inPos += 0x03;
				//printf("Track %u: FX %u disable: 0x%02X at position 0x%04X\n", trkInf->id, fxType, onOff, inPos);
				switch(fxType)
				{
				case 0x00:	// Vibrato
					if (onOff & 0x80)
						trk->chnFlags &= ~0x20;
					else
						trk->chnFlags |= 0x20;	// vibrato enable
					if (onOff & 0x01)
						trk->chnFlags &= ~0x80;	// prevent vibrato while portamento is active
					else
						trk->chnFlags |= 0x80;	// allow vibrato while portamento is active
					break;
				case 0x01:	// Pitch Slide
					if (onOff)
						trk->chnFlags &= ~0x40;
					else
						trk->chnFlags |= 0x40;
					break;
				case 0x02:	// Tremolo
					if (onOff)
						trk->chnFlags &= ~0x100;
					else
						trk->chnFlags |= 0x100;
					break;
				case 0x03:	// Volume Envelope
					if (onOff)
						trk->chnFlags &= ~0x200;
					else
						trk->chnFlags |= 0x200;
					break;
				}
			}
			break;
		case 0x8B:	// Jump
			{
				INT16 jumpOfs = (INT16)ReadLE16(&songData[inPos + 0x01]);
				inPos += 0x03;
				if (! jumpOfs)
				{
					// null offset = track end
					trkEnd = 1;
				}
				else
				{
					mstLoopCnt ++;
					if (mstLoopCnt < 0x80)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
					if (mstLoopCnt < trkInf->loopTimes)
						inPos += jumpOfs;
					else
						trkEnd = 1;
				}
			}
			break;
		case 0x8C:	// Pan
			if ((chnMode & 0xF0) == 0x00)
				trk->chnPan = OPNA_PAN_LUT[songData[inPos + 0x01] & 0x03];
			else
				trk->chnPan = songData[inPos + 0x01];
			WriteEvent(fInf, MTS, 0xB0, 0x0A, trk->chnPan);
			inPos += 0x02;
			break;
		case 0x8D:	// Channel Volume
			trk->chnVol = songData[inPos + 0x01];
			inPos += 0x02;
			if (trk->chnFlags & 0x200)
			{
				// for software envelopes, ensure Expression is rewritten,
				// but write it at Note On
				trk->lastVol = 0xFF;
			}
			else if (! (trk->chnVolScale & 0x80))
			{
				if (DRIVER_BUGS && trk->lastNote == 0xFF)
					break;	// The driver applies the channel volume later - when playing notes or running the envelope generator.
				// applying immediately is nicer for analyzing the original data though
				if (trk->lastVol == 0 && trk->chnVol > 0)
					break;	// however it causes stray notes in ED411.M_, bar 30, so delay the volume change in this case
				WriteEvent(fInf, MTS, 0xB0, 0x0B, trk->chnVol * trk->chnVolScale);	// yes, this goes to MIDI Expression
				trk->lastVol = trk->chnVol;
			}
			break;
		case 0x8E:	// Channel Volume Accumulation
			{
				INT16 vol16 = trk->chnVol + (INT8)songData[inPos + 0x01];
				if (DRIVER_BUGS)
				{
					// The driver only checks the "sign" and resets the volume to 0 when negative.
					// Thus, it actually only checks *underflow* and not overflow.
					if (vol16 & 0x80)
						vol16 = 0x00;
				}
				else
				{
					if (vol16 < 0x00)
						vol16 = 0x00;
					else if (vol16 > 0x7F)
						vol16 = 0x7F;
				}
				trk->chnVol = (UINT8)vol16;
				inPos += 0x02;
			}
			if (songData[inPos] == cmdType)
				break;	// don't write when there are consecutive events
			if (! (trk->chnVolScale & 0x80))
			{
				if (DRIVER_BUGS && trk->lastNote == 0xFF)
					break;
				if (trk->lastVol == 0 && trk->chnVol > 0)
					break;
				WriteEvent(fInf, MTS, 0xB0, 0x0B, trk->chnVol * trk->chnVolScale);
				trk->lastVol = trk->chnVol;
			}
			break;
		case 0x90:	// Instrument
			WriteEvent(fInf, MTS, 0xC0, songData[inPos + 0x01], 0x00);
			inPos += 0x02;
			break;
		case 0x91:	// set SSG noise frequency
			WriteEvent(fInf, MTS, 0xB0, 0x54, songData[inPos + 0x01]);	// Portamento Control (Note)
			inPos += 0x02;
			break;
		case 0x92:	// set Volume Envelope
			//printf("Track %u: Volume Envelope at position 0x%04X!\n", trkInf->id, prevPos);
			trk->vevAtkLvl = songData[inPos + 0x01];
			trk->vevAtkTime = songData[inPos + 0x02];
			trk->vevDecTime = songData[inPos + 0x03];
			trk->vevDecLvl = songData[inPos + 0x04];
			trk->vevSusRate = songData[inPos + 0x05];
			trk->vevRelTime = songData[inPos + 0x06];
			trk->vevPhase = 0;	// done by the driver and required by VT_12_MD.M_
			trk->vevTick = 0;
			trk->chnFlags |= 0x200;	// enable volume envelope
			inPos += 0x07;
			break;
		case 0x93:	// set SSG noise mode
			WriteEvent(fInf, MTS, 0xB0, 0x03, songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0x94:	// set Marker
			printf("Track %u: Set Marker Byte = 0x%02X at position 0x%04X\n", trkInf->id, songData[inPos + 0x01], prevPos);
			sprintf(tempStr, "Marker = %u", songData[inPos + 0x01]);
			WriteMetaEvent(fInf, MTS, 0x06, strlen(tempStr), tempStr);
			WriteEvent(fInf, MTS, 0xB0, 0x6E, songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0x95:	// Pitch Slide
			trk->psldDelay = songData[inPos + 0x01];
			trk->psldCurDly = trk->psldDelay;	// the driver does this explicitly here
			trk->psldDelta = (INT8)songData[inPos + 0x02];
			trk->chnFlags |= 0x40;	// enable pitch slide
			inPos += 0x03;
			break;
		case 0x96:	// set Note Velocity
			if (! (songData[inPos + 0x01] & 0x80))
				trk->noteVel = songData[inPos + 0x01];
			else
				trk->nVelSingle = songData[inPos + 0x01] & 0x7F;
			inPos += 0x02;
			break;
		case 0x97:	// Control Change
			{
				UINT8 ctrl = songData[inPos + 0x01];
				UINT8 value = songData[inPos + 0x02];
				WriteEvent(fInf, MTS, 0xB0, ctrl, value);
				inPos += 0x03;
			}
			break;
		case 0x98:	// Software Tremolo
			trk->trmDelay = songData[inPos + 0x01];
			trk->trmSpeed = songData[inPos + 0x02];
			trk->trmStrength = songData[inPos + 0x03];
			trk->trmVolScale = songData[inPos + 0x04];
			trk->chnFlags |= 0x100;	// enable tremolo
			inPos += 0x05;
			break;
		case 0x99:	// FM channel register write
			{
				UINT8 reg = songData[inPos + 0x01];
				UINT8 data = songData[inPos + 0x02];
				printf("Track %u: FM Chn Reg Write (Reg 0x%02X, Data 0x%02X) at position 0x%04X\n",
						trkInf->id, reg, data, inPos);
				inPos += 0x03;
			}
			break;
		case 0x9A:	// SysEx command
			if (chnMode == 0x00)	// inline FM instrument
			{
				inPos += 0x1A;
			}
			else if (chnMode == 0x01)	// use global SSG instrument
			{
				WriteEvent(fInf, MTS, 0xC0, songData[inPos + 0x01], 0x00);
				inPos += 0x02;
			}
			else if (chnMode == 0x02)	// set OPNA Rhythm master volume
			{
				WriteEvent(fInf, MTS, 0xB0, 0x27, (songData[inPos + 0x01] & 0x3F) * 2);
				inPos += 0x02;
			}
			else if (chnMode == 0x10)	// inline FM instrument
			{
				UINT32 syxPos;
				UINT32 dataLen;
				
				for (syxPos = inPos + 0x01; syxPos < songLen; syxPos ++)
				{
					if (songData[syxPos] == 0xF7)
					{
						syxPos ++;
						break;
					}
				}
				dataLen = syxPos - (inPos + 0x01);
				
				WriteLongEvent(fInf, MTS, songData[inPos + 0x01], dataLen - 1, &songData[inPos + 0x02]);
				inPos += 0x01 + dataLen;
			}
			else
			{
				inPos += 0x02;
			}
			break;
		case 0x9B:	// set Marker Flag
			printf("Track %u: Set Marker Flag at position 0x%04X\n", trkInf->id, prevPos);
			WriteEvent(fInf, MTS, 0xB0, 0x6D, 0x7F);
			sprintf(tempStr, "Flag = %s", "set");
			WriteMetaEvent(fInf, MTS, 0x06, strlen(tempStr), tempStr);
			inPos += 0x01;
			break;
		case 0x9C:	// clear Marker Flag
			printf("Track %u: Clear Marker Flag at position 0x%04X\n", trkInf->id, prevPos);
			sprintf(tempStr, "Flag = %s", "clear");
			WriteMetaEvent(fInf, MTS, 0x06, strlen(tempStr), tempStr);
			WriteEvent(fInf, MTS, 0xB0, 0x6D, 0x00);
			inPos += 0x01;
			break;
		default:
			printf("Error Track %u: Unhandled command 0x%02X at position 0x%04X!\n", trkInf->id, cmdType, prevPos);
			WriteEvent(fInf, MTS, 0xB0, 0x70, cmdType & 0x7F);
			WriteEvent(fInf, MTS, 0xB0, 0x26, songData[inPos + 0x01]);
			trkEnd = 1;
			break;
		}	// end if (cmdType >= 0x80) / switch(cmdType)
	}	// end while(! trkEnd)
	if (trk->lastNote != 0xFF)
		WriteEvent(fInf, MTS, 0x90, (trk->lastNote + trk->noteTransp) & 0x7F, 0x00);
	
	return 0x00;
}

static UINT8 LookAheadCommand(UINT32 songLen, const UINT8* songData, UINT32 startPos, UINT8 cmd, UINT8 chnMode)
{
	UINT32 inPos = startPos;
	
	if (! TIE_LOOKAHEAD)
		return songData[inPos] == cmd;
	
	// advanced look-ahead, fixes fading sawtooth lead in VT_16_MD.M_, bar 15
	while(inPos < songLen)
	{
		if (songData[inPos] == cmd)
			return 1;	// found the searched command
		
		if (songData[inPos] < 0x80)
			return 0;	// found note/delay - stop looking further ahead
		switch(songData[inPos])
		{
		case 0x80:	// Loop Start
		case 0x81:	// Loop Exit
		case 0x82:	// Loop End
		case 0x8B:	// Jump
			return 0;	// stop at loop or jump
		case 0x83:	// Pitch Bend / no-attack mode
		case 0x9B:	// set Marker Flag
		case 0x9C:	// clear Marker Flag
			inPos += 0x01;
			break;
		case 0x85:	// Song Tempo
		case 0x8C:	// Pan
		case 0x8D:	// Channel Volume
		case 0x8E:	// Channel Volume Accumulation
		case 0x90:	// Instrument
		case 0x91:	// set SSG noise frequency
		case 0x93:	// set SSG noise mode
		case 0x94:	// set Marker
		case 0x96:	// set Note Velocity
			inPos += 0x02;
			break;
		case 0x86:	// Detune Set
		case 0x87:	// Note Duration Modifier
		case 0x89:	// Portamento
		case 0x8A:	// Effect Disable
		case 0x95:	// Pitch Slide
		case 0x97:	// Control Change
		case 0x99:	// FM channel register write
			inPos += 0x03;
			break;
		case 0x88:	// Software Vibrato
		case 0x98:	// Software Tremolo
			inPos += 0x05;
			break;
		case 0x92:	// set Volume Envelope
			inPos += 0x07;
			break;
		case 0x9A:	// SysEx command
			if (chnMode == 0x00)	// inline FM instrument
				inPos += 0x1A;
			else if (chnMode == 0x01)	// use global SSG instrument
				inPos += 0x02;
			else if (chnMode == 0x02)	// set OPNA Rhythm master volume
				inPos += 0x02;
			else if (chnMode == 0x10)	// inline FM instrument
			{
				inPos += 0x01;
				for (; inPos < songLen; inPos ++)
				{
					if (songData[inPos] == 0xF7)
					{
						inPos ++;
						break;
					}
				}
			}
			else
				inPos += 0x02;
			break;
		}
	}
	
	return 0;	// not found
}

static UINT8 PreparseTsdTrack(UINT32 songLen, const UINT8* songData, TRK_INF* trkInf, UINT8 mode)
{
	UINT32 inPos;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT8 loopIdx;
	UINT32 loopPos[8];
	UINT32 loopTick[8];
	UINT16 loopMax[8];
	UINT16 loopCnt[8];
	UINT8 chnFlags;
	UINT8 lastNote;
	UINT16 ctrlUse;
	UINT16 useFlags;
	
	if (! mode)
	{
		trkInf->tickCnt = 0;
		trkInf->loopOfs = 0x00;
		trkInf->useFlags = 0x0000;
	}
	trkInf->loopTick = 0;
	
	if (trkInf->startOfs >= songLen)
		return 0x01;
	
	inPos = trkInf->startOfs;
	
	trkEnd = 0;
	loopIdx = 0x00;
	chnFlags = 0x00;
	ctrlUse = 0x00;
	useFlags = 0x0000;
	lastNote = 0xFF;
	
	while(inPos < songLen && ! trkEnd)
	{
		if (mode && inPos == trkInf->loopOfs)
			break;
		cmdType = songData[inPos];
		if (cmdType < 0x80)
		{
			UINT16 noteDelay = songData[inPos + 0x01];
			inPos += 0x02;
			if (noteDelay == 0xFF)
			{
				noteDelay = ReadLE16(&songData[inPos]);
				inPos += 0x02;
			}
			if (cmdType < 0x7F)
			{
				if (chnFlags & 0x01)
				{
					if (cmdType != lastNote)
						useFlags |= 0x01;	// requires pitch bend
				}
				if (! (ctrlUse & 0x01))
				{
					ctrlUse |= 0x01;
					useFlags |= (ctrlUse & 0xFFF8);
				}
			}
			lastNote = cmdType;
			chnFlags &= ~0x01;
			if (! mode)
				trkInf->tickCnt += noteDelay;
			else
				trkInf->loopTick += noteDelay;
		}
		else switch(cmdType)
		{
		case 0x9B:	// set Marker Flag
		case 0x9C:	// clear Marker Flag
			inPos += 0x01;
			break;
		case 0x85:	// Song Tempo
		case 0x90:	// Instrument
		case 0x91:	// set SSG noise frequency
		case 0x93:	// set SSG noise mode
		case 0x94:	// set Marker
		case 0x96:	// set Note Velocity
			inPos += 0x02;
			break;
		case 0x87:	// Note Duration Modifier
		case 0x8A:	// Effect Disable
		case 0x99:	// write OPN register
			inPos += 0x03;
			break;
		case 0x83:	// Pitch Bend / no-attack mode
			chnFlags |= 0x01;	// set Pitch Bend flag
			inPos += 0x01;
			break;
		case 0x88:	// Software Vibrato
			useFlags |= 0x01;	// requires pitch bend
			inPos += 0x05;
			break;
		case 0x86:	// Detune Set
			useFlags |= 0x01;	// requires pitch bend
			ctrlUse |= 0x100;	// uses pitch bend
			inPos += 0x03;
			break;
		case 0x89:	// Portamento
		case 0x95:	// Pitch Slide
			useFlags |= 0x01;	// requires pitch bend
			inPos += 0x03;
			break;
		case 0x8C:	// Pan
			ctrlUse |= 0x20;	// uses Pan
			inPos += 0x02;
			break;
		case 0x8D:	// Channel Volume
		case 0x8E:	// Channel Volume Accumulation
			ctrlUse |= 0x08;	// uses Channel Volume
			inPos += 0x02;
			break;
		case 0x92:	// set Volume Envelope
			inPos += 0x07;
			break;
		case 0x97:	// Control Change
			if (songData[inPos + 0x01] == 0x07)
				ctrlUse |= 0x10;	// uses CC Main Volume
			else if (songData[inPos + 0x01] == 0x0A)
				ctrlUse |= 0x20;	// uses Pan
			else if (songData[inPos + 0x01] == 0x0B)
				ctrlUse |= 0x08;	// uses Channel Volume
			else if (songData[inPos + 0x01] == 0x5B)
				ctrlUse |= 0x40;	// uses Reverb
			else if (songData[inPos + 0x01] == 0x5D)
				ctrlUse |= 0x80;	// uses Chorus
			inPos += 0x03;
			break;
		case 0x98:	// Tremolo
			inPos += 0x05;
			break;
		case 0x9A:	// SysEx command
			inPos += 0x01;
			for (; inPos < songLen; inPos ++)
			{
				if (songData[inPos] == 0xF7)
				{
					inPos ++;
					break;
				}
			}
			break;
		case 0x80:	// Loop Start
			{
				UINT8 loopTimes = songData[inPos + 0x01];
				UINT8 loopCntr = songData[inPos + 0x02];
				inPos += 0x03;
				
				if (loopIdx >= 8)
					break;
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopMax[loopIdx] = loopTimes;
				loopCnt[loopIdx] = loopCntr;
				loopIdx ++;
			}
			break;
		case 0x81:	// Loop Exit
			{
				INT16 exitOfs = (INT16)ReadLE16(&songData[inPos + 0x01]);
				inPos += 0x03;
				
				if (loopIdx == 0)
					break;
				loopIdx --;
				
				if (loopCnt[loopIdx] == loopMax[loopIdx] - 1)
					inPos += exitOfs;
				else
					loopIdx ++;
			}
			break;
		case 0x82:	// Loop End
			{
				UINT8 takeLoop = 0;
				INT16 loopOfs = (INT16)ReadLE16(&songData[inPos + 0x01]);
				inPos += 0x03;
				
				if (loopIdx == 0)
					break;
				loopIdx --;
				
				loopCnt[loopIdx] ++;
				if (loopCnt[loopIdx] < loopMax[loopIdx])
				{
					inPos += loopOfs + 0x01;
					loopIdx ++;
				}
			}
			break;
		case 0x8B:	// Jump
			{
				INT16 jumpOfs = (INT16)ReadLE16(&songData[inPos + 0x01]);
				inPos += 0x03;
				if (! jumpOfs)
				{
					trkEnd = 1;	// null offset = track end
				}
				else
				{
					inPos += jumpOfs;
					if (jumpOfs < 0)
					{
						trkInf->loopOfs = inPos;
						trkInf->loopTick = trkInf->tickCnt;
						trkEnd = 1;
					}
				}
			}
			break;
		default:
			trkEnd = 1;
			break;
		}	// end switch(cmdType)
	}	// end while(! trkEnd)
	if (! mode)
		trkInf->useFlags = useFlags;
	
	return 0x00;
}


INLINE INT32 NoteFrac2PitchBend(INT16 noteTransp, INT32 noteFrac)
{
	if (! HIGH_PREC_PB)
	{
		noteFrac += noteTransp * 0x30;
		return noteFrac * 683 / 0x30;	// original driver formula
	}
	else
	{
		noteFrac += noteTransp * 0x30000;
		if (noteFrac < 0)	// add 0.5 for better rounding
			noteFrac -= (PB_RANGE * 0x18 / 2);
		else
			noteFrac += (PB_RANGE * 0x18 / 2);
		//return noteFrac * 0x2000 / PB_RANGE / 0x30000;	// slightly more accurate
		return noteFrac / PB_RANGE / 0x18;	// optimized
	}
}

static void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, INT32 pbVal)
{
	INT32 pbFinal = (INT32)0x2000 + pbVal;
	if (! ED4_MODE)
	{
		if (pbFinal < 0x0000)
			pbFinal = 0x0000;
		else if (pbFinal > 0x3FFF)
			pbFinal = 0x3FFF;
	}
	else
	{
		// The slightly older sound driver version from Legend of Heroes IV performs no boundary checking.
		// This may result in it sending garbage pitch bend parameter bytes (>= 0x80).
		// The PB commands with invalid values seem to get discarded at some point and have no audible effect,
		// so let's just discard them for the conversion.
		// ED447.M_, track "MIDI 7", bar 17 requires this.
		if (pbFinal < 0x0000 || pbFinal > 0x3FFF)
			return;
	}
	
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

INLINE UINT8 Note_OPNARhy2MidiDrum(UINT8 note)
{
	// 0 = kick, 1 = snare, 2 = cymbal, 3 = hi-hat, 4 = tom, 5 = rim
	static const UINT8 OPNA_RHYTHM_NOTES[6] = {0x24, 0x26, 0x35, 0x2A, 0x2D, 0x25};
	UINT8 rhyNote = (note % 12) / 2;
	return OPNA_RHYTHM_NOTES[rhyNote];
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}
