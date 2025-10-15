// Core Design MegaDrive -> Midi Converter
// ---------------------------------------
// Written by Valley Bell, October 2025
// TODO:
//	- "instrument analyze" mode that can calculate a fixed Key Off timeout + ignore Release

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


typedef struct _channel_info
{
	// bit 0 (01) - arpeggio (effect 0)
	// bit 1 (02) - pitch effect (effect 1/2)
	// bit 2 (04) - pitch to next note (effect 3)
	// bit 3 (08) - vibrato (effect 4)
	// bit 8 (80) - vibrato trigger [for Modulation CC] (effect 4)
	UINT8 flags;
	UINT8 lastNote;
	UINT8 pan2Side;
	UINT8 pan;
	UINT8 vol;
	UINT8 pbRange;
	UINT8 pitchOct;		// YM2612 octave for current pitch (required for proper pitch frequency calculation)
	UINT16 pitchBase;	// pitch of the current MIDI note, 8.8 fixed point (for pitch bend calculation)
	UINT16 curPitch;	// current note pitch (after applied pitch effects), 8.8 fixed point
	INT16 pitchStep;	// raw YM2612 frequency value
	UINT16 pitchEnd;	// 8.8 fixed point
	UINT16 lastPB;
	UINT8 lastMod;
	UINT8 vibSpdCntr;
	UINT8 vibSpeed;
	UINT8 vibDepth;
	UINT8 arpNotes[2];	// arpeggio note offsets
} CHN_INF;
typedef struct _sound_state
{
	UINT32 frameCnt;
	UINT8 rowTicks;
	UINT8 arpTick;
	UINT8 chnCnt;
	CHN_INF chnInf[6];
} SOUND_STATE;


static INT8 CompareFMIns(const UINT8* insA, const UINT8* insB);
static void CreateInstrumentMap(UINT32 romLen, const UINT8* romData, UINT32 startOfs);
static UINT32 GetPatternDataPos(UINT16 orderId, UINT16* patternId);
UINT8 CoreDesign2Mid(UINT32 songLen, const UINT8* songData, UINT16 orderId);
static void PreparseCoreDesign(UINT32 songLen, const UINT8* songData, UINT16 startOrderId, UINT32* loopOfs);
static UINT8 Note2Octave(UINT8 note);
static UINT16 NotePitch2YMFreq(UINT16 pitch, UINT8 preferredOct);
static UINT16 YMFreq2NotePitch(UINT16 ymFreq);
static void ApplyYMDeltaToPitch(UINT16* pitch, UINT8* oct, INT16 delta);
static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData, UINT32 maxFileSize);
static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const UINT8* data);
static void EnsurePBRange(FILE_INF* fInf, MID_TRK_STATE* MTS, CHN_INF* ci, INT16 pitchDiff);
static void MinimizePBRange(FILE_INF* fInf, MID_TRK_STATE* MTS, CHN_INF* ci, INT16 pitchDiff);
static void WriteNotePitch(FILE_INF* fInf, MID_TRK_STATE* MTS, CHN_INF* ci, UINT16 notePitch, UINT8 optional);
static UINT8 StereoMask2Pan(UINT8 mask, UINT8* pan2Side);
static float OPNInsVol2DB(UINT8 algo, const UINT8* tls, UINT8 panMode);
INLINE UINT8 DB2Mid(float DB);
INLINE UINT8 NoteVelWithPan(UINT8 velocity, UINT8 panMode);
INLINE UINT32 Tempo2Mid(UINT16 tickFrames);

UINT8 CoreDesign_InsDump(UINT32 insLen, const UINT8* insData);

INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT16 ReadBE16(const UINT8* data);
INLINE UINT32 ReadBE32(const UINT8* data);
static const char* GetLastDirSepPos(const char* fileName);
INLINE const char* GetFileTitle(const char* fileName);
static UINT8 DetectDriverInfo(void);
static int memcmp_wc(const UINT8* data, const UINT8* compare, UINT32 count);
static UINT32 ScanForData_WC(UINT32 scanLen, const UINT8* scanData,
							UINT32 matchLen, const UINT8* matchData, UINT32 startPos,
							UINT32 step);


// frequency values used by the sound driver
#define BASE_NOTE	10	// The lowest MIDI note in the frequency table is a Bb. (YM2612 f-num 0x0248)
// Note: v2 starts with frequency 0x269, but keeps the clamping to 0x248.
static const UINT16 OPN_Freqs[14] = {
	// Note: The frequencies that repeat for every octave are 0x269 .. 0x48C. The others seem to be there for interpolation.
	0x248, 0x269, 0x28D, 0x2B4, 0x2DD, 0x309, 0x337, 0x368, 0x39C, 0x3D3, 0x40D, 0x44B, 0x48C, 0x4D9,
};
#define PITCH_MIN	(0x0000 | 0x248)	// lowest useable f-num vaulue, anything lower will be clamped
#define PITCH_MAX	(0x3800 | 0x4D9)	// highest useable f-num vaulue, anything higher will be clamped
#define PITCH_WRAP_LOW	0x248	// in-octave frequency below which the next lower octave will be used
#define PITCH_WRAP_HIGH	0x4D0	// in-octave frequency above which the next higher octave will be used
#define PITCH_WRAP_INC	0x244	// when jumping to lower octave, increase f-num by this amount
#define PITCH_WRAP_DEC	0x268	// when jumping to lower octave, decrease f-num by this amount

static const UINT8 ALGO_TL_MASKS[8] = {0x08, 0x08, 0x08, 0x08, 0x0C, 0x0E, 0x0E, 0x0F};


static const char SEQ_COMMANDS_V1[] = {
	'#',	// 00 Note
	'0',	// 02 arpeggio
	'1',	// 04 Portamento Up (pitch slide)
	'2',	// 06 Portamento Down (pitch slide)
	'3',	// 08 Tone Portamento (pitch slide from previous to next note)
	'D',	// 0A Pattern Break
	'B',	// 0C Position Jump (order 0x000..0x0FF)
	'B',	// 0E Position Jump (order 0x100..0x1FF)
	'F',	// 10 set speed (ticks per row)
	'=',	// 12 Note Off
	'E',	// 14 set game callback variable
};
static const char SEQ_COMMANDS_V2[] = {
	'#',	// 00 Note
	'=',	// 02 Note Off
	'0',	// 04 arpeggio
	'1',	// 06 pitch upwards
	'2',	// 08 pitch downwards
	'3',	// 0A pitch to next note
	'4',	// 0C vibrato
	'D',	// 0E pattern break
	'B',	// 10 position jump (order 0x000..0x0FF)
	'B',	// 12 position jump (order 0x100..0x1FF)
	'F',	// 14 set speed (ticks per row)
	'E',	// 16 set game callback variable
};


#define MODE_MUS	0x00
#define MODE_INS	0x01

#define TMODE_TPQ	0
#define TMODE_RPB	1

#define INSMODE_RAW		0
#define INSMODE_CLEAN	1
#define INSMODE_FUZZY	2


static UINT32 ROMLen;
static UINT8* ROMData = NULL;
static UINT32 Z80DumpLen;
static UINT8* Z80DumpData = NULL;
static UINT32 MidLen;
static UINT8* MidData = NULL;

#define BEAT_TICKS		24	// we generally assume 24 ticks (frames) per quarter beat
							// (24 ticks/beat @ 60 Hz = 150 BPM)
#define RPB_TICK_MULT	20	// tick multiplier in "rows per beat" mode
static UINT16 MIDI_RES = BEAT_TICKS;
static UINT16 BEAT_ROWS = 4;
static UINT16 NUM_LOOPS = 2;
static UINT8 TICK_TEMPO = TMODE_TPQ;
static UINT8 INS_MODE = INSMODE_CLEAN;
static INT8 NOTE_VELO = 0;
static UINT8 MOD_CC = 0;
static UINT8 DEBUG_CC = 0;

