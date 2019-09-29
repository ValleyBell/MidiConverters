// TGL FMP -> Midi Converter
// -------------------------
// Written by Valley Bell, 27 August 2017
// based on Twinkle Soft -> Midi Converter
// Updated with FMP v2 support and module-specific commands on 06 August 2018
// Updated with correct tempo calculation and disassembly information on 10 August 2019
// Updated with FMP v1 support on 01 September 2019
//
// known games and driver versions:
//	- Appare Den: Fukuryuu no Sho: FMP v3.95b (code says "3.95", but it's newer than Briganty)
//	- Briganty: The Roots of Darkness: FMP v3.95
//	- Edge: FMP v2.1
//	- Farland Story: Tooikuni no Monogatari: FMP v3.04a
//	- Farland Story 2: Arc no Ensei: FMP v3.04a
//	- Farland Story 3: Tenshi no Namida: FMP v3.04a
//	- Farland Story 4: Hakugin no Tsubasa: FMP v3.04a
//	- Farland Story 5: Daichi no Kizuna: FMP v3.05
//	- Farland Story 6: Kamigami no Isen: FMP v3.05
//	- Farland Story 7: Shishi Ou no Akashi: FMP v3.05
//	- Harlem Blade: The Greatest of All Time: FMP v3.95
//	- Kisoushinden Genkaizer: FMP v3.05
//	- Steam-Heart's: FMP v3.04
//	- Sword Dancer: FMP v1 (MIDIBS.COM v1)
//	- Sword Dancer: Goddess of Evil Blade: FMP v3.04a
//	- Sword Dancer Zoukango: FMP v1 (MIDI.COM v2)
//	- Sword Dancer Zoukango 93: FMP v2.2b
//	- V.G.: Variable Geo: FMP v3.04
//	- V.G.: Variable Geo [demo]: FMP v3.0
//	- V.G. II: The Bout of Cabalistic Goddess: FMP v3.05
//	- V.G. II: The Bout of Cabalistic Goddess [demo]: FMP v3.04a

// Misc notes:
//	- Header:
//		FMP v1 begins right with the 28 track pointers. FMP v2/3 have an additional 4-byte header with the first byte being a version number.
//		FMP v2 has 18 track pointers.
//		FMP v3 has 20 track pointers. However, the file header has space for 28 tracks reserved.
//	- Timing:
//		FMP v2.x doesn't support the OPN Timer for song timing. (command 0x82 is 5 bytes long)
//		FMP v3.0 (used by V.G. demo) supports *only* the OPN Timer for song timing. (command 0x82 is 2 bytes long)
//		FMP v3.04 and later supports OPN and PC98 timers for song timing. (command 0x82 is 6 bytes long)
//	- Commands:
//		FMP v2.1 supports commands 0x80 .. 0xB4. (0xB5 is missing for some reason)
//		FMP v3.04 adds commands 0xB6/0xB7; only in this version module specific commands (0x93..0xAA, 0xB0..0xB5) don't work
//		FMP v3.05 adds command 0xB8
//		FMP v3.95b adds command 0xB9

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
} TRK_INF;

typedef struct _event_list
{
	UINT32 evtAlloc;
	UINT32 evtCount;
	UINT32 (*data)[2];	// data[][0] = tick time, data[][1] = event data
} EVENT_LIST;


#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


UINT8 Fmp2Mid(UINT16 SongLen, const UINT8* SongData);
static void AddEventToList(EVENT_LIST* evtList, UINT32 tick, UINT32 data);
static int EvtList_ItemCompare(const void* evtPtrA, const void* evtPtrB);
static void PreparseFmp(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, EVENT_LIST* gblEvts);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 PCTimer2MidTempo(UINT32 period, UINT32 baseClock);
INLINE UINT32 YMTimerB2MidTempo(UINT8 timerB, UINT32 baseClock);

INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT32 ReadLE24(const UINT8* data);
INLINE UINT32 ReadLE32(const UINT8* data);


static const UINT8 MIDI_MOD_MASKS[0x05] =
{
	0x00,	// omit module-specific events
	1<<0,	// MT-32 mode
	1<<1,	// CM-64 mode
	1<<2,	// SC-55 mode
	0xFF,	// keep module-specific events
};

#define PC98_BASECLK_FM		1996800	// FM base clock
#define PC98_BASECLK_5MHZ	2457600	// base clock in 5 MHz mode according to Neko Project 2
#define PC98_BASECLK_8MHZ	1996800	// base clock in 8 MHz mode according to Neko Project 2

