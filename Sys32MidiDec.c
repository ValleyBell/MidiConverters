// Model2 MIDI Decoder

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>	// for memcpy()
#include <string.h>	// for strlen()
#include <math.h>	// for powf()
#include "stdtype.h"
#include "stdbool.h"

#define INLINE	static __inline


typedef struct _rom_data ROM_DATA;

static UINT8 LoadROMData(const char* FileName, ROM_DATA* Rom, UINT32 ExpectedSize);
INLINE UINT32 GetPtrList(UINT32 ListBase, UINT8 PtrID);
INLINE UINT32 GetPtrListC(UINT32 ListBase, UINT8 PtrID);
INLINE UINT32 GetDataPtr(const UINT8* Data);
INLINE const UINT8* GetRomPtr(UINT32 Address);
static void DecodeMidiData(UINT32 PtrBase);
static UINT32 GetTempoValue(UINT32 OPNClock, UINT16 OPNDiv, UINT16 SwDiv, UINT16 TimerAVal, UINT16 TickpQrt);
static UINT32 WriteMarker(const char* Text, UINT32 MidStPos);
static UINT32 DecodeMidiSegment(UINT32 ROMStPos, UINT32 MidStPos);
static UINT32 DoCommandB0(UINT32 MidStPos, UINT8 Command, UINT8 Arg1, UINT8 Arg2);

INLINE UINT8 TrkVol2MidiVol(UINT8 Volume);
INLINE UINT8 NoteVel2MidiVol(UINT8 Velocity);
INLINE UINT8 DB2MidiVol(float DB);

INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);


/*static const UINT8 WAVE_Header[0x2C] =
{	0x52, 0x49, 0x46, 0x46, 0xFF, 0xFF, 0xFF, 0xFF, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
	0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x22, 0x56, 0x00, 0x00, 0x44, 0xAC, 0x00, 0x00,
	0x01, 0x00, 0x08, 0x00, 0x64, 0x61, 0x74, 0x61, 0xFF, 0xFF, 0xFF, 0xFF};*/



#define MODE_MIDI	0x01	// convert sequences to MIDI
#define MODE_WAV	0x10	// dump samples to WAV
#define MODE_SF2	0x11	// dump samples to SF2 soundfonts

typedef struct _rom_data
{
	UINT32 Size;
	UINT8* Data;
} ROM_DATA;

static UINT32 ROMSize;
static UINT8* ROMData;
static ROM_DATA SmpROM[4];
static UINT32 MidSize;
static UINT8* MidData;
static INT8 NoteTransp[0x10];

//#define SEGASONIC
#define ORUNNERS

#ifdef SEGASONIC
static UINT32 GLOBAL_PTR_OFS = 0x004A78;
static UINT32 GLOBAL_SEG_OFS = 0x004C66;
static UINT32 GLOBAL_TRK_HDR_OFS = 0x001594;
#define MASTER_LISTS
#define SND_DRV_FILE	"epr15785.36"
#endif
#ifdef ORUNNERS
static UINT32 GLOBAL_PTR_OFS = 0x0044DB;
static UINT32 GLOBAL_SEG_OFS = 0x00439B;
static UINT32 GLOBAL_TRK_HDR_OFS = 0x001847;
#define SND_DRV_FILE	"epr15550.bin"
#endif

static bool FixVolume;
static bool FixDrumNotes;
static UINT8 Mode;

#define CMD_LIST		0x00
#define MUSIC_LIST		0x01
#define SFX_LIST		0x02
#define VOICE_LIST		0x03

#define PTR_GROUP	0x00