static UINT32 z80DrvPos = 0x006D5C;
static UINT16 z80BaseBank = 0x3D;
static UINT16 z80DrvDataBase = 0x07B6;
static UINT32 romSongListPos = 0x006BD0;
static UINT16 z80InsTblPos = 0;
static UINT8 z80DrvVer = 1;

static UINT32 Z80DataLen;
static const UINT8* Z80DataPtr;	// pointer to Z80 sound driver data (points to either Z80DumpData or &ROMData[z80DrvPos])
static UINT32 SndDataLen;
static const UINT8* SndDataPtr;	// pointer to sound data (points to &ROMData[z80BaseBank << 15])

// This table remaps instruments so that there are only "unique" instruments.
// i.e. when multiple instruments match aside from panning and volume, they will get the same ID
static UINT8 insTableMap[0x80];

int main(int argc, char* argv[])
{
	int argbase;
	UINT8 retVal;
	UINT32 orderParam;
	UINT8 convMode;
	int result;
	const char* z80DumpFileName;
	const char* romFileName;
	const char* outFileName;
	
	printf("Core Design MegaDrive -> Midi Converter\n---------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage, dump songs:          %s -mus [options] rom.bin output.mid OrderValue\n", argv[0]);
		printf("Usage, dump instruments:    %s -ins [options] rom.bin output.gyb\n", argv[0]);
		printf("Modes:\n");
		printf("    -mus        Music Mode (convert sequences to MID)\n");
		printf("    -ins        Instrument Mode (dump instruments to GYB)\n");
		printf("Options:\n");
		printf("    -Z80Dump fn load file \"fn\" as Z80 sound driver data (use when autodetection fails)\n");
		printf("    -Loops n    Loop song n times. (default: %u)\n", NUM_LOOPS);
		printf("    -TpQ n      Convert with n Ticks per Quarter. (default: %u)\n", MIDI_RES);
		printf("    -RpB n      Scale tempo to n Rows per (quarter) Beat. (default: off)\n");
		printf("    -InsMode n  instrument mode: (default: %u)\n", INS_MODE);
		printf("                0 - raw mode: [mus] dump raw instrument IDs, [ins] dump original instrument TLs\n");
		printf("                1 - clean mode: [mus] same instrument IDs for duplicates, [ins] normalize TLs\n");
		printf("                2 - fuzzy mode: like 1, but ignores all TL registers when comparing instruments\n");
		printf("    -Velo n     convert with fixed note velocity of n (for use with GYB file with \"raw\" InsMode)\n");
		printf("                Values <0 result in velocity |n| with pan-law compensation enabled.\n");
		printf("    -ModCC      convert Vibrato effect to Modulation CCs instead of Pitch Bends\n");
		printf("    -DebugCC    include MIDI CCs for effect commands (for debugging)\n");
		printf("OrderValue parameter:\n");
		printf("        This number has a different effect depending on its value.\n");
		printf("    0x000 .. 0x1FF      dump single song: order ID where the song starts\n");
		printf("    0x200 or larger     dump all songs: ROM offset where the BGM test song list is stored\n");
		printf("                        (This is a list of 2-byte Big Endian order IDs.)\n");
		//printf("Recommended settings: -RpB 8\n");
		return 0;
	}
	
	argbase = 1;
	z80DumpFileName = NULL;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Mus"))
		{
			convMode = MODE_MUS;
		}
		else if (! stricmp(argv[argbase] + 1, "Ins"))
		{
			convMode = MODE_INS;
		}
		else if (! stricmp(argv[argbase] + 1, "Z80Dump"))
		{
			argbase ++;
			if (argbase < argc)
				z80DumpFileName = argv[argbase];
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
					MIDI_RES = BEAT_TICKS;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "RpB"))
		{
			argbase ++;
			if (argbase < argc)
			{
				BEAT_ROWS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! BEAT_ROWS)
				{
					BEAT_ROWS = 4;
					TICK_TEMPO = TMODE_TPQ;
				}
				else
				{
					TICK_TEMPO = TMODE_RPB;
				}
			}
		}
		else if (! stricmp(argv[argbase] + 1, "InsMode"))
		{
			argbase ++;
			if (argbase < argc)
				INS_MODE = (UINT8)strtoul(argv[argbase], NULL, 0);
		}
		else if (! stricmp(argv[argbase] + 1, "Velo"))
		{
			argbase ++;
			if (argbase < argc)
			{
				int velo = strtol(argv[argbase], NULL, 0);
				if (velo >= -0x7F && velo <= 0x7F)
					NOTE_VELO = (INT8)velo;
				else
					NOTE_VELO = 0;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "ModCC"))
		{
			MOD_CC = 1;
		}
		else if (! stricmp(argv[argbase] + 1, "DebugCC"))
		{
			DEBUG_CC = 1;
		}
		else
			break;
		argbase ++;
	}
	
	switch(convMode)
	{
	case MODE_MUS:
		if (argc < argbase + 3)
		{
			printf("Not enough arguments.\n");
			return 0;
		}
		break;
	case MODE_INS:
		if (argc < argbase + 2)
		{
			printf("Not enough arguments.\n");
			return 0;
		}
		break;
	}
	
	romFileName = argv[argbase + 0];
	outFileName = argv[argbase + 1];
	if (z80DumpFileName != NULL)
	{
		retVal = ReadFileData(z80DumpFileName, &Z80DumpLen, &Z80DumpData, 0x2000);	// 8 KB
		if (retVal)
			return 1;
	}
	
	retVal = ReadFileData(romFileName, &ROMLen, &ROMData, 0x400000);	// 4 MB
	if (retVal)
		return 1;
	
	retVal = DetectDriverInfo();
	if (retVal)
	{
		free(ROMData);	ROMData = NULL;
		return 1;
	}
	
	// prepare pointers
	if (Z80DumpData != NULL)
	{
		Z80DataLen = Z80DumpLen;
		Z80DataPtr = Z80DumpData;
	}
	else
	{
		Z80DataLen = ROMLen - z80DrvPos;
		Z80DataPtr = &ROMData[z80DrvPos];
	}
	if (Z80DataLen > 0x2000)
		Z80DataLen = 0x2000;	// limit to 8 KB, which is the MegaDrive Z80's RAM size
	SndDataLen = ROMLen - (z80BaseBank << 15);
	SndDataPtr = &ROMData[z80BaseBank << 15];
	
	CreateInstrumentMap(Z80DataLen, Z80DataPtr, z80InsTblPos);
	
	result = 0;
	switch(convMode)
	{
	case MODE_MUS:
		orderParam = (UINT32)strtoul(argv[argbase + 2], NULL, 0);
		if (orderParam < 0x200)
		{
			// single song mode
			printf("Starting at order ID %u\n", orderParam);
			retVal = CoreDesign2Mid(SndDataLen, SndDataPtr, (UINT16)orderParam);
			if (! retVal)
				WriteFileData(outFileName, MidLen, MidData);
			free(MidData);	MidData = NULL;
			result = retVal;
		}
		else
		{
			// all songs
			const char* fileExt;
			char* outName;
			char* outExt;
			UINT16 curFile;
			UINT16 fileCnt;
			
			fileExt = strrchr(GetFileTitle(outFileName), '.');
			if (fileExt == NULL)
				fileExt = outFileName + strlen(outFileName);
			outName = (char*)malloc(strlen(outFileName) + 0x10);
			strcpy(outName, outFileName);
			outExt = outName + (fileExt - outFileName);
			
			romSongListPos = orderParam;
			
			for (fileCnt = 0; fileCnt < 0x100; fileCnt ++)
			{
				UINT16 orderId = ReadBE16(&ROMData[romSongListPos + fileCnt * 0x02]);
				if (orderId >= 0x200)
					break;
			}
			printf("Song list offset: 0x%06X\n", romSongListPos);
			
			for (curFile = 0; curFile < fileCnt; curFile ++)
			{
				UINT16 orderId = ReadBE16(&ROMData[romSongListPos + curFile * 0x02]);
				printf("File %u / %u (order %u) ...", 1 + curFile, fileCnt, orderId);
				
				retVal = CoreDesign2Mid(SndDataLen, SndDataPtr, orderId);
				if (retVal)
				{
					result = retVal;
					continue;
				}
				
				sprintf(outExt, "_%02X%s", curFile, fileExt);
				if (! retVal)
					WriteFileData(outName, MidLen, MidData);
				free(MidData);	MidData = NULL;
				printf("\n");
			}
		}
		break;
	case MODE_INS:
		if (! z80InsTblPos)
		{
			printf("Can not dump instruments when data pointer was not found!\n");
			result = 1;
			break;
		}
		if (z80InsTblPos >= Z80DataLen)
		{
			printf("Found instrument data pointer was invalid!\n");
			result = 1;
			break;
		}
		
		retVal = CoreDesign_InsDump(Z80DataLen - z80InsTblPos, &Z80DataPtr[z80InsTblPos]);
		if (! retVal)
			WriteFileData(outFileName, MidLen, MidData);
		free(MidData);	MidData = NULL;
		result = retVal;
		break;
	}
	
	printf("Done.\n");
	
	if (Z80DumpData != NULL)
	{
		free(Z80DumpData);	Z80DumpData = NULL;
	}
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	//getchar();
#endif
	
	return 0;
}