// clock values used for creating FMP v2 songs (reverse-engineered using songs from Edge)
#define FMP2_BASECLK_5MHZ	2458000
#define FMP2_BASECLK_8MHZ	1998000
// clock values used for creating FMP v3 songs (reverse-engineered using songs from V.G. II)
#define FMP3_BASECLK_FM		2005632
#define FMP3_BASECLK_5MHZ	2467584
#define FMP3_BASECLK_8MHZ	2005632


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT16 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

static UINT16 MIDI_RES = 0;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 FIX_SYX_CHKSUM = 0;
static UINT8 USE_PC98_CLK = 0;
static UINT8 NO_LOOP_DELAY = 0;

static UINT8 FMP_VER = 3;
static UINT8 MIDI_MODE = 0x03;
static UINT8 MODULE_MASK = 0x00;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("TGL FMP -> Midi Converter\n-------------------------\n");
	if (argc < 3)
	{
		printf("Usage: Fmp2Mid.exe [options] input.bin output.mid\n");
		printf("Options:\n");
		printf("    -Ver n      set FMP version (default: %u)\n", FMP_VER);
		printf("                1/2/3 - version 1.x / 2.x / 3.x\n");
		printf("                30 - version 3.0 (for V.G. demo)\n");
		printf("    -Module n   set MIDI module mode (0 to 4, default: %u)\n", MIDI_MODE);
		printf("                0 - omit module-specific events\n");
		printf("                1 - convert MT-32 specific events\n");
		printf("                2 - convert CM-64 specific events\n");
		printf("                3 - convert SC-55 specific events\n");
		printf("                4 - convert all module-specific events\n");
		printf("    -Loops n    Loop each track at least n times. (default: %u)\n", NUM_LOOPS);
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -FixSyxSum  Fix an erroneous Roland SysEx checksum of 0x80 to 0x00.\n");
		printf("    -UsePC98Clk Use actual PC-98 clock for tempo calculation.\n");
		printf("                By default, clock values reversed from existing songs are used that\n");
		printf("                result in accurate BPM values and were likely used by the devs.\n");
		printf("    -NoLoopDly  ignore 1-tick-delay after Loop Start commands (some games do that)\n");
		printf("Note: Only MIDI-based FMP songs are supported. OPN(A) songs don't work.\n");
		return 0;
	}
	
	MidiDelayCallback = MidiDelayHandler;
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Ver"))
		{
			argbase ++;
			if (argbase < argc)
			{
				FMP_VER = (UINT8)strtoul(argv[argbase], NULL, 0);
				if (! FMP_VER)
					FMP_VER = 3;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "Module"))
		{
			argbase ++;
			if (argbase < argc)
				MIDI_MODE = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (! stricmp(argv[argbase] + 1, "Loops"))
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
		else if (! stricmp(argv[argbase] + 1, "FixSyxSum"))
			FIX_SYX_CHKSUM = 1;
		else if (! stricmp(argv[argbase] + 1, "UsePC98Clk"))
			USE_PC98_CLK = 1;
		else if (! stricmp(argv[argbase] + 1, "NoLoopDly"))
			NO_LOOP_DELAY = 1;
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 2)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	if (MIDI_MODE >= sizeof(MIDI_MOD_MASKS))
		MIDI_MODE = 0x00;
	MODULE_MASK = MIDI_MOD_MASKS[MIDI_MODE];
	
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
	TRK_INF trkInf[28];
	TRK_INF* tempTInf;
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT16 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 cmdModMask;
	UINT8 LoopIdx;
	UINT16 mstLoopCount;
	UINT16 LoopCount[8];
	UINT16 LoopStPos[8];
	UINT16 LoopEndPos[8];
	UINT8 tickMult;
	UINT8 tickDelay;
	EVENT_LIST gblEvts;
	UINT32 curGblEvt;
	UINT32 curTrkTick;
	UINT8 dlyIgnore;
	
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
	
	if (FMP_VER >= 2 && SongData[0x00] != 0x02)
	{
		printf("Unsupported FMP format!\n");
		MidData = NULL;
		MidLen = 0x00;
		return 0x80;
	}
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	sysExAlloc = 0x20;
	sysExLen = 0x00;
	sysExData = (UINT8*)malloc(sysExAlloc);
	gblEvts.evtAlloc = 0;
	gblEvts.evtCount = 0;
	gblEvts.data = NULL;
	
	if (FMP_VER == 1)
	{
		MIDI_RES = 24;
		trkCnt = 28;	// FMP v1 has space for 28 tracks reserved (FM+MIDI?)
		inPos = 0x00;	// FMP v1 starts right with the track pointers
	}
	else if (FMP_VER == 2)
	{
		MIDI_RES = 48;
		trkCnt = 18;	// FMP v2 has 18 MIDI tracks
		inPos = 0x04;	// skip FM/MIDI mode, FM instrument count / FM instrument data pointer
	}
	else
	{
		MIDI_RES = 48;
		trkCnt = 20;	// FMP v3 has 20 MIDI tracks (though the header has space reserved for 28)
		inPos = 0x04;	// skip FM/MIDI mode, FM instrument count / FM instrument data pointer
	}
	
	tempLng = 0xFFFF;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
	{
		if (inPos >= tempLng)	// just for safety
		{
			trkCnt = curTrk;
			break;
		}
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = ReadLE16(&SongData[inPos]);
		if (tempLng > tempTInf->startOfs)
			tempLng = tempTInf->startOfs;
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		
		PreparseFmp(SongLen, SongData, tempTInf, &gblEvts);
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	qsort(gblEvts.data, gblEvts.evtCount, sizeof(UINT32) * 2, &EvtList_ItemCompare);
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, 1);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		inPos = trkInf[curTrk].startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		trkEnd = 0;
		LoopIdx = 0x00;
		mstLoopCount = 0;
		MTS.midChn = curTrk;
		curNoteVol = 0x00;	// values other than 0 cause stray notes in VG2_07.MGS/MG2
		RunNoteCnt = 0;
		
		tickMult = 1;
		curGblEvt = 0;
		curTrkTick = 0;
		dlyIgnore = 0;
		while(inPos < SongLen)
		{
			while(curGblEvt < gblEvts.evtCount && gblEvts.data[curGblEvt][0] <= curTrkTick)
			{
				curCmd = (gblEvts.data[curGblEvt][1] >> 0) & 0xFF;
				if (curCmd == 0xAD)	// Tick Multiplier
				{
					// The Tick Multiplier changes the global timing and the effect is immediate.
					// The delay code only allows for changes per-event, so we need to fix the timing.
					tempLng = curTrkTick - gblEvts.data[curGblEvt][0];
					MTS.curDly -= tempLng * tickMult;
					//if (gblEvts.data[curGblEvt][0] != curTrkTick)
					//	printf("Warning: Mid-Delay Tick Multiplier Change on track %u at %04X\n", curTrk, inPos);
					
					tickMult = (gblEvts.data[curGblEvt][1] >> 8) & 0xFF;
					
					MTS.curDly += tempLng * tickMult;
				}
				curGblEvt ++;
			}
			
			curCmd = SongData[inPos];
			if (curCmd < 0x80)
			{
				curNoteLen = (UINT16)SongData[inPos + 0x01] * tickMult;
				inPos += 0x02;
				
				curNote = curCmd;
				if (! curNote || ! curNoteLen)	// rest for note 0 or length 0 (confirmed via disassembly)
					curNote = 0xFF;
				
				CheckRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes);
				for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
				{
					if (RunNotes[tempByt].note == curNote)
					{
						RunNotes[tempByt].remLen = MTS.curDly + curNoteLen;
						break;
					}
				}
				if (tempByt >= RunNoteCnt && curNote != 0xFF)
				{
					WriteEvent(&midFileInf, &MTS, 0x90, curNote, curNoteVol);
					AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
									MTS.midChn, curNote, 0x00, curNoteLen);	// FMP sends 8# note 00
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
					{
						UINT8 tmrValYM;		// YM2203 timer value
						UINT16 tmrPeriod5;	// timer value for 5 MHz mode
						UINT16 tmrPeriod8;	// timer value for 8 MHz mode
						UINT32 midiTempo;
						//char* tempBuf = (char*)sysExData;
						
						if (FMP_VER == 30)
						{
							// weird variant of FMP v3.0, found in V.G. demo
							//	- inPos+1 = OPN Timer B value
							tmrValYM = SongData[inPos + 0x01];
							inPos += 0x02;
						
							//tempLng = sprintf(tempBuf, "YM Tempo: %.3f BPM", 60000000.0 / YMTimerB2MidTempo(tmrValYM, FMP3_BASECLK_FM));
							//WriteMetaEvent(&midFileInf, &MTS, 0x06, tempLng, tempBuf);
							
							if (USE_PC98_CLK)
								midiTempo = YMTimerB2MidTempo(tmrValYM, PC98_BASECLK_FM);
							else
								midiTempo = YMTimerB2MidTempo(tmrValYM, FMP3_BASECLK_FM);
						}
						else if (FMP_VER == 3)
						{
							// FMP v3:
							//	- inPos+1 = OPN Timer B value
							//	- inPos+2..3 = uPD8253-5 timer value (5 MHz mode)
							//	- inPos+4..5 = uPD8253-5 timer value (8 MHz mode)
							// The FM Timer B value results in approximate the same tempo, but due to rounding and
							// the lower resolution of the timer, it's always a bit faster.
							tmrValYM = SongData[inPos + 0x01];
							tmrPeriod5 = ReadLE16(&SongData[inPos + 0x02]);
							tmrPeriod8 = ReadLE16(&SongData[inPos + 0x04]);
							inPos += 0x06;
						
							//tempLng = sprintf(tempBuf, "YM Tempo: %.3f BPM", 60000000.0 / YMTimerB2MidTempo(tmrValYM, FMP3_BASECLK_FM));
							//WriteMetaEvent(&midFileInf, &MTS, 0x06, tempLng, tempBuf);
							//tempLng = sprintf(tempBuf, "T5 Tempo: %.3f BPM", 60000000.0 / PCTimer2MidTempo(tmrPeriod5, FMP3_BASECLK_5MHZ));
							//WriteMetaEvent(&midFileInf, &MTS, 0x06, tempLng, tempBuf);
							//tempLng = sprintf(tempBuf, "T8 Tempo: %.3f BPM", 60000000.0 / PCTimer2MidTempo(tmrPeriod8, FMP3_BASECLK_8MHZ));
							//WriteMetaEvent(&midFileInf, &MTS, 0x06, tempLng, tempBuf);
							
							if (USE_PC98_CLK)
							{
								midiTempo = PCTimer2MidTempo(tmrPeriod5, PC98_BASECLK_5MHZ);
								//midiTempo = PCTimer2MidTempo(tmrPeriod8, PC98_BASECLK_8MHZ);
							}
							else
							{
								midiTempo = PCTimer2MidTempo(tmrPeriod5, FMP3_BASECLK_5MHZ);
								//midiTempo = PCTimer2MidTempo(tmrPeriod8, FMP3_BASECLK_8MHZ);
							}
						}
						else
						{
							// FMP v1/2:
							//	- inPos+1..2 = uPD8253-5 timer value (5 MHz mode)
							//	- inPos+3..4 = uPD8253-5 timer value (8 MHz mode)
							tmrPeriod5 = ReadLE16(&SongData[inPos + 0x01]);
							tmrPeriod8 = ReadLE16(&SongData[inPos + 0x03]);
							inPos += 0x05;
						
							//tempLng = sprintf(tempBuf, "T5 Tempo: %.3f BPM", 60000000.0 / PCTimer2MidTempo(tmrPeriod5, FMP2_BASECLK_5MHZ));
							//WriteMetaEvent(&midFileInf, &MTS, 0x06, tempLng, tempBuf);
							//tempLng = sprintf(tempBuf, "T8 Tempo: %.3f BPM", 60000000.0 / PCTimer2MidTempo(tmrPeriod8, FMP2_BASECLK_8MHZ));
							//WriteMetaEvent(&midFileInf, &MTS, 0x06, tempLng, tempBuf);
							
							if (USE_PC98_CLK)
							{
								midiTempo = PCTimer2MidTempo(tmrPeriod5, PC98_BASECLK_5MHZ);
								//midiTempo = PCTimer2MidTempo(tmrPeriod8, PC98_BASECLK_8MHZ);
							}
							else
							{
								midiTempo = PCTimer2MidTempo(tmrPeriod5, FMP2_BASECLK_5MHZ);
								//midiTempo = PCTimer2MidTempo(tmrPeriod8, FMP2_BASECLK_8MHZ);
							}
						}
						WriteBE32(tempArr, midiTempo);
						WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					}
					break;
				case 0x83:	// Set Note Velocity
					curNoteVol = SongData[inPos + 0x01];
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
					if (FMP_VER >= 3)	// FMP v3
					{
						// The Loop End offset is an absolute file offset that must point at the
						// delay byte *before* the Loop End command in order to work properly.
						// I assume that this is the reason, that most Loop End commands are
						// preceded by the bytes "40 00 00". (dummy note + 0 tick delay)
						// However, none of the files I've seen sets this offset correctly, so I'll just ignore it
						// and handle it the FMP v2 way. (I haven't seen any file use the Loop Exit command though.)
						//LoopEndPos[LoopIdx] = ReadLE16(&SongData[inPos + 0x01]);
						LoopEndPos[LoopIdx] = 0x0000;
						LoopCount[LoopIdx] = SongData[inPos + 0x03];
						inPos += 0x04;
					}
					else	// FMP v2
					{
						// In FMP v2, the Loop End offset is always set when encountering the respective Loop End command.
						// However, it's broken and the driver ends up processing the command byte as delay.
						LoopEndPos[LoopIdx] = 0x0000;
						LoopCount[LoopIdx] = SongData[inPos + 0x01];
						inPos += 0x02;
					}
					LoopStPos[LoopIdx] = inPos;
					
					if (LoopCount[LoopIdx] == 0x00)
					{
						mstLoopCount = 0;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
					}
					LoopIdx ++;
					if (NO_LOOP_DELAY)
						dlyIgnore = 1;
					break;
				case 0x89:	// Loop End
					if (! LoopIdx)
					{
						printf("Warning: Loop End without Loop Start on track %u at %04X\n", curTrk, inPos);
						trkEnd = 1;
						break;
					}
					inPos += 0x01;
					
					LoopIdx --;
					if (! LoopEndPos[LoopIdx])
						LoopEndPos[LoopIdx] = inPos - 0x02;
					if (! LoopCount[LoopIdx])
					{
						// master loop
						mstLoopCount ++;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
						if (mstLoopCount >= trkInf[curTrk].loopTimes)
							break;
					}
					else
					{
						LoopCount[LoopIdx] --;
						if (! LoopCount[LoopIdx])
							break;
					}
					// loop back
					inPos = LoopStPos[LoopIdx];
					LoopIdx ++;
					if (NO_LOOP_DELAY)
						dlyIgnore = 1;
					break;
				case 0x8A:	// Loop Exit
					if (! LoopIdx)
					{
						printf("Warning: Loop Exit without Loop Start on track %u at %04X\n", curTrk, inPos);
						trkEnd = 1;
						break;
					}
					inPos += 0x01;
					
					if (LoopCount[LoopIdx - 1] == 1)
					{
						// output warning, because I have yet to find a file that uses this
						printf("Warning: Loop Exit on track %u at %04X\n", curTrk, inPos);
						inPos = LoopEndPos[LoopIdx - 1];	// jump to Loop End command
					}
					break;
				case 0x8B:	// Set Pan
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0x8C:	// send Roland SysEx Data
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
					if (FIX_SYX_CHKSUM && sysExLen > 1)
					{
						if (sysExData[sysExLen - 1] == 0x80)
							sysExData[sysExLen - 1] = 0x00;
					}
					if (sysExData[sysExLen] == 0xF7)
						sysExLen ++;	// count end SysEx End command
					tempByt = 1;
					if (1)
					{
						// comfort option: ignore SysEx messages that don't fit the device
						// The actual driver sends them regardless.
						// (Commands A8..AA have to be used for device-specific messages.)
						if ((MODULE_MASK == 0x01) && (sysExData[0x02] & 0xF0) != 0x10)	// check for MT-32 ID (0x16)
							tempByt = 0;
						else if ((MODULE_MASK == 0x04) && (sysExData[0x02] & 0xF0) != 0x40)	// check for SC-55 ID (0x42/0x45)
							tempByt = 0;
					}
					if (tempByt)
						WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen, sysExData);
					inPos += sysExLen;
					break;
				case 0x8D:	// send Raw Data
					printf("Sending raw MIDI data on track %u at %04X\n", curCmd, curTrk, inPos);
					for (sysExLen = 0x01; inPos + sysExLen < SongLen; sysExLen ++)
					{
						if (SongData[inPos + sysExLen] == 0xFF)
							break;
					}
					sysExLen ++;
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
				case 0x91:	// set Marker 1 value
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6C, SongData[inPos + 0x01] & 0x7F);
					inPos += 0x02;
					break;
				case 0x92:	// set Marker 2 value (used for game sync in Sword Dancer: BGMA.MD)
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6D, SongData[inPos + 0x01] & 0x7F);
					inPos += 0x02;
					break;
				case 0x93:	// MT-32 MIDI channel
				case 0x94:	// CM-64 MIDI channel
				case 0x95:	// SC-55 MIDI channel
					cmdModMask = 1 << (curCmd - 0x93);
					if (MODULE_MASK & cmdModMask)
					{
						MTS.midChn = SongData[inPos + 0x01] & 0x0F;
						WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
					}
					inPos += 0x02;
					break;
				case 0x96:	// MT-32 Expression
				case 0x97:	// CM-64 Expression
				case 0x98:	// SC-55 Expression
					cmdModMask = 1 << (curCmd - 0x96);
					if (MODULE_MASK & cmdModMask)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x99:	// MT-32 Volume
				case 0x9A:	// CM-64 Volume
				case 0x9B:	// SC-55 Volume
					cmdModMask = 1 << (curCmd - 0x99);
					if (MODULE_MASK & cmdModMask)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0x9C:	// MT-32 Stop Track
				case 0x9D:	// CM-64 Stop Track
				case 0x9E:	// SC-55 Stop Track
					//printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					cmdModMask = 1 << (curCmd - 0x9C);
					// The masking here is slightly different, because I want to:
					//  1. stop the track when MIDI_MODE == command_mode
					//  2. stop the track when MIDI_MODE == none
					//  3. continue when MIDI_MODE == all
					if (! (MODULE_MASK & ~cmdModMask))
						trkEnd = 1;
					inPos += 0x01;
					break;
				case 0x9F:	// MT-32 MIDI controller
				case 0xA0:	// CM-64 MIDI controller
				case 0xA1:	// SC-55 MIDI controller
					cmdModMask = 1 << (curCmd - 0x9F);
					if (MODULE_MASK & cmdModMask)
						WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xA2:	// MT-32 instrument
				case 0xA3:	// CM-64 instrument
				case 0xA4:	// SC-55 instrument
					cmdModMask = 1 << (curCmd - 0xA2);
					if (MODULE_MASK & cmdModMask)
						WriteEvent(&midFileInf, &MTS, 0xC0, SongData[inPos + 0x01], 0x00);
					inPos += 0x02;
					break;
				case 0xA5:	// MT-32 Pan
				case 0xA6:	// CM-64 Pan
				case 0xA7:	// SC-55 Pan
					cmdModMask = 1 << (curCmd - 0xA5);
					if (MODULE_MASK & cmdModMask)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, SongData[inPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xA8:	// send MT-32 SysEx Data
				case 0xA9:	// send CM-64 SysEx Data
				case 0xAA:	// send SC-55 SysEx Data
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
					if (FIX_SYX_CHKSUM && sysExLen > 1)
					{
						if (sysExData[sysExLen - 1] == 0x80)
							sysExData[sysExLen - 1] = 0x00;
					}
					if (sysExData[sysExLen] == 0xF7)
						sysExLen ++;	// count end SysEx End command
					cmdModMask = 1 << (curCmd - 0xA8);
					if (MODULE_MASK & cmdModMask)
						WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen, sysExData);
					inPos += sysExLen;
					break;
				case 0xAB:	// increase Note Velocity
					curNoteVol ++;
					inPos += 0x01;
					break;
				case 0xAC:	// decrease Note Velocity
					curNoteVol --;
					inPos += 0x01;
					break;
				case 0xAD:	// set global Tick Multiplier
					// The tick multiplier is handled separately using gblEvts.
					//printf("Track %u at %04X: Set Tick Multiplier = %u\n", curTrk, inPos, tickMult);
					inPos += 0x02;
					break;
				case 0xAE:	// set OPN Timer A/B
					printf("Setting OPN timer on track %u at %04X\n", curTrk, inPos);
					if (FMP_VER >= 3)	// FMP v3
					{
						// set OPN Timer A
						// SongData[inPos + 0x01] -> register 24h
						// SongData[inPos + 0x02] -> register 25h
						inPos += 0x03;
					}
					else
					{
						// set OPN Timer B
						// SongData[inPos + 0x01] -> register 26h
						inPos += 0x02;
					}
					break;
				case 0xAF:	// some sort of Track End command?
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					inPos = 0xFFFF - 1;	// this is exactly what the driver does
					break;
				case 0xB0:	// MT-32 Pitch Bend
				case 0xB1:	// CM-64 Pitch Bend
				case 0xB2:	// SC-55 Pitch Bend
					cmdModMask = 1 << (curCmd - 0xB0);
					if (MODULE_MASK & cmdModMask)
						WriteEvent(&midFileInf, &MTS, 0xE0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xB3:	// SC-55 something (yes, SC-55 first)
				case 0xB4:	// CM-64 something
				case 0xB5:	// MT-32 something
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					cmdModMask = 1 << (0xB5 - curCmd);
					if (MODULE_MASK & cmdModMask)
					{
						// It sets a global variable that defaults to 0x0E and is used to send a Note Off.
						//WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					}
					inPos += 0x02;
					break;
				case 0xB6:
					// set unknown flag to 0
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					inPos += 0x01;
					break;
				case 0xB7:
					// set unknown flag to 1
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					inPos += 0x01;
					break;
				case 0xB8:	// send SysEx Data
					// for generic SysEx Data (no enforced Roland ID)
					for (sysExLen = 0x00; inPos + 0x01 + sysExLen < SongLen; sysExLen ++)
					{
						if (sysExAlloc <= sysExLen)
						{
							sysExAlloc *= 2;
							sysExData = (UINT8*)realloc(sysExData, sysExAlloc);
						}
						sysExData[sysExLen] = SongData[inPos + 0x01 + sysExLen];
						if (sysExData[sysExLen] == 0xF7)
							break;
					}
					if (FIX_SYX_CHKSUM && sysExLen > 1)
					{
						if (sysExData[sysExLen - 1] == 0x80)
							sysExData[sysExLen - 1] = 0x00;
					}
					if (sysExData[sysExLen] == 0xF7)
						sysExLen ++;	// count end SysEx End command
					WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen, sysExData);
					inPos += 0x01 + sysExLen;
					break;
				case 0xB9:
					// Note: The driver's implementation and how the sequences use it differ.
#if 0
					// GS variation sound
					// This is how the driver implements it.
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x00, SongData[inPos + 0x01]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x20, 0x00);
#else
					// reset Bank Select
					// This is how it seems to be used by the sequences in "Appare Den". (the only game to use support)
					// It is used in: OR_12.MGS, OR_18.MGS, OR_24.MGS, OR_27.MGS, OR_29.MGS
					WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x01], 0x00);