int main(int argc, char* argv[])
{
	FILE* hFile;
	char FileName[0x10];
	UINT16 SongCnt;
	UINT16 CurSng;
	UINT32 BasePos;
	UINT32 CurPos;
	
	//chdir("E:/mame0148/roms/Sonic_disasm");
	chdir("E:/mame0148/roms/orunners_disasm");
	
	FixVolume = false;
	FixDrumNotes = true;
	Mode = MODE_SF2;
	Mode = MODE_MIDI;
	
	hFile = fopen(SND_DRV_FILE, "rb");
	if (hFile == NULL)
		return 1;
	
	fseek(hFile, 0, SEEK_END);
	ROMSize = ftell(hFile);
	
	ROMData = (UINT8*)malloc(ROMSize);
	fseek(hFile, 0, SEEK_SET);
	fread(ROMData, 0x01, ROMSize, hFile);
	
	fclose(hFile);
	
	/*if ((Mode & 0xF0) == 0x10)
	{
		LoadROMData("mpr15782.33", &SmpROM[0], 0x100000);
		LoadROMData("mpr15783.34", &SmpROM[1], 0x100000);
		LoadROMData("mpr15784.35", &SmpROM[2], 0x100000);
	}
	else*/
	{
		for (CurSng = 0; CurSng < 4; CurSng ++)
		{
			SmpROM[CurSng].Size = 0x00;
			SmpROM[CurSng].Data = NULL;
		}
	}
	
	
	switch(Mode)
	{
	case MODE_MIDI:
		MidSize = 0x20000;
		MidData = (UINT8*)malloc(MidSize);
		
#ifdef MASTER_LISTS
		BasePos = GetPtrList(GLOBAL_PTR_OFS, MUSIC_LIST);
		SongCnt = ROMData[BasePos] + 1;
#else
		BasePos = GLOBAL_PTR_OFS;
		SongCnt = 0x100;
#endif
		for (CurSng = 0x00; CurSng < SongCnt; CurSng ++)
		{
			printf("Song %02X / %02X\n", CurSng, SongCnt);
#ifdef MASTER_LISTS
			CurPos = GetPtrListC(BasePos, (UINT8)CurSng);
#else
			CurPos = GetPtrList(BasePos, (UINT8)CurSng);
#endif
			if (! CurPos)
			{
				printf("Invalid Song Pointer!\n");
				continue;
			}
			DecodeMidiData(CurPos);
			if (! MidSize)
				continue;
			
			sprintf(FileName, "SONG_%02X.MID", CurSng);
			hFile = fopen(FileName, "wb");
			if (hFile == NULL)
			{
				printf("Error saving %s!\n", FileName);
				continue;
			}
			
			fwrite(MidData, 0x01, MidSize, hFile);
			
			fclose(hFile);
		}
		free(MidData);	MidData = NULL;
		break;
	case MODE_WAV:
		printf("Extracting Samples to .wav format ...");
		//ExtractInstrumentSamples("InsData/");
		break;
	}
	
	printf("  Done.\n");
	
	for (CurSng = 0; CurSng < 4; CurSng ++)
	{
		if (SmpROM[CurSng].Data != NULL)
		{
			free(SmpROM[CurSng].Data);
			SmpROM[CurSng].Data = NULL;
		}
	}
	free(ROMData);
	ROMData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

static UINT8 LoadROMData(const char* FileName, ROM_DATA* Rom, UINT32 ExpectedSize)
{
	FILE* hFile;
	UINT32 FileSize;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
	{
		Rom->Size = 0x00;
		Rom->Data = NULL;
		return 0xFF;
	}
	
	fseek(hFile, 0, SEEK_END);
	FileSize = ftell(hFile);
	if (FileSize != ExpectedSize)
	{
		printf("Warning! %s has an incorrect file size! (%u != %u)\n",
				FileSize, ExpectedSize);
		if (FileSize > ExpectedSize)
			FileSize = ExpectedSize;
	}
	
	Rom->Data = (UINT8*)malloc(ExpectedSize);
	if (Rom->Data == NULL)
	{
		fclose(hFile);
		return 0x80;
	}
	Rom->Size = ExpectedSize;
	fseek(hFile, 0, SEEK_SET);
	fread(Rom->Data, 0x01, FileSize, hFile);
	if (FileSize < ExpectedSize)
		memset(Rom->Data + FileSize, 0x00, ExpectedSize - FileSize);
	
	fclose(hFile);
	
	return 0x00;
}

INLINE UINT32 GetPtrList(UINT32 ListBase, UINT8 PtrID)
{
	return ReadLE16(&ROMData[ListBase + PtrID * 0x02]);
}

INLINE UINT32 GetPtrListC(UINT32 ListBase, UINT8 PtrID)
{
	if (PtrID > ROMData[ListBase])
		return 0x0000;
	ListBase ++;
	return GetPtrList(ListBase, PtrID);
}

INLINE UINT32 GetDataPtr(const UINT8* Data)
{
	if (! Data[0x01])
		return ReadLE16(&Data[0x02]);
	else
		return (Data[0x01] << 13) | (ReadLE16(&Data[0x02]) & 0x1FFF);
}

INLINE const UINT8* GetRomPtr(UINT32 Address)
{
	switch((Address & 0xE00000) >> 16)
	{
	/*case 0x00:
	case 0x20:
	case 0x40:
		return NULL;*/
	case 0x60:
		return ROMData;
	case 0x80:
	case 0xA0:
	case 0xC0:
	case 0xE0:
		return SmpROM[(Address >> 21) & 3].Data;
	default:
		return NULL;
	}
}

static void DecodeMidiData(UINT32 PtrBase)
{
	UINT32 SegBase;
	UINT32 CurPos;
	UINT32 SegPos;
	UINT32 MidPos;
	UINT32 MidTrkBase;
	UINT32 TempLng;
	UINT8 Mode;
	UINT8 CurSeg;
	UINT8 LoopCnt;
	char TempStr[0x10];
	
	SegBase = PtrBase;
	memset(NoteTransp, 0x00, 0x10);
	
	Mode = ROMData[SegBase];	SegBase ++;
	if (Mode >= 0x80 || Mode == 0x20)
	{
		MidSize = 0x00;
		return;
	}
	if (Mode >= 0x20)
	{
		MidSize = 0x00;
	}
	
	MidPos = 0x00;
	WriteBE32(&MidData[MidPos], 0x4D546864);	MidPos += 0x04;	// 'MThd' Signature
	WriteBE32(&MidData[MidPos], 0x00000006);	MidPos += 0x04;	// Header Size
	WriteBE16(&MidData[MidPos], 0x0000);		MidPos += 0x02;	// Format: 0
	WriteBE16(&MidData[MidPos], 0x0001);		MidPos += 0x02;	// Tracks: 1
	WriteBE16(&MidData[MidPos], 0x01E0);		MidPos += 0x02;	// Resolution
	
	WriteBE32(&MidData[MidPos], 0x4D54726B);	MidPos += 0x04;	// 'MTrk' Signature
	WriteBE32(&MidData[MidPos], 0x00000000);	MidPos += 0x04;	// Track Size
	MidTrkBase = MidPos;
	
	LoopCnt = 0x00;
	SegPos = SegBase;
	MidData[MidPos] = 0x00;		MidPos ++;	// Delay
	MidData[MidPos] = 0xFF;		MidPos ++;	// FF - Meta Event
	MidData[MidPos] = 0x51;		MidPos ++;	// Meta Event 51 - Tempo
	MidData[MidPos] = 0x03;		MidPos ++;	// Data Length
#ifdef SEGASONIC
	TempLng = GetTempoValue(8053975, 6*24, 4, 0x3EF, 0x1E0);
#elif defined(ORUNNERS)
	TempLng = GetTempoValue(8053975, 6*24, 4, 0x3F0, 0x1E0);
#endif
	MidData[MidPos] = (TempLng >> 16) & 0xFF;	MidPos ++;
	MidData[MidPos] = (TempLng >>  8) & 0xFF;	MidPos ++;
	MidData[MidPos] = (TempLng >>  0) & 0xFF;	MidPos ++;
	
	MidData[MidPos] = 0x00;		MidPos ++;	// Delay
	
	for (CurSeg = 0x00; ; CurSeg ++, SegPos ++)
	{
		if (! ROMData[SegPos])
		{
			// Song End
			
			strcpy(TempStr, "End");
			MidPos += WriteMarker(TempStr, MidPos);
			break;
		}
		else if (ROMData[SegPos] == 0xF0)
		{
			// Jump to other Segment
			SegPos ++;
			TempLng = ReadLE16(&ROMData[SegPos]);
			
			CurSeg = (TempLng - SegBase) - 1;
			SegPos = TempLng - 1;
			LoopCnt ++;
			
			//sprintf(TempStr, "loop%s", LoopCnt ? "End" : "Start");
			sprintf(TempStr, "Loop %hu", LoopCnt);
			MidPos += WriteMarker(TempStr, MidPos);
			
			if (LoopCnt >= 0x02)
				break;	// Terminate Song
		}
		else if (ROMData[SegPos] > 0xF0)
		{
			printf("SegID %02X! (SegPos %04X)\n", ROMData[SegPos], SegPos);
		}
		else
		{
			sprintf(TempStr, "Segment %hu", CurSeg);
			MidPos += WriteMarker(TempStr, MidPos);
			
			TempLng = ReadLE16(&ROMData[GLOBAL_SEG_OFS + ROMData[SegPos] * 0x02]);
			CurPos = GetDataPtr(&ROMData[TempLng]);
			if (CurPos < 0x10000)
			{
				printf("Invalid Segment Offset %04X! (SegPos %04X, SegID %02X)\n", CurPos, SegPos, ROMData[SegPos]);
				//break;
				MidSize = 0;
				return;
			}
			MidPos += DecodeMidiSegment(CurPos, MidPos);
		}
	}
	
	MidData[MidPos] = 0xFF;	MidPos ++;	// FF - Meta Event
	MidData[MidPos] = 0x2F;	MidPos ++;	// Meta Event 2F - Track End
	MidData[MidPos] = 0x00;	MidPos ++;	// Length 00
	
	// Fix Track Size
	WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);
	MidSize = MidPos;
	
	return;
}