static INT8 CompareFMIns(const UINT8* insA, const UINT8* insB)
{
	UINT8 compareMask[0x20];
	UINT8 tlMask;
	UINT8 pos;
	
	memset(compareMask, 0xFF, 0x20);
	compareMask[0x1E] = compareMask[0x1F] = 0x00;	// unused
	compareMask[0x1D] &= ~0xC0;	// strip panning bits
	
	// get TL register mask based on algorithms
	tlMask = ALGO_TL_MASKS[insA[0x1C] & 0x07] & ALGO_TL_MASKS[insB[0x1C] & 0x07];
	if (INS_MODE >= INSMODE_FUZZY)
	{
		// just ignore TL values completely
		// (Some instruments vary in output AND carrier to get
		// e.g. a "soft" and a "hard" attack on the bass.)
		memset(&compareMask[0x04], 0x00, 0x04);
	}
	else
	{
		for (pos = 0; pos < 4; pos ++)
		{
			if (tlMask & (1 << pos))
				compareMask[0x04 + pos] = 0x00;	// don't do an exact match on TL operators that affect volume
		}
	}
	
	for (pos = 0x00; pos < 0x20; pos ++)
	{
		// compare the allowed bits
		INT8 diff = (insB[pos] & compareMask[pos]) - (insA[pos] & compareMask[pos]);
		if (diff > 0)
			return +1;
		else if (diff < 0)
			return -1;
	}
	if (INS_MODE >= INSMODE_FUZZY)
		return 0;	// in "fuzzy comparison mode", just ignore the TL values
	
	// now compare TL operators that affect volume
	for (pos = 0x00; pos < 0x04; pos ++)
	{
		INT8 diffA, diffB, diff;
		if (! (tlMask & (1 << pos)))
			continue;
		// calculate the change (relation) of the specific "output operator"
		// to operator 3, which is always part of the output
		diffA = insA[0x04 + pos] - insA[0x07];
		diffB = insB[0x04 + pos] - insB[0x07];
		// The TL operator change must be the same between both instruments.
		diff = diffB - diffA;
		if (diff > 0)
			return +1;
		else if (diff < 0)
			return -1;
	}
	return 0;
}

static void CreateInstrumentMap(UINT32 romLen, const UINT8* romData, UINT32 startOfs)
{
	const UINT8* insData = &romData[startOfs];
	UINT8 curIns;
	UINT8 compIns;
	
	for (curIns = 0x00; curIns < 0x80; curIns ++)
	{
		insTableMap[curIns] = curIns;
		for (compIns = 0x00; compIns < curIns; compIns ++)
		{
			if (! CompareFMIns(&insData[curIns * 0x20], &insData[compIns * 0x20]))
			{
				insTableMap[curIns] = insTableMap[compIns];
				break;
			}
		}
	}
	
	return;
}

static UINT32 GetPatternDataPos(UINT16 orderId, UINT16* patternId)
{
	UINT16 patternOfs = ReadLE16(&SndDataPtr[orderId * 0x02]);
	UINT32 patPtrPos = z80DrvDataBase + ReadLE16(&Z80DataPtr[z80DrvDataBase]) + patternOfs;
	
	UINT8 patternBank = Z80DataPtr[patPtrPos + 0x00];
	UINT16 patternPos = ReadLE16(&Z80DataPtr[patPtrPos + 0x01]);
	if (patternId != NULL)
		*patternId = patternOfs / 0x03;
	return (patternBank << 15) | (patternPos & 0x7FFF);
}

static void ProcessDriverTicks(FILE_INF* fInf, MID_TRK_STATE* MTS, SOUND_STATE* sndState, UINT16 rows)
{
	for (; rows > 0; rows --)
	{
		UINT16 frm;
		for (frm = 0; frm < sndState->rowTicks; frm ++)
		{
			UINT8 curChn;
			for (curChn = 0; curChn < sndState->chnCnt; curChn ++)
			{
				CHN_INF* ci = &sndState->chnInf[curChn];
				if (ci->pitchBase == 0xFFFF)
					continue;	// prevent processing before the first note
				MTS->midChn = curChn;
				// *Note:* We must apply pitch effects even after Key Off.
				// In "Asterix and the Great Rescue", some instruments (e.g. the bass)
				// have a slow Release phase and the songs apply pitch effects during that phase.
				if (ci->flags & 0x01)
				{
					UINT16 arpPitch = ci->pitchBase;
					if (sndState->arpTick > 0)
						arpPitch += (ci->arpNotes[sndState->arpTick - 1] << 8);
					WriteNotePitch(fInf, MTS, ci, arpPitch, 0);
				}
				else if ((ci->flags & 0x02) && ci->pitchStep != 0)
				{
					ApplyYMDeltaToPitch(&ci->curPitch, &ci->pitchOct, ci->pitchStep);
					if ((ci->pitchStep > 0 && ci->curPitch >= ci->pitchEnd) ||
						(ci->pitchStep < 0 && ci->curPitch <= ci->pitchEnd))
					{
						ci->curPitch = ci->pitchEnd;
						ci->pitchOct = Note2Octave((UINT8)(ci->curPitch >> 8));
						ci->flags &= ~0x02;
					}
					WriteNotePitch(fInf, MTS, ci, ci->curPitch, 0);
				}
				else if (ci->flags & 0x08)
				{
					INT16 pitchMod;
					INT16 ymFreq;
					
					ci->vibSpdCntr += ci->vibSpeed;
					pitchMod = (ci->vibSpdCntr & 0x80) ? -(INT8)ci->vibSpdCntr : ci->vibSpdCntr;	// effectively abs(ci->vibSpdCntr)
					pitchMod -= 0x40;
					pitchMod /= (1 << ci->vibDepth);
					
					ymFreq = (INT16)NotePitch2YMFreq(ci->curPitch, ci->pitchOct);
					ymFreq += pitchMod;	// apply OPN frequency delta
					WriteNotePitch(fInf, MTS, ci, YMFreq2NotePitch(ymFreq), 0);
					// The vibrato frequency is applied only temporarily and not saved back.
				}
			}
			if (TICK_TEMPO == TMODE_TPQ)
			{
				MTS->curDly += 1;
			}
			else if (TICK_TEMPO == TMODE_RPB)
			{
				UINT32 tick0 = (frm + 0) * BEAT_TICKS * RPB_TICK_MULT / (BEAT_ROWS * sndState->rowTicks);
				UINT32 tick1 = (frm + 1) * BEAT_TICKS * RPB_TICK_MULT / (BEAT_ROWS * sndState->rowTicks);
				MTS->curDly += (tick1 - tick0);
			}
			sndState->arpTick ++;
			sndState->arpTick %= 3;
		}
	}
	return;
}