#endif
					inPos += 0x02;
					break;
				case 0xFF:	// Track End
					trkEnd = 1;
					inPos += 0x01;
					break;
				default:
					printf("Unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					//WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
			}
			if (trkEnd)
				break;
			
			tickDelay = SongData[inPos];	inPos ++;
			curTrkTick += tickDelay;
			if (! dlyIgnore)
			{
				MTS.curDly += (UINT16)tickDelay * tickMult;
			}
			else
			{
				printf("Ignoring loop delay of %u %s on track %u at %04X\n", tickDelay, (tickDelay == 1) ? "tick" : "ticks", curTrk, inPos - 1);
				dlyIgnore = 0;
			}
		}
		FlushRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes, 0);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	free(sysExData);	sysExData = NULL;
	
	return 0x00;
}

static void AddEventToList(EVENT_LIST* evtList, UINT32 tick, UINT32 data)
{
	if (evtList->evtCount >= evtList->evtAlloc)
	{
		evtList->evtAlloc += 0x100;
		evtList->data = (UINT32(*)[2])realloc(evtList->data, evtList->evtAlloc * sizeof(UINT32) * 2);
	}
	evtList->data[evtList->evtCount][0] = tick;
	evtList->data[evtList->evtCount][1] = data;
	evtList->evtCount ++;
	
	return;
}

