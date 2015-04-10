// Hummer -> Midi Converter
// ------------------------
// Written by Valley Bell, 09 April 2015

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <memory.h>
#include "stdtype.h"

#define INLINE	static __inline

int main(int argc, char* argv[]);
UINT8 ConvertHummer2MID(UINT16 SeqOfs, UINT32 ROMBankOfs);
INLINE UINT8 DB2Mid(double DB);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);


const UINT8 NES_SIG[0x03] = {'N', 'E', 'S'};	// .nes: "NES\x1A", .nsf: "NESM"
//const UINT8 HUMMER_LOADSEQ[0x07] = {0xAA, 0x98, 0x48, 0x8A, 0x0A, 0xAA, 0xBD};	// works in Somari (Hummer v2)
const UINT8 HUMMER_LOADSEQ[0x06] = {0x98, 0x48, 0x8A, 0x0A, 0xAA, 0xBD};

typedef struct _track_header
{
	UINT8 ChnID;
	UINT16 StartOfs;
} TRK_HDR;

static UINT32 ROMSize;
static UINT8* ROMData;
static UINT32 MidAlloc;
static UINT32 MidSize;
static UINT8* MidData;

static UINT16 MIDI_RES;
static UINT16 NUM_LOOPS;
static UINT8 ACCURATE_TEMPO;
static UINT32 TEMPO_BASE;