UINT8 CoreDesign2Mid(UINT32 songLen, const UINT8* songData, UINT16 orderId)
{
	UINT16 patternId;
	UINT32 inPos;
	UINT32 loopOfs;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	SOUND_STATE sndState;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 curChn;
	UINT16 mstLoopCnt;
	char tempStr[0x20];
	UINT8 tempArr[0x04];
	UINT8 tempU8;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	loopOfs = 0x00;
	PreparseCoreDesign(songLen, songData, orderId, &loopOfs);
	
	if (TICK_TEMPO == TMODE_RPB)
		WriteMidiHeader(&midFileInf, 0x0000, 1, MIDI_RES * RPB_TICK_MULT);
	else
		WriteMidiHeader(&midFileInf, 0x0000, 1, MIDI_RES);
	
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.midChn = 0x00;
	
	sndState.frameCnt = 0;
	sndState.rowTicks = 1+3;
	sndState.arpTick = 1;	// the driver increments the counter before doing any parsing
	sndState.chnCnt = sizeof(sndState.chnInf) / sizeof(sndState.chnInf[0]);
	if (TICK_TEMPO == TMODE_TPQ)
	{
		WriteBE32(tempArr, Tempo2Mid(BEAT_TICKS / BEAT_ROWS));
		WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
	}
	
	inPos = GetPatternDataPos(orderId, &patternId);
	sprintf(tempStr, "Order %u (Pattern %u)", orderId, patternId);
	WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(tempStr), tempStr);
	
	for (curChn = 0; curChn < sndState.chnCnt; curChn ++)
	{
		CHN_INF* ci = &sndState.chnInf[curChn];
		ci->flags = 0x00;
		ci->lastNote = 0xFF;
		ci->pitchBase = 0xFFFF;
		ci->vol = 0x7F;
		ci->pan = 0xFF;	// "uninitalized"
		ci->pan2Side = 0;
		ci->pbRange = 0;
		ci->lastPB = 0xFFFF;
		ci->lastMod = 0xFF;
	}
	
	mstLoopCnt = 0;
	trkEnd = 0;
	while(inPos < songLen && ! trkEnd)
	{
		if (midFileInf.pos >= 0x100000)	// 1 MB
		{
			printf("Cancelling conversion due to large output file! (possible infinite loop)\n");
			break;
		}
		if (inPos == loopOfs && mstLoopCnt == 0)
		{
			MTS.midChn = 0x00;
			WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
			mstLoopCnt ++;
		}
		
		curCmd = songData[inPos];	inPos ++;
		if (curCmd == 0x00)	// data end
		{
			trkEnd = 1;
		}
		else if (curCmd >= 0xC0)	// Delay
		{
			UINT16 ticks = (curCmd & 0x3F) + 1;
			ProcessDriverTicks(&midFileInf, &MTS, &sndState, ticks);
		}
		else
		{
			UINT8 chn = (curCmd & 0xE0) >> 5;
			CHN_INF* ci = &sndState.chnInf[chn];
			UINT8 cmd = (curCmd & 0x1E);
			UINT8 procNote = (curCmd & 0x01);
			char realCmd = '\0';
			
			MTS.midChn = chn;
			ci->flags &= ~0x0F;	// remove pitch effect flags
			if (z80DrvVer == 1)
			{
				UINT8 cmdId = cmd >> 1;
				if (cmdId < sizeof(SEQ_COMMANDS_V1))
					realCmd = SEQ_COMMANDS_V1[cmdId];
			}
			else if (z80DrvVer == 2)
			{
				UINT8 cmdId = cmd >> 1;
				if (cmdId < sizeof(SEQ_COMMANDS_V2))
					realCmd = SEQ_COMMANDS_V2[cmdId];
			}
			switch(realCmd)
			{
			case '#':	// Note
				procNote = 1;
				break;
			case '0':	// arpeggio
				tempU8 = songData[inPos];	inPos ++;
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x30, tempU8);
				ci->arpNotes[0] = (tempU8 >> 0) & 0x0F;
				ci->arpNotes[1] = (tempU8 >> 4) & 0x0F;
				ci->flags |= 0x01;
				break;
			case '1':	// pitch upwards
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x31, songData[inPos]);
				ci->pitchStep = (UINT16)songData[inPos];	inPos ++;
				ci->pitchEnd = (BASE_NOTE + 7 * 12 + 13) << 8;	// end = last note in the frequency table
				ci->flags |= 0x02;
				break;
			case '2':	// pitch downwards
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x32, songData[inPos]);
				ci->pitchStep = 0 - (UINT16)songData[inPos];	inPos ++;
				ci->pitchEnd = BASE_NOTE << 8;	// end = first note in the frequency table
				ci->flags |= 0x02;
				break;
			case '3':	// pitch to next note
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x33, songData[inPos]);
				ci->pitchStep = (UINT16)songData[inPos];	inPos ++;
				ci->flags |= 0x06;
				break;
			case '4':	// vibrato
				tempU8 = songData[inPos];	inPos ++;
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x34, tempU8);
				ci->vibSpeed = ((tempU8 >> 0) & 0x0F) * 4;
				ci->vibDepth = ((tempU8 >> 5) & 0x07) + 0;
				ci->vibSpdCntr = 0;
				if (MOD_CC)	// vibrato via Modulation Controller
				{
					// vibDepth is a divider exponent (0 = strongest depth, 7 = weakest depth)
					UINT8 modDepth;
					if (ci->vibDepth == 0)
						modDepth = 127;
					else if (ci->vibDepth == 1)
						modDepth = 112;
					else
						modDepth = (UINT8)(384 >> ci->vibDepth);	// vibDepth == 2 equals approximately MIDI Modulation 96
					if (modDepth != ci->lastMod)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, modDepth);
						ci->lastMod = modDepth;
					}
					ci->flags |= 0x88;
				}
				else	// vibrato via Pitch Bends
				{
					ci->flags |= 0x08;
				}
				break;
			case 'D':	// pattern break
				procNote = 0;
				orderId ++;
				
				ProcessDriverTicks(&midFileInf, &MTS, &sndState, 1);
				
				MTS.midChn = 0x00;
				inPos = GetPatternDataPos(orderId, &patternId);
				sprintf(tempStr, "Order %u (Pattern %u)", orderId, patternId);
				WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(tempStr), tempStr);
				break;
			case 'B':	// position jump (order 0x000..0x1FF)
				procNote = 0;
				orderId = ((cmd & 0x02) << 7) | (songData[inPos] << 0);	inPos ++;
				sprintf(tempStr, "Position Jump -> Order %u", orderId);
				WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(tempStr), tempStr);
				
				ProcessDriverTicks(&midFileInf, &MTS, &sndState, 1);
				
				MTS.midChn = 0x00;
				inPos = GetPatternDataPos(orderId, &patternId);
				if (inPos == loopOfs)
				{
					if (mstLoopCnt < 0x80)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
					if (mstLoopCnt >= NUM_LOOPS)
					{
						trkEnd = 1;
						break;
					}
					mstLoopCnt ++;
				}
				sprintf(tempStr, "Order %u (Pattern %u)", orderId, patternId);
				WriteMetaEvent(&midFileInf, &MTS, 0x01, strlen(tempStr), tempStr);
				break;
			case 'F':	// set speed (ticks per row)
				sndState.rowTicks = songData[inPos] + 1;	inPos ++;
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, cmd);
				if (TICK_TEMPO == TMODE_RPB)
				{
					WriteBE32(tempArr, Tempo2Mid(sndState.rowTicks));
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
				}
				break;
			case '=':	// Note Off
				if (ci->lastNote != 0xFF)
				{
					WriteEvent(&midFileInf, &MTS, 0x90, ci->lastNote, 0x00);
					ci->lastNote = 0xFF;
				}
				break;
			case 'E':	// set game callback variable
				tempU8 = songData[inPos];
				printf("Game Callback = value %02X (chn %u) at %06X\n", tempU8, chn, inPos - 0x01);
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x10, tempU8);
				inPos ++;
				break;
			default:
				printf("Unknown event %02X (chn %u) at %06X\n", cmd, chn, inPos - 0x01);
				if (DEBUG_CC)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, cmd);
				trkEnd = 1;
				break;
			}
			
			if (procNote)
			{
				UINT8 note = songData[inPos];	inPos ++;
				if (ci->lastNote != 0xFF)
					WriteEvent(&midFileInf, &MTS, 0x90, ci->lastNote, 0x00);
				
				// handle Modulation CC
				if ((ci->flags & 0x88) == 0x80)	// vibrato was triggered last time - turn it off now
				{
					if (ci->lastMod != 0)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, 0);
						ci->lastMod = 0;
					}
					ci->flags &= ~0x80;
				}
				
				if (note & 0x01)
				{
					UINT8 ins = note >> 1;	// offset -> index
					if (INS_MODE == INSMODE_RAW)
						WriteEvent(&midFileInf, &MTS, 0xC0, ins, 0x00);	// raw instrument ID
					else
						WriteEvent(&midFileInf, &MTS, 0xC0, insTableMap[ins], 0x00);	// remapped instrument
					if (z80InsTblPos != 0)	// only available when Z80 driver info is present
					{
						const UINT8* insDataPtr = &Z80DataPtr[z80InsTblPos + ins * 0x20];
						UINT8 midPan = StereoMask2Pan(insDataPtr[0x1D] & 0xC0, &ci->pan2Side);
						if (midPan != ci->pan)
						{
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, midPan);
							ci->pan = midPan;
						}
						ci->vol = DB2Mid(OPNInsVol2DB(insDataPtr[0x1C] & 0x07, &insDataPtr[0x04], ci->pan2Side));
					}
					// Note: This also sets volume + panning.
					note = songData[inPos];	inPos ++;
				}
				
				note >>= 1;	// offset -> index
				if (z80DrvVer == 2)
					note += BASE_NOTE + 1;	// v2: frequency table starts with 0x269
				else
					note += BASE_NOTE;	// v1: frequency table starts with 0x248
				ci->pitchBase = note << 8;
				if (ci->flags & 0x01)
				{
					UINT8 maxArp = (ci->arpNotes[0] >= ci->arpNotes[1]) ? ci->arpNotes[0] : ci->arpNotes[1];
					UINT16 arpPitch = ci->pitchBase;
					ci->curPitch = ci->pitchBase;
					ci->pitchOct = Note2Octave((UINT8)(ci->curPitch >> 8));
					MinimizePBRange(&midFileInf, &MTS, ci, maxArp << 8);
					
					if (sndState.arpTick > 0)
						arpPitch += (ci->arpNotes[sndState.arpTick - 1] << 8);
					WriteNotePitch(&midFileInf, &MTS, ci, arpPitch, 1);
				}
				else if (ci->flags & 0x04)
				{
					ci->pitchEnd = ci->pitchBase;
					if (ci->curPitch > ci->pitchEnd)
						ci->pitchStep *= -1;
					MinimizePBRange(&midFileInf, &MTS, ci, ci->curPitch - ci->pitchBase);
					WriteNotePitch(&midFileInf, &MTS, ci, ci->curPitch, 0);
				}
				else
				{
					ci->curPitch = ci->pitchBase;
					ci->pitchOct = Note2Octave((UINT8)(ci->curPitch >> 8));
					WriteNotePitch(&midFileInf, &MTS, ci, ci->curPitch, 1);	// reset PB to 0 (no redundant writes)
					MinimizePBRange(&midFileInf, &MTS, ci, 0);
				}
				if (NOTE_VELO == 0)
					WriteEvent(&midFileInf, &MTS, 0x90, note, ci->vol);
				else if (NOTE_VELO > 0)
					WriteEvent(&midFileInf, &MTS, 0x90, note, (UINT8)NOTE_VELO);
				else if (NOTE_VELO < 0)
					WriteEvent(&midFileInf, &MTS, 0x90, note, NoteVelWithPan((UINT8)(-NOTE_VELO), ci->pan2Side));
				ci->lastNote = note;
			}
			if ((ci->flags & 0x88) == 0x88)	// handle Modulation CC - vibrato was triggered with this command
				ci->flags &= ~0x08;	// remove "trigger" flag
		}
	}
	
	// terminate notes, reset modulation / pitch bend
	for (curChn = 0; curChn < sndState.chnCnt; curChn ++)
	{
		CHN_INF* ci = &sndState.chnInf[curChn];
		MTS.midChn = curChn;
		if (ci->lastNote != 0xFF)
			WriteEvent(&midFileInf, &MTS, 0x90, ci->lastNote, 0x00);
		if (ci->lastMod != 0)
			WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, 0);
		WriteNotePitch(&midFileInf, &MTS, ci, ci->pitchBase, 1);
	}
	
	MTS.midChn = 0x00;
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}
	
