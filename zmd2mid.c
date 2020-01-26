// ZmuSiC -> Midi Converter
// ------------------------
// Written by Valley Bell, 04/05 August 2018
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


#define FLAG_START_THIS_TICK	0x01
typedef struct running_note
{
	UINT8 midChn;
	UINT8 note;
	UINT16 remLen;
	UINT8 flags;	// bit 0 (01) - was played this tick
} RUN_NOTE;

typedef struct _track_info
{
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT16 chnMode;
} TRK_INFO;


UINT8 Zmd2Mid(UINT16 songLen, const UINT8* songData);
static void PreparseZmd(UINT32 songLen, const UINT8* songData, TRK_INFO* trkInf);
static void GuessLoopTimes(UINT16 TrkCnt, TRK_INFO* trkInf);
static void CheckRunningNotes(FILE_INF* fInf, UINT32* delay);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static void FlushRunningNotes(FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 BPM2Mid(UINT16 valBPM);
INLINE UINT8 BPM2OPMTimerB(UINT16 valBPM);
INLINE UINT32 OPMTimerB2Mid(UINT8 timerVal);

static UINT16 ReadBE16(const UINT8* data);
static UINT32 ReadBE32(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT8 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];

static const char* ZMD_SIG = "\x10ZmuSiC";

static const UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 USE_OPM_TMR = 0;

// TempoBaseCounter = 16 * 4000 * 60000 / (192 * 256)
static UINT32 TEMPO_BASE_RATE = 78125;	// value used by ZmuSiC driver (can be overridden by songs)

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("ZmuSiC -> Midi Converter\n------------------------\n");
	if (argc < 3)
	{
		printf("Usage: zmd2mid.exe input.bin output.mid\n");
		printf("Verified games: Cyber Block Metal Orange EX, Magical Block Carat, Asuka 120%\n");
		printf("Currently only MIDI-based ZMD files are supported.\n");
		// OPMTimer: use OPM Timer B for timing (results in slightly lower BPM)
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
		else if (! _stricmp(argv[argbase] + 1, "OPMTimer"))
			USE_OPM_TMR = 1;
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
	
	retVal = Zmd2Mid(ROMLen, ROMData);
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

UINT8 Zmd2Mid(UINT16 songLen, const UINT8* songData)
{
	TRK_INFO trkInf[20];
	TRK_INFO* tempTInf;
	UINT16 trkCnt;
	UINT16 curTrk;
	UINT16 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 loopIdx;	// loop stack index
	UINT16 loopMax[8];	// maximum number of times to loop
	UINT16 loopCur[8];	// current loop number
	UINT16 mstLoopPos;	// master loop file offset
	UINT16 mstLoopCur;
	
	//UINT32 tempLng;
	UINT16 tempSht;
	INT16 tempSSht;
	UINT8 tempByt;
	UINT8 tempArr[4];
	
	UINT8 curNote;
	UINT8 curNoteLen;
	UINT8 curNoteVol;
	INT16 curPBend;
	
	UINT32 sysExAlloc;
	UINT32 sysExLen;
	UINT8* sysExData;
	
	UINT16 songTempo;
	const char* gameTitle;
	const char* songTitle;
	
	MidData = NULL;
	MidLen = 0x00;
	if (memcmp(&songData[0x00], ZMD_SIG, 0x07))
	{
		printf("Not a ZMD file!\n");
		return 0x80;
	}
	// verified versions: 0x14 (Cyber Block Metal Orange EX, Magical Block Carat)
	if (songData[0x07] > 0x15)
	{
		printf("Unsupported ZMD version %02X!\n", songData[0x07]);
		return 0x80;
	}
	
	songTempo = 120;
	gameTitle = NULL;
	songTitle = NULL;
	
	inPos = 0x08;
	trkEnd = 0;
	while(inPos < songLen && ! trkEnd)
	{
		curCmd = songData[inPos];	inPos ++;
		switch(curCmd)
		{
		case 0x05:	// song tempo in BPM
			songTempo = ReadBE16(&songData[inPos]);
			inPos += 0x02;
			break;
		//case 0x15:	// "base channel setting"?? (FM vs. MIDI)
		case 0x18:	// "MIDI data transfer" (for initialization SysEx)
			tempSht = ReadBE16(&songData[inPos + 0x00]);
			//sysExData = &songData[inPos + 0x02];
			printf("Warning! Found SysEx Initialization Block! (skipped)\n");
			inPos += 0x02 + tempSht;
			break;
		case 0x04:	// FM instrument (m_vset format)
		case 0x1B:	// FM instrument (m_vset2 format)
			tempByt = songData[inPos + 0x00];	// instrument ID
			inPos += 0x38;
			break;
		case 0x42:	// set tempo base rate
			tempByt = songData[inPos + 0x00];	// "master clock" (not sure where it's used)
			TEMPO_BASE_RATE = ReadBE32(&songData[inPos + 0x01]);
			inPos += 0x05;
			break;
		//case 0x61:	// print text
		//case 0x62:	// filename of MIDI data dump to send
		case 0x63:	// [game title?] ZPD file name
			gameTitle = (char*)&songData[inPos];
			inPos += strlen(gameTitle) + 1;
			break;
		//case 0x7E:	// NOP
		case 0x7F:	// comment (often "song title")
			songTitle = (char*)&songData[inPos];
			inPos += strlen(songTitle) + 1;
			break;
		case 0xFF:
			trkEnd = 1;
			break;
		default:
			printf("Found unknown header block %02X! Unable to parse file header!\n", curCmd);
			return 0x82;
		}
	}
	if (inPos >= songLen)
	{
		printf("Error reading ZMD file: Unexpected EOF!\n");
		return 0x81;
	}
	inPos = (inPos + 0x01) & ~0x01;	// align to 2 bytes
	
	trkCnt = ReadBE16(&songData[inPos]);	inPos += 0x02;
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++, inPos += 0x06)
	{
		tempTInf = &trkInf[curTrk];
		// The track pointer might be 4 bytes large, but I haven't seen any ZMD file >64 KB so far.
		tempTInf->startOfs = inPos + 0x04 + ReadBE16(&songData[inPos + 0x02]);
		tempTInf->chnMode = ReadBE16(&songData[inPos + 0x04]);
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTimes = NUM_LOOPS;
		tempTInf->loopTick = 0;
		
		PreparseZmd(songLen, songData, tempTInf);
	}
	
	if (! NO_LOOP_EXT)
		GuessLoopTimes(trkCnt, trkInf);
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	sysExAlloc = 0x20;
	sysExLen = 0x00;
	sysExData = (UINT8*)malloc(sysExAlloc);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	for (curTrk = 0x00; curTrk < trkCnt; curTrk ++)
	{
		inPos = trkInf[curTrk].startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (curTrk == 0)
		{
			if (songTitle != NULL)
				WriteMetaEvent(&midFileInf, &MTS, 0x03, strlen(songTitle), songTitle);
			if (gameTitle != NULL)
				WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(gameTitle), gameTitle);
			if (USE_OPM_TMR)
				WriteBE32(tempArr, OPMTimerB2Mid(BPM2OPMTimerB(songTempo)));
			else
				WriteBE32(tempArr, BPM2Mid(songTempo));
			WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
		}
		
		trkEnd = 0;
		loopIdx = 0x00;
		mstLoopPos = 0x0000;
		mstLoopCur = 0;
		
		if (trkInf[curTrk].chnMode < 0x08)
			MTS.midChn = (UINT8)trkInf[curTrk].chnMode;
		else if (trkInf[curTrk].chnMode == 0x08)
			MTS.midChn = 0x09;
		else //if (trkInf[curTrk].chnMode < 0x08)
			MTS.midChn = trkInf[curTrk].chnMode - 0x09;
		WriteMetaEvent(&midFileInf, &MTS, 0x20, 0x01, &MTS.midChn);
		curNoteVol = 0x7F;
		curPBend = 0;
		RunNoteCnt = 0;
		
		while(! trkEnd && inPos < songLen)
		{
			curCmd = songData[inPos];
			if (curCmd <= 0x80)
			{
				curNoteLen = songData[inPos + 0x02];
				
				CheckRunningNotes(&midFileInf, &MTS.curDly);
				
				for (curNote = 0; curNote < RunNoteCnt; curNote ++)
				{
					if (RunNotes[curNote].note == curCmd)
					{
						RunNotes[curNote].flags |= FLAG_START_THIS_TICK;
						break;
					}
				}
				if (curNote >= RunNoteCnt && curCmd < 0x80)
				{
					WriteEvent(&midFileInf, &MTS, 0x90, curCmd, curNoteVol);
					if (RunNoteCnt < MAX_RUN_NOTES)
					{
						RunNotes[RunNoteCnt].midChn = MTS.midChn;
						RunNotes[RunNoteCnt].note = curCmd;
						RunNotes[RunNoteCnt].remLen = 0xFFFF;
						RunNotes[RunNoteCnt].flags = FLAG_START_THIS_TICK;
						RunNoteCnt ++;
					}
				}
				for (curNote = 0; curNote < RunNoteCnt; curNote ++)
				{
					if (RunNotes[curNote].flags & FLAG_START_THIS_TICK)
					{
						// continue playing all notes that were called during this tick
						RunNotes[curNote].remLen = (UINT16)MTS.curDly + curNoteLen;
						RunNotes[curNote].flags &= ~FLAG_START_THIS_TICK;
					}
					else
					{
						// and stop all other ones (required by CRTXI_.ZMD)
						//printf("Track %X at %04X: Shortening Note!\n", curTrk, inPos);
						RunNotes[curNote].remLen = (UINT16)MTS.curDly;
					}
				}
				
				MTS.curDly += songData[inPos + 0x01];
				inPos += 0x03;
			}
			else
			{
				switch(curCmd)
				{
				case 0x91:	// Set Tempo
					tempSht = ReadBE16(&songData[inPos + 0x01]);
					if (USE_OPM_TMR)
						WriteBE32(tempArr, OPMTimerB2Mid(BPM2OPMTimerB(tempSht)));
					else
						WriteBE32(tempArr, BPM2Mid(tempSht));
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					inPos += 0x03;
					break;
				case 0x96:	// relative Pitch Bend up
					curPBend += ReadBE16(&songData[inPos + 0x01]);
					tempSSht = curPBend + 0x2000;
					WriteEvent(&midFileInf, &MTS, 0xE0, (tempSSht >> 0) & 0x7F, (tempSSht >> 7) & 0x7F);
					inPos += 0x03;
					break;
				case 0x97:	// relative Pitch Bend down
					curPBend -= ReadBE16(&songData[inPos + 0x01]);
					tempSSht = curPBend + 0x2000;
					WriteEvent(&midFileInf, &MTS, 0xE0, (tempSSht >> 0) & 0x7F, (tempSSht >> 7) & 0x7F);
					inPos += 0x03;
					break;
				case 0xA0:	// Set Instrument
					// Note: sometimes it seems to reset Bank MSB??
					tempByt = songData[inPos + 0x01] - 0x01;
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					inPos += 0x02;
					break;
				case 0xA3:	// Set channel
					// 0x00-0x07: FM 1-8, 0x08 - ADPCM, 0x09..0x18 - MIDI channel 1..16, 0x19..0x1F - ADPCM channel 2..8
					tempByt = songData[inPos + 0x01];
					if (tempByt >= 0x09 && tempByt <= 0x18)
					{
						MTS.midChn = tempByt - 0x09;
					}
					else
					{
						printf("Ignored 'Set Channel = %02X' on track %u at %04X\n",
							songData[inPos + 0x01], curTrk, inPos);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					}
					inPos += 0x02;
					break;
				case 0xA6:	// Fade In/Out (TODO)
					// param: 0 = cancel, -85..-1 = fade in, +1..+85 = fade out
					tempByt = songData[inPos + 0x01];
					printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					inPos += 0x02;
					break;
				case 0xA7:	// Damper Pedal (TODO)
					tempByt = songData[inPos + 0x01];
					printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x40, tempByt);
					inPos += 0x02;
					break;
				case 0xA8:	// Pitch Bend Range (TODO)
					tempByt = songData[inPos + 0x01];
					printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x65, 0x00);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x64, 0x00);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, tempByt);
					inPos += 0x02;
					break;
				case 0xAC:	// No Key-Off Mode (TODO)
					tempByt = songData[inPos + 0x01];
					if (tempByt)
						printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					inPos += 0x02;
					break;
				case 0xB4:	// Set Pan
					tempByt = songData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0xB5:	// MIDI Controller
					WriteEvent(&midFileInf, &MTS, 0xB0, songData[inPos + 0x01], songData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xB6:	// Set Channel Volume
					tempByt = songData[inPos + 0x01] ^ 0x7F;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					inPos += 0x02;
					break;
				case 0xB9:	// Set Note Velocity
					curNoteVol = songData[inPos + 0x01];
					inPos += 0x02;
					break;
				case 0xBA:
					printf("Ignored event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x01;
					break;
				case 0xBB:	// Set Modulation
					tempByt = songData[inPos + 0x01];
					if (tempByt)
						printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, tempByt);
					inPos += 0x02;
					break;
				case 0xBD:	// Auto Pitch Bend Switch ?? (TODO)
					tempByt = songData[inPos + 0x01];
					if (tempByt)
						printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					inPos += 0x02;
					break;
				case 0xBE:	// Aftertouch Sequence Switch ?? (TODO)
					tempByt = songData[inPos + 0x01];
					if (tempByt)
						printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					inPos += 0x02;
					break;
				case 0xBF:	// force key off
					//printf("Force Key Off Event on track %u at %04X\n", curTrk, inPos);
					{
						// confirmed to work this way via MO51.ZMD
						UINT8 hadNoteStopCmd = 0x00;
						for (curNote = 0; curNote < RunNoteCnt; curNote ++)
						{
							if (RunNotes[curNote].remLen > (UINT16)MTS.curDly)
							{
								RunNotes[curNote].remLen = (UINT16)MTS.curDly;
								hadNoteStopCmd |= 0x01;
							}
						}
						CheckRunningNotes(&midFileInf, &MTS.curDly);
						if (! hadNoteStopCmd)
							printf("Note: Event %02X on track %u found, but no notes were shortened at %04X\n", curCmd, curTrk, inPos);
					}
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x01;
					break;
				case 0xC0:	// Control Command
					tempByt = songData[inPos + 0x01];
					switch(tempByt)
					{
					//case 0x03:	// D.C. (Da Capo - restart from beginning)
					//case 0x04:	// Segno
					//case 0x05:	// D.S.
					//case 0x06:	// Coda
					//case 0x07:	// To Coda
					//case 0x08:	// Fine
					case 0x09:	// Do (used for Master Loop Start)
						mstLoopPos = inPos;
						mstLoopCur = 0;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCur);
						break;
					case 0x0A:	// Loop (used for Master Loop End)
						mstLoopCur ++;
						if (mstLoopCur < 0x80)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCur);
						if (mstLoopCur >= trkInf[curTrk].loopTimes)
							break;
						inPos = mstLoopPos;
						break;
					//case 0x0B:	// "!"
					//case 0x0C:	// "@"
					default:
						printf("Ignored event %02X (param %02X) on track %u at %04X\n",
							curCmd, songData[inPos + 0x01], curTrk, inPos);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						break;
					}
					inPos += 0x02;
					break;
				case 0xC1:	// Loop Start
					loopCur[loopIdx] = 0x00;
					loopMax[loopIdx] = 0x00;
					inPos += 0x01;
					
					if (songData[inPos] == 0xCF)	// look ahead for Master Loop marker
					{
						if (songData[inPos + 0x01] >= 0xF0)	// 0xFF usually, but MO51.ZMD has 0xFE in track 4
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, 0x00);	// Master Loop start marker
					}
					else
					{
						printf("Warning: Loop Start NOT followed by Loop Count Set on track %u at %04X\n", curTrk, inPos);
					}
					loopIdx ++;
					break;
				case 0xC2:	// Loop End
					if (! loopIdx)
					{
						printf("Warning: Loop End without Loop Start!\n");
						trkEnd = 1;
						break;
					}
					tempSht = ReadBE16(&songData[inPos + 0x01]);
					inPos += 0x03;
					
					loopIdx --;
					loopCur[loopIdx] ++;
					if (loopMax[loopIdx] >= 0xF0)
					{
						// master loop
						if (loopCur[loopIdx] < 0x80)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCur[loopIdx]);
						if (loopCur[loopIdx] >= trkInf[curTrk].loopTimes)
							break;
					}
					else
					{
						if (loopCur[loopIdx] >= loopMax[loopIdx])
							break;
					}
					// loop back
					inPos -= tempSht;
					loopIdx ++;
					break;
				//case 0xC3:	// Loop Jump 1
				//	tempByt = songData[inPos + 0x01];	// loop counter
				//	tempSht = ReadBE16(&songData[inPos + 0x02]);
				//	inPos += 0x03;
				//	break;
				case 0xC4:	// Loop Jump 2
					printf("Warning: Loop Exit found!\n");
					if (! loopIdx)
					{
						printf("Warning: Loop Exit without Loop Start!\n");
						trkEnd = 1;
						break;
					}
					tempSht = ReadBE16(&songData[inPos + 0x01]);
					inPos += 0x03;
					
					loopIdx --;
					loopCur[loopIdx] ++;
					if (loopCur[loopIdx] + 1 == loopMax[loopIdx])
					{
						// jump out of the loop
						inPos += tempSht;
						break;
					}
					loopIdx ++;
					break;
				case 0xCD:	// Chord Note
					CheckRunningNotes(&midFileInf, &MTS.curDly);
					// The note will be marked as "played this tick", so that the actual
					// (non-chord) note command will set the proper note length later.
					tempByt = songData[inPos + 0x01];
					for (curNote = 0; curNote < RunNoteCnt; curNote ++)
					{
						if (RunNotes[curNote].note == tempByt)
						{
							RunNotes[curNote].flags |= FLAG_START_THIS_TICK;
							break;
						}
					}
					if (curNote >= RunNoteCnt)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, tempByt, curNoteVol);
						if (RunNoteCnt < MAX_RUN_NOTES)
						{
							RunNotes[RunNoteCnt].midChn = MTS.midChn;
							RunNotes[RunNoteCnt].note = tempByt;
							RunNotes[RunNoteCnt].remLen = 0xFFFF;
							RunNotes[RunNoteCnt].flags = FLAG_START_THIS_TICK;
							RunNoteCnt ++;
						}
					}
					inPos += 0x02;
					break;
				case 0xCF:	// set Loop Count
					if (! loopIdx)
					{
						printf("Warning: Loop Count Set without Loop Start!\n");
						trkEnd = 1;
						break;
					}
					loopIdx --;
					loopMax[loopIdx] = songData[inPos + 0x01];
					loopIdx ++;
					inPos += 0x02;
					break;
				case 0xD1:	// Pitch Bend
					// +01/02: FM pitch bend (64 steps per semitone)
					// +03/04: MIDI pitch bend (683 steps per semitone)
					curPBend = (INT16)ReadBE16(&songData[inPos + 0x03]);
					tempSSht = curPBend + 0x2000;
					WriteEvent(&midFileInf, &MTS, 0xE0, (tempSSht >> 0) & 0x7F, (tempSSht >> 7) & 0x7F);
					inPos += 0x05;
					break;
				//case 0xD2:	// set NRPN
				//	WriteEvent(&midFileInf, &MTS, 0xB0, 0x63, songData[inPos + 0x01]);
				//	WriteEvent(&midFileInf, &MTS, 0xB0, 0x62, songData[inPos + 0x02]);
				//	WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, songData[inPos + 0x03]);
				//	WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos + 0x04]);
				//	inPos += 0x05;
				//	break;
				case 0xD3:	// Bank MSB/LSB
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x00, songData[inPos + 0x01]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x20, songData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xE2:	// Chord Note (TODO)
					printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					inPos += 0x0E;
					break;
				case 0xE6:	// Set Modulation
					tempByt = songData[inPos + 0x01];
					if (tempByt)
						printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, songData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xEA:	// Send SysEx Data
					for (sysExLen = 0x01; inPos + sysExLen < songLen; sysExLen ++)
					{
						if (sysExAlloc <= sysExLen)
						{
							sysExAlloc *= 2;
							sysExData = (UINT8*)realloc(sysExData, sysExAlloc);
						}
						if (songData[inPos + sysExLen] == 0xFF)
						{
							sysExData[0x03 + sysExLen] = 0xF7;
							sysExLen ++;
							break;
						}
						sysExData[0x03 + sysExLen] = songData[inPos + sysExLen];
					}
					WriteLongEvent(&midFileInf, &MTS, 0xF0, 0x03 + sysExLen, sysExData);
					inPos += sysExLen;
					break;
				case 0xEB:	// set SysEx Manufacturer ID + Device ID + Model ID
					sysExData[0x00] = songData[inPos + 0x01];	// Manufacturer ID
					sysExData[0x01] = songData[inPos + 0x02];	// Device ID
					sysExData[0x02] = songData[inPos + 0x03];	// Model ID
					sysExData[0x03] = 0x12;	// the command seems to be hardcoded?
					inPos += 0x04;
					break;
				case 0xEC:	// Send SysEx Data (with length)
					sysExLen = ReadBE16(&songData[inPos + 0x01]);
					if (songData[inPos + 0x03] == 0xF0)
						WriteLongEvent(&midFileInf, &MTS, 0xF0, sysExLen - 1, &songData[inPos + 0x04]);
					else
						printf("Invalid Raw-Data-Write on track %u at %04X\n", curTrk, inPos);
					inPos += 0x03 + sysExLen;
					break;
				case 0xED:	// MT-32 Effect Control (TODO)
					// Params: +01 = Reverb Level, +02 = Chorus Level, +03 = unused
					// MT-32/CM-64 Params: +01 = Part Number, +02 = Reverb Switch, +03 = unused
					// Parameter == 0xFF -> omit
					printf("Event %02X with value %02X on track %u at %04X\n", curCmd, tempByt, curTrk, inPos);
					inPos += 0x04;
					break;
				case 0xF0:	// NOP command
					inPos += 0x01;
					break;
				case 0xFC:	// MIDI Note Off
					//printf("Note Off Event on track %u at %04X\n", curTrk, inPos);
					tempByt = songData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0x80, tempByt, songData[inPos + 0x02]);
					// remove from note list (the ZMD driver apparently does this)
					for (curNote = 0; curNote < RunNoteCnt; curNote ++)
					{
						if (RunNotes[curNote].note == tempByt)
						{
							RunNoteCnt --;
							if (curNote != RunNoteCnt)
								RunNotes[curNote] = RunNotes[RunNoteCnt];
							break;
						}
					}
					inPos += 0x03;
					break;
				case 0xFD:	// MIDI Note On
					printf("Note On Event on track %u at %04X\n", curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0x90, songData[inPos + 0x01], songData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xFF:	// Track End
					trkEnd = 1;
					inPos += 0x01;
					break;
#if 0	// FM/PCM only
				case 0xBC:	// TODO
					printf("Ignored event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x04;
					break;
				case 0xB1:	// TODO
				case 0xB2:	// TODO
				case 0xB3:	// TODO
					printf("Ignored event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x01;
					break;
				case 0x98:	// TODO
				case 0xAA:	// TODO
					printf("Ignored event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x02;
					break;
				case 0xE8:	// TODO
					printf("Ignored event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x09;
					break;
#endif
				default:
					printf("Unknown event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
			}
		}
		
		// stop all notes like the ZMD driver would do
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
		{
			if (RunNotes[curNote].remLen > MTS.curDly)
				RunNotes[curNote].remLen = (UINT16)MTS.curDly;
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

static void PreparseZmd(UINT32 songLen, const UINT8* songData, TRK_INFO* trkInf)
{
	UINT16 inPos;
	UINT16 cmdLen;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 loopIdx;
	UINT16 loopMax[8];
	UINT16 loopCur[8];
	UINT8 tempByt;
	UINT16 tempSht;
	
	trkEnd = 0;
	loopIdx = 0x00;
	trkInf->loopOfs = 0x0000;
	inPos = trkInf->startOfs;
	while(inPos < songLen && ! trkEnd)
	{
		curCmd = songData[inPos];
		if (curCmd <= 0x80)
		{
			tempByt = songData[inPos + 0x01];
			trkInf->tickCnt += tempByt;
			if (! trkInf->loopOfs)
				trkInf->loopTick += tempByt;
			cmdLen = 0x03;
		}
		else
		{
			cmdLen = 0x00;
			switch(curCmd)
			{
			case 0xBA:
			case 0xBF:
			case 0xF0:
				cmdLen = 0x01;
				break;
			case 0xA0:	// Set Instrument
			case 0xA3:
			case 0xB4:	// Set Pan
			case 0xB6:	// Set Channel Volume
			case 0xB9:	// Set Note Velocity
			case 0xBB:	// Set Modulation
			case 0xBE:
			case 0xCD:	// Chord Note
				cmdLen = 0x02;
				break;
			case 0x91:	// Set Tempo
			case 0x96:	// relative Pitch Bend up
			case 0x97:	// relative Pitch Bend down
			case 0xB5:	// MIDI Controller
			case 0xD3:	// Bank MSB/LSB
			case 0xE6:	// Set Modulation
			case 0xFC:	// stop playing note length
				cmdLen = 0x03;
				break;
			case 0xEB:	// set SysEx Manufacturer ID + Device ID + Model ID
				cmdLen = 0x04;
				break;
			case 0xD1:	// Pitch Bend
				cmdLen = 0x05;
				break;
			case 0xC0:	// Loop thing
				cmdLen = 0x02;
				switch(songData[inPos + 0x01])
				{
				case 0x09:	// Master Loop Start
					trkInf->loopOfs = inPos;
					break;
				case 0x0A:	// Master Loop End
					trkEnd = 1;
					break;
				}
				break;
			case 0xC1:	// Loop Start
				loopCur[loopIdx] = 0x00;
				loopMax[loopIdx] = 0x00;
				cmdLen = 0x01;
				
				if (songData[inPos + 0x01] == 0xCF)	// look ahead for Master Loop marker
				{
					if (songData[inPos + 0x02] >= 0xF0)
						trkInf->loopOfs = inPos;	// Master Loop start marker
				}
				loopIdx ++;
				break;
			case 0xC2:	// Loop End
				if (! loopIdx)
				{
					trkEnd = 1;
					break;
				}
				tempSht = ReadBE16(&songData[inPos + 0x01]);
				cmdLen = 0x03;
				
				loopIdx --;
				loopCur[loopIdx] ++;
				if (loopMax[loopIdx] >= 0xF0)
				{
					trkEnd = 1;
					break;	// master loop
				}
				if (loopCur[loopIdx] < loopMax[loopIdx])
				{
					// loop back
					inPos = inPos + cmdLen - tempSht;
					cmdLen = 0x00;
					loopIdx ++;
				}
				break;
			case 0xC4:	// Loop Exit
				if (! loopIdx)
				{
					trkEnd = 1;
					break;
				}
				tempSht = ReadBE16(&songData[inPos + 0x01]);
				cmdLen = 0x03;
				
				loopIdx --;
				loopCur[loopIdx] ++;
				if (loopCur[loopIdx] + 1 == loopMax[loopIdx])
				{
					// jump out of the loop
					inPos += cmdLen + tempSht;
					cmdLen = 0x00;
					break;
				}
				loopIdx ++;
				break;
			case 0xCF:	// set Loop Count
				if (loopIdx)
				{
					loopIdx --;
					loopMax[loopIdx] = songData[inPos + 0x01];
					loopIdx ++;
				}
				cmdLen = 0x02;
				break;
			case 0xEA:	// Send SysEx Data
				cmdLen = 0x01;
				for (cmdLen = 0x01; inPos + cmdLen < songLen; cmdLen ++)
				{
					if (songData[inPos + cmdLen] == 0xFF)
					{
						cmdLen ++;	// count SysEx End command
						break;
					}
				}
				break;
			case 0xEC:	// Send SysEx Data (with length)
				cmdLen = 0x03 + ReadBE16(&songData[inPos + 0x01]);
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
	}
	
	return;
}

static void GuessLoopTimes(UINT16 TrkCnt, TRK_INFO* trkInf)
{
	UINT16 CurTrk;
	TRK_INFO* TempTInf;
	UINT32 TrkLen;
	UINT32 TrkLoopLen;
	UINT32 MaxTrkLen;
	
	MaxTrkLen = 0x00;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &trkInf[CurTrk];
		if (TempTInf->loopOfs)
			TrkLoopLen = TempTInf->tickCnt - TempTInf->loopTick;
		else
			TrkLoopLen = 0x00;
		
		TrkLen = TempTInf->tickCnt + TrkLoopLen * (TempTInf->loopTimes - 1);
		if (MaxTrkLen < TrkLen)
			MaxTrkLen = TrkLen;
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &trkInf[CurTrk];
		if (TempTInf->loopOfs)
			TrkLoopLen = TempTInf->tickCnt - TempTInf->loopTick;
		else
			TrkLoopLen = 0x00;
		if (TrkLoopLen < 0x20)
			continue;
		
		TrkLen = TempTInf->tickCnt + TrkLoopLen * (TempTInf->loopTimes - 1);
		if (TrkLen * 5 / 4 < MaxTrkLen)
		{
			// TrkLen = desired length of the loop
			TrkLen = MaxTrkLen - TempTInf->loopTick;
			
			TempTInf->loopTimes = (UINT16)((TrkLen + TrkLoopLen / 3) / TrkLoopLen);
			printf("Trk %u: Extended loop to %u times\n", CurTrk, TempTInf->loopTimes);
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
				// MIDI logs from Cyber Block Metal Orange EX indicate that it sends 8# nn 00
				fInf->data[fInf->pos + 0x00] = 0x80 | tempNote->midChn;
				fInf->data[fInf->pos + 0x01] = tempNote->note;
				fInf->data[fInf->pos + 0x02] = 0x00;
				fInf->pos += 0x03;
				
				RunNoteCnt --;
				if (curNote != RunNoteCnt)
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

INLINE UINT32 BPM2Mid(UINT16 valBPM)
{
	return 60000000 / 48 * MIDI_RES / valBPM;
}

INLINE UINT8 BPM2OPMTimerB(UINT16 valBPM)
{
	UINT16 timerVal;
	UINT16 timerRest;
	
	if (valBPM == 0)
		return 0xFF;
	timerVal = TEMPO_BASE_RATE / (valBPM << 4);
	timerRest = TEMPO_BASE_RATE % (valBPM << 4);
	if (timerRest > 0)
		timerVal ++;	// this is what the driver does
	
	return (UINT8)(0x100 - timerVal);
}

INLINE UINT32 OPMTimerB2Mid(UINT8 timerVal)
{
	UINT16 timerPeriod = (0x100 - timerVal) << 4;
	double valBPM = TEMPO_BASE_RATE / (double)timerPeriod;
	return (UINT32)(60000000 / 48 * MIDI_RES / valBPM);
}


static UINT16 ReadBE16(const UINT8* data)
{
	return (data[0x00] << 8) | (data[0x01] << 0);
}

static UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}
