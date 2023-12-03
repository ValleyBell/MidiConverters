// MDC -> Midi Converter
// ---------------------
// Written by Valley Bell, 03 October 2019
// based on TGL FMP -> Midi Converter

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
	UINT32 startOfs;
	UINT32 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 trkID;
	UINT8 chnID;
	UINT8 flags;	// bit 0 (01) - master loop via jump, bit 4 (10) - has Expression before first note
} TRK_INF;


#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


UINT8 Mdc2Mid(UINT32 SongLen, const UINT8* SongData);
INLINE UINT16 ReadVarLenInt(UINT32 SongLen, const UINT8* SongData, UINT32* pos);
INLINE void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, INT16 bend);
static void PreparseMdc(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, UINT8 pass);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 BPM2Mid(UINT16 valBPM);

INLINE UINT16 ReadBE16(const UINT8* data);
INLINE UINT32 ReadBE32(const UINT8* data);


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

static UINT32* midWrtTick = NULL;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("MDC -> Midi Converter\n---------------------\n");
	if (argc < 3)
	{
		printf("Usage: Mdc2Mid.exe [options] input.bin output.mid\n");
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
	if (ROMLen > 0xFFFF)	// 64 KB
		ROMLen = 0xFFFF;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	retVal = Mdc2Mid(ROMLen, ROMData);
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

UINT8 Mdc2Mid(UINT32 SongLen, const UINT8* SongData)
{
	TRK_INF trkInf[0x20];
	TRK_INF* tempTInf;
	UINT32 trkBaseOfs;
	UINT16 trkCnt;
	UINT8 curTrk;
	UINT32 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 LoopIdx;
	UINT16 mstLoopCount;
	UINT16 LoopCount[8];
	UINT32 LoopStPos[8];
	UINT32 LoopEndPos[8];
	
	//UINT32 tempLng;
	//UINT16 tempSht;
	UINT8 tempByt;
	UINT8 tempArr[4];
	INT16 tempPB;
	
	UINT8 chnMode;
	UINT8 curNote;
	UINT8 curNoteVol;
	UINT8 flags;
	INT8 noteLenMod;
	UINT8 pbRange;
	UINT8 portaBaseNote;
	// portaState:
	//	bit 0 (01) - prepare portamento
	//	bit 1 (02) - pitch to new note
	UINT8 portaState;
	INT8 portaDelta;
	INT16 curDetune;
	UINT16 slideDurat;
	INT16 slideRange;
	UINT16 slideRemDurat;
	INT32 slideDelta;
	INT32 slideOffset;
	UINT32 slideStart;	// slideStart/midiTick are a hack so that I can properly place the pitch slides
	UINT32 midiTick;
	INT16 curChnPB;
	UINT8 curChnExpr;
	
	UINT32 sysExLen;
	const char* songTitle;
	UINT32 songTLen;
	
	if (memcmp(&SongData[0x00], "MDC\x1A", 0x04))
	{
		printf("Invalid MDC file!\n");
		MidData = NULL;
		MidLen = 0x00;
		return 0x80;
	}
	//SongLen = ReadBE32(&SongData[0x08]);
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	midWrtTick = &midiTick;
	
	trkBaseOfs = ReadBE32(&SongData[0x10]);
	MIDI_RES = ReadBE16(&SongData[0x2C]);
	
	songTitle = NULL;
	inPos = ReadBE32(&SongData[0x14]);
	if (inPos && inPos < SongLen)
	{
		songTitle = (char*)&SongData[inPos];
		songTLen = strlen(songTitle);
		// strip off "0D 0A 1A" sequence
		while(songTLen > 0 && (UINT8)songTitle[songTLen - 1] < 0x20)
			songTLen --;
	}
	
	inPos = trkBaseOfs;
	trkCnt = ReadBE16(&SongData[inPos]);	inPos += 0x02;
	if (trkCnt > 0x20)
		trkCnt = 0x20;
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x08)
	{
		tempTInf = &trkInf[curTrk];
		tempTInf->startOfs = trkBaseOfs + ReadBE32(&SongData[inPos + 0x00]);
		tempTInf->trkID = SongData[inPos + 0x04];
		tempTInf->chnID = SongData[inPos + 0x05];
		tempTInf->loopOfs = 0x0000;
		tempTInf->tickCnt = 0;
		tempTInf->loopTick = 0;
		tempTInf->flags = 0x00;
		
		PreparseMdc(SongLen, SongData, tempTInf, 0);
		if (tempTInf->flags & 0x01)	// loop via GoTo - parse track again to get loop tick
			PreparseMdc(SongLen, SongData, tempTInf, 1);
		tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(trkCnt, trkInf, MIDI_RES / 4, 0xFF);
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tempTInf = &trkInf[curTrk];
		inPos = tempTInf->startOfs;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (curTrk == 0)
		{
			if (songTitle != NULL && songTLen > 0)
				WriteMetaEvent(&midFileInf, &MTS, 0x03, songTLen, songTitle);
		}
		
		trkEnd = 0;
		LoopIdx = 0x00;
		mstLoopCount = 0;
		MTS.midChn = tempTInf->chnID & 0x0F;
		if (tempTInf->chnID & 0x80)
		{
			// MIDI channels
		}
		else if ((tempTInf->chnID & 0xF0) == 0x00)
		{
			// FM channels
		}
		else if ((tempTInf->chnID & 0xF0) == 0x10)
		{
			// PCM channel
			MTS.midChn = 0x09;
		}
		RunNoteCnt = 0;
		
		chnMode = 0x00;
		flags = 0x00;
		curNoteVol = 0x7F;	// confirmed via driver disassembly
		noteLenMod = 0;
		pbRange = 12;	// according to the disasm, it defaults to 12
		curDetune = 0;
		midiTick = 0;
		portaState = 0x00;
		portaDelta = 0;
		portaBaseNote = 0xFF;
		slideOffset = 0;
		
		curChnExpr = 106;
		curChnPB = 0;
		if (! (tempTInf->flags & 0x10))
			WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, curChnExpr);	// default Expression value
		while(inPos < SongLen)
		{
			if ((flags & 0x80) && slideStart < midiTick + MTS.curDly)
			{
				// do pitch slide
				INT16 pbBase = curDetune + portaDelta * 0x2000 / pbRange;
				UINT32 remTicks;
				for (remTicks = MTS.curDly; remTicks > 0 && slideRemDurat > 0; slideRemDurat --)
				{
					if (midiTick < slideStart)
						MTS.curDly = slideStart + 1 - midiTick;
					else
						MTS.curDly = 1;
					remTicks -= MTS.curDly;
					slideOffset += slideDelta;
					// The formula below is a sort of stateless version. But due to slideDelta being 16.16 fixed point,
					// it's accuracy is not noticeably higher.
					//slideOffset = ((INT64)slideRange << 23) * (midiTick + MTS.curDly - slideStart) / pbRange / slideDurat;
					tempPB = pbBase + (slideOffset >> 16);
					if (tempPB != curChnPB && slideRemDurat > 1)	// omit the PB where the slide is stopped
					{
						curChnPB = tempPB;
						WritePitchBend(&midFileInf, &MTS, curChnPB);
					}
				}
				MTS.curDly += remTicks;
				if (slideRemDurat < 1)
				{
					flags &= ~0x80;
					slideOffset = 0;
				}
			}
			
			if (inPos == tempTInf->loopOfs && ! (flags & 0x10) && mstLoopCount == 0)
			{
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
				flags |= 0x10;
			}
			
			curCmd = SongData[inPos];
			if (curCmd < 0x80 || (flags & 0x01))
			{
				UINT32 noteLen;
				UINT32 noteDelay;
				UINT8 noteVol;
				
				if (flags & 0x01)	// prefixed by command 0x81
				{
					flags &= ~0x01;
					inPos ++;
					noteLen = ReadVarLenInt(SongLen, SongData, &inPos);
					noteDelay = noteLen;
					noteVol = curNoteVol;
				}
				else
				{
					tempByt = SongData[inPos + 0x01];
					inPos += 0x02;
					if (tempByt & 0x80)
					{
						tempByt &= 0x7F;
						noteLen = tempByt;
						noteDelay = tempByt;
						noteVol = curNoteVol;
					}
					else
					{
						noteVol = tempByt;
						if (! noteVol)
							noteVol = curNoteVol;
						else
							noteVol = tempByt;
						noteLen = ReadVarLenInt(SongLen, SongData, &inPos);
						noteDelay = ReadVarLenInt(SongLen, SongData, &inPos);
					}
				}
				curNote = curCmd;
				if (noteLenMod > 0)
				{
					if (noteLen > 0)
						noteLen = (noteLen - 1) * noteLenMod / 8 + 1;
				}
				else if (noteLenMod < 0)
				{
					if (-noteLenMod < noteLen)
						noteLen = noteLen + noteLenMod;
					else
						noteLen = 1;
				}
				if (portaState & 0x01)
					noteLen = 0xFFFF;
				
				if (noteLen == 0)	// chord note?
				{
					noteLen = 0x10000;	// need a workaround, because it will stop 0-length notes before the next event
				}
				else
				{
					// ignore for chords
					portaDelta = (portaState & 0x02) ? (curNote - portaBaseNote) : 0;
					tempPB = curDetune + portaDelta * 0x2000 / pbRange + (slideOffset >> 16);
					if (tempPB != curChnPB)
					{
						curChnPB = tempPB;
						WritePitchBend(&midFileInf, &MTS, curChnPB);
					}
				}
				
				// modifying midiTick here, because CheckRunningNotes doesn't fix it by itself
				midiTick += MTS.curDly;
				CheckRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes);
				midiTick -= MTS.curDly;
				
				if (portaState & 0x02)
				{
					for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
					{
						// this is exactly what the driver does
						if (RunNotes[tempByt].note == portaBaseNote)
						{
							RunNotes[tempByt].remLen = MTS.curDly + noteLen;
							break;
						}
					}
				}
				else
				{
					for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
					{
						if (RunNotes[tempByt].note == curNote)
						{
							RunNotes[tempByt].remLen = MTS.curDly + noteLen;
							break;
						}
					}
					if (tempByt >= RunNoteCnt)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, curNote, noteVol);
						AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes,
										MTS.midChn, curNote, 0x80, noteLen);	// the MDC driver sends 9# note 00
						portaBaseNote = curNote;
					}
				}
				
				if (noteDelay > 0)
				{
					// special note length handling to fix chords
					// (The first few notes have length 0 and the driver always overwrites "length 0" with the current length.)
					for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
					{
						if (RunNotes[tempByt].remLen == MTS.curDly + 0x10000)
							RunNotes[tempByt].remLen = MTS.curDly + noteLen;
					}
					if (portaState & 0x02)
					{
						// when "portamento" was set for the *previous* note, set chord note length for all playing ones
						// Note: Required for SRMP4, RM4M01C.MDC/RM4M01S.MDC, as it has a "chord" where
						//       the initial 3 notes don't match the extending notes.
						for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
							RunNotes[tempByt].remLen = MTS.curDly + noteLen;
					}
					portaState = (portaState << 1) & 0x03;	// the state is NOT refreshed on 0-delay notes
					MTS.curDly += noteDelay;
				}
			}
			else
			{
				switch(curCmd)
				{
				case 0x80:	// delay
					inPos ++;
					// fix "length 0" notes
					for (tempByt = 0; tempByt < RunNoteCnt; tempByt ++)
					{
						if (RunNotes[tempByt].remLen == MTS.curDly + 0x10000)
							RunNotes[tempByt].remLen = MTS.curDly + 0;
					}
					portaState = (portaState << 1) & 0x03;
					MTS.curDly += ReadVarLenInt(SongLen, SongData, &inPos);
					break;
				case 0x81:	// note
					flags |= 0x01;
					inPos += 0x01;
					break;
				case 0x86:	// prepare portamento
					portaState |= 0x01;
					inPos += 0x01;
					break;
				case 0x88:	// Loop Start
					LoopEndPos[LoopIdx] = 0x0000;
					LoopCount[LoopIdx] = SongData[inPos + 0x01];
					inPos += 0x03;
					LoopStPos[LoopIdx] = inPos;
					LoopIdx ++;
					break;
				case 0x89:	// Loop End
					if (! LoopIdx)
					{
						printf("Warning: Loop End without Loop Start on track %u at %04X\n", curTrk, inPos);
						trkEnd = 1;
						break;
					}
					
					LoopIdx --;
					if (! LoopEndPos[LoopIdx])
						LoopEndPos[LoopIdx] = inPos;
					inPos += 0x03;	// ignore pointer to Loop Start command for now
					LoopCount[LoopIdx] --;
					if (! LoopCount[LoopIdx])
						break;
					
					// loop back
					inPos = LoopStPos[LoopIdx];
					LoopIdx ++;
					break;
				case 0x8A:	// Loop Exit
					if (! LoopIdx)
					{
						printf("Warning: Loop Exit without Loop Start on track %u at %04X\n", curTrk, inPos);
						trkEnd = 1;
						break;
					}
					inPos += 0x03;	// ignore pointer to Loop End command for now
					
					if (LoopCount[LoopIdx - 1] == 1)
					{
						// output warning, because I have yet to find a file that uses this
						//printf("Warning: Loop Exit on track %u at %04X\n", curTrk, inPos);
						inPos = LoopEndPos[LoopIdx - 1];	// jump to Loop End command
					}
					break;
				case 0x8C:	// unknown/ignored
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					inPos += 0x01;
					break;
				case 0xA0:	// set Note Velocity
				case 0xA2:	// set Expression
					tempByt = SongData[inPos + 0x01];
					if (tempByt & 0x80)
						tempByt = (tempByt & 0x0F) * 0x08 + 0x07;
					
					if (curCmd == 0xA0)
					{
						curNoteVol = tempByt;
					}
					else if (curCmd == 0xA1)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					}
					else if (curCmd == 0xA2)
					{
						flags &= 0x02;
						if (SongData[inPos + 0x01] & 0x80)
							flags |= 0x02;
						curChnExpr = tempByt;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, curChnExpr);
					}
					inPos += 0x02;
					break;
				case 0xA3:	// change Expression
					printf("Event %02X on track %u at %04X\n", curCmd, curTrk, inPos);
					tempByt = SongData[inPos + 0x01];
					if (flags & 0x02)
					{
						INT16 lutIdx = curChnExpr / 0x08 + (INT8)tempByt;
						if (lutIdx < 0x00)
							lutIdx = 0x00;
						else if (lutIdx > 0x0F)
							lutIdx = 0x0F;
						curChnExpr = lutIdx * 0x08 + 0x07;
					}
					else
					{
						INT16 newExpr = curChnExpr + (INT8)tempByt;
						if (newExpr < 0x00)
							newExpr = 0x00;
						else if (newExpr > 0x7F)
							newExpr = 0x7F;
						curChnExpr = (UINT8)newExpr;
					}
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, curChnExpr);
					inPos += 0x02;
					break;
				//case 0xA4:	// set Main Volume
				//	// TODO: does this according to driver disasm, but I haven't seen it in the wild yet
				//	tempByt = SongData[inPos + 0x01];
				//	WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
				//	inPos += 0x02;
				//	break;
				case 0xA6:	// set Pan
					tempByt = SongData[inPos + 0x01];
					if (tempByt & 0x80)
					{
						tempByt &= 0x03;
						if (tempByt == 0x01)
							tempByt = 0x00;	// left
						else if (tempByt == 0x02)
							tempByt = 0x7F;	// right
						else if (tempByt == 0x03)
							tempByt = 0x40;	// centre
						else
							tempByt = 0x40;	// actually invalid - use centre
					}
					// Note: The actual driver does NOT send an event for (tempByt >= 0x80).
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					inPos += 0x02;
					break;
				case 0xAA:	// set Pitch Bend Range
					pbRange = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x65, 0x00);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x64, 0x00);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, pbRange);
					inPos += 0x02;
					break;
				case 0xAC:	// set Detune (byte)
					curDetune = (INT8)SongData[inPos + 0x01];
					tempPB = curDetune + portaDelta * 0x2000 / pbRange + (slideOffset >> 16);
					if (tempPB != curChnPB)
					{
						curChnPB = tempPB;
						WritePitchBend(&midFileInf, &MTS, curChnPB);
					}
					inPos += 0x02;
					break;
				case 0xAE:	// set Note Length Modificator
					noteLenMod = (INT8)SongData[inPos + 0x01];
					if (noteLenMod == 8)
						noteLenMod = 0;
					inPos += 0x02;
					break;
				case 0xB0:	// set Detune (word)
					curDetune = (INT16)ReadBE16(&SongData[inPos + 0x01]);
					tempPB = curDetune + portaDelta * 0x2000 / pbRange + (slideOffset >> 16);
					if (tempPB != curChnPB)
					{
						curChnPB = tempPB;
						WritePitchBend(&midFileInf, &MTS, curChnPB);
					}
					inPos += 0x03;
					break;
				case 0xB8:	// Pitch Slide
					//printf("Track %u at %04X: Pitch Slide\n", curTrk, inPos);
					slideRange = (INT16)ReadBE16(&SongData[inPos + 0x01]);
					inPos += 0x03;
					slideDurat = ReadVarLenInt(SongLen, SongData, &inPos);
					slideDelta = ((INT64)slideRange << 23) / pbRange / slideDurat;
					
					slideStart = midiTick + MTS.curDly;
					slideRemDurat = slideDurat;
					slideOffset = 0;
					flags |= 0x80;	// enable slide
					break;
				case 0xE0:	// set Instrument
					tempByt = SongData[inPos + 0x01];
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt, 0x00);
					inPos += 0x02;
					break;
				case 0xEC:	// MIDI Controller
					WriteEvent(&midFileInf, &MTS, 0xB0, SongData[inPos + 0x01], SongData[inPos + 0x02]);
					inPos += 0x03;
					break;
				case 0xEF:	// set channel mode
					printf("Track %u at %04X: Channel Mode %u\n", curTrk, inPos, SongData[inPos + 0x01]);
					chnMode &= 0x80;
					chnMode |= SongData[inPos + 0x01];
					inPos += 0x02;
					break;
				case 0xF0:	// Tempo
					{
						UINT16 tempoBPM;
						UINT32 midiTempo;
						
						tempoBPM = ReadBE16(&SongData[inPos + 0x01]);
						midiTempo = BPM2Mid(tempoBPM);
						WriteBE32(tempArr, midiTempo);
						WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
						inPos += 0x03;
					}
					break;
				case 0xFA:	// send raw MIDI data (for SysEx)
					inPos ++;
					sysExLen = ReadVarLenInt(SongLen, SongData, &inPos);
					if (SongData[inPos] == 0xF0)
						WriteLongEvent(&midFileInf, &MTS, SongData[inPos], sysExLen - 1, &SongData[inPos + 1]);
					else
						printf("Track %u at %04X: Sending unknown raw MIDI command %02X\n", curTrk, inPos, SongData[inPos + 0x01]);
					inPos += sysExLen;
					break;
				case 0xFE:	// Jump / Track End
					{
						INT16 destPos = ReadBE16(&SongData[inPos + 0x01]);
						inPos += 0x03;
						if (! destPos)
						{
							trkEnd = 1;
						}
						else
						{
							inPos += destPos;
							mstLoopCount ++;
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
							if (mstLoopCount >= trkInf[curTrk].loopTimes)
								trkEnd = 1;
						}
					}
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
		}
		FlushRunningNotes(&midFileInf, &MTS.curDly, &RunNoteCnt, RunNotes, 0);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