static void PreparseCoreDesign(UINT32 songLen, const UINT8* songData, UINT16 startOrderId, UINT32* loopOfs)
{
	UINT16 orderId;
	UINT32 inPos;
	UINT32 tickCnt;
	UINT16 rowTicks;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 orderUsage[0x200 / 8];
	UINT8 orderMask;
	
	tickCnt = 0;
	*loopOfs = 0x00;
	memset(&orderUsage, 0x00, sizeof(orderUsage));
	orderId = startOrderId;
	rowTicks = 1;
	
	inPos = GetPatternDataPos(orderId, NULL);
	orderUsage[orderId / 8] |= (1 << (orderId & 0x07));
	trkEnd = 0;
	while(inPos < songLen && ! trkEnd)
	{
		curCmd = songData[inPos];	inPos ++;
		if (curCmd == 0x00)	// data end
		{
			trkEnd = 1;
		}
		else if (curCmd >= 0xC0)	// Delay
		{
			UINT16 ticks = (curCmd & 0x3F) + 1;
			tickCnt += ticks * rowTicks;
		}
		else
		{
			UINT8 chn = (curCmd & 0xE0) >> 5;
			UINT8 cmd = (curCmd & 0x1E);
			UINT8 procNote = (curCmd & 0x01);
			char realCmd = '\0';
			
			if (z80DrvVer == 1)
			{
				UINT8 cmdId = cmd >> 1;
				if (cmdId < sizeof(SEQ_COMMANDS_V1))
					realCmd = SEQ_COMMANDS_V1[cmdId];
			}
			else if (z80DrvVer == 2)
			{
				UINT8 cmdId = cmd >> 1;
				if (cmdId < sizeof(SEQ_COMMANDS_V2))
					realCmd = SEQ_COMMANDS_V2[cmdId];
			}
			switch(realCmd)
			{
			case '#':	// Note
				procNote = 1;
				break;
			case '=':	// Note Off
				break;
			case '0':	// arpeggio
			case '1':	// pitch upwards
			case '2':	// pitch downwards
			case '3':	// pitch to next note
			case '4':	// vibrato
			case 'E':	// set game callback variable
				inPos ++;
				break;
			case 'D':	// pattern break
				procNote = 0;
				tickCnt += rowTicks;
				orderId ++;
				if (orderId >= 0x200)
				{
					trkEnd = 1;
					break;
				}
				
				inPos = GetPatternDataPos(orderId, NULL);
				orderMask = 1 << (orderId & 0x07);
				orderUsage[orderId / 8] |= orderMask;
				break;
			case 'B':	// position jump
				procNote = 0;
				tickCnt += rowTicks;
				orderId = ((cmd & 0x02) << 7) | (songData[inPos] << 0);	inPos ++;
				
				inPos = GetPatternDataPos(orderId, NULL);
				orderMask = 1 << (orderId & 0x07);
				if (orderUsage[orderId / 8] & orderMask)
				{
					*loopOfs = inPos;
					trkEnd = 1;
				}
				else
				{
					orderUsage[orderId / 8] |= orderMask;
				}
				break;
			case 'F':	// set tick speed
				rowTicks = songData[inPos] + 1;
				inPos ++;
				break;
			default:
				trkEnd = 1;
				break;
			}
			if (procNote)
			{
				if (songData[inPos] & 0x01)
					inPos += 0x02;
				else
					inPos += 0x01;
			}
		}
	}
	
	return;
}

