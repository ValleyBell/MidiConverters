// Twinkle Soft -> Midi Converter
// ------------------------------
// Written by Valley Bell, 26 February 2017, 10 March 2017
// based on Wolf Team MegaDrive -> Midi Converter

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <stdtype.h>


typedef struct running_note
{
	UINT8 MidChn;
	UINT8 Note;
	UINT16 RemLen;
} RUN_NOTE;

typedef struct _midi_track_state
{
	UINT32 TrkBase;
	UINT32 CurDly;	// delay until next event
	UINT8 Flags;	// Bit 0 - raw MIDI mode (don't do any fixes)
	UINT8 MidChn;
	INT8 NoteMove;
	UINT8 PBRange;
	UINT8 MidIns;
	UINT8 NoteVol;
} MID_TRK_STATE;

typedef struct file_information
{
	UINT32 Alloc;	// allocated bytes
	UINT32 Pos;		// current file offset
	UINT8* Data;	// file data
} FILE_INF;

typedef struct _track_info
{
	UINT16 StartOfs;
	UINT16 LoopOfs;
} TRK_INFO;


UINT8 Twinkle2Mid(UINT16 SongLen, const UINT8* SongData);
static void PreparseTwinkleTrack(UINT16 SongLen, const UINT8* SongData, TRK_INFO* TrkInf);
static UINT8 NeedPBRangeFix(UINT8* curPBRange, INT16 PBend);
static void CheckRunningNotes(FILE_INF* fInf, UINT32* Delay);
static void WriteMidiDelay(FILE_INF* fInf, UINT32* Delay);
static void WriteEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteLongEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT32 DataLen, const UINT8* Data);
static void WriteMetaEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(FILE_INF* fInf, UINT32 Value);
static void File_CheckRealloc(FILE_INF* FileInf, UINT32 BytesNeeded);
static void WriteMidiHeader(FILE_INF* fInf, UINT16 Format, UINT16 Tracks, UINT16 Resolution);
static void WriteMidiTrackStart(FILE_INF* fInf, MID_TRK_STATE* MTS);
static void WriteMidiTrackEnd(FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);
static double OPN2DB(UINT8 TL);
static UINT8 DB2Mid(double DB);
static UINT32 Tempo2Mid(UINT8 TempoVal);
static UINT8 PanBits2MidiPan(UINT8 Pan);
static UINT8 CalcGSChecksum(UINT16 DataSize, const UINT8* Data);

static UINT16 ReadBE16(const UINT8* Data);
static UINT32 ReadBE32(const UINT8* Data);
static UINT16 ReadLE16(const UINT8* Data);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT8 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];
static UINT8 FixVolume;

static UINT16 MIDI_RES = 48;
static UINT16 NUM_LOOPS = 2;

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	
	printf("Twinkle Soft -> Midi Converter\n------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: twinkle2mid.exe Options input.bin output.mid\n");
		printf("Options: (options can be combined, default setting is 'r')\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    v   fix Volume (convert db levels to logarithmic MIDI, OPN(A) only)\n");
		printf("Supported/verified games: Bunretsu Shugo Shin Twinkle Star\n");
		return 0;
	}
	
	FixVolume = 0;
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			FixVolume = 0;
			break;
		case 'V':
			FixVolume = 1;
			break;
		}
		StrPtr ++;
	}
	
	hFile = fopen(argv[2], "rb");
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
	
	Twinkle2Mid(ROMLen, ROMData);
	WriteFileData(MidLen, MidData, argv[3]);
	free(MidData);	MidData = NULL;
	
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