int main(int argc, char* argv[])
{
	FILE* hFile;
	UINT8 FileSig[0x04];
	UINT8 FileMode;
	UINT8 RetVal;
	UINT8 CurSng;
	UINT8 SongCnt;
	UINT32 CurPos;
	UINT32 SeqPtrsPos;
	UINT32 BankPos;
	UINT16 SongOfs;
	char OutFileBase[0x100];
	char OutFile[0x100];
	char* TempPnt;
	
	if (argc <= 1)
	{
		printf("Usage: hum2mid ROM.nes/nsf\n");
#ifdef _DEBUG
		_getch();
#endif
		return 0;
	}
	
	MIDI_RES = 32;
	NUM_LOOPS = 2;
	ACCURATE_TEMPO = 0x00;
	SongCnt = 0x00;
	
	strcpy(OutFileBase, argv[1]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
		return 1;
	
	fread(FileSig, 0x01, 0x04, hFile);
	FileMode = FileSig[0x03];
	if (memcmp(FileSig, NES_SIG, 0x03) || (FileMode != 0x1A && FileMode != 'M'))
	{
		fclose(hFile);
		printf("Invalid file type! Must be .nes or .nsf!\n");
		return 2;
	}
	fseek(hFile, 0, SEEK_END);
	ROMSize = ftell(hFile);
	
	if (FileMode == 0x1A)
	{
		// NES File
		fseek(hFile, 0x10, SEEK_SET);
		ROMSize -= 0x10;	// skip header
	}
	else
	{
		fseek(hFile, 0x80, SEEK_SET);
		ROMSize -= 0x80;	// skip header
	}
	ROMData = (UINT8*)malloc(ROMSize);
	fread(ROMData, 0x01, ROMSize, hFile);
	
	fclose(hFile);
	
	SeqPtrsPos = 0x00;
	for (CurPos = 0x00; CurPos < ROMSize - 0x09; CurPos ++)
	{
		if (! memcmp(ROMData + CurPos, HUMMER_LOADSEQ, sizeof(HUMMER_LOADSEQ)))
		{
			CurPos += sizeof(HUMMER_LOADSEQ);
			SeqPtrsPos = ReadLE16(ROMData + CurPos);
			if (ROMData[CurPos + 0x03] == 0xFF)
				SeqPtrsPos --;
			BankPos = CurPos & ~0x3FFF;
			
			// Check the first Song (ID 20h)
			SongOfs = ReadLE16(&ROMData[BankPos | (SeqPtrsPos & 0x3FFF)] + 0x40);
			if ((SongOfs & 0xC000) == (SeqPtrsPos & 0xC000))
			{
				RetVal = ROMData[BankPos | (SongOfs & 0x3FFF)];
				if (RetVal == 0xFF || (RetVal & 0x7F) < 0x05)
					break;
			}
			// This might be a wrong bank.
			SeqPtrsPos = 0x00;
		}
	}
	if (! SeqPtrsPos)
	{
		printf("Unable to find sequence list!\n");
		return 4;
	}
	printf("Sequence Loader found at offset %06X.\n", CurPos);
	printf("Sequence List Pointer: %04X\n", SeqPtrsPos);
	
	if (! SongCnt)
	{
		UINT16 MaxPos;
		
		// Song Count autodetection
		CurSng = 0x00;
		CurPos = BankPos | (SeqPtrsPos & 0x3FFF);
		MaxPos = 0x3FFF;
		while(CurPos < ROMSize)
		{
			if ((CurPos & 0x3FFF) >= MaxPos)
				break;
			
			SongOfs = ReadLE16(ROMData + CurPos);
			if ((SongOfs & 0xC000) != (SeqPtrsPos & 0xC000))
				break;
			SongOfs &= 0x3FFF;
			RetVal = ROMData[BankPos | SongOfs];
			if (RetVal != 0xFF && (RetVal & 0x7F) > 0x04)	// invalid first channel - exit
				break;
			if (SongOfs < MaxPos)
				MaxPos = SongOfs;
			
			CurPos += 0x02;
			CurSng ++;
		}
		SongCnt = CurSng;
		printf("Songs detected: 0x%02X (%u)\n", SongCnt, SongCnt);
	}
	
	if (ACCURATE_TEMPO)
		TEMPO_BASE = (UINT32)(1000000 * MIDI_RES / 60.098 + 0.5);
	else
		TEMPO_BASE = (UINT32)(1000000 * MIDI_RES / 60.0 + 0.5);
	MidAlloc = 0x20000;
	MidData = (UINT8*)malloc(MidAlloc);
	CurPos = BankPos | (SeqPtrsPos & 0x3FFF);
	for (CurSng = 0x00; CurSng < SongCnt; CurSng ++, CurPos += 0x02)
	{
		printf("File %02X / %02X ...", CurSng, SongCnt);
		SongOfs = ReadLE16(ROMData + CurPos) & 0x3FFF;
		
		MidSize = 0x00;
		RetVal = ConvertHummer2MID(SongOfs, BankPos);
		if (RetVal)
			return RetVal;
		
		sprintf(OutFile, "%s_%02X.mid", OutFileBase, CurSng);
		
		hFile = fopen(OutFile, "wb");
		if (hFile == NULL)
		{
			printf("Error saving file!\n");
			continue;
		}
		fwrite(MidData, 0x01, MidSize, hFile);
		
		fclose(hFile);
		printf("\n");
	}
	printf("Done.\n");
	
	free(MidData);	MidData = NULL;
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

UINT8 ConvertHummer2MID(UINT16 SeqOfs, UINT32 ROMBankOfs)
{
	static const char* TRK_NAMES[5] = {"Square 1", "Square 2", "Triangle", "Noise", "DPCM"};
	const UINT8* SeqData;
	UINT16 SeqSize;
	TRK_HDR TrkHdrs[10];	// 5x Music + 5x PCM
	UINT16 SeqPos;
	UINT32 MidPos;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT32 MidTrkBase;
	UINT32 CurDly;
	UINT8 MidChn;
	UINT8 TrkEnd;
	UINT8 MstLoopCnt;
	UINT8 TrkMode;	// 00 - FM, 01 - SSG
	INT8 NoteMove;
	
	UINT8 CurCmd;
	
	UINT8 CurNote;
	UINT8 CurNoteVol;
	UINT8 CurChnVol;
	UINT8 CurNoteLen;
	UINT8 LastNote;
	UINT8 HoldNote;
	
	UINT8 LoopCnt;
	UINT16 LoopOfs;
	UINT16 SubRetOfs;
	
	UINT8 TempByt;
	INT16 TempPos;
	UINT32 TempLng;
	UINT8 TempArr[0x20];
	
	SeqData = ROMData + ROMBankOfs;
	if (ROMSize - ROMBankOfs >= 0x4000)
		SeqSize = 0x4000;
	else
		SeqSize = ROMSize - ROMBankOfs;
	
	SeqPos = SeqOfs & 0x3FFF;
	for (CurTrk = 0; CurTrk < 10; CurTrk ++, SeqPos += 0x03)
	{
		if (SeqData[SeqPos] == 0xFF)
			break;
		TrkHdrs[CurTrk].ChnID = SeqData[SeqPos + 0x00];
		TrkHdrs[CurTrk].StartOfs = ReadLE16(&SeqData[SeqPos + 0x01]) & 0x3FFF;
	}
	TrkCnt = CurTrk;
	printf("  %u Track%s", TrkCnt, (TrkCnt == 1) ? "" : "s");
	
	MidPos = 0x00;
	WriteBE32(&MidData[MidPos], 0x4D546864);	MidPos += 0x04;	// 'MThd' Signature
	WriteBE32(&MidData[MidPos], 0x00000006);	MidPos += 0x04;	// Header Size
	WriteBE16(&MidData[MidPos], 0x0001);		MidPos += 0x02;	// Format: 1
	WriteBE16(&MidData[MidPos], TrkCnt);		MidPos += 0x02;	// Tracks
	WriteBE16(&MidData[MidPos], MIDI_RES);		MidPos += 0x02;	// Ticks per Quarter
	
#if 0	// Every sequence contains a F5 Tick Multiplier command that writes the Tempo anyway.
	WriteBE32(&MidData[MidPos], 0x4D54726B);	MidPos += 0x04;	// 'MTrk' Signature
	WriteBE32(&MidData[MidPos], 0x00000000);	MidPos += 0x04;	// Track Size
	MidTrkBase = MidPos;
	CurDly = 0;
	
	WriteBE32(TempArr, TEMPO_BASE);
	WriteMetaEvent_Data(MidData, &MidPos, &CurDly, 0x51, 0x03, &TempArr[1]);
	
	WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
#endif
	
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++)
	{
		WriteBE32(&MidData[MidPos], 0x4D54726B);	// write 'MTrk'
		MidPos += 0x08;
		MidTrkBase = MidPos;
		
		CurDly = 0;
		TrkEnd = (SeqPos == 0x0000);
		
		SeqPos = TrkHdrs[CurTrk].StartOfs;
		TrkMode = TrkHdrs[CurTrk].ChnID & 0x7F;
		MidChn = TrkHdrs[CurTrk].ChnID;
		if (TrkHdrs[CurTrk].ChnID & 0x80)
		{
			MidChn = 0x0A + (TrkMode & 0x0F);
			TempLng = sprintf((char*)TempArr, "SFX Track %u (%s)", TrkMode, TRK_NAMES[TrkMode]);
		}
		else
		{
			MidChn = 0x00 + (TrkMode & 0x0F);
			TempLng = sprintf((char*)TempArr, "Music Track %u (%s)", TrkMode, TRK_NAMES[TrkMode]);
		}
		WriteMetaEvent_Data(MidData, &MidPos, &CurDly, 0x03, TempLng, TempArr);
		
		MstLoopCnt = 0x00;
		NoteMove = 0;
		
		CurNote = 0x00;
		LastNote = 0x00;
		CurNoteVol = 0x7F;
		CurChnVol = 0x00;
		CurNoteLen = 0x00;
		HoldNote = 0x00;
		LoopCnt = 0x00;
		LoopOfs = 0x00;
		SubRetOfs = 0x00;
		while(! TrkEnd && SeqPos)
		{
			/*if (! MstLoopCnt && SeqPos == TrkHdrs[CurTrk].LoopOfs)
			{
				MstLoopCnt ++;
				WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, 0x00);
			}*/
			
			CurCmd = SeqData[SeqPos];	SeqPos ++;
			if (CurCmd < 0x80)
			{
				CurNote = CurCmd;
				if (CurNote)
					CurNote += NoteMove + 24;
				
				if (LastNote != CurNote || ! HoldNote)
				{
					if (HoldNote)
					{
						if (CurDly >= 1)
						{
							CurDly --;
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);
							CurDly ++;
						}
						else
						{
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);
						}
					}
					
					if (LastNote)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					if (CurNote)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, CurNote, CurNoteVol);
					LastNote = CurNote;
					
					if (HoldNote)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x00);
				}
				HoldNote = 0x00;
				
				CurDly += CurNoteLen;
			}
			else if (CurCmd < 0xF0)
			{
				CurNoteLen = CurCmd & 0x7F;
			}
			else
			{
				switch(CurCmd)
				{
				case 0xF0:	// GoSub
					TempPos = ReadLE16(&SeqData[SeqPos]) & 0x3FFF;
					SeqPos += 0x02;
					SubRetOfs = SeqPos;
					SeqPos = TempPos;
					break;
				case 0xF1:	// Return;
					SeqPos = SubRetOfs;
					SubRetOfs = 0x0000;
					break;
				case 0xF2:	// Loop Start
					printf("Command %02X at Offset %04X!\n", CurCmd, 0x8000 | SeqPos);
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, CurCmd);
					LoopCnt = SeqData[SeqPos];
					SeqPos ++;
					LoopOfs = SeqPos;
					break;
				case 0xF3:	// Loop End
					LoopCnt --;
					if (LoopCnt)
						SeqPos = LoopOfs;
					else
						LoopOfs = 0x0000;
					break;
				case 0xF4:	// GoTo
					TempPos = ReadLE16(&SeqData[SeqPos]) & 0x3FFF;
					SeqPos = TempPos;
					
					MstLoopCnt ++;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, MstLoopCnt);
					if (MstLoopCnt >= NUM_LOOPS)
						TrkEnd = 0x01;
					break;
				case 0xF5:	// Tick Multiplier
					TempByt = SeqData[SeqPos];
					SeqPos ++;
					
					WriteBE32(TempArr, TEMPO_BASE * TempByt);
					WriteMetaEvent_Data(MidData, &MidPos, &CurDly, 0x51, 0x03, &TempArr[1]);
					break;
				case 0xF6:	// Transpose
					NoteMove = (INT8)SeqData[SeqPos];
					SeqPos ++;
					break;
				case 0xF7:	// broken/dummy
					printf("Command %02X at Offset %04X!\n", CurCmd, 0x8000 | SeqPos);
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, CurCmd);
					break;
				case 0xF8:	// Set Instrument
					TempByt = SeqData[SeqPos] & 0x7F;
					WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, TempByt, 0x00);
					SeqPos ++;
					break;
				case 0xF9:	// Set Volume Envelope
					TempByt = SeqData[SeqPos];
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x27, TempByt);
					SeqPos ++;
					break;
				case 0xFA:	// Set Modulation Envelope
					TempByt = SeqData[SeqPos];
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x21, TempByt);
					SeqPos ++;
					break;
				case 0xFB:	// alternative Loop
					printf("Command %02X at Offset %04X!\n", CurCmd, 0x8000 | SeqPos);
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, CurCmd);
					TempByt = SeqData[SeqPos + 0x00];
					TempPos = ReadLE16(&SeqData[SeqPos + 0x02]) & 0x3FFF;
					
					if (! LoopCnt)
						LoopCnt = TempByt;
					LoopCnt --;
					if (LoopCnt)
						SeqPos = TempPos;
					break;
				case 0xFC:	// Hold
					SeqPos ++;
					printf("Command %02X at Offset %04X!\n", CurCmd, 0x8000 | SeqPos);
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, CurCmd);
					CurDly += CurNoteLen;
					break;
				case 0xFD:	// broken
					printf("Command %02X at Offset %04X!\n", CurCmd, 0x8000 | SeqPos);
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, CurCmd);
					SeqPos ++;
					break;
				case 0xFE:	// broken
					printf("Command %02X at Offset %04X!\n", CurCmd, 0x8000 | SeqPos);
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, CurCmd);
					break;
				case 0xFF:	// Track End
					TrkEnd = 0x01;
					break;
				}
			}
		}
		if (LastNote)
			WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
		
		WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
		WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
	}
	MidSize = MidPos;
	
	return 0x00;
}