static UINT8 Note2Octave(UINT8 note)
{
	// The note table has 1 additional frequency at the beginning and end.
	INT8 octave = (note - 1 - BASE_NOTE) / 12;
	if (octave < 0)
		return 0;
	else if (octave > 7)
		return 7;
	else
		return (UINT8)octave;
}

static UINT16 NotePitch2YMFreq(UINT16 pitch, UINT8 preferredOct)
{
	UINT8 octave;
	INT8 note;
	UINT8 frac;
	UINT16 ymFreq;
	
	frac = (pitch >> 0) & 0xFF;
	note = (pitch >> 8) & 0xFF;
	// We need a separate "octave" setting, so that we can represent pitch effects correctly.
	// e.g. octave 2, freq 0x268 is *not* the same as octave 1, freq 0x4D0, even though
	// those result in the same 8.8 fixed point pitch value.
	if (preferredOct == 0xFF)
		octave = Note2Octave((UINT8)note);
	else
		octave = preferredOct;
	note = note - (octave * 12) - BASE_NOTE;
	if (note < 0)
	{
		note = 0;
		frac = 0;
	}
	else if (note >= 13)	// >= so that we don't try to interpolate at note==13
	{
		note = 13;
		frac = 0;
	}
	
	ymFreq = OPN_Freqs[note];
	if (frac > 0)
	{
		UINT16 noteFreqDiff = OPN_Freqs[note + 1] - ymFreq;
		//ymFreq += noteFreqDiff * frac / 0x100;	// linear curve
		ymFreq += (UINT16)((pow(2, frac / 256.0) - 1.0) * noteFreqDiff + 0.5);	// exponential curve (more accurate)
	}
	return (octave * 0x800) | ymFreq;
}

static UINT16 YMFreq2NotePitch(UINT16 ymFreq)
{
	UINT8 octave = (ymFreq / 0x800) & 0x07;
	UINT8 note;
	UINT8 frac;
	
	ymFreq &= 0x7FF;
	for (note = 0; note < 12; note ++)
	{
		if (OPN_Freqs[note + 1] > ymFreq)
			break;
	}
	if (note >= 13 || ymFreq <= OPN_Freqs[note])
	{
		frac = 0;
	}
	else
	{
		UINT16 noteFreqDiff = OPN_Freqs[note + 1] - OPN_Freqs[note];
		UINT16 freqDiff = ymFreq - OPN_Freqs[note];
		double freqFrac = (double)freqDiff / (double)noteFreqDiff;
		double dFrac = log(1.0 + freqFrac) / 0.693147180559945309417 * 0x100;
		if (dFrac < 0)
			dFrac = 0;
		else if (dFrac > 255)
			dFrac = 255;
		frac = (UINT8)(dFrac + 0.5);
	}
	note = BASE_NOTE + note + octave * 12;
	return (note << 8) | (frac << 0);
}

static void ApplyYMDeltaToPitch(UINT16* pitch, UINT8* oct, INT16 delta)
{
	INT16 ymFreq = (INT16)NotePitch2YMFreq(*pitch, *oct);
	
	// apply OPN frequency delta
	ymFreq += delta;
	// do octave jumps and clamping like the actual sound driver
	if ((ymFreq & 0x7FF) < PITCH_WRAP_LOW)
	{
		if ((ymFreq & 0x3800) > 0)
		{
			ymFreq = ymFreq - 0x800 + PITCH_WRAP_INC;
			(*oct) --;
		}
		else if (ymFreq < PITCH_MIN)
		{
			ymFreq = PITCH_MIN;
		}
	}
	else if ((ymFreq & 0x7FF) >= PITCH_WRAP_HIGH)
	{
		if ((ymFreq & 0x3800) < 0x3800)
		{
			ymFreq = ymFreq + 0x800 - PITCH_WRAP_DEC;
			(*oct) ++;
		}
		else if (ymFreq > PITCH_MAX)
		{
			ymFreq = PITCH_MAX;
		}
	}
	
	*pitch = YMFreq2NotePitch(ymFreq);
	return;
}

static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData, UINT32 maxFileSize)
{
	FILE* hFile = fopen(fileName, "rb");
	if (hFile == NULL)
	{
		printf("Error reading %s!\n", fileName);
		return 0xFF;
	}
	
	fseek(hFile, 0, SEEK_END);
	*retSize = ftell(hFile);
	if (*retSize > maxFileSize)
		*retSize = maxFileSize;
	
	*retData = (UINT8*)malloc(*retSize);
	fseek(hFile, 0x00, SEEK_SET);
	fread(*retData, 0x01, *retSize, hFile);
	
	fclose(hFile);
	
	return 0x00;
}

static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const UINT8* data)
{
	FILE* hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(data, 0x01, dataLen, hFile);
	fclose(hFile);
	
	return 0x00;
}


static void EnsurePBRange(FILE_INF* fInf, MID_TRK_STATE* MTS, CHN_INF* ci, INT16 pitchDiff)
{
	UINT16 absPDiff = (pitchDiff < 0) ? -pitchDiff : pitchDiff;
	UINT8 minRange = (absPDiff + 0xFF) >> 8;
	
	if (minRange < 2)
		minRange = 2;
	if (ci->pbRange >= minRange)
		return;
	
	//if (minRange < 2)
	//	minRange = 2;
	//else
	if (minRange <= 12)
		minRange = 12;
	else if (minRange <= 24)
		minRange = 24;
	else if (minRange <= 48)
		minRange = 48;
	else if (minRange <= 64)
		minRange = 64;
	else if (minRange <= 96)
		minRange = 96;
	else
		minRange = 127;
	WriteEvent(fInf, MTS, 0xB0, 0x65, 0x00);
	WriteEvent(fInf, MTS, 0xB0, 0x64, 0x00);
	WriteEvent(fInf, MTS, 0xB0, 0x06, minRange);
	ci->pbRange = minRange;
	return;
}

static void MinimizePBRange(FILE_INF* fInf, MID_TRK_STATE* MTS, CHN_INF* ci, INT16 pitchDiff)
{
	UINT16 absPDiff = (pitchDiff < 0) ? -pitchDiff : pitchDiff;
	UINT8 minRange = (absPDiff + 0xFF) >> 8;
	
	if (ci->pbRange <= 12)
		return;	// This is generally fine and nice to handle.
	// For larger PB ranges, reset them so that we choose a good minimum next time.
	if (ci->pbRange > minRange)
		ci->pbRange = 0;
	return;
}

static void WriteNotePitch(FILE_INF* fInf, MID_TRK_STATE* MTS, CHN_INF* ci, UINT16 notePitch, UINT8 optional)
{
	INT16 pitchDiff = notePitch - ci->pitchBase;
	INT32 pbVal;
	UINT16 midPB;
	
	if (optional && pitchDiff == 0 && ci->pbRange == 0)
		return;
	EnsurePBRange(fInf, MTS, ci, pitchDiff);
	
	pbVal = pitchDiff * 8192 / ci->pbRange / 256;
	if (pbVal < -0x2000)
		pbVal = -0x2000;
	else if (pbVal > +0x1FFF)
		pbVal = +0x1FFF;
	midPB = (UINT16)(0x2000 + pbVal);
	if (optional && midPB == ci->lastPB)
		return;
	WriteEvent(fInf, MTS, 0xE0, (midPB >> 0) & 0x7F, (midPB >> 7) & 0x7F);
	ci->lastPB = midPB;
	
	return;
}

static UINT8 StereoMask2Pan(UINT8 mask, UINT8* pan2Side)
{
	UINT8 midPan = 0x40;
	UINT8 panSide = 0;
	
	switch(mask & 0xC0)
	{
	case 0x40:	// Left Channel
		midPan = 0x01;
		panSide = 1;
		break;
	case 0x80:	// Right Channel
		midPan = 0x7F;
		panSide = 1;
		break;
	case 0x00:	// No Channel
	case 0xC0:	// Both Channels
		// keep default values
		break;
	}
	if (pan2Side != NULL)
		*pan2Side = panSide;
	return midPan;
}