UINT8 Twinkle2Mid(UINT16 SongLen, const UINT8* SongData)
{
	TRK_INFO TrkInf[0x10];
	
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 InPos;
	FILE_INF MidFileInf;
	MID_TRK_STATE MTS;
	UINT8 TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopIdx;
	UINT16 LoopCount[8];
	UINT16 LoopPos[8];
	UINT32 TempLng;
	UINT16 TempSht;
	INT16 TempSSht;
	UINT8 TempByt;
	UINT8 CurNote;
	
	UINT8 CurNoteLen;
	UINT8 CurNoteDly;
	
	UINT8 SysExHdr[2];
	UINT8 SysExData[4];
	UINT8 TempArr[0x10];
	
	TrkCnt = 10;	// 3xSSG + 6xFM
	
	MidFileInf.Alloc = 0x20000;	// 128 KB should be enough
	MidFileInf.Data = (UINT8*)malloc(MidFileInf.Alloc);
	MidFileInf.Pos = 0x00;
	
	WriteMidiHeader(&MidFileInf, 0x0001, TrkCnt, MIDI_RES);
	
	InPos = 0x00;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, InPos += 0x02)
	{
		TrkInf[CurTrk].StartOfs = ReadLE16(&SongData[InPos]);
		PreparseTwinkleTrack(SongLen, SongData, &TrkInf[CurTrk]);
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		InPos = TrkInf[CurTrk].StartOfs;
		
		WriteMidiTrackStart(&MidFileInf, &MTS);
		
		TrkEnd = 0;
		LoopIdx = 0x00;
		MTS.Flags = 0x00;
		MTS.MidChn = CurTrk;
		MTS.NoteVol = 0x7F;
		if (CurTrk < 3)
			MTS.NoteMove = +24;	// SSG channel
		else
			MTS.NoteMove = 0;	// FM channel
		MTS.PBRange = 0;
		RunNoteCnt = 0x00;
		
		while(! TrkEnd)
		{
			CurCmd = SongData[InPos];
			if (CurCmd < 0x80)
			{
				CurCmd = SongData[InPos + 0x00];
				CurNoteDly = SongData[InPos + 0x01];
				CurNoteLen = SongData[InPos + 0x02];
				InPos += 0x03;
				
				if (CurCmd == 0)
					CurCmd = 0x30;	// fix for TWED.MF2 (OPN/OPNA)
				if (CurNoteDly == 0 && CurNoteLen == 0)
					CurNoteDly = CurNoteLen = 48;	// fix for TWED.MF2 (OPN/OPNA)
				
				CurNote = CurCmd;
				if (! (MTS.Flags & 0x01))
					CurNote += MTS.NoteMove;
				if (! CurNoteLen)	// length == 0 -> rest (confirmed with MIDI log of sound driver)
					CurNote = 0x00;
				
				WriteEvent(&MidFileInf, &MTS, 0x00, 0x00, 0x00);
				
				for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
				{
					if (RunNotes[TempByt].Note == CurNote)
					{
						RunNotes[TempByt].RemLen = (UINT16)MTS.CurDly + CurNoteLen;
						break;
					}
				}
				if (TempByt >= RunNoteCnt && CurNote > 0x00)
				{
					WriteEvent(&MidFileInf, &MTS, 0x90, CurNote, MTS.NoteVol);
					if (RunNoteCnt < MAX_RUN_NOTES)
					{
						RunNotes[RunNoteCnt].MidChn = MTS.MidChn;
						RunNotes[RunNoteCnt].Note = CurNote;
						RunNotes[RunNoteCnt].RemLen = CurNoteLen;
						RunNoteCnt ++;
					}
				}
				
				MTS.CurDly += CurNoteDly;
			}
			else
			{
				switch(CurCmd)
				{
				case 0x82:	// Set Instrument
					MTS.MidIns = SongData[InPos + 0x01];
					WriteEvent(&MidFileInf, &MTS, 0xC0, MTS.MidIns, 0x00);
					InPos += 0x02;
					break;
				case 0x85:	// Set Volume
					if (FixVolume && ! (MTS.Flags & 0x01))
						TempByt = DB2Mid(OPN2DB(SongData[InPos + 0x01] ^ 0x7F));
					else
						TempByt = SongData[InPos + 0x01];
					// Note: Velocity 0 results in a rest in the sound driver as well
					MTS.NoteVol = TempByt;
					//WriteEvent(&MidFileInf, &MTS, 0xB0, 0x07, TempByt);
					InPos += 0x02;
					break;
				case 0x8A:	// Tempo in BPM
					TempLng = (UINT32)(60000000.0 / SongData[InPos + 0x01] + 0.5);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent(&MidFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
					InPos += 0x02;
					break;
				case 0x9B:	// Loop End
					if (! LoopIdx)
					{
						printf("Warning: Loop End without Loop Start!\n");
						TrkEnd = 1;
						break;
					}
					LoopIdx --;
					LoopCount[LoopIdx] ++;
					TempSht = SongData[InPos + 0x01];
					if (! TempSht || TempSht >= 0xF0)	// infinite loop
					{
						if (LoopCount[LoopIdx] <= 0x7F)
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x6F, (UINT8)LoopCount[LoopIdx]);
						TempSht = NUM_LOOPS;
					}
					if (LoopCount[LoopIdx] < TempSht)
					{
						// loop back
						InPos = LoopPos[LoopIdx];
						LoopIdx ++;
					}
					else
					{
						// finish loop
						InPos += 0x02;
					}
					break;
				case 0x9C:	// Loop Start
					if (InPos == TrkInf[CurTrk].LoopOfs)
						WriteEvent(&MidFileInf, &MTS, 0xB0, 0x6F, 0);
					InPos += 0x01;
					LoopPos[LoopIdx] = InPos;
					LoopCount[LoopIdx] = 0;
					LoopIdx ++;
					break;
				case 0x9D:	// Detune
					TempSht = (INT8)SongData[InPos + 0x01];
					TempSht *= 8;
					TempSht += 0x2000;
					WriteEvent(&MidFileInf, &MTS, 0xE0, TempSht & 0x7F, TempSht >> 7);
					InPos += 0x02;
					break;
				case 0x9F:	// Set Pan
					TempByt = PanBits2MidiPan(SongData[InPos + 0x01]);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x0A, TempByt);
					InPos += 0x02;
					break;
				case 0xA4:	// Pitch Bend
					// Note: MSB = semitone, LSB = fraction
					TempSSht = ReadLE16(&SongData[InPos + 0x01]);
					if (MTS.PBRange != 0xFF)
					{
						if (NeedPBRangeFix(&MTS.PBRange, TempSSht))
						{
							// write Pitch Bend Range
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x65, 0x00);
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x64, 0x00);
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x06, MTS.PBRange);
						}
						TempSSht = TempSSht * 8192 / MTS.PBRange / 256;
					}
					TempSSht += 0x2000;
					WriteEvent(&MidFileInf, &MTS, 0xE0, TempSSht & 0x7F, TempSSht >> 7);
					InPos += 0x03;
					break;
				case 0xA5:	// unknown
					printf("Ignored unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x06, SongData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xDD:	// set SysEx Data 1
					SysExData[0] = SongData[InPos + 0x02];
					SysExData[1] = SongData[InPos + 0x03];
					//MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xDE:	// set SysEx Data 2 + send
					SysExData[2] = SongData[InPos + 0x02];
					SysExData[3] = SongData[InPos + 0x03];
					// Generate Roland GS SysEx command
					TempArr[0x00] = 0x41;			// Roland ID
					TempArr[0x01] = SysExHdr[0];	// Device ID
					TempArr[0x02] = SysExHdr[1];	// Model ID (0x42 == GS)
					TempArr[0x03] = 0x12;			// Command ID (0x12 == DT1)
					memcpy(&TempArr[0x04], SysExData, 0x04);
					TempArr[0x08] = CalcGSChecksum(0x04, SysExData);
					TempArr[0x09] = 0xF7;
					TempByt = 0x0A;	// SysEx data size
					
					WriteLongEvent(&MidFileInf, &MTS, 0xF0, TempByt, TempArr);
					//MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xDF:	// set SysEx Device ID + Model ID
					SysExHdr[0] = SongData[InPos + 0x02];	// Device ID
					SysExHdr[1] = SongData[InPos + 0x03];	// Model ID
					//MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xE6:	// set MIDI Channel
					MTS.MidChn = SongData[InPos + 0x02] & 0x0F;
					MTS.CurDly += SongData[InPos + 0x01];
					MTS.Flags |= 0x01;
					MTS.NoteMove = 0;
					MTS.PBRange = 0xFF;
					InPos += 0x03;
					break;
				case 0xE7:	// unknown (doesn't result in a MIDI event)
					printf("Ignored unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x03, SongData[InPos + 0x02]);
					MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xEB:	// MIDI Controller
					WriteEvent(&MidFileInf, &MTS, 0xB0, SongData[InPos + 0x02], SongData[InPos + 0x03]);
					MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xEC:	// MIDI Instrument
					MTS.MidIns = SongData[InPos + 0x02];
					WriteEvent(&MidFileInf, &MTS, 0xC0, MTS.MidIns, 0x00);
					MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0xEE:	// Pitch Bend with Delay
					TempSSht = ReadLE16(&SongData[InPos + 0x02]);
					if (MTS.PBRange != 0xFF)
					{
						if (NeedPBRangeFix(&MTS.PBRange, TempSSht))
						{
							// write Pitch Bend Range
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x65, 0x00);
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x64, 0x00);
							WriteEvent(&MidFileInf, &MTS, 0xB0, 0x06, MTS.PBRange);
						}
						TempSSht = TempSSht * 8192 / MTS.PBRange / 256;
					}
					TempSSht += 0x2000;
					WriteEvent(&MidFileInf, &MTS, 0xE0, TempSSht & 0x7F, TempSSht >> 7);
					MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xFE:	// Track End
					TrkEnd = 1;
					InPos += 0x01;
					break;
				/*case 0xFF:
					TrkEnd = 1;
					InPos += 0x01;
					break;*/
				default:
					printf("Unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x6E, CurCmd & 0x7F);
					InPos += 0x01;
					TrkEnd = 1;
					break;
				}
			}
		}
		for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
		{
			if (RunNotes[TempByt].RemLen > MTS.CurDly)
				MTS.CurDly = RunNotes[TempByt].RemLen;
		}
		WriteEvent(&MidFileInf, &MTS, 0x7F, 0x00, 0x00);	// flush all notes
		
		WriteEvent(&MidFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&MidFileInf, &MTS);
	}
	MidData = MidFileInf.Data;
	MidLen = MidFileInf.Pos;
	
	return 0x00;
}

