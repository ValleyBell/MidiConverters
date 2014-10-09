// Speedy Gonzales SPC -> Midi Converter
// -------------------------------------

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#ifndef M_PI_2
#define M_PI_2	1.57079632679489661923 
#endif
#ifndef M_SQRT2
#define M_SQRT2	1.41421356237309504880
#endif

#include "stdtype.h"
#include "stdbool.h"

typedef struct song_pointers
{
	UINT16 TrkPtr[8];
} SONG_PTRS;
typedef struct instrument_data
{
	UINT8 SmplID;
	INT8 VolL;
	INT8 VolR;
	UINT8 ADSR[4];
} INS_DATA;
typedef struct instrument_midi_data
{
	UINT8 Vol;
	UINT8 Pan;
} INS_MDATA;
typedef struct instrument_library
{
	UINT8 InsCnt;
	INS_DATA* Ins[0x40];
	INS_MDATA MIns[0x40];
} INS_LIB;


typedef struct _midi_track_setting
{
	UINT8 Vol;
	UINT8 Pan;
} MID_TRK_CFG;
typedef struct _midi_track_state
{
	UINT32 CurDly;	// delay until next event
	UINT8 MidChn;
	UINT8 DrumMode;
	INT8 NoteMove;
	UINT8 TrkState;
	UINT8 PBRange;
	UINT8 MidIns;
	MID_TRK_CFG Mid;
	MID_TRK_CFG Drm;
	UINT8 NoteVol;
} MID_TRK_STATE;


void LoadPointerTables(void);
static void LoadInstrumentLib(INS_LIB* InsL, UINT16 TblOffset);

UINT8 SpeedyG2Mid(UINT8 SongID);
static void RefreshChannelIns(UINT8* Buffer, UINT32* Pos, MID_TRK_STATE* MTS, UINT8 TrkID, UINT8 SpcIns);
static void RefreshChannelMode(UINT8* Buffer, UINT32* Pos, MID_TRK_STATE* MTS, UINT8 TrkMode);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, MID_TRK_STATE* MTS, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, UINT8 SongID, const char* Extention);
static double Lin2DB(UINT8 LinVol);
static UINT8 DB2Mid(double DB);
static void CalcInstrumentMidiData(const INS_DATA* InsData, INS_MDATA* MInsData);
static UINT8 GetInsVol(const INS_DATA* InsData);
static UINT8 GetInsPan(const INS_DATA* InsData, double* RetVolFact);

static UINT16 ReadLE16(const UINT8* Data);
static UINT32 ReadMidiValue(const UINT8* Data, UINT32* RetVal);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);


const UINT8 MOD_RANGE[0x03] = {0x18, 0x46, 0x0A};

UINT32 SpcLen;
UINT8* SpcData;
UINT32 Z80DrvLen;
UINT8* Z80DrvData;
UINT32 MidLen;
UINT8* MidData;

bool FixDrumSet;
bool FixVolume;
char OutFileBase[0x100];