static float OPNInsVol2DB(UINT8 algo, const UINT8* tls, UINT8 panMode)
{
	UINT8 tlMask = ALGO_TL_MASKS[algo & 0x07];
	double logVol;
	
	if (tlMask == 0x08)
	{
		logVol = (double)tls[3] / -8.0;	// 8 steps for "half volume" (== -6 db)
	}
	else
	{
		UINT8 tlIdx;
		double volume = 0.0;	// sum the raw (linear) amplitude of all "output operators"
		for (tlIdx = 0; tlIdx < 4; tlIdx ++)
		{
			if (tlMask & (1 << tlIdx))
				volume += pow(2.0, tls[tlIdx] / -8.0);
		}
		//logVol = log(volume) / log(2.0);
		logVol = log(volume) * 0.693147180559945309417;
	}
	if (panMode & 0x01)
		logVol -= 0.5;	// panned to the side - dampen volume a bit
	if (1)
		logVol -= 1.0;	// reduce volume further to be able to accurately capture "4 op additive"
	
	if (logVol > 0.0)
		logVol = 0.0;
	return (float)(logVol * 6.0);	// -6 db = half volume
}

INLINE UINT8 DB2Mid(float DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

INLINE UINT8 NoteVelWithPan(UINT8 velocity, UINT8 panMode)
{
	if (panMode & 0x01)
		return (UINT8)(velocity * 0.841395 + 0.5);	// panned to the side - dampen volume by 10^(-3db/40db)
	else
		return velocity;	// centered - full volume
}

INLINE UINT32 Tempo2Mid(UINT16 rowTicks)
{
	// formula: (60 000 000 / 150 BPM) * (MIDI_RES / 24_BEAT_TICKS) * (BEAT_ROWS * rowTicks / 24_BEAT_TICKS)
	return 6250 * MIDI_RES * BEAT_ROWS * rowTicks / 9;
}

UINT8 CoreDesign_InsDump(UINT32 insLen, const UINT8* insData)
{
	UINT32 gybAlloc;
	UINT8* gybData;
	UINT8 insCnt;
	UINT8 curIns;
	char tempStr[0x80];
	UINT32 curPos;
	
#if 0
	// Not all instruments seem to be fully valid data,
	// (e.g. instrument 0x1F in Asterix and the Great Rescue)
	// so the detection doesn't work properly.
	for (insCnt = 0; insCnt < 0x80; insCnt ++)
	{
		const UINT8* insPtr = &insData[insCnt * 0x20];
		if (insPtr[0x1E] != 0x00 || insPtr[0x1F] != 0x00)
			break;
	}
	printf("Instruments counted: 0x%02X\n", insCnt);
#else
	// I'll just hardcode it to the maximum number of instruments. (AGR uses all of them)
	insCnt = 0x80;
#endif
	
	// required space: 0x06 + 0x100 + 0x80*[insDataLen==0x20 + 0x01 + nameMax==0x32] + 0x04 = 0x2A8A
	gybAlloc = 0x4000;	// This is be more than enough space
	gybData = (UINT8*)malloc(gybAlloc);
	
	// write header
	curPos = 0x00;
	gybData[curPos + 0x00] = 26;		// signature byte 1
	gybData[curPos + 0x01] = 12;		// signature byte 2
	gybData[curPos + 0x02] = 0x02;		// version
	gybData[curPos + 0x03] = insCnt;	// melody instruments
	gybData[curPos + 0x04] = 0x00;		// drum instruments
	curPos += 0x05;
	
	// write MIDI instrument mappings (melody + drums)
	memset(&gybData[curPos], 0xFF, 0x80 * 2);	// default: not assigned (0xFF)
	// order is: melody 0, drum 0, melody 1, drum 1, ..., drum 127
	for (curIns = 0; curIns < insCnt; curIns ++)
	{
		gybData[curPos + curIns * 0x02 + 0x00] = curIns;	// GM Mapping: melody
	}
	curPos += 0x100;
	
	// write LFO Value
	gybData[curPos] = (Z80DataPtr != NULL) ? Z80DataPtr[z80DrvDataBase + 0x04] : 0x00;
	curPos += 0x01;
	
	// write instrument data
	for (curIns = 0; curIns < insCnt; curIns ++, curPos += 0x20)
	{
		// The register order happens to match the GYB format:
		// 30 34 38 3C 40 44 48 4C 50 54 58 5C 60 64 68 6C
		// 70 74 78 7C 80 84 88 8C 90 94 98 9C B0 B4
		memcpy(&gybData[curPos + 0x00], &insData[curIns * 0x20], 0x1E);
		gybData[curPos + 0x1E] = 0;		// transposition
		gybData[curPos + 0x1F] = 0x00;	// padding
		
		if (INS_MODE == INSMODE_CLEAN || INS_MODE == INSMODE_FUZZY)
		{
			// normalize instruments
			UINT8 tlMask = ALGO_TL_MASKS[gybData[curPos + 0x1C] & 0x07];
			UINT8 tlIdx;
			UINT8 tlMin = 0x7F;
			// determine loudest TL operator (i.e. smallest TL attenuation value)
			for (tlIdx = 0; tlIdx < 4; tlIdx ++)
			{
				if (tlMask & (1 << tlIdx))
				{
					UINT8 tl = gybData[curPos + 0x04 + tlIdx] & 0x7F;
					if (tl < tlMin)
						tlMin = tl;
				}
			}
			// scale volume so that the loudest TL operator gets "attenuation = 0"
			for (tlIdx = 0; tlIdx < 4; tlIdx ++)
			{
				if (tlMask & (1 << tlIdx))
					gybData[curPos + 0x04 + tlIdx] -= tlMin;
			}
		}
	}
	
	// write instrument names
	for (curIns = 0; curIns < insCnt; curIns ++)
	{
		int nameLen;
		if (insTableMap[curIns] != curIns)
			nameLen = sprintf(tempStr, "Instrument %02X (var. of %02X)", curIns, insTableMap[curIns]);
		else
			nameLen = sprintf(tempStr, "Instrument %02X", curIns);
		if (nameLen > 0xFF)
			nameLen = 0xFF;
		
		gybData[curPos + 0x00] = (UINT8)nameLen;
		memcpy(&gybData[curPos + 0x01], tempStr, nameLen);
		curPos += 0x01 + nameLen;
	}
	
	// "dummy" checksum
	gybData[curPos + 0x00] = 0x00;
	gybData[curPos + 0x01] = 0x00;
	gybData[curPos + 0x02] = 0x00;
	gybData[curPos + 0x03] = 0x00;
	curPos += 0x04;
	
	MidData = gybData;
	MidLen = curPos;
	
	return 0x00;
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x00] << 0) | (data[0x01] << 8);
}

INLINE UINT16 ReadBE16(const UINT8* data)
{
	return	(data[0x00] << 8) | (data[0x01] << 0);
}

INLINE UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) | (data[0x02] << 8) | (data[0x03] << 0);
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