static void PreparseTwinkleTrack(UINT16 SongLen, const UINT8* SongData, TRK_INFO* TrkInf)
{
	// this function is only used to detect the offset of the master loop
	UINT16 InPos;
	UINT8 CurCmd;
	
	UINT8 LoopIdx;
	UINT8 LoopCount[8];
	UINT16 LoopPos[8];
	
	InPos = TrkInf->StartOfs;
	TrkInf->LoopOfs = 0x0000;
	LoopIdx = 0x00;
	while(1)
	{
		CurCmd = SongData[InPos];
		if (CurCmd < 0x80)
		{
			InPos += 0x03;
		}
		else
		{
			switch(CurCmd)
			{
			case 0x9B:	// Loop End
				if (! LoopIdx)
					return;
				LoopIdx --;
				LoopCount[LoopIdx] ++;
				if (! SongData[InPos + 0x01])	// infinite loop
				{
					TrkInf->LoopOfs = LoopPos[LoopIdx] - 0x01;
					return;
				}
				if (LoopCount[LoopIdx] < SongData[InPos + 0x01])
				{
					// loop back
					InPos = LoopPos[LoopIdx];
					LoopIdx ++;
				}
				else
				{
					// finish loop
					InPos += 0x02;
				}
				break;
			case 0x9C:	// Loop Start
				InPos += 0x01;
				LoopPos[LoopIdx] = InPos;
				LoopCount[LoopIdx] = 0;
				LoopIdx ++;
				break;
			case 0x82:	// Set Instrument
			case 0x85:	// Set Volume
			case 0x8A:	// Tempo in BPM
			case 0x9D:	// Detune
			case 0x9F:	// Set Pan
			case 0xA5:	// unknown
				InPos += 0x02;
				break;
			case 0xA4:	// Pitch Bend
			case 0xE6:	// set MIDI Channel
			case 0xEC:	// MIDI Instrument
				InPos += 0x03;
				break;
			case 0xDD:	// set SysEx Data 1
			case 0xDE:	// set SysEx Data 2 + send
			case 0xDF:	// set SysEx Device ID + Model ID
			case 0xE7:	// unknown
			case 0xEB:	// MIDI Controller
			case 0xEE:	// Pitch Bend with Delay
				InPos += 0x04;
				break;
			case 0xFE:	// Track End
			default:
				InPos += 0x01;
				return;
			}
		}
	}
	
	return;
}