static UINT32 GetTempoValue(UINT32 OPNClock, UINT16 OPNDiv, UINT16 SwDiv, UINT16 TimerAVal, UINT16 TickpQrt)
{
	/*
	Countdown = 0x400 - Timer A value
	Tick Time = Countdown / (OPN Clock / OPN Divider) * Software Divider
	Time per Quarter = Tick Time * Ticks/Quarter
	MIDI Time = uSec per Quarter = 1 000 000 / Time per Quarter
	*/
#if 0
	// using floating-point
	UINT32 TmrVal;
	double TickTime;
	
	TmrVal = 0x400 - TimerAVal;
	TickTime = TmrVal * (OPNDiv * SwDiv) / (double)OPNClock;	// Tick Time
	TickTime *= TickpQrt;
	return (UINT32)(1000000 * TickTime + 0.5);	// uSec / Quarter Note
#else
	// I prefer UINT64
	UINT32 TmrVal;
	UINT64 TickTime;
	
	TmrVal = 0x400 - TimerAVal;
	TickTime = TmrVal * (OPNDiv * SwDiv) * TickpQrt;
	TickTime *= 1000000;
	TickTime = (TickTime + OPNClock/2) / OPNClock;
	return (UINT32)TickTime;
#endif
}

static UINT32 WriteMarker(const char* Text, UINT32 MidStPos)
{
	UINT32 MidPos;
	UINT32 StrLen;
	UINT8 DataLen;
	
	MidPos = MidStPos;
	
	StrLen = (UINT8)strlen(Text);
	DataLen = StrLen & 0x7F;
	MidData[MidPos] = 0xFF;		MidPos ++;	// FF - Meta Event
	MidData[MidPos] = 0x06;		MidPos ++;	// Meta Event 06 - Marker
	MidData[MidPos] = DataLen;	MidPos ++;	// Text Length
	memcpy(&MidData[MidPos], Text, DataLen);
	MidPos += DataLen;
	
	MidData[MidPos] = 0x00;		MidPos ++;	// Delay
	return MidPos - MidStPos;
}