static UINT8 DetectDriverInfo(void)
{
	UINT32 z80Len;
	const UINT8* z80Data;

	if (Z80DumpData != NULL)
	{
		z80Len = Z80DumpLen;
		z80Data = Z80DumpData;
	}
	else
	{
		// --- search for sound driver ---
		static const UINT8 Z80_DRV_LOADER[] = {
			0x41, 0xF9, 0xAA, 0xAA, 0xAA, 0xAA,	//	LEA 	xxxxxxx.L, A0
			0x43, 0xF9, 0x00, 0xA0, 0x00, 0x00,	//	LEA 	$A00000.L, A1
			0x3E, 0x3C,							//	MOVE.W	#xxxx, D7
		};
		
		UINT32 pos = ScanForData_WC(ROMLen, ROMData, sizeof(Z80_DRV_LOADER), Z80_DRV_LOADER, 0x00, 0x02);
		if (pos == (UINT32)-1)
		{
			printf("Unable to find Z80 sound driver.\n");
			return 0x01;
		}
		z80DrvPos = ReadBE32(&ROMData[pos + 0x02]) & 0x00FFFFFF;
		if (z80DrvPos >= 0x880000 && z80DrvPos < 0x900000)	// first 512 KiB of ROM in 32X mode
			z80DrvPos &= 0x07FFFF;
		
		printf("Z80 sound driver ROM offset: 0x%06X (loader: 0x%06X)\n", z80DrvPos, pos);
		if (z80DrvPos >= 0x00E00000)	// RAM area
		{
			printf("Sound driver location points to 68000 RAM - unable to proceed. (compressed driver data?)\n");
			return 0x01;
		}
		if (z80DrvPos >= ROMLen)
		{
			printf("Sound driver location points to invalid location - unable to proceed.\n");
			return 0x01;
		}
		z80Len = ROMLen - z80DrvPos;
		if (z80Len > 0x2000)
			z80Len = 0x2000;
		z80Data = &ROMData[z80DrvPos];
	}
	
	{
		// --- search for Z80 ROM bank with sound data ---
		static const UINT8 Z80_BANK_LOADER[] = {
			0x13, 0xFC, 0x00, 0xAA, 0x00, 0xA0, 0x00, 0x40,	// MOVE.B	#xx, $A00040.L
		};
		// for Sega 32X games, the banking code looks like this:
		//		13 FC 00 xx 00 A0 00 40	MOVE.B	#xx, $A00040.L	; sound driver bank
		//		13 FC 00 xx 00 A1 51 04	MOVE.B	#xx, $A15404.L	; 32X ROM bank for 900000..9FFFFF
		
		UINT32 pos = ScanForData_WC(ROMLen, ROMData, sizeof(Z80_BANK_LOADER), Z80_BANK_LOADER, 0x00, 0x02);
		if (pos == (UINT32)-1)
		{
			printf("Sound data bank ID not found.\n");
			return 0x02;
		}
		z80BaseBank = ROMData[pos + 0x03];
		if (ReadBE32(&ROMData[pos + 0x0C]) == 0xA15104)	// check for write to 32X ROM bank register
		{
			// Sega 32X mode
			UINT8 bank90 = ROMData[pos + 0x0B] & 0x03;
			z80BaseBank |= 0x100;
			if (z80BaseBank >= 0x110 && z80BaseBank < 0x120)	// 68k offset 880000..8FFFFF
				z80BaseBank &= 0x00F;	// equals ROM offset 000000..07FFFF
			else if (z80BaseBank >= 0x120 && z80BaseBank < 0x140)	// 68k offset 900000..9FFFFF
				z80BaseBank = (bank90 * 0x20) | (z80BaseBank & 0x1F);
		}
		printf("Sound data bank: 0x%02X (ROM offset 0x%06X)\n", z80BaseBank, z80BaseBank << 15);
		if ((UINT32)(z80BaseBank << 15) >= ROMLen)
		{
			printf("Sound data bank points to invalid location - unable to proceed.\n");
			return 0x01;
		}
	}
	
	{
		// --- search for sound driver main data --- (instruments, pattern pointers, etc.)
		static const UINT8 Z80_DRV_BASE[] = {
			0x7E,					// LD	A, (HL)
			0x23,					// INC	HL
			0x66,					// LD	H, (HL)
			0x6F,					// LD	L, A
			0xED, 0x5B, 0xAA, 0xAA,	// LD	DE, (xxxx)
			0x19,					// ADD	HL, DE
		//	0x11, 0xAA, 0xAA,		// [variant 1]	LD	DE, xxxx	; Asterix and the Great Rescue
		//	0xCB, 0xDC,				// [variant 2]	SET	3, H		; Asterix and the Power of The Gods, Bubba N Stix, Skeleton Krew
		};
		
		UINT32 pos = ScanForData_WC(z80Len, z80Data, sizeof(Z80_DRV_BASE), Z80_DRV_BASE, 0x00, 0x01);
		if (pos == (UINT32)-1)
		{
			printf("Z80 driver data offset not found.\n");
			return 0x03;
		}
		z80DrvDataBase = ReadLE16(&z80Data[pos + 0x06]);
		printf("Z80 driver data offset: 0x%04X\n", z80DrvDataBase);
		// z80DrvDataBase + 00h/01h: offset of music pattern table (relative to z80DrvDataBase)
		// z80DrvDataBase + 02h/03h: offset of sound effect pattern table (relative to z80DrvDataBase)
		// z80DrvDataBase + 04h: LFO setting (value for YM2612 register 22h)
		z80InsTblPos = z80DrvDataBase + 0x10;
	}
	
	{
		// --- detect sound driver version ---
		// "Asterix and the Great Rescue" uses an early version with different command IDs
		// All other games seem to use a later version.
		static const UINT8 Z80_SEQCMD_LOADER[] = {
			0xFD, 0x7E, 0x00,	// LD	A, (IY+00h)	; get 1st byte of address from jump table
			0x32, 0xAA, 0xAA,	// LD	(xxxx), A	; copy into JP command, destination address low byte
			0xFD, 0x7E, 0x01,	// LD	A, (IY+01h)	; get 2nd byte of address
			0x32, 0xAA, 0xAA,	// LD	(xxxx), A	; copy into JP command, destination address high byte
			0x7B,				// LD	A, E		; put command ID back into register A
			0xC3,				// JP	xxxx		; jump to offset previously written
		};
		UINT16 ptrTblPos;
		UINT16 cmd02CodePos;
		
		UINT32 pos = ScanForData_WC(z80Len, z80Data, sizeof(Z80_SEQCMD_LOADER), Z80_SEQCMD_LOADER, 0x00, 0x01);
		if (pos == (UINT32)-1)
		{
			printf("Z80 driver sequence command table not found. Unable to determine sound driver version.\n");
			return 0x04;
		}
		ptrTblPos = ReadLE16(&z80Data[pos + 0x04]);	// get "jump" pointer, which takes also place of the 1st entry in the pointer table
		cmd02CodePos = ReadLE16(&z80Data[ptrTblPos + 0x02]);	// get offset of first command
		if (((cmd02CodePos ^ ptrTblPos) & 0xFF00) == 0)
		{
			// high byte of [ptr table] == high byte of [offset of command 02]
			// -> early version of the driver ("Asterix and the Great Rescue", ptr table @ 0x044D, command 02 @ 0x047D)
			z80DrvVer = 1;	// This has the Arpeggio command at ID 0x02. (Note Off has ID 0x12)
		}
		else
		{
			// high byte of [ptr table] != high byte of [offset of command 02]
			// -> final version (e.g. "Asterix and the Power of The Gods", ptr table @ 0x046A, command 02 @ 0x0624)
			z80DrvVer = 2;	// This has the Note Off command at ID 0x02. (Arpeggio has ID 0x04)
		}
		printf("Detected sound driver version: %u\n", z80DrvVer);
	}
	
	return 0x00;
}

// memcmp, wildcard version (the wildcard is 0xAA)
static int memcmp_wc(const UINT8* data, const UINT8* compare, UINT32 count)
{
	for (; count > 0; count --)
	{
		if (*compare != 0xAA)
		{
			if (*data < *compare)
				return -1;
			else if (*data > *compare)
				return +1;
		}
		data ++;	compare ++;
	}
	return 0;
}

static UINT32 ScanForData_WC(UINT32 scanLen, const UINT8* scanData,
							UINT32 matchLen, const UINT8* matchData, UINT32 startPos,
							UINT32 step)
{
	UINT32 curPos;
	
	if (scanLen < matchLen)
		return (UINT32)-1;
	for (curPos = startPos; curPos < scanLen - matchLen; curPos += step)
	{
		if (! memcmp_wc(&scanData[curPos], matchData, matchLen))
			return curPos;
	}
	
	return (UINT32)-1;
}
