// New World Computing v1 -> Midi Converter
// ----------------------------------------
// Written by Valley Bell, October/November 2020
// based on EA Steve Hayes -> Midi Converter

// File format info:
//	- There is no header. It's just a sequence of commands.
//	- The lower nibble of each command is the channel number.
//	- channels 0..8 equal MIDI channels 1..9
//
// Commands:
//	00 ll mm - execute subroutine from absolute file offset mmll
//	10 dd - delay by dd ticks
//	2# o1 o2 o3 o4 o5 o6 o7 o8 o9 o10 o11 t1 t2 mi - define instrument
//		o1..o11 = OPL parameters
//		t1/t2 = Tandy Envelope?
//		mt = MIDI instrument (Cx mt)
//		Note: The "channel" is the instrument ID here.
//	3# vv - MIDI Channel Volume (Bx 07 vv), range 00..7F
//	4# mm ll - MIDI Pitch Bend (sent as Ex ll mm), range of mmll is 0000..7F7F
//		Note: Apparently no effect in OPL playback.
//	6# pp - MIDI Pan pp (Bx 0A pp)
//	8# - turn last note off
//		Note: For some pre-release files, the format is "80 nn" where nn is the note.
//	9# nn - note on
//		Note: The drum channel (channel 8) uses direct MIDI note values.
//		All melody channels use a custom format:
//			Bits 0..4 (mask 1F): note value (see NOTE_LUT later in the file for details)
//			Bits 5..7 (mask E0): octave
//	A# vv - OPL volume
//	B# vv - Tandy volume
//	C# ii - load instrument (This load instrument ii, which was previously defined using command 2#.)
//	D# - OPL frequency modulation off (current frequency delta stays active until next Note On)
//	E# tt mm ll - OPL frequency modulation on (only seen in PETHEME.M so far)
//		tt - duration in ticks?
//		mmll - frequency delta per tick (signed 16-bit)
//	FE - song end
//	FF - return from subroutine (if active) OR restart song from beginning

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
	UINT8 midChn;
} TRK_INFO;
typedef struct chord_note
{
	UINT8 note;
	UINT32 startTick;
} CHORD_NOTE;


UINT8 EaSH2Mid(UINT32 songLen, const UINT8* songData);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
INLINE UINT32 Tempo2Mid(void);