INLINE UINT8 DB2Mid(double DB)
{
	DB += 6.0;
	if (DB > 0.0)
		DB = 0.0;
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}


static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	if (! (Evt & 0x80))
		return;
	
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0x00;
	
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		MidData[*Pos + 0x00] = Evt;
		MidData[*Pos + 0x01] = Val1;
		MidData[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		MidData[*Pos + 0x00] = Evt;
		MidData[*Pos + 0x01] = Val1;
		*Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		MidData[*Pos + 0x00] = Evt;
		MidData[*Pos + 0x01] = Val1;
		MidData[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	default:
		break;
	}
	
	return;
}

static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0x00;
	
	MidData[*Pos + 0x00] = 0xFF;
	MidData[*Pos + 0x01] = MetaType;
	*Pos += 0x02;
	WriteMidiValue(Buffer, Pos, DataLen);
	memcpy(MidData + *Pos, Data, DataLen);
	*Pos += DataLen;
	
	return;
}

static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value)
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
	
	ValData = &Buffer[*Pos];
	CurPos = ValSize;
	TempLng = Value;
	do
	{
		CurPos --;
		ValData[CurPos] = 0x80 | (TempLng & 0x7F);
		TempLng >>= 7;
	} while(TempLng);
	ValData[ValSize - 1] &= 0x7F;
	
	*Pos += ValSize;
	
	return;
}

INLINE UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x00] << 0) | (Data[0x01] << 8);
}

INLINE void WriteBE16(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value & 0xFF00) >> 8;
	Buffer[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}

INLINE void WriteBE32(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value & 0xFF000000) >> 24;
	Buffer[0x01] = (Value & 0x00FF0000) >> 16;
	Buffer[0x02] = (Value & 0x0000FF00) >>  8;
	Buffer[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}