static UINT8 NeedPBRangeFix(UINT8* curPBRange, INT16 PBend)
{
	UINT16 pbAbs;
	UINT8 pbRequired;
	
	pbAbs = (PBend >= 0) ? PBend : -PBend;
	pbRequired = (UINT8)((pbAbs + 0xFF) >> 8);
	if (*curPBRange >= pbRequired)
		return 0;
	
	if (*curPBRange == 0 && pbRequired < 16)
		*curPBRange = 16;
	else
		*curPBRange = (pbRequired + 7) & ~7;	// round up to 8
	
	return 1;
}

static void CheckRunningNotes(FILE_INF* fInf, UINT32* Delay)
{
	UINT8 CurNote;
	UINT32 TempDly;
	RUN_NOTE* TempNote;
	
	while(RunNoteCnt)
	{
		// 1. Check if we're going beyond a note's timeout.
		TempDly = *Delay + 1;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
		{
			TempNote = &RunNotes[CurNote];
			if (TempNote->RemLen < TempDly)
				TempDly = TempNote->RemLen;
		}
		if (TempDly > *Delay)
			break;	// not beyond the timeout - do the event
		
		// 2. advance all notes by X ticks
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
			RunNotes[CurNote].RemLen -= (UINT16)TempDly;
		(*Delay) -= TempDly;
		
		// 3. send NoteOff for expired notes
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
		{
			TempNote = &RunNotes[CurNote];
			if (! TempNote->RemLen)	// turn note off, it going beyond the Timeout
			{
				WriteMidiValue(fInf, TempDly);
				TempDly = 0;
				
				File_CheckRealloc(fInf, 0x03);
				fInf->Data[fInf->Pos + 0x00] = 0x90 | TempNote->MidChn;
				fInf->Data[fInf->Pos + 0x01] = TempNote->Note;
				fInf->Data[fInf->Pos + 0x02] = 0x00;
				fInf->Pos += 0x03;
				
				RunNoteCnt --;
				if (RunNoteCnt)
					*TempNote = RunNotes[RunNoteCnt];
				CurNote --;
			}
		}
	}
	
	return;
}

