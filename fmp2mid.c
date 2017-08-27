// FMP3 -> Midi Converter
// ----------------------
// Written by Valley Bell, 27 August 2017
// based on Twinkle Soft -> Midi Converter

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
} TRK_INFO;


UINT8 Fmp2Mid(UINT16 SongLen, const UINT8* SongData);
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

static UINT16 ReadLE16(const UINT8* Data);
static UINT32 ReadLE24(const UINT8* Data);
static UINT32 ReadLE32(const UINT8* Data);
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
	UINT8 retVal;
	
	printf("FMP3 -> Midi Converter\n----------------------\n");
	if (argc < 3)
	{
		printf("Usage: Fmp2Mid.exe Options input.bin output.mid\n");
		printf("Options: (options can be combined, default setting is 'r')\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    v   fix Volume (convert db levels to logarithmic MIDI, OPN(A) only)\n");
		printf("Supported/verified games: V.G. II\n");
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
	
	retVal = Fmp2Mid(ROMLen, ROMData);
	if (! retVal)
		WriteFileData(MidLen, MidData, argv[3]);
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
	TRK_INFO TrkInf[20];
	
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 InPos;
	FILE_INF MidFileInf;
	MID_TRK_STATE MTS;
	UINT8 TrkEnd;
	UINT8 CurCmd;
	
	//UINT8 LoopIdx;
	//UINT16 LoopCount[8];
	//UINT16 LoopPos[8];
	UINT16 MstLoopCount;
	UINT16 MstLoopPos;
	UINT32 TempLng;
	//UINT16 TempSht;
	UINT8 TempByt;
	UINT8 CurNote;
	UINT8 CurNoteLen;
	
	UINT32 SysExAlloc;
	UINT32 SysExLen;
	UINT8* SysExData;
	UINT8 TempArr[0x04];
	
	if (SongData[0x00] != 0x02)
	{
		printf("Unsupported FMP format!\n");
		MidData = NULL;
		MidLen = 0x00;
		return 0x80;
	}
	
	TrkCnt = 20;
	
	MidFileInf.Alloc = 0x20000;	// 128 KB should be enough
	MidFileInf.Data = (UINT8*)malloc(MidFileInf.Alloc);
	MidFileInf.Pos = 0x00;
	SysExAlloc = 0x20;
	SysExLen = 0x00;
	SysExData = (UINT8*)malloc(SysExAlloc);
	
	WriteMidiHeader(&MidFileInf, 0x0001, TrkCnt, MIDI_RES);
	
	InPos = 0x04;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, InPos += 0x02)
		TrkInf[CurTrk].StartOfs = ReadLE16(&SongData[InPos]);
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		InPos = TrkInf[CurTrk].StartOfs;
		
		WriteMidiTrackStart(&MidFileInf, &MTS);
		
		TrkEnd = 0;
		//LoopIdx = 0x00;
		MstLoopPos = 0x0000;
		MstLoopCount = 0;
		MTS.Flags = 0x00;
		MTS.MidChn = CurTrk;
		MTS.NoteVol = 0x7F;
		MTS.NoteMove = 0;	// FM channel
		MTS.PBRange = 0;
		RunNoteCnt = 0x00;
		
		while(! TrkEnd)
		{
			CurCmd = SongData[InPos];
			if (CurCmd < 0x80)
			{
				CurNoteLen = SongData[InPos + 0x01];
				InPos += 0x02;
				
				CurNote = CurCmd;
				if (! (MTS.Flags & 0x01))
					CurNote += MTS.NoteMove;
				if (! CurNoteLen)	// length == 0 -> rest (confirmed with MIDI log of sound driver)
					CurNote = 0xFF;
				
				WriteEvent(&MidFileInf, &MTS, 0x00, 0x00, 0x00);
				
				for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
				{
					if (RunNotes[TempByt].Note == CurNote)
					{
						RunNotes[TempByt].RemLen = (UINT16)MTS.CurDly + CurNoteLen;
						break;
					}
				}
				if (TempByt >= RunNoteCnt && CurNote != 0xFF)
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
			}
			else
			{
				switch(CurCmd)
				{
				case 0x80:	// Set Instrument
					MTS.MidIns = SongData[InPos + 0x01];
					WriteEvent(&MidFileInf, &MTS, 0xC0, MTS.MidIns, 0x00);
					InPos += 0x02;
					break;
				case 0x81:	// Set Volume
					TempByt = SongData[InPos + 0x01];
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x07, TempByt);
					InPos += 0x02;
					break;
				case 0x83:	// Set Velocity
					TempByt = SongData[InPos + 0x01];
					MTS.NoteVol = TempByt;
					InPos += 0x02;
					break;
				case 0x85:	// Pitch Bend
					WriteEvent(&MidFileInf, &MTS, 0xE0, SongData[InPos + 0x01], SongData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0x8B:	// Set Pan
					TempByt = SongData[InPos + 0x01];
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x0A, TempByt);
					InPos += 0x02;
					break;
				case 0x8C:	// Send SysEx Data
					SysExData[0x00] = 0x41;			// Roland ID
					for (SysExLen = 0x01; InPos + SysExLen < SongLen; SysExLen ++)
					{
						if (SysExAlloc <= SysExLen)
						{
							SysExAlloc *= 2;
							SysExData = (UINT8*)realloc(SysExData, SysExAlloc);
						}
						SysExData[SysExLen] = SongData[InPos + SysExLen];
						if (SysExData[SysExLen] == 0xF7)
							break;
					}
					if (SysExData[SysExLen] == 0xF7)
					{
						SysExLen ++;	// count end SysEx End command
						WriteLongEvent(&MidFileInf, &MTS, 0xF0, SysExLen, SysExData);
					}
					InPos += SysExLen;
					break;
				case 0x8E:	// set MIDI Channel
					MTS.MidChn = SongData[InPos + 0x01] & 0x0F;
					MTS.Flags |= 0x01;
					MTS.NoteMove = 0;
					MTS.PBRange = 0xFF;
					InPos += 0x02;
					break;
				case 0x90:	// MIDI Controller
					WriteEvent(&MidFileInf, &MTS, 0xB0, SongData[InPos + 0x01], SongData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xB6:	// Tempo
					TempLng = ReadLE24(&SongData[InPos + 0x03]);
					TempLng = (UINT32)((UINT64)500000 * TempLng / 0x32F000);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent(&MidFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
					InPos += 0x08;
					break;
#if 0
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
				case 0xA5:	// unknown
					printf("Ignored unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x06, SongData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xE7:	// unknown (doesn't result in a MIDI event)
					printf("Ignored unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x03, SongData[InPos + 0x02]);
					MTS.CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
#endif
				case 0xFF:	// Track End
					if (! MstLoopPos)
					{
						// detect pattern for non-looping tracks
						if ((SongData[InPos + 0x01] == 0x01 && SongData[InPos + 0x02] == 0xFF) ||
							InPos + 0x03 >= SongLen)
						{
							TrkEnd = 1;
							break;
						}
						MstLoopPos = InPos;
					}
					else
					{
						InPos = MstLoopPos;
					}
					
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x6F, (UINT8)MstLoopCount);
					if (MstLoopCount >= NUM_LOOPS)
						TrkEnd = 1;
					InPos += 0x01;
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(&MidFileInf, &MTS, 0xB0, 0x6E, CurCmd & 0x7F);
					InPos += 0x01;
					TrkEnd = 1;
					break;
				}
			}
			
			TempByt = SongData[InPos];	InPos ++;
			MTS.CurDly += TempByt;
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
	
	free(SysExData);	SysExData = NULL;
	
	return 0x00;
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


static UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

static UINT32 ReadLE24(const UINT8* Data)
{
	return (Data[0x02] << 16) | (Data[0x01] <<  8) | (Data[0x00] <<  0);
}

static UINT32 ReadLE32(const UINT8* Data)
{
	return	(Data[0x03] << 24) | (Data[0x02] << 16) |
			(Data[0x01] <<  8) | (Data[0x00] <<  0);
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