static UINT32 DecodeMidiSegment(UINT32 ROMStPos, UINT32 MidStPos)
{
	UINT32 CurPos;
	UINT32 MidPos;
	UINT8 LastCmd;
	UINT8 TempByt;
	UINT8 TrkEnd;
	UINT8 NoDelay;
	UINT8 Param1;
	UINT8 Param2;
	
	CurPos = ROMStPos;
	MidPos = MidStPos;
	
	LastCmd = 0x00;
	TrkEnd = 0x00;
	NoDelay = 0x02;
	while(! TrkEnd)
	{
		if (! NoDelay)
		{
			TempByt = ROMData[CurPos];
			MidData[MidPos] = TempByt;
			CurPos ++;	MidPos ++;
			if (TempByt & 0x80)
			{
				MidData[MidPos] = ROMData[CurPos];
				CurPos ++;	MidPos ++;
			}
		}
		else if (NoDelay == 0x01)
		{
			MidData[MidPos] = 0x00;
			MidPos ++;
		}
		
		NoDelay = 0x00;
		
		TempByt = ROMData[CurPos];
		if (TempByt & 0x80)
		{
			if (TempByt < 0xF0)
			{
				if ((TempByt & 0x0F) == 0x09)
					TempByt = (TempByt & 0xF0) | 0x0F;
				else if ((TempByt & 0x0F) == 0x0F)
					TempByt = (TempByt & 0xF0) | 0x09;
			}
			MidData[MidPos] = TempByt;
			CurPos ++;	MidPos ++;
			LastCmd = TempByt;
		}
		else if (LastCmd >= 0xF0)
		{
			MidData[MidPos] = LastCmd;
			MidPos ++;
		}
		
		switch(LastCmd & 0xF0)
		{
		case 0xA0:	// loc_603E28
		case 0xB0:	// loc_603EB0 (MidEvt_CtrlChg)
		case 0xE0:	// loc_603F0C (MidEvt_NoteOn)
		default:	// loc_603F0C (MidEvt_NoteOn)
			Param1 = ROMData[CurPos];
			MidData[MidPos] = Param1;
			CurPos ++;	MidPos ++;
			
			Param2 = ROMData[CurPos];
			if (Param2 & 0x80)
			{
				NoDelay = 0x01;
				Param2 &= 0x7F;
			}
			
			// additional fixes
			if ((LastCmd & 0xF0) == 0xB0)
			{
				if (Param1 == 0x07 && FixVolume)
					Param2 = TrkVol2MidiVol(Param2);
			}
			MidData[MidPos] = Param2;
			CurPos ++;	MidPos ++;
			
			if ((LastCmd & 0xF0) == 0xB0)
				MidPos += DoCommandB0(MidPos, LastCmd, Param1, Param2);
			break;
		case 0x80:	// loc_603EDE (MidEvt_NoteOff)
			Param1 = ROMData[CurPos];
			if (Param1 & 0x80)
			{
				NoDelay = 0x01;
				Param1 &= 0x7F;
			}
			Param1 += NoteTransp[LastCmd & 0x0F];
			MidData[MidPos] = Param1;
			CurPos ++;	MidPos ++;
			MidData[MidPos] = 0x7F;
			MidPos ++;
			break;
		case 0x90:	// loc_603F0C (MidEvt_NoteOn)
			Param1 = ROMData[CurPos];
			Param1 += NoteTransp[LastCmd & 0x0F];
			MidData[MidPos] = Param1;
			CurPos ++;	MidPos ++;
			
			Param2 = ROMData[CurPos];
			if (Param2 & 0x80)
			{
				NoDelay = 0x01;
				Param2 &= 0x7F;
			}
			
			if (FixVolume)
				Param2 = NoteVel2MidiVol(Param2);
			MidData[MidPos] = Param2;
			CurPos ++;	MidPos ++;
			
			if (LastCmd == 0x99 && FixDrumNotes)
			{
				// Drum notes are never turned off
				MidData[MidPos] = 0x00;						MidPos ++;	// Delay
				MidData[MidPos] = ROMData[CurPos - 0x02];	MidPos ++;	// Note
				MidData[MidPos] = 0x00;						MidPos ++;	// Velocity: 00 (Note Off)
			}
			break;
		case 0xC0:	// loc_603F38
		case 0xD0:
			Param1 = ROMData[CurPos];
			if (Param1 & 0x80)
			{
				NoDelay = 0x01;
				Param1 &= 0x7F;
			}
			MidData[MidPos] = Param1;
			CurPos ++;	MidPos ++;
			break;
		case 0xF0:
			switch(LastCmd)
			{
			case 0xF7:	// loc_603F82
				TempByt = ROMData[CurPos];
				MidData[MidPos] = TempByt;
				CurPos ++;	MidPos ++;
				while(TempByt)
				{
					MidData[MidPos] = ROMData[CurPos];
					CurPos ++;	MidPos ++;
					TempByt --;
				}
				break;
			case 0xF0:	// loc_603F8C
				while(ROMData[CurPos] != 0xF7)
				{
					MidData[MidPos] = ROMData[CurPos];
					CurPos ++;	MidPos ++;
				}
				break;
			case 0xFF:	// loc_603F96
				Param1 = ROMData[CurPos];	// Meta Event Type
				if (Param1 == 0x2F)		// Type 2F - Track End
				{
					// loc_603FE6
					MidPos --;	// undo last byte
					TrkEnd = 0x01;
					break;
				}
				MidData[MidPos] = Param1;
				CurPos ++;	MidPos ++;
				
				TempByt = ROMData[CurPos];	// Meta Event Length
				MidData[MidPos] = TempByt;
				CurPos ++;	MidPos ++;
				while(TempByt)
				{
					MidData[MidPos] = ROMData[CurPos];
					CurPos ++;	MidPos ++;
					TempByt --;
				}
				break;
			default:	// loc_603FE6
				TrkEnd = 0x01;
				break;
			}
			break;
		}
	}
	
	return MidPos - MidStPos;
}