static void WriteMidiDelay(FILE_INF* fInf, UINT32* Delay)
{
	CheckRunningNotes(fInf, Delay);
	
	WriteMidiValue(fInf, *Delay);
	if (*Delay)
	{
		UINT8 CurNote;
		
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
			RunNotes[CurNote].RemLen -= (UINT16)*Delay;
		*Delay = 0x00;
	}
	
	return;
}

static void WriteEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	if (! (Evt & 0x80))
	{
		CheckRunningNotes(fInf, &MTS->CurDly);
		return;
	}
	
	WriteMidiDelay(fInf, &MTS->CurDly);
	
	File_CheckRealloc(fInf, 0x03);
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		fInf->Data[fInf->Pos + 0x00] = Evt | MTS->MidChn;
		fInf->Data[fInf->Pos + 0x01] = Val1;
		fInf->Data[fInf->Pos + 0x02] = Val2;
		fInf->Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		fInf->Data[fInf->Pos + 0x00] = Evt | MTS->MidChn;
		fInf->Data[fInf->Pos + 0x01] = Val1;
		fInf->Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		fInf->Data[fInf->Pos + 0x00] = Evt;
		fInf->Data[fInf->Pos + 0x01] = Val1;
		fInf->Data[fInf->Pos + 0x02] = Val2;
		fInf->Pos += 0x03;
		break;
	default:
		break;
	}
	
	return;
}

static void WriteLongEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiDelay(fInf, &MTS->CurDly);
	
	File_CheckRealloc(fInf, 0x01 + 0x04 + DataLen);	// worst case: 4 bytes of data length
	fInf->Data[fInf->Pos + 0x00] = Evt;
	fInf->Pos += 0x01;
	WriteMidiValue(fInf, DataLen);
	memcpy(&fInf->Data[fInf->Pos], Data, DataLen);
	fInf->Pos += DataLen;
	
	return;
}

static void WriteMetaEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiDelay(fInf, &MTS->CurDly);
	
	File_CheckRealloc(fInf, 0x02 + 0x05 + DataLen);	// worst case: 5 bytes of data length
	fInf->Data[fInf->Pos + 0x00] = 0xFF;
	fInf->Data[fInf->Pos + 0x01] = MetaType;
	fInf->Pos += 0x02;
	WriteMidiValue(fInf, DataLen);
	memcpy(&fInf->Data[fInf->Pos], Data, DataLen);
	fInf->Pos += DataLen;
	
	return;
}

static void WriteMidiValue(FILE_INF* fInf, UINT32 Value)
{
	UINT8 ValSize;
	UINT8* ValData;
	UINT32 TempLng;
	UINT32 CurPos;
	
	ValSize = 0x00;
	TempLng = Value;
	do
	{
		TempLng >>= 7;
		ValSize ++;
	} while(TempLng);
	
	File_CheckRealloc(fInf, ValSize);
	ValData = &fInf->Data[fInf->Pos];
	CurPos = ValSize;
	TempLng = Value;
	do
	{
		CurPos --;
		ValData[CurPos] = 0x80 | (TempLng & 0x7F);
		TempLng >>= 7;
	} while(TempLng);
	ValData[ValSize - 1] &= 0x7F;
	
	fInf->Pos += ValSize;
	
	return;
}