UINT8 SongCnt;
SONG_PTRS SongPtrs[0x20];
UINT8 SFXCnt;
UINT16 SFXPtrs[0x80];
INS_LIB InsLib[2];	// 00 - music, 01 - SFX
UINT16 SmplCnt;
UINT16 SmplPtrs[0x100];

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	UINT8 PLMode;
	char TempArr[0x08];
	char* TempPnt;
	UINT8 RetVal;
	UINT8 CurSng;
	
	printf("Speedy Gonzales SPC -> Midi Converter\n-------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: sg2mid.exe Options ROM.bin\n");
		//printf("Options: (options can be combined, default setting is 'dv')\n");
		//printf("    r   Raw conversion (other options are ignored)\n");
		//printf("    d   fix Drums (remaps to GM drums)\n");
		//printf("    v   fix Volume (convert linear to logarithmic MIDI)\n");
		//printf("Supported/verified games: Earnest Evans, El Viento, Arcus Odyssey.\n");
		return 0;
	}
	
	FixDrumSet = true;
	FixVolume = true;
	PLMode = 0x01;
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			FixDrumSet = false;
			FixVolume = false;
			break;
		case 'D':
			FixDrumSet = true;
			break;
		case 'V':
			FixVolume = true;
			break;
		}
		StrPtr ++;
	}
	
	strcpy(OutFileBase, argv[2]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;
	
	hFile = fopen(argv[2], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fread(TempArr, 0x01, 0x08, hFile);
	if (strncmp(TempArr, "SNES-SPC", 0x08))
	{
		fclose(hFile);
		printf("Not an SPC file!\n");
		return 2;
	}
	
	fseek(hFile, 0x100, SEEK_SET);	// jump to SPC RAM dump
	
	SpcLen = 0x10000;	// 64 KB
	SpcData = (UINT8*)malloc(SpcLen);
	fread(SpcData, 0x01, SpcLen, hFile);
	
	fclose(hFile);
	
	LoadPointerTables();
	//if (RetVal)
	//	return 3;
	
	for (CurSng = 0x00; CurSng < SongCnt; CurSng ++)
	{
		printf("Song %02X/%02X\n", 1 + CurSng, SongCnt);
		SpeedyG2Mid(CurSng);
		if (MidLen)
			WriteFileData(MidLen, MidData, CurSng, "mid");
		free(MidData);	MidData = NULL;
	}
	free(SpcData);	SpcData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}


void LoadPointerTables(void)
{
	UINT16 CurPos;
	UINT16 TrkPtr;
	UINT16 TempPtr;
	UINT16 MinPtr;
	UINT8 CurPtr;
	
	/*CurPos = 0x1500;
	do
	{
		CurPos -= 0x02;
		TempPtr = ReadLE16(&SpcData[CurPos]);
	} while(CurPos >= 0x1300 && ! TempPtr);
	SongCnt = (CurPos - 0x1300 + 0x0F) / 0x10;	// Number of Songs = first 0x10-byte-line with non-null pointer*/
	
	CurPos = 0x1300;
	MinPtr = 0x0000;
	for (SongCnt = 0x00; SongCnt < 0x20; SongCnt ++)
	{
		TempPtr = 0x0000;
		for (CurPtr = 0; CurPtr < 8; CurPtr ++, CurPos += 0x02)
		{
			TrkPtr = ReadLE16(&SpcData[CurPos]);
			SongPtrs[SongCnt].TrkPtr[CurPtr] = TrkPtr;
			if (TrkPtr)
			{
				if (TrkPtr < MinPtr)
				{
					TempPtr = 0x0000;	// enforce 2xbreak
					break;
				}
				MinPtr = TrkPtr;
				TempPtr |= TrkPtr;
			}
		}
		if (! TempPtr)
			break;
	}
	
	CurPos = 0x1500;
	for (SFXCnt = 0x00; SFXCnt < 0x80; SFXCnt ++, CurPos += 0x02)
	{
		SFXPtrs[SFXCnt] = ReadLE16(&SpcData[CurPos]);
		if (! SFXPtrs[SFXCnt])
			break;
	}
	
	LoadInstrumentLib(&InsLib[0], 0x1600);
	LoadInstrumentLib(&InsLib[1], 0x1680);
	
	CurPos = 0x1700;
	for (SmplCnt = 0x00; SmplCnt < 0x100; SmplCnt ++, CurPos += 0x02)
	{
		SmplPtrs[SmplCnt] = ReadLE16(&SpcData[CurPos]);
		if (! SmplPtrs[SmplCnt])
			break;
	}
	
	return;
}

static void LoadInstrumentLib(INS_LIB* InsL, UINT16 TblOffset)
{
	UINT16 CurPos;
	UINT8 CurIns;
	UINT16 InsPtr;
	UINT8 LastValidIns;
	
	CurPos = TblOffset;
	LastValidIns = (UINT8)-1;
	for (CurIns = 0x00; CurIns < 0x40; CurIns ++, CurPos += 0x02)
	{
		InsPtr = ReadLE16(&SpcData[CurPos]);
		if (! InsPtr)
		{
			InsL->Ins[CurIns] = NULL;
		}
		else
		{
			InsL->Ins[CurIns] = (INS_DATA*)&SpcData[InsPtr];
			CalcInstrumentMidiData(InsL->Ins[CurIns], &InsL->MIns[CurIns]);
			LastValidIns = CurIns;
		}
	}
	InsL->InsCnt = LastValidIns + 1;
	
	return;
}


UINT8 SpeedyG2Mid(UINT8 SongID)
{
	UINT16 TickpQrt;
	UINT8 MainLoopCnt;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 InPos;
	MID_TRK_STATE MTS;
	UINT32 DstPos;
	UINT32 TrkBase;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 MainLoopIdx;
	
	INT8 Sub_Transp;
	UINT8 Sub_Count;
	UINT16 Sub_RetAddr;
	UINT16 Sub_Base;
	UINT8 LoopCnt;
	UINT16 LoopOfs;
	UINT8 LastSpcIns;
	
	UINT32 TempLng;
	UINT8 TempByt;
	UINT8 CurNote;
	
	UINT32 CurNoteLen;
	UINT32 CurNoteDly;
	
	UINT8 MsgMask;
	
	TrkCnt = 0;
	for (CurTrk = 0; CurTrk < 8; CurTrk ++)
	{
		if (SongPtrs[SongID].TrkPtr[CurTrk])
			TrkCnt ++;
	}
	TickpQrt = 48;
	MainLoopCnt = 2;
	
	MidLen = 0x20000;	// 128 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBE32(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBE16(&MidData[DstPos + 0x00], 0x0001);		// Format 1
	WriteBE16(&MidData[DstPos + 0x02], TrkCnt);
	WriteBE16(&MidData[DstPos + 0x04], TickpQrt);	// Ticks per Quarter: 48
	DstPos += 0x06;
	
	for (CurTrk = 0; CurTrk < 8; CurTrk ++)
	{
		InPos = SongPtrs[SongID].TrkPtr[CurTrk];
		if (! InPos)
			continue;
		
		WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		MTS.CurDly = 0x00;
		
		TrkEnd = ! InPos;
		MainLoopIdx = 0x00;
		Sub_Base = 0x0000;
		Sub_RetAddr = 0x0000;
		Sub_Count = 0;
		Sub_Transp = 0x00;
		LoopCnt = 0x00;
		LoopOfs = 0x0000;
		
		MTS.MidChn = CurTrk;
		MTS.DrumMode = 0x00;
		MTS.TrkState = 0x00;
		MTS.NoteMove = 0x00;
		MsgMask = 0x00;
		MTS.PBRange = 0x00;
		
		LastSpcIns = 0xFF;
		MTS.MidIns = 0xFF;
		MTS.Mid.Vol = 0xFF;
		MTS.Mid.Pan = 0xFF;
		MTS.Drm.Vol = 0xFF;
		MTS.Drm.Pan = 0xFF;
		MTS.NoteVol = 0x7F;
		while(! TrkEnd)
		{
			CurCmd = SpcData[InPos];
			if (CurCmd < 0x80)
			{
				CurNote = CurCmd + Sub_Transp;
				InPos ++;
				TempByt = SpcData[InPos];	InPos ++;
				if (TempByt != LastSpcIns)
				{
					LastSpcIns = TempByt;
					RefreshChannelIns(MidData, &DstPos, &MTS, CurTrk, LastSpcIns);
				}
				if (MTS.DrumMode && MTS.NoteMove)
					CurNote = (UINT8)MTS.NoteMove;
				else
					CurNote += MTS.NoteMove;
				
				InPos += ReadMidiValue(&SpcData[InPos], &CurNoteDly);
				InPos += ReadMidiValue(&SpcData[InPos], &CurNoteLen);
				
				WriteEvent(MidData, &DstPos, &MTS, 0x90, CurNote, MTS.NoteVol);
				if (CurNoteLen < CurNoteDly)
				{
					MTS.CurDly += CurNoteDly - CurNoteLen;
					CurNoteDly = CurNoteLen;
				}
				else
				{
					printf("Warning: Note Len %u >= Delay %u!\n", CurNoteLen, CurNoteDly);
				}
				WriteEvent(MidData, &DstPos, &MTS, 0x90, CurNote, 0x00);
				MTS.CurDly += CurNoteDly;
			}
			else
			{
				switch(CurCmd)
				{
				case 0x80:	// Track End
					TrkEnd = true;
					InPos += 0x01;
					break;
				case 0x81:	// looped Subroutine
					Sub_Transp = (INT8)SpcData[InPos + 0x01];
					Sub_Count = SpcData[InPos + 0x02];
					Sub_Base = ReadLE16(&SpcData[InPos + 0x03]);
					InPos += 0x05;
					Sub_RetAddr = InPos;
					
					InPos = Sub_Base;
					break;
				case 0x82:	// Subroutine Return
					Sub_Count --;
					InPos ++;
					
					if (Sub_Count)
					{
						InPos = Sub_Base;
					}
					else
					{
						InPos = Sub_RetAddr;
						Sub_Base = 0x0000;
						Sub_RetAddr = 0x0000;
						Sub_Transp = 0x00;
					}
					break;
				case 0x83:	// GoTo
					InPos = ReadLE16(&SpcData[InPos + 0x01]);
					MainLoopIdx ++;
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x6F, MainLoopIdx);
					if (MainLoopIdx >= MainLoopCnt)
						TrkEnd = 0x01;
					break;
				case 0x84:	// Portamento On
					TempByt = SpcData[InPos + 0x01];
					TempByt = (UINT8)(sqrt(TempByt / 255.0f) * 0x7F + 0.5);
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x05, TempByt);
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x41, 0x7F);
					MTS.TrkState |= 0x01;
					InPos += 0x03;
					break;
				case 0x85:	// Portamento Off
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x41, 0x00);
					MTS.TrkState &= ~0x01;
					InPos ++;
					break;
				case 0x88:	// Rest
					InPos += 0x02;
					InPos += ReadMidiValue(&SpcData[InPos], &CurNoteDly);
					InPos += ReadMidiValue(&SpcData[InPos], &CurNoteLen);
					MTS.CurDly += CurNoteDly;
					break;
				case 0x89:
					printf("Event %02X %02X on track %X at %04X\n",
							SpcData[InPos + 0x00], SpcData[InPos + 0x01], CurTrk, InPos);
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x6E, CurCmd & 0x7F);
					InPos += 0x02;
					break;
				case 0x8A:	// Tempo
					TempByt = SpcData[InPos + 0x01];
					
					TempLng = TempByt * 0x80 * TickpQrt;
					WriteEvent(MidData, &DstPos, &MTS, 0xFF, 0x51, 0x03);
					WriteBE32(&MidData[DstPos - 0x01], TempLng);
					MidData[DstPos - 0x01] = 0x03;	// write again, because the above instruction overwrote it
					DstPos += 0x03;
					
					InPos += 0x02;
					break;
				case 0x8B:	// Modulation On
					TempByt = SpcData[InPos + 0x01];
					if (TempByt > 0x02)
						TempByt = 0x01;
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x01, MOD_RANGE[TempByt]);
					MTS.TrkState |= 0x02;
					InPos += 0x02;
					break;
				case 0x8C:	// Modulation Off
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x01, 0x00);
					MTS.TrkState &= ~0x02;
					InPos ++;
					break;
				case 0x8E:	// Loop Start
					LoopCnt = SpcData[InPos + 0x01];
					InPos += 0x02;
					LoopOfs = InPos;
					break;
				case 0x8F:	// Loop End
					InPos ++;
					LoopCnt --;
					if (LoopCnt && LoopOfs)
						InPos = LoopOfs;
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", SpcData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x6E, CurCmd & 0x7F);
					InPos += 0x01;
					TrkEnd = true;
					break;
				}
			}
		}
		if (MTS.TrkState & 0x02)	// enforce Modulation Off at track end
			WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x01, 0x00);
		if (MTS.TrkState & 0x01)	// enforce Portamento Off at track end
			WriteEvent(MidData, &DstPos, &MTS, 0xB0, 0x41, 0x00);
		WriteEvent(MidData, &DstPos, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);		// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
}