static UINT32 DoCommandB0(UINT32 MidStPos, UINT8 Command, UINT8 Arg1, UINT8 Arg2)
{
#ifdef SEGASONIC
#define TRK_SIZE	0x09
#elif defined(ORUNNERS)
#define TRK_SIZE	0x0A
#endif
	UINT32 MidPos;
	UINT32 BasePtr;
	UINT32 CurPos;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT8 MidChn;
	UINT8 TempByt;
	
	MidPos = MidStPos;
	
	switch(Arg1)
	{
	case 0x20:	// Init All Track Instruments
		BasePtr = GetPtrList(GLOBAL_TRK_HDR_OFS, Arg2);
		
		TrkCnt = 0x08;	// It's hardcoded :(
		CurPos = BasePtr;
		for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, CurPos += TRK_SIZE)
		{
			// Format:
			//	00 - Track Flags
			//	01 - MIDI Channel
			//	02 - Volume -> (MidiVol * 2) + 1
			//	03 - Instrument Number
			//	04 - Transpose
			//	05 - Pitch Bend (always 0)
			//	06 - unknown
			//	07 - unknown
			//	08 - Final Track Volume (always 00 and recalculated after loading the track)
			//	09 - some Pitch Bend modifier
			
			MidChn = ROMData[CurPos + 0x01];
			if (MidChn == 0x09)
				MidChn = 0x0F;
			else if (MidChn == 0x0F)
				MidChn = 0x09;
			
			MidData[MidPos] = 0x00;						MidPos ++;
			MidData[MidPos] = 0xC0 | MidChn;			MidPos ++;
			if (ROMData[CurPos + 0x03] < 0xF0)
				MidData[MidPos] = ROMData[CurPos + 0x03];
			else
				MidData[MidPos] = ROMData[CurPos + 0x03] & 0x0F;
			MidPos ++;
			
			TempByt = ROMData[CurPos + 0x02] / 2;
			if (FixVolume)
				TempByt = TrkVol2MidiVol(TempByt);
			MidData[MidPos] = 0x00;				MidPos ++;
			MidData[MidPos] = 0xB0 | MidChn;	MidPos ++;
			MidData[MidPos] = 0x07;				MidPos ++;
			MidData[MidPos] = TempByt;			MidPos ++;
			
#ifndef ORUNNERS
			NoteTransp[MidChn] = ROMData[CurPos + 0x04];
#endif
			
			if (ROMData[CurPos + 0x05])
			{
			}
		}
		
		break;
	default:
		return 0;
	}
	
	return MidPos - MidStPos;
}