static void File_CheckRealloc(FILE_INF* FileInf, UINT32 BytesNeeded)
{
#define REALLOC_STEP	0x8000	// 32 KB block
	UINT32 MinPos;
	
	MinPos = FileInf->Pos + BytesNeeded;
	if (MinPos <= FileInf->Alloc)
		return;
	
	while(MinPos > FileInf->Alloc)
		FileInf->Alloc += REALLOC_STEP;
	FileInf->Data = (UINT8*)realloc(FileInf->Data, FileInf->Alloc);
	
	return;
}

static void WriteMidiHeader(FILE_INF* fInf, UINT16 Format, UINT16 Tracks, UINT16 Resolution)
{
	File_CheckRealloc(fInf, 0x08 + 0x06);
	
	WriteBE32(&fInf->Data[fInf->Pos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&fInf->Data[fInf->Pos + 0x04], 0x00000006);	// Header Length
	fInf->Pos += 0x08;
	
	WriteBE16(&fInf->Data[fInf->Pos + 0x00], Format);		// MIDI Format (0/1/2)
	WriteBE16(&fInf->Data[fInf->Pos + 0x02], Tracks);		// number of tracks
	WriteBE16(&fInf->Data[fInf->Pos + 0x04], Resolution);	// Ticks per Quarter
	fInf->Pos += 0x06;
	
	return;
}

static void WriteMidiTrackStart(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	File_CheckRealloc(fInf, 0x08);
	
	WriteBE32(&fInf->Data[fInf->Pos + 0x00], 0x4D54726B);	// write 'MTrk'
	WriteBE32(&fInf->Data[fInf->Pos + 0x04], 0x00000000);	// write dummy length
	fInf->Pos += 0x08;
	
	MTS->TrkBase = fInf->Pos;
	MTS->CurDly = 0;
	
	return;
}

static void WriteMidiTrackEnd(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT32 TrkLen;
	
	TrkLen = fInf->Pos - MTS->TrkBase;
	WriteBE32(&fInf->Data[MTS->TrkBase - 0x04], TrkLen);	// write Track Length
	
	return;
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

static double OPN2DB(UINT8 TL)
{
	return -(TL * 3 / 4.0f);
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

static UINT32 Tempo2Mid(UINT8 TempoVal)
{
	// Note: The tempo value is the value of YM Timer B.
	// higher value = higher tick frequency = higher tempo
	
	// Base Clock = 2 MHz
	// Prescaler: 6 * 12
	// internal Timer Countdown: (100h - value) * 10h
	// Timer Frequency: Clock / (Countdown * Prescaler)
	double TicksPerSec;
	UINT16 TmrVal;
	
	TmrVal = (0x100 - TempoVal) << 4;
	TicksPerSec = 2000000.0 / (6 * 12 * TmrVal);
	return (UINT32)(500000 * MIDI_RES / TicksPerSec/4 + 0.5);
}

static UINT8 PanBits2MidiPan(UINT8 Pan)
{
	switch(Pan & 0x03)
	{
	case 0x00:	// no sound
		return 0x3F;
	case 0x01:	// Right Channel
		return 0x7F;
	case 0x02:	// Left Channel
		return 0x00;
	case 0x03:	// Center
		return 0x40;
	}
	return 0x3F;
}

static UINT8 CalcGSChecksum(UINT16 DataSize, const UINT8* Data)
{
	UINT8 ChkSum;
	UINT16 CurPos;
	
	ChkSum = 0x00;
	for (CurPos = 0x00; CurPos < DataSize; CurPos ++)
		ChkSum += Data[CurPos];
	return (0x80 - ChkSum) & 0x7F;
}


static UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

static UINT32 ReadBE32(const UINT8* Data)
{
	return	(Data[0x00] << 24) | (Data[0x01] << 16) |
			(Data[0x02] <<  8) | (Data[0x03] <<  0);
}

static UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

static void WriteBE32(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value & 0xFF000000) >> 24;
	Buffer[0x01] = (Value & 0x00FF0000) >> 16;
	Buffer[0x02] = (Value & 0x0000FF00) >>  8;
	Buffer[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}

static void WriteBE16(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value & 0xFF00) >> 8;
	Buffer[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}