static void RefreshChannelIns(UINT8* Buffer, UINT32* Pos, MID_TRK_STATE* MTS, UINT8 TrkID, UINT8 SpcIns)
{
	const UINT8 DrumNote[0x04] = {0x24, 0x26, 0x2A, 0x2E};
	const INS_DATA* TempIns;
	const INS_MDATA* TempMIns;
	MID_TRK_CFG* MidSet;
	
	if (SpcIns < 0x40)
	{
		TempIns = InsLib[0].Ins[SpcIns];
		TempMIns = &InsLib[0].MIns[SpcIns];
	}
	else
	{
		printf("Warning: used instrument 0x%02X!\n", SpcIns);
		TempIns = InsLib[1].Ins[SpcIns & 0x3F];
		TempMIns = &InsLib[1].MIns[SpcIns & 0x3F];
	}
	if (TempIns == NULL)
	{
		RefreshChannelMode(Buffer, Pos, MTS, 0x00 | TrkID);
		MidSet = &MTS->Mid;
		MTS->MidIns = SpcIns;
		MidSet->Vol = 0x7F;
		MidSet->Pan = 0x40;
		WriteEvent(MidData, Pos, MTS, 0xC0, SpcIns, 0x00);
		WriteEvent(MidData, Pos, MTS, 0xB0, 0x07, MidSet->Vol);
		WriteEvent(MidData, Pos, MTS, 0xB0, 0x0A, MidSet->Pan);
		return;
	}
	
	if (TempIns->SmplID != MTS->MidIns)
	{
		MTS->MidIns = TempIns->SmplID;
		if (MTS->MidIns < 0x03)
		{
			RefreshChannelMode(Buffer, Pos, MTS, 0x80 | TrkID);
			if (! (MTS->TrkState & 0x10))
			{
				MTS->TrkState |= 0x10;
				WriteEvent(MidData, Pos, MTS, 0xC0, 0x00, 0x00);
			}
			MTS->NoteMove = DrumNote[MTS->MidIns];
		}
		else
		{
			RefreshChannelMode(Buffer, Pos, MTS, 0x00 | TrkID);
			WriteEvent(MidData, Pos, MTS, 0xC0, TempIns->SmplID, 0x00);
		}
	}
	
	MidSet = MTS->DrumMode ? &MTS->Drm : &MTS->Mid;
	
	if (MidSet->Vol != 0x7F)
	{
		MidSet->Vol = 0x7F;
		WriteEvent(MidData, Pos, MTS, 0xB0, 0x07, MidSet->Vol);
	}
	MTS->NoteVol = TempMIns->Vol;
	
	if (TempMIns->Pan != MidSet->Pan)
	{
		MidSet->Pan = TempMIns->Pan;
		WriteEvent(MidData, Pos, MTS, 0xB0, 0x0A, MidSet->Pan);
	}
	
	return;
}