INLINE UINT8 TrkVol2MidiVol(UINT8 Volume)
{
	// Volume is 00..7F
	// The driver scales it up to 00 (min) to FF (max). One step is 0.1875 db.
	// So the non-scaled volume uses 0.375 db steps.
	UINT8 DBVal;
	float DBFlt;
	
	if (! Volume)
		return 0x00;
	
	DBVal = Volume ^ 0x7F;	// 00..7F -> 7F..00
	DBFlt = DBVal * -0.375f;
	return DB2MidiVol(DBFlt);
}

INLINE UINT8 NoteVel2MidiVol(UINT8 Velocity)
{
	/*UINT8 DBVal;
	float DBFlt;
	
	if (! Velocity)
		return 0x00;
	
	DBVal = VELOC_DATA[Velocity & 0x7F];
	// Every entry in the table has bit 0 set, so
	// I'll scale it down to 00..7F here.
	DBVal = (DBVal >> 1) ^ 0x7F;
	DBFlt = DBVal * -0.375f;
	return DB2MidiVol(DBFlt);*/
	return Velocity;
}

INLINE UINT8 DB2MidiVol(float DB)
{
	float TempSng;
	
	TempSng = (float)pow(10.0, DB / 40.0);
	if (TempSng > 1.0f)
		TempSng = 1.0f;
	return (UINT8)(TempSng * 0x7F + 0.5);
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