static int EvtList_ItemCompare(const void* evtPtrA, const void* evtPtrB)
{
	const UINT32* evtA = (UINT32*)evtPtrA;
	const UINT32* evtB = (UINT32*)evtPtrB;
	return evtA[0] - evtB[0];
}

static void PreparseFmp(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, EVENT_LIST* gblEvts)
{
	UINT16 inPos;
	UINT16 cmdLen;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 LoopIdx;
	UINT16 LoopCount[8];
	UINT16 LoopStPos[8];
	UINT16 LoopEndPos[8];
	UINT8 tempByt;
	
	trkEnd = 0;
	LoopIdx = 0x00;
	trkInf->loopOfs = 0x0000;
	inPos = trkInf->startOfs;
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
			case 0xAB:	// increase Note Velocity
			case 0xAC:	// decrease Note Velocity
			case 0xB6:
			case 0xB7:
				cmdLen = 0x01;
				break;
			case 0x80:	// Set Instrument
			case 0x81:	// Set Volume
			case 0x83:	// Set Note Velocity
			case 0x84:	// Set Modulation
			case 0x8B:	// Set Pan
			case 0x8E:	// set MIDI Channel
			case 0x8F:	// Set Expression
			case 0x93:	// MT-32 MIDI channel
			case 0x94:	// CM-64 MIDI channel
			case 0x95:	// SC-55 MIDI channel
			case 0x96:	// MT-32 Expression
			case 0x97:	// CM-64 Expression
			case 0x98:	// SC-55 Expression
			case 0x99:	// MT-32 Volume
			case 0x9A:	// CM-64 Volume
			case 0x9B:	// SC-55 Volume
			case 0xA2:	// MT-32 instrument
			case 0xA3:	// CM-64 instrument
			case 0xA4:	// SC-55 instrument
			case 0xA5:	// MT-32 Pan
			case 0xA6:	// CM-64 Pan
			case 0xA7:	// SC-55 Pan
			case 0xB3:	// SC-55 something
			case 0xB4:	// CM-64 something
			case 0xB9:	// GS variation sound
				cmdLen = 0x02;
				break;
			case 0x85:	// Pitch Bend
			case 0x90:	// MIDI Controller
			case 0x9F:	// MT-32 MIDI controller
			case 0xA0:	// CM-64 MIDI controller
			case 0xA1:	// SC-55 MIDI controller
			case 0xB0:	// MT-32 Pitch Bend
			case 0xB1:	// CM-64 Pitch Bend
			case 0xB2:	// SC-55 Pitch Bend
				cmdLen = 0x03;
				break;
			case 0x82:	// Tempo
				if (FMP_VER == 30)	// FMP v3.0
					cmdLen = 0x02;
				else if (FMP_VER == 3)	// FMP v3
					cmdLen = 0x06;
				else	// FMP v2
					cmdLen = 0x05;
				break;
			case 0x88:	// Loop Start
				if (FMP_VER >= 3)	// FMP v3
				{
					LoopEndPos[LoopIdx] = ReadLE16(&SongData[inPos + 0x01]);
					LoopCount[LoopIdx] = SongData[inPos + 0x03];
					cmdLen = 0x04;
				}
				else	// FMP v2
				{
					LoopEndPos[LoopIdx] = 0x0000;
					LoopCount[LoopIdx] = SongData[inPos + 0x01];
					cmdLen = 0x02;
				}
				
				LoopStPos[LoopIdx] = inPos + cmdLen;
				if (LoopCount[LoopIdx] == 0x00)
					trkInf->loopOfs = inPos;
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
				if (! LoopEndPos[LoopIdx])
					LoopEndPos[LoopIdx] = inPos;
				if (LoopCount[LoopIdx])
					LoopCount[LoopIdx] --;
				if (LoopCount[LoopIdx])
				{
					// loop back
					inPos = LoopStPos[LoopIdx];
					cmdLen = 0x00;
					LoopIdx ++;
				}
				break;
			case 0x8A:	// Loop Exit
				if (! LoopIdx)
				{
					trkEnd = 1;
					break;
				}
				inPos += 0x01;
					
				if (LoopCount[LoopIdx - 1] == 1)
					inPos = LoopEndPos[LoopIdx - 1];	// jump to Loop End command
				break;
			case 0x8C:	// send Roland SysEx Data
			case 0xB8:	// send SysEx Data
				for (cmdLen = 0x01; inPos + cmdLen < SongLen; cmdLen ++)
				{
					if (SongData[inPos + cmdLen] == 0xF7)
					{
						cmdLen ++;	// count SysEx End command
						break;
					}
				}
				break;
			case 0x8D:	// send Raw Data
				for (cmdLen = 0x01; inPos + cmdLen < SongLen; cmdLen ++)
				{
					if (SongData[inPos + cmdLen] == 0xFF)
					{
						cmdLen ++;	// count terminator byte
						break;
					}
				}
				break;
			case 0x9C:	// MT-32 Stop Track
			case 0x9D:	// CM-64 Stop Track
			case 0x9E:	// SC-55 Stop Track
				tempByt = 1 << (curCmd - 0x9C);
				if (! (MODULE_MASK & ~tempByt))
					trkEnd = 1;
				cmdLen = 0x01;
				break;
			case 0xA8:	// send MT-32 SysEx Data
			case 0xA9:	// send CM-64 SysEx Data
			case 0xAA:	// send SC-55 SysEx Data
				for (cmdLen = 0x01; inPos + cmdLen < SongLen; cmdLen ++)
				{
					if (SongData[inPos + cmdLen] == 0xF7)
					{
						cmdLen ++;
						break;
					}
				}
				break;
			case 0xAD:	// set global Tick Multiplier
				AddEventToList(gblEvts, trkInf->tickCnt, (curCmd << 0) | (SongData[inPos + 0x01] << 8));
				//printf("Track %u at %04X: Set Tick Multiplier = %u\n", curTrk, inPos, tickMult);
				cmdLen = 0x02;
				break;
			case 0xAE:	// set OPN Timer A/B
				if (FMP_VER >= 3)	// FMP v3
					cmdLen = 0x03;
				else	// FMP v2
					cmdLen = 0x02;
				break;
			case 0xAF:	// ??
				trkEnd = 1;
				break;
			case 0xFF:	// Track End
				trkEnd = 1;
				cmdLen = 0x01;
				break;
			default:
				//printf("Preparser break\n");	getchar();
				return;
			}
		}
		inPos += cmdLen;
		if (trkEnd)
			break;
		
		tempByt = SongData[inPos];
		inPos ++;
		trkInf->tickCnt += tempByt;
		if (! trkInf->loopOfs)
			trkInf->loopTick += tempByt;
	}
	
	return;
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

INLINE UINT32 PCTimer2MidTempo(UINT32 period, UINT32 baseClock)
{
	// BPM = 60 * (baseClock / 2) / period / MIDI_RES;
	// MIDI tempo = 60000000 / BPM
	return (UINT32)((UINT64)1000000 * MIDI_RES * period * 2 / baseClock);
}

INLINE UINT32 YMTimerB2MidTempo(UINT8 timerB, UINT32 baseClock)
{
	UINT16 timerPeriod = (0x100 - timerB) << 4;
	double ticksPerSec = baseClock * 2.0 / (6 * 12 * timerPeriod);
	return (UINT32)(1000000 * MIDI_RES / ticksPerSec + 0.5);
}


INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 ReadLE24(const UINT8* data)
{
	return (data[0x02] << 16) | (data[0x01] <<  8) | (data[0x00] <<  0);
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}