static void RefreshChannelMode(UINT8* Buffer, UINT32* Pos, MID_TRK_STATE* MTS, UINT8 TrkMode)
{
	if (TrkMode & 0x80)
	{
		if (MTS->DrumMode)
			return;
		MTS->DrumMode = 0x01;
		MTS->MidChn = 0x09;
	}
	else
	{
		if (! MTS->DrumMode)
			return;
		
		MTS->DrumMode = 0x00;
		MTS->MidChn = TrkMode & 0x0F;
		MTS->NoteMove = 0x00;
	}
}


static void WriteEvent(UINT8* Buffer, UINT32* Pos, MID_TRK_STATE* MTS, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	if (! (Evt & 0x80))
		return;
	
	WriteMidiValue(Buffer, Pos, MTS->CurDly);
	MTS->CurDly = 0x00;
	
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		MidData[*Pos + 0x00] = Evt | MTS->MidChn;
		MidData[*Pos + 0x01] = Val1;
		MidData[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		MidData[*Pos + 0x00] = Evt | MTS->MidChn;
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

static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, UINT8 SongID, const char* Extention)
{
	char FileName[0x100];
	FILE* hFile;
	
	if (SongID == 0xFF)
		sprintf(FileName, "%s.%s", OutFileBase, Extention);
	else
		sprintf(FileName, "%s_%02X.%s", OutFileBase, SongID, Extention);
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

static double Lin2DB(UINT8 LinVol)
{
	//return log(LinVol / 254.0) / log(2.0) * 6.0;
	return log(LinVol / 254.0) * 8.65617024533378;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

static void CalcInstrumentMidiData(const INS_DATA* InsData, INS_MDATA* MInsData)
{
	UINT8 VolL;
	UINT8 VolR;
	double VolPanFact;
	double LinVol;
	double DBVol;
	UINT8 FinVol;
	
	MInsData->Pan = GetInsPan(InsData, &VolPanFact);
	
	VolL = abs(InsData->VolL);	VolR = abs(InsData->VolR);
	LinVol = (VolL + VolR) / 254.0 * VolPanFact;
	DBVol = log(LinVol) * 8.65617024533378;	// log(x) / log(2) * 6;
	
	DBVol += 6;	// 200% volume
	if (DBVol < -12)
		DBVol += 6;
	FinVol = DB2Mid(DBVol);
	if (FinVol <= 0)
		FinVol = 1;
	else if (FinVol > 0x7F)
		FinVol = 0x7F;
	MInsData->Vol = FinVol;
	
	return;
}

static UINT8 GetInsVol(const INS_DATA* InsData)
{
	UINT8 VolL;
	UINT8 VolR;
	double DBVol;
	UINT8 FinVol;
	
	// The SNES uses a signed volume.
	VolL = abs(InsData->VolL);	VolR = abs(InsData->VolR);
	DBVol = Lin2DB(VolL + VolR);
	FinVol = DB2Mid(DBVol + 6);	// 200% volume
	if (FinVol <= 0)
		FinVol = 1;
	else if (FinVol > 0x7F)
		FinVol = 0x7F;
	return FinVol;
}

static UINT8 GetInsPan(const INS_DATA* InsData, double* RetVolFact)
{
	// GM Pan Formula:
	//	PanAmount = (PanCtrlVal - 1) / 126
	//	Left  Channel Gain [dB] = 20 * log10(cos(Pi / 2 * PanAmount))
	//	Right Channel Gain [dB] = 20 * log10(sin(Pi / 2 * PanAmount))
	UINT8 VolL;
	UINT8 VolR;
	double VolDiff;
	double VolBoost;
	double PanAngle;
	double PanVal;
	UINT8 FinPan;
	
	VolL = abs(InsData->VolL);	VolR = abs(InsData->VolR);
	if (VolL == VolR)
	{
		if (RetVolFact != NULL)
			*RetVolFact = 1.0;
		return 0x40;
	}
	VolDiff = VolR / (double)(VolL + VolR);
	
	PanAngle = atan2(VolDiff, 1.0 - VolDiff);
	VolBoost = M_SQRT2 / (cos(PanAngle) + sin(PanAngle));
	PanVal = PanAngle / M_PI_2;
	
	FinPan = (UINT8)(PanVal * 0x80 + 0.5);	// actually the range is 1..126, but this looks nicer
	if (FinPan > 0x7F)
		FinPan = 0x7F;
	if (RetVolFact != NULL)
		*RetVolFact = VolBoost;
	return FinPan;
	
#if 0
	{
		double VolM;
		
		VolM = (VolL + VolR) / 2.0;
		DBDiff = (VolR - VolM) / VolM;
		if (DBDiff < -1.0)
			DBDiff = -1.0;
		else if (DBDiff > +1.0)
			DBDiff = +1.0;
		FinPan = (UINT8)(0x40 + DBDiff * 0x40 + 0.5);
		if (FinPan > 0x7F)
			FinPan = 0x7F;
		return FinPan;
	}
#endif
}


static UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

static UINT32 ReadMidiValue(const UINT8* Data, UINT32* RetVal)
{
	UINT32 Value;
	UINT32 CurPos;
	
	CurPos = 0x00;
	Value = 0x00;
	while(Data[CurPos] & 0x80)
	{
		Value <<= 7;
		Value |= (Data[CurPos] & 0x7F);
		CurPos ++;
	}
	Value <<= 7;
	Value |= Data[CurPos];
	CurPos ++;
	
	*RetVal = Value;
	return CurPos;
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