INLINE UINT16 ReadLE16(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 PIT_PERIOD = 0x4006;	// 1193182 / 0x4006 = 72.80 Hz
static UINT16 MIDI_RES = 36;
static UINT16 NUM_LOOPS = 2;
static UINT8 FILE_VER = 1;
static UINT8 ORG_TRANSP = 0;
static UINT8 CHORD_TICKS = 0;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("New World Computing v1 -> Midi Converter\n----------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: nwc12mid.exe [options] song.m output.mid\n");
		printf("Converts music from New World Computing's early format to Midi.\n");
		printf("Options:\n");
		printf("    -Ver n          file format version (default: %u)\n", FILE_VER);
		printf("                    0 - note off with note value (beta format)\n");
		printf("                    1 - note off turns off all notes\n");
		printf("    -Loops n        loop song n times (default: %u)\n", NUM_LOOPS);
		printf("    -TpQ n          convert with n ticks per quarter (default: %u)\n", MIDI_RES);
		printf("    -OrgTransp      restore original song transposition (+2 semitones)\n");
		printf("    -AltTiming      use alternate timing, speed is closer to original compositions\n");
		printf("    -ChordTicks n   attempt to restore chords, allow notes to be n tick apart\n");
		printf("                    (recommended value: 1 or 2)\n");
		printf("\n");
		return 0;
	}
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Ver"))
		{
			argbase ++;
			if (argbase < argc)
				FILE_VER = (UINT8)strtoul(argv[argbase], NULL, 0);
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
		else if (! stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				MIDI_RES = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! MIDI_RES)
					MIDI_RES = 30;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "OrgTransp"))
		{
			ORG_TRANSP = 1;
		}
		else if (! stricmp(argv[argbase] + 1, "AltTiming"))
		{
			PIT_PERIOD = 0x3F92;	// seems to match the original composition timing more
		}
		else if (! stricmp(argv[argbase] + 1, "ChordTicks"))
		{
			argbase ++;
			if (argbase < argc)
				CHORD_TICKS = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
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
	
	retVal = EaSH2Mid(ROMLen, ROMData);
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

static UINT8 NoteVal2Midi(UINT8 noteVal, UINT8 trkID)
{
	static const UINT8 NOTE_LUT[0x20] =
	{
		0x00, 0x0C, 0x0E, 0x10, 0x11, 0x13, 0x15, 0x17,	// C, D, E, F, G, A, B
		0x00, 0x0D, 0x0F, 0x11, 0x12, 0x14, 0x16, 0x18,	// C#,D#,E#,F#,G#,A#,B#
		0x00, 0x0B, 0x0D, 0x0F, 0x10, 0x12, 0x14, 0x16,	// Cb,Db,Eb,Fb,Gb,Ab,Bb
		0x00, 0x0C, 0x0E, 0x10, 0x11, 0x13, 0x15, 0x17,	// for safety, not present in actual sound driver
	};
	UINT8 oct;
	UINT8 note;
	
	if (trkID == 8)	// the drum channel uses actual MIDI note numbers
		return noteVal;
	
	oct = noteVal >> 5;
	note = NOTE_LUT[noteVal & 0x1F];
	if (ORG_TRANSP)
		note += 2;	// restore transposition of the original music
	return oct * 12 + note;
}

#define MAX_CHORD_NOTES	8

UINT8 EaSH2Mid(UINT32 songLen, const UINT8* songData)
{
	UINT32 inPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 curChn;
	UINT8 midChn;
	UINT32 tempLng;
	UINT8 tempByt;
	UINT8 tempArr[4];
	char tempStr[0x40];
	UINT16 loopCnt;
	UINT8 chnNotes[0x10];
	const UINT8* insDef[0x10] = {NULL};
	UINT8 subStackID;
	UINT16 subStackPos[0x20];
	UINT8 chordNtCnt[0x10];
	CHORD_NOTE chordNotes[0x10][MAX_CHORD_NOTES];
	UINT32 curTick;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x0000, 1, MIDI_RES);
	
	WriteMidiTrackStart(&midFileInf, &MTS);
	
	tempLng = Tempo2Mid();
	WriteBE32(tempArr, tempLng);
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
	
	loopCnt = 0;
	trkEnd = 0;
	MTS.midChn = 0x00;
	subStackID = 0x00;
	curTick = 0;
	memset(chnNotes, 0xFF, 0x10);
	memset(chordNtCnt, 0, 0x10);
	while(! trkEnd && inPos < songLen)
	{
		curCmd = songData[inPos] & 0xF0;
		curChn = songData[inPos] & 0x0F;
		// adjust channel ID
		//	sound data: 0..7 (melody), 8 (drums)
		//	MT-32: 1..8 (melody), 9 (drums)
		midChn = (curChn + 1) & 0x0F;
		inPos ++;
		switch(curCmd)
		{
		case 0x00:	// Subroutine call
			{
				UINT16 dstPos = ReadLE16(&songData[inPos]);
				inPos += 0x02;
				if (subStackID < 0x10)
				{
					sprintf(tempStr, "Entering subroutine #%u (pos 0x%04X)", 1 + subStackID, dstPos);
					WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(tempStr), tempStr);
					subStackPos[subStackID] = inPos;
					subStackID ++;
					inPos = dstPos;
				}
			}
			break;
		case 0x10:	// delay
			MTS.curDly += songData[inPos];
			curTick += songData[inPos];
			inPos += 0x01;
			break;
		case 0x20:	// instrument definition
			// bytes 00..0A: FM registers
			// bytes 0B..0C: Tandy envelope?
			// byte    0D:   MT-32 instrument
			insDef[curChn] = &songData[inPos];
			inPos += 0x0E;
			break;
		case 0x30:	// [MIDI] volume (00h..7Fh)
			WriteEvent(&midFileInf, &MTS, 0xB0 | midChn, 0x07, songData[inPos]);
			inPos += 0x01;
			break;
		case 0x40:	// [MIDI] pitch bend
			WriteEvent(&midFileInf, &MTS, 0xE0 | midChn, songData[inPos + 0x01], songData[inPos + 0x00]);
			inPos += 0x02;
			break;
		case 0x60:	// Pan
			WriteEvent(&midFileInf, &MTS, 0xB0 | midChn, 0x0A, songData[inPos]);
			inPos += 0x01;
			break;
		case 0x80:	// Note Off
			if (chnNotes[curChn] != 0xFF && chordNtCnt[curChn] > 0)
			{
				UINT8 curNote;
				for (curNote = 0; curNote < chordNtCnt[curChn]; curNote ++)
					WriteEvent(&midFileInf, &MTS, 0x80 | midChn, chordNotes[curChn][curNote].note, 0x7F);
				chordNtCnt[curChn] = 0;
				chnNotes[curChn] = 0xFF;
			}
			if (FILE_VER == 0)
			{
				if (chnNotes[curChn] != 0xFF)
				{
					UINT8 note = NoteVal2Midi(songData[inPos], curChn);
					WriteEvent(&midFileInf, &MTS, 0x80 | midChn, note, 0x7F);
					if (note != chnNotes[curChn])
						printf("Warning at %04X: Turning note 0x%02X off while 0x%02X is playing!\n",
								inPos - 0x01, note, chnNotes[curChn]);
				}
				chnNotes[curChn] = 0xFF;
				inPos += 0x01;
			}
			else //if (FILE_VER == 1)
			{
				if (chnNotes[curChn] != 0xFF)
					WriteEvent(&midFileInf, &MTS, 0x80 | midChn, chnNotes[curChn], 0x7F);
				chnNotes[curChn] = 0xFF;
				inPos += 0x00;
			}
			break;
		case 0x90:	// Note On
			if (chnNotes[curChn] != 0xFF)
			{
				if (chordNtCnt[curChn] == 0)
				{
					WriteEvent(&midFileInf, &MTS, 0x80 | midChn, chnNotes[curChn], 0x7F);
				}
				else
				{
					// Overlapping notes that turn off each other, may have been chords originally.
					// Try to restore those chords when they are <=N ticks apart.
					CHORD_NOTE* lastCN = &chordNotes[curChn][chordNtCnt[curChn] - 1];
					if (curTick - lastCN->startTick > CHORD_TICKS)
					{
						// and turn all notes off when the last delay is > N ticks
						UINT8 curNote;
						for (curNote = 0; curNote < chordNtCnt[curChn]; curNote ++)
							WriteEvent(&midFileInf, &MTS, 0x80 | midChn, chordNotes[curChn][curNote].note, 0x7F);
						chordNtCnt[curChn] = 0;
					}
				}
			}
			
			chnNotes[curChn] = NoteVal2Midi(songData[inPos], curChn);
			WriteEvent(&midFileInf, &MTS, 0x90 | midChn, chnNotes[curChn], 0x7F);
			if (CHORD_TICKS > 0 && chordNtCnt[curChn] < MAX_CHORD_NOTES)
			{
				CHORD_NOTE* cn = &chordNotes[curChn][chordNtCnt[curChn]];
				cn->note = chnNotes[curChn];
				cn->startTick = curTick;
				chordNtCnt[curChn] ++;
			}
			inPos += 0x01;
			break;
		case 0xA0:	// [OPL] volume (00h..3Fh)
			inPos += 0x01;
			break;
		case 0xB0:	// [Tandy] volume (00h..1Fh?)
			inPos += 0x01;
			break;
		case 0xC0:	// instrument
			// The parameter is used to look up instruments previously defined using the 2x command.
			tempByt = insDef[songData[inPos]][0x0D];
			WriteEvent(&midFileInf, &MTS, 0xC0 | midChn, tempByt, 0x00);
			inPos += 0x01;
			break;
		case 0xD0:	// [OPL] Frequency Modulation Off
			//printf("Event %02X at %04X\n", curCmd | curChn, inPos - 0x01);
			WriteEvent(&midFileInf, &MTS, 0xB0 | midChn, 0x6E, curCmd >> 4);
			inPos += 0x00;
			break;
		case 0xE0:	// [OPL] Frequency Modulation On
			//printf("Event %02X at %04X\n", curCmd | curChn, inPos - 0x01);
			WriteEvent(&midFileInf, &MTS, 0xB0 | midChn, 0x6E, curCmd >> 4);
			inPos += 0x03;
			break;
		case 0xF0:	// song end
			curCmd |= curChn;
			
			// terminate any hanging notes
			for (curChn = 0x00; curChn < 0x10; curChn ++)
			{
				midChn = (curChn + 1) & 0x0F;
				if (chnNotes[curChn] != 0xFF)
					WriteEvent(&midFileInf, &MTS, 0x80 | midChn, chnNotes[curChn], 0x7F);
			}
			
			if (curCmd <= 0xFE)
			{
				// track end
				trkEnd = 1;
			}
			else if (curCmd == 0xFF)
			{
				if (subStackID > 0x00)
				{
					subStackID --;
					inPos = subStackPos[subStackID];
					sprintf(tempStr, "Leaving subroutine #%u", 1 + subStackID);
					WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(tempStr), tempStr);
				}
				else
				{
					// loop back
					loopCnt ++;
					if (loopCnt < 0x80)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)loopCnt);
					if (loopCnt < NUM_LOOPS)
						inPos = 0x00;
					else
						trkEnd = 1;
				}
			}
			break;
		default:
			printf("Unknown event %02X at %04X\n", curCmd | curChn, inPos - 0x01);
			WriteEvent(&midFileInf, &MTS, 0xB0 | midChn, 0x6E, curCmd >> 4);
			inPos += 0x01;
			trkEnd = 1;
			break;
		}
	}
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
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

INLINE UINT32 Tempo2Mid(void)
{
	// BPM = TicksPerSec * 60 / MIDI_RES
	double TicksPerSec = 13125000 / 11.0 / PIT_PERIOD;
	return (UINT32)(1000000 * MIDI_RES / TicksPerSec + 0.5);
}


INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x00] << 0) | (data[0x01] << 8);
}