INLINE UINT16 ReadVarLenInt(UINT32 SongLen, const UINT8* SongData, UINT32* pos)
{
	UINT32 delay;
	
	delay = 0;
	while(SongData[*pos] & 0x80)
	{
		delay <<= 7;
		delay |= (SongData[*pos] & 0x7F);
		(*pos) ++;
	}
	delay <<= 7;
	delay |= (SongData[*pos] & 0x7F);
	(*pos) ++;
	return (UINT16)delay;
}

INLINE void WritePitchBend(FILE_INF* fInf, MID_TRK_STATE* MTS, INT16 bend)
{
	UINT16 bendVal;
	
	if (bend < -0x2000)
		bend = -0x2000;
	else if (bend > +0x1FFF)
		bend = +0x1FFF;
	bendVal = 0x2000 + bend;
	WriteEvent(fInf, MTS, 0xE0, (bendVal >> 0) & 0x7F, (bendVal >> 7) & 0x7F);
	
	return;
}

static void PreparseMdc(UINT32 SongLen, const UINT8* SongData, TRK_INF* trkInf, UINT8 pass)
{
	UINT32 inPos;
	UINT16 cmdLen;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 LoopIdx;
	UINT16 LoopCount[8];
	UINT32 LoopStPos[8];
	UINT32 LoopEndPos[8];
	UINT8 tempByt;
	UINT8 flags;
	UINT16 delay;
	
	trkEnd = 0;
	flags = 0x00;
	LoopIdx = 0x00;
	if (pass == 0)
	{
		trkInf->tickCnt = 0;
		trkInf->loopOfs = 0x0000;
	}
	trkInf->loopTick = 0;
	inPos = trkInf->startOfs;
	while(inPos < SongLen && ! trkEnd)
	{
		if (pass == 1 && inPos == trkInf->loopOfs)
			break;
		
		curCmd = SongData[inPos];
		if (curCmd < 0x80 || (flags & 0x01))
		{
			if (flags & 0x01)	// prefixed by command 0x81
			{
				flags &= ~0x01;
				inPos ++;
				delay = ReadVarLenInt(SongLen, SongData, &inPos);
			}
			else
			{
				tempByt = SongData[inPos + 0x01];
				inPos += 0x02;
				if (tempByt & 0x80)
				{
					delay = tempByt & 0x7F;
				}
				else
				{
					ReadVarLenInt(SongLen, SongData, &inPos);	// ignored
					delay = ReadVarLenInt(SongLen, SongData, &inPos);
				}
			}
			flags |= 0x10;	// has note
			if (pass == 0)
				trkInf->tickCnt += delay;
			else
				trkInf->loopTick += delay;
		}
		else
		{
			cmdLen = 0x00;
			switch(curCmd)
			{
			case 0x80:	// delay
				inPos ++;
				delay = ReadVarLenInt(SongLen, SongData, &inPos);
				if (pass == 0)
					trkInf->tickCnt += delay;
				else
					trkInf->loopTick += delay;
				break;
			case 0x81:	// special note??
				flags |= 0x01;
				cmdLen = 0x01;
				break;
			case 0xA2:	// set Expression
				if (! (flags & 0x10))
					trkInf->flags |= 0x10;
				inPos += 0x02;
				break;
			case 0x86:	// prepare portamento
				cmdLen = 0x01;
				break;
			case 0xA0:	// set Note Velocity
			//case 0xA1:	// set Volume (just a guess)
			case 0xA3:	// ??
			case 0xA6:	// set Pan
			case 0xAA:	// set Pitch Bend Range
			case 0xAC:	// set Detune (byte)
			case 0xAE:	// set Note Length
			case 0xEF:	// set channel mode ??
			case 0xE0:	// set Instrument
				inPos += 0x02;
				break;
			case 0xB0:	// set Detune (word)
			case 0xEC:	// MIDI Controller
			case 0xF0:	// Tempo
				inPos += 0x03;
				break;
			case 0xB8:	// Pitch Slide
				inPos += 0x04;
				break;
			case 0x88:	// Loop Start
				LoopEndPos[LoopIdx] = 0x0000;
				LoopCount[LoopIdx] = SongData[inPos + 0x01];
				cmdLen = 0x03;
				LoopStPos[LoopIdx] = inPos;
				
				LoopStPos[LoopIdx] = inPos + cmdLen;
				LoopIdx ++;
				break;
			case 0x89:	// Loop End
				if (! LoopIdx)
				{
					trkEnd = 1;
					break;
				}
				cmdLen = 0x03;
				
				LoopIdx --;
				if (! LoopEndPos[LoopIdx])
					LoopEndPos[LoopIdx] = inPos;
				LoopCount[LoopIdx] --;
				if (! LoopCount[LoopIdx])
					break;
				
				// loop back
				inPos = LoopStPos[LoopIdx];
				cmdLen = 0x00;
				LoopIdx ++;
				break;
			case 0x8A:	// Loop Exit
				if (! LoopIdx)
				{
					trkEnd = 1;
					break;
				}
				cmdLen = 0x03;
				
				if (LoopCount[LoopIdx - 1] == 1)
				{
					inPos = LoopEndPos[LoopIdx - 1];	// jump to Loop End command
					cmdLen = 0x00;
				}
				break;
			case 0xFA:	// send raw MIDI data (for SysEx)
				inPos ++;
				inPos += ReadVarLenInt(SongLen, SongData, &inPos);
				break;
			case 0xFE:	// Track End
				{
					INT16 destPos = ReadBE16(&SongData[inPos + 0x01]);
					cmdLen = 0x03;
					trkEnd = 1;
					if (destPos)
					{
						trkInf->loopOfs = inPos + cmdLen + destPos;
						trkInf->loopTick = (UINT32)-1;
						trkInf->flags |= 0x01;	// loop via GoTo
					}
				}
				break;
			default:
				//printf("Preparser break\n");	getchar();
				return;
			}
			inPos += cmdLen;
		}
	}
	
	return;
}

static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay)
{
	if (midWrtTick != NULL)
		(*midWrtTick) += *delay;
	
	CheckRunningNotes(fInf, delay, &RunNoteCnt, RunNotes);
	if (*delay)
	{
		UINT8 curNote;
		
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= *delay;
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

INLINE UINT32 BPM2Mid(UINT16 valBPM)
{
	return 60000000 / 48 * MIDI_RES / valBPM;
}


INLINE UINT16 ReadBE16(const UINT8* data)
{
	return (data[0x00] << 8) | (data[0x01] << 0);
}

INLINE UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}
