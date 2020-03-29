// Model2 MIDI Decoder

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>	// for memcpy()
#include <string.h>	// for strlen()
#include <math.h>	// for powf()
#include "stdtype.h"
#include "stdbool.h"
#include "Soundfont.h"

#define INLINE	static __inline


typedef struct _rom_data ROM_DATA;
typedef struct _sample_def SMPL_DEF;

static UINT8 LoadROMData(const char* FileName, ROM_DATA* Rom, UINT32 ExpectedSize);
static void RomByteswap(UINT32 Size, UINT8* Data);
INLINE UINT32 GetGlobalPtr(UINT32 PtrID);
INLINE const UINT8* GetRomPtr(UINT32 Address);
static void DecodeMidiData(UINT32 PtrBase, UINT8 SongID);
static UINT32 DecodeMidiSegment(UINT32 ROMStPos, UINT32 MidStPos);
static UINT32 DoCommandA0(UINT32 MidStPos, UINT8 Command, UINT8 Arg1, UINT8 Arg2);

INLINE UINT8 TrkVol2MidiVol(UINT8 Volume);
INLINE UINT8 NoteVel2MidiVol(UINT8 Velocity);
INLINE UINT8 DB2MidiVol(float DB);

INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT32 ReadBE24(const UINT8* Data);
INLINE UINT32 ReadBE32(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);

static void ExtractInstrumentSamples(const char* Folder);
static void ExtractSample(const char* FileName, UINT16 SmplID, UINT8 FreqMod);

static void CreateSoundfont(const char* FileName);
static UINT16 GenerateSampleTable(SF2_DATA* SF2Data, UINT8** RetLoopMsk);
static UINT16 GenerateInstruments(SF2_DATA* SF2Data, UINT16 SmplCnt, const UINT8* LoopMsk, UINT8* RetDrmMask);
static void ReadInsData(const UINT8* Data, UINT8 LastNote, UINT8 CurNote, SMPL_DEF* RetSmplDef,
						UINT16 SmplCnt, const UINT8* LoopMask);
static INT16 SCSPtoSF2Rate(UINT16 SCSPRate, UINT8 IsAtk);
static INT16 SCSPtoSF2Level(UINT16 SCSPLevel);
static INT16 RoundTo16(double Value);
static void AddInsBag(UINT16* BagAlloc, UINT16* BagCount, sfInstBag** Bags, UINT16 GenIdx, UINT16 ModIdx);
static void AddInsGen_S16(UINT16* GenAlloc, UINT16* GenCount, sfInstGenList** Gens, UINT16 Type, INT16 Data);
static void AddInsGen_U16(UINT16* GenAlloc, UINT16* GenCount, sfInstGenList** Gens, UINT16 Type, UINT16 Data);
static void AddInsGen_8(UINT16* GenAlloc, UINT16* GenCount, sfInstGenList** Gens, UINT16 Type, UINT8 DataL, UINT8 DataH);
static void GeneratePresets(SF2_DATA* SF2Data, UINT16 InsCnt, const UINT8* DrumMask);


static const UINT8 VELOC_DATA[0x80] =
{	0x00, 0x09, 0x11, 0x19, 0x21, 0x25, 0x29, 0x2D, 0x31, 0x35, 0x39, 0x3D, 0x41, 0x45, 0x49, 0x4D,
	0x51, 0x55, 0x59, 0x5D, 0x61, 0x69, 0x71, 0x79, 0x81, 0x85, 0x89, 0x8D, 0x91, 0x95, 0x99, 0x9D,
	0xA1, 0xA3, 0xA5, 0xA7, 0xA9, 0xAB, 0xAD, 0xAF, 0xB1, 0xB3, 0xB5, 0xB7, 0xB9, 0xBB, 0xBD, 0xBF,
	0xC1, 0xC5, 0xC9, 0xCD, 0xD1, 0xD5, 0xD9, 0xDD, 0xE1, 0xE1, 0xE3, 0xE3, 0xE5, 0xE5, 0xE7, 0xE7,
	0xE9, 0xE9, 0xE9, 0xE9, 0xEB, 0xEB, 0xEB, 0xEB, 0xED, 0xED, 0xED, 0xED, 0xEF, 0xEF, 0xEF, 0xEF,
	0xF1, 0xF1, 0xF1, 0xF1, 0xF3, 0xF3, 0xF3, 0xF3, 0xF5, 0xF5, 0xF5, 0xF5, 0xF7, 0xF7, 0xF7, 0xF7,
	0xF9, 0xF9, 0xF9, 0xF9, 0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0xFB, 0xFD, 0xFD, 0xFD, 0xFD,
	0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF};

static const UINT8 WAVE_Header[0x2C] =
{	0x52, 0x49, 0x46, 0x46, 0xFF, 0xFF, 0xFF, 0xFF, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
	0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x22, 0x56, 0x00, 0x00, 0x44, 0xAC, 0x00, 0x00,
	0x01, 0x00, 0x08, 0x00, 0x64, 0x61, 0x74, 0x61, 0xFF, 0xFF, 0xFF, 0xFF};



#define MODE_MIDI	0x01	// convert sequences to MIDI
#define MODE_WAV	0x10	// dump samples to WAV
#define MODE_SF2	0x11	// dump samples to SF2 soundfonts

#define SONICFIGHTER
//#define FVIPERS

#ifdef SONICFIGHTER
#define SND_DRV_FILE	"epr-19021.31"
#define SMP_ROM_0		"mpr-19022.32"
#define SMP_ROM_1		"mpr-19023.33"
#define SMP_ROM_2		"mpr-19024.34"
#define SMP_ROM_3		"mpr-19025.35"
#endif
#ifdef FVIPERS
#define SND_DRV_FILE	"epr-18628.31"
#define SMP_ROM_0		"mpr-18629.32"
#define SMP_ROM_1		"mpr-18630.33"
#define SMP_ROM_2		"mpr-18631.34"
#define SMP_ROM_3		"mpr-18632.35"
#endif

typedef struct _rom_data
{
	UINT32 Size;
	UINT8* Data;
} ROM_DATA;

typedef struct _sample_def
{
	UINT8 MinNote;
	UINT8 MaxNote;
	UINT8 RootNote;
	UINT8 LoopMode;
	UINT16 SmplID;
	INT16 AtkRate;	// Attack Rate
	INT16 DecRate;	// Decay Rate
	INT16 SusRate;	// Sustain Rate
	INT16 RelRate;	// Release Rate
	UINT16 SusLvl;	// Sustain Level
} SMPL_DEF;

static UINT32 ROMSize;
static UINT8* ROMData;
static ROM_DATA SmpROM[4];
static UINT32 MidSize;
static UINT8* MidData;

static bool FixVolume;		// convert volume to MIDI scale
static UINT8 FixDrumNotes;	// turn off endlessly playing drum notes
							// 01 - end when replaying note of segment ends
							// 02 - end immediately
static bool PatchDrumChn;	// swap MIDI channels 0F and 09
static UINT32 GLOBAL_PTR_OFS = 0x008000;
static UINT8 Mode;

#define SMPL_DATA_ID	0x00	// Offset 0x008000
#define INS_DATA_ID		0x01	// Offset 0x008004
#define TRK_INIT_ID		0x03	// Offset 0x00800C
#define PLAYLIST_ID		0x07	// Offset 0x00801C
#define VEL_DATA_ID		0x0A	// Offset 0x008028

#define PTR_GROUP	0x00

static UINT8 RunningDrmNotes[0x80];

int main(int argc, char* argv[])
{
	FILE* hFile;
	char FileName[0x10];
	UINT8 SongCnt;
	UINT8 CurSng;
	UINT32 BasePos;
	UINT32 CurPos;
	
	FixVolume = true;
	FixDrumNotes = 0x01;
	PatchDrumChn = true;
	Mode = MODE_SF2;
	//Mode = MODE_MIDI;
	
	hFile = fopen(SND_DRV_FILE, "rb");
	if (hFile == NULL)
		return 1;
	
	fseek(hFile, 0, SEEK_END);
	ROMSize = ftell(hFile);
	
	ROMData = (UINT8*)malloc(ROMSize);
	fseek(hFile, 0, SEEK_SET);
	fread(ROMData, 0x01, ROMSize, hFile);
	
	fclose(hFile);
	
	if ((Mode & 0xF0) == 0x10)
	{
		LoadROMData(SMP_ROM_0, &SmpROM[0], 0x200000);
		LoadROMData(SMP_ROM_1, &SmpROM[1], 0x200000);
		LoadROMData(SMP_ROM_2, &SmpROM[2], 0x200000);
		LoadROMData(SMP_ROM_3, &SmpROM[3], 0x200000);
	}
	else
	{
		for (CurSng = 0; CurSng < 4; CurSng ++)
		{
			SmpROM[CurSng].Size = 0x00;
			SmpROM[CurSng].Data = NULL;
		}
	}
	
	
	if (ReadBE16(&ROMData[0x04]) == 0x6000)
	{
		printf("This ROM is byteswapped.\n");
		printf("Swapping bytes again to get a clean ROM ...\n");
		
		printf("Main ROM ...");
		RomByteswap(ROMSize, ROMData);
		printf("  Done.\n");
		for (CurSng = 0; CurSng < 4; CurSng ++)
		{
			if (SmpROM[CurSng].Data != NULL)
			{
				printf("Sample ROM #%u ...", 1 + CurSng);
				RomByteswap(SmpROM[CurSng].Size, SmpROM[CurSng].Data);
				printf("  Done.\n");
			}
		}
	}
	
	switch(Mode)
	{
	case MODE_MIDI:
		MidSize = 0x10000;
		MidData = (UINT8*)malloc(MidSize);
		
		BasePos = GetGlobalPtr(PLAYLIST_ID) & 0x7FFFF;
		CurPos = BasePos + ReadBE16(&ROMData[BasePos + 2 + PTR_GROUP * 2]);	// get Pointer Group Offset
		SongCnt = ReadBE16(&ROMData[CurPos]) + 1;
		
		for (CurSng = 0x00; CurSng < SongCnt; CurSng ++)
		{
			printf("Song %02X / %02X\n", CurSng, SongCnt);
			DecodeMidiData(GetGlobalPtr(PLAYLIST_ID) & 0x7FFFF, CurSng);
			
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
		ExtractInstrumentSamples("InsData/");
		break;
	case MODE_SF2:
		printf("Creating SoundFont ...");
		CreateSoundfont("out.sf2");
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

static void RomByteswap(UINT32 Size, UINT8* Data)
{
	UINT32 CurPos;
	UINT8 TempByt;
	
	for (CurPos = 0x00; CurPos < Size; CurPos += 0x02)
	{
		TempByt = Data[CurPos + 0x00];
		Data[CurPos + 0x00] = Data[CurPos + 0x01];
		Data[CurPos + 0x01] = TempByt;
	}
	
	return;
}

INLINE UINT32 GetGlobalPtr(UINT32 PtrID)
{
	return ReadBE24(&ROMData[GLOBAL_PTR_OFS + PtrID * 0x04]);
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

static void DecodeMidiData(UINT32 PtrBase, UINT8 SongID)
{
	UINT32 SegBase;
	UINT32 CurPos;
	UINT32 SegPos;
	UINT32 MidPos;
	UINT32 MidTrkBase;
	UINT32 TempLng;
	UINT8 TempByt;
	UINT8 CurSeg;
	UINT8 LoopCnt;
	char TempStr[0x10];
	
	// Table Format:
	// - Group Table
	// - Group 1 Table
	// - Group 2 Table
	// etc.
	// 
	// Pointer Table:
	//	 2  Bytes - Number of Pointer n
	//	n*2 Bytes - Pointer List (relative to Table Base)
	//	
	
	// Sound Data:
	//	Ofs	Len	Desc
	//	00	01	Sound Type (SFX/Music)
	//	01	03	unused
	//	04	04	(goes to 003004)
	//	08	??	Data
	
	CurPos = PtrBase + ReadBE16(&ROMData[PtrBase + 2 + PTR_GROUP * 2]);	// get Pointer Group Offset
	SegBase = PtrBase + ReadBE16(&ROMData[CurPos + 2 + SongID * 2]);	// get actual Song Pointer
	
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
	SegPos = SegBase + 0x04;
	MidData[MidPos] = 0x00;		MidPos ++;	// Delay
	MidData[MidPos] = 0xFF;		MidPos ++;	// FF - Meta Event
	MidData[MidPos] = 0x51;		MidPos ++;	// Meta Event 51 - Tempo
	MidData[MidPos] = 0x03;		MidPos ++;	// Data Length
//	TempLng = 0x0927C0;	// 100 BPM (600 000 usec per Quarter)
	TempLng = 0x0927C0 * 0x1E0 / 0x200;
	MidData[MidPos] = (TempLng >> 16) & 0xFF;	MidPos ++;
	MidData[MidPos] = (TempLng >>  8) & 0xFF;	MidPos ++;
	MidData[MidPos] = (TempLng >>  0) & 0xFF;	MidPos ++;
	
	MidData[MidPos] = 0x00;		MidPos ++;	// Delay
	
	memset(RunningDrmNotes, 0x00, 0x80);
	for (CurSeg = 0x00; ; CurSeg ++, SegPos += 0x04)
	{
		sprintf(TempStr, "Segment %hu", CurSeg);
		TempByt = (UINT8)strlen(TempStr);
		MidData[MidPos] = 0xFF;		MidPos ++;	// FF - Meta Event
		MidData[MidPos] = 0x06;		MidPos ++;	// Meta Event 06 - Marker
		MidData[MidPos] = TempByt;	MidPos ++;	// Text Length
		memcpy(&MidData[MidPos], TempStr, TempByt);
		MidPos += TempByt;
		
		MidData[MidPos] = 0x00;		MidPos ++;	// Delay
		
		TempLng = ReadBE32(&ROMData[SegPos]);
		if (ROMData[SegPos + 0x00] == 0x80)
		{
			if ((ROMData[SegPos + 0x01] & 0xE0) == 0xC0)
			{
				MidData[MidPos] = ROMData[SegPos + 0x01];	MidPos ++;
				MidData[MidPos] = ROMData[SegPos + 0x02];	MidPos ++;
			}
			else
			{
				MidData[MidPos] = ROMData[SegPos + 0x01];	MidPos ++;
				MidData[MidPos] = ROMData[SegPos + 0x02];	MidPos ++;
				MidData[MidPos] = ROMData[SegPos + 0x03];	MidPos ++;
			}
			if ((ROMData[SegPos + 0x01] & 0xF0) == 0xA0)
				MidPos += DoCommandA0(MidPos, ROMData[SegPos + 0x01],
										ROMData[SegPos + 0x02], ROMData[SegPos + 0x03]);
			
			MidData[MidPos] = 0x00;	MidPos ++;	// Delay
		}
		else if (TempLng == 0xFFFFFFFF)
		{
			// Song End
			break;
		}
		else if (TempLng == 0xFFFFFFF1)
		{
			// Jump to other Segment
			SegPos += 0x04;
			TempLng = ReadBE32(&ROMData[SegPos]) & 0x7FFFF;
			
			CurSeg = (TempLng - SegBase) / 4 - 1;
			SegPos = TempLng - 0x04;
			LoopCnt ++;
			
			//sprintf(TempStr, "loop%s", LoopCnt ? "End" : "Start");
			sprintf(TempStr, "Loop %hu", LoopCnt);
			TempByt = (UINT8)strlen(TempStr);
			MidData[MidPos] = 0xFF;		MidPos ++;	// FF - Meta Event
			MidData[MidPos] = 0x06;		MidPos ++;	// Meta Event 06 - Marker
			MidData[MidPos] = TempByt;	MidPos ++;	// Text Length
			memcpy(&MidData[MidPos], TempStr, TempByt);
			MidPos += TempByt;
			
			MidData[MidPos] = 0x00;		MidPos ++;	// Delay
			
			if (LoopCnt >= 0x02)
				break;	// Terminate Song
		}
		else if (TempLng == 0xFFFFFFF2)
		{
			printf("Encountered Segment Call F2 at Offset 0x%06X!\n", SegPos);
			break;
		}
		else
		{
			TempLng &= 0x7FFFF;
			MidPos += DecodeMidiSegment(TempLng, MidPos);
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

static UINT32 DecodeMidiSegment(UINT32 ROMStPos, UINT32 MidStPos)
{
	UINT32 CurPos;
	UINT32 MidPos;
	UINT8 LastCmd;
	UINT8 TempByt;
	UINT8 TrkEnd;
	UINT8 NoDelay;
	UINT8 Param1;
	UINT8 IsDrum;
	
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
			if (TempByt < 0xF0 && (TempByt & 0x0F) == 0x0F)
				IsDrum = 0x01;
			else
				IsDrum = 0x00;
			if (TempByt < 0xF0 && PatchDrumChn)
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
			
			TempByt = ROMData[CurPos];
			if (TempByt & 0x80)
			{
				NoDelay = 0x01;
				TempByt &= 0x7F;
			}
			
			// additional fixes
			if ((LastCmd & 0xF0) == 0xB0)
			{
				if (Param1 == 0x07 && FixVolume)
					TempByt = TrkVol2MidiVol(TempByt);
			}
			MidData[MidPos] = TempByt;
			CurPos ++;	MidPos ++;
			
			if ((LastCmd & 0xF0) == 0xA0)
				MidPos += DoCommandA0(MidPos, LastCmd, ROMData[CurPos - 0x02], ROMData[CurPos - 0x01]);
			break;
		case 0x80:	// loc_603EDE (MidEvt_NoteOff)
			TempByt = ROMData[CurPos];
			if (TempByt & 0x80)
			{
				NoDelay = 0x01;
				TempByt &= 0x7F;
			}
			MidData[MidPos] = TempByt;
			if (IsDrum)
				RunningDrmNotes[TempByt] = 0x00;
			CurPos ++;	MidPos ++;
			MidData[MidPos] = 0x7F;
			MidPos ++;
			break;
		case 0x90:	// loc_603F0C (MidEvt_NoteOn)
			if (IsDrum)
			{
				TempByt = ROMData[CurPos];
				if (FixDrumNotes == 0x01 && RunningDrmNotes[TempByt])
				{
					// Drum notes are never turned off
					MidData[MidPos] = ROMData[CurPos];	MidPos ++;	// Note
					MidData[MidPos] = 0x00;				MidPos ++;	// Velocity: 00 (Note Off)
					MidData[MidPos] = 0x00;				MidPos ++;	// Delay
				}
				RunningDrmNotes[TempByt] = 0x01;
			}
			
			MidData[MidPos] = ROMData[CurPos];
			CurPos ++;	MidPos ++;
			TempByt = ROMData[CurPos];
			if (TempByt & 0x80)
			{
				NoDelay = 0x01;
				TempByt &= 0x7F;
			}
			
			if (FixVolume)
				TempByt = NoteVel2MidiVol(TempByt);
			MidData[MidPos] = TempByt;
			CurPos ++;	MidPos ++;
			
			if (IsDrum && FixDrumNotes == 0x02)
			{
				// Drum notes are never turned off
				MidData[MidPos] = 0x00;						MidPos ++;	// Delay
				MidData[MidPos] = ROMData[CurPos - 0x02];	MidPos ++;	// Note
				MidData[MidPos] = 0x00;						MidPos ++;	// Velocity: 00 (Note Off)
			}
			break;
		case 0xC0:	// loc_603F38
		case 0xD0:
			TempByt = ROMData[CurPos];
			if (TempByt & 0x80)
			{
				NoDelay = 0x01;
				TempByt &= 0x7F;
			}
			MidData[MidPos] = TempByt;
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
	
	if (FixDrumNotes == 0x01)
	{
		LastCmd = PatchDrumChn ? 0x99 : 0x9F;
		for (TempByt = 0x00; TempByt < 0x80; TempByt ++)
		{
			if (RunningDrmNotes[TempByt])
			{
				MidData[MidPos] = LastCmd;	MidPos ++;	// Command
				MidData[MidPos] = TempByt;	MidPos ++;	// Note
				MidData[MidPos] = 0x00;		MidPos ++;	// Velocity: 00 (Note Off)
				MidData[MidPos] = 0x00;		MidPos ++;	// Delay
				RunningDrmNotes[TempByt] = 0x00;
			}
		}
	}
	
	return MidPos - MidStPos;
}

static UINT32 DoCommandA0(UINT32 MidStPos, UINT8 Command, UINT8 Arg1, UINT8 Arg2)
{
	UINT32 MidPos;
	UINT32 BasePtr;
	UINT32 CurPos;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT8 MidChn;
	UINT8 TempByt;
	
	MidPos = MidStPos;
	
	// Used Commands: 00, 01, 03..0A
	// Command 10 plays music or sounds.
	switch(Arg1)
	{
	case 0x00:
		// Used Sub-Commands: 01..04, 09..0C, 0E
		switch(Arg2)
		{
		case 0x02:	// Reset All Channels
			// I *could* send something, but this is placed only at the
			// beginning of the song anyway.
			return 0;
		default:
			return 0;
		}
		break;
	case 0x05:	// Load Instruments (Pointer is at 0x008000)
		return 0;
	case 0x07:	// Init All Track Instruments
		BasePtr = GetGlobalPtr(TRK_INIT_ID) & 0x7FFFF;
		if (Arg2 > ReadBE16(&ROMData[BasePtr]))	// the value is the last valid ID
			return 0;
		CurPos = BasePtr + ReadBE16(&ROMData[BasePtr + 0x02 + Arg2 * 0x02]);
		
		// Base+0x00
		// TODO: Enqueue Command A0 01 ROMData[CurPos]
		CurPos ++;
		
		// Base+0x01
//		if (ROMData[CurPos] < 0x80)
//			;	// call Sound Command A0 08
		CurPos ++;
		
		// Base+0x02
//		if (ROMData[CurPos] < 0x80)
//			;	// call Sound Command A0 06
		CurPos ++;
		
		// Base+0x03
//		if (ROMData[CurPos] < 0x80)
//			;	// call Sound Command A0 05
		CurPos ++;
		
		// Base+0x04 - ignored
		CurPos ++;
		// Base+0x05 - Track Count (for the 68000 dbf instruction)
		TrkCnt = ROMData[CurPos] + 1;
		CurPos ++;
		
		for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, CurPos += 0x10)
		{
			// Format:
			//	00 - Track Flags?
			//	01 - MIDI Channel
			//	02 - Instrument Number
			//	03 - Volume -> (MidiVol * 2) + 1
			//	04 - Transpose -> (Ctrl_11 & 0x7F) - 0x40
			//	05 - ?? -> changed by Ctrl_12, read from PanTable
			//	06 - SCSP Direct Data Register
			//		-> bits 0-5 (0x1F) are Pan (Ctrl_0A)
			//		-> bits 6-7 (0xE0) are changed by Ctrl_19 (Instrument Volume)
			//	07 - Unknown -> Ctrl_13
			//	08/09 - SCSP LFO Bits
			//		-> bits  0- 2 (0x0007) are changed by Ctrl_17 (Amp. Mod. Level)
			//		-> bits  3- 4 (0x0018) are changed by Ctrl_15 (Amp. Mod. Wave Select)
			//		-> bits  5- 7 (0x00E0) are changed by Ctrl_16 (Freq. Mod. Level)
			//		-> bits  8- 9 (0x0300) are changed by Ctrl_14 (Freq. Mod. Wave Select)
			//		-> bits 10-14 (0x7C00) are changed by Ctrl_18 (LFO Frequency)
			
			// Example Data:
			//	0001 0203 0405 0607 0809 0A0B 0C0D 0E0F
			//	8000 09FB 0000 6001 52F7 0000 0000 0000
			
			MidChn = ROMData[CurPos + 0x01];
			if (PatchDrumChn)
			{
				if (MidChn == 0x09)
					MidChn = 0x0F;
				else if (MidChn == 0x0F)
					MidChn = 0x09;
			}
			
			MidData[MidPos] = 0x00;						MidPos ++;
			MidData[MidPos] = 0xC0 | MidChn;			MidPos ++;
			MidData[MidPos] = ROMData[CurPos + 0x02];	MidPos ++;
			
			TempByt = ROMData[CurPos + 0x03] / 2;
			if (FixVolume)
				TempByt = TrkVol2MidiVol(TempByt);
			MidData[MidPos] = 0x00;				MidPos ++;
			MidData[MidPos] = 0xB0 | MidChn;	MidPos ++;
			MidData[MidPos] = 0x07;				MidPos ++;
			MidData[MidPos] = TempByt;			MidPos ++;
			
			/*if (ROMData[CurPos + 0x04])
			{
				MidData[MidPos] = 0x00;								MidPos ++;
				MidData[MidPos] = 0xB0 | MidChn;					MidPos ++;
				MidData[MidPos] = 0x11;								MidPos ++;
				MidData[MidPos] = ROMData[CurPos + 0x04] + 0x40;	MidPos ++;
			}
			
			if (ROMData[CurPos + 0x07])
			{
				MidData[MidPos] = 0x00;								MidPos ++;
				MidData[MidPos] = 0xB0 | MidChn;					MidPos ++;
				MidData[MidPos] = 0x13;								MidPos ++;
				MidData[MidPos] = ROMData[CurPos + 0x07];			MidPos ++;
			}*/
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
	UINT8 DBVal;
	float DBFlt;
	
	if (! Velocity)
		return 0x00;
	
	DBVal = VELOC_DATA[Velocity & 0x7F];
	// Every entry in the table has bit 0 set, so
	// I'll scale it down to 00..7F here.
	DBVal = (DBVal >> 1) ^ 0x7F;
	DBFlt = DBVal * -0.375f;
	return DB2MidiVol(DBFlt);
}

INLINE UINT8 DB2MidiVol(float DB)
{
	float TempSng;
	
	TempSng = (float)pow(10.0, DB / 40.0);
	if (TempSng > 1.0f)
		TempSng = 1.0f;
	return (UINT8)(TempSng * 0x7F + 0.5);
}



INLINE UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

INLINE UINT32 ReadBE24(const UINT8* Data)
{
	return	(Data[0x01] << 16) | (Data[0x02] <<  8) | (Data[0x03] <<  0);
}

INLINE UINT32 ReadBE32(const UINT8* Data)
{
	return	(Data[0x00] << 24) | (Data[0x01] << 16) |
			(Data[0x02] <<  8) | (Data[0x03] <<  0);
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



static void ExtractInstrumentSamples(const char* Folder)
{
	UINT32 InsBasePtr;
	UINT32 CurPos;
	UINT8 InsCnt;
	UINT8 CurIns;
	UINT8 CurNote;
	UINT8 MaxNote;
	UINT16 SmplID;
	char* FileName;
	
	FileName = (char*)malloc(strlen(Folder) + 0x20);
	
	InsBasePtr = GetGlobalPtr(INS_DATA_ID) & 0x7FFFF;
	
	InsCnt = ReadBE16(&ROMData[InsBasePtr]) + 0x01;
	for (CurIns = 0x00; CurIns < InsCnt; CurIns ++)
	{
		CurPos = InsBasePtr + ReadBE16(&ROMData[InsBasePtr + 0x02 + CurIns * 0x02]);
		if (ROMData[CurPos] & 0x80)
		{
			// Drum Mode
			CurNote = ROMData[CurPos + 0x02];
			MaxNote = ROMData[CurPos + 0x03];
			CurPos += 0x04;
			for (; CurNote <= MaxNote; CurNote ++, CurPos += 0x0C)
			{
				SmplID = ReadBE16(&ROMData[CurPos + 0x00]);
				sprintf(FileName, "%sIns%02X_d%02X_Smpl%02X.wav", Folder, CurIns, CurNote, SmplID);
				ExtractSample(FileName, SmplID, ROMData[CurPos + 0x02]);
			}
		}
		else
		{
			// Melody Instrument Mode
			do
			{
				CurNote = ROMData[CurPos + 0x00];
				SmplID = ReadBE16(&ROMData[CurPos + 0x04]);
				sprintf(FileName, "%sIns%02X_n%02X_Smpl%02X.wav", Folder, CurIns, CurNote, SmplID);
				ExtractSample(FileName, SmplID, 0xF0);
				
				CurPos += 0x0A;
			} while(CurNote < 0x7F);
		}
	}
	
	free(FileName);
	
	return;
}

static void ExtractSample(const char* FileName, UINT16 SmplID, UINT8 FreqMod)
{
	UINT32 SmplBasePtr;
	UINT32 SmplStart;
	UINT32 SmplLen;
	UINT32 SmplLoopSt;
	UINT32 SmplLoopLen;
	const UINT8* SmplPtr;
	FILE* hFile;
	UINT32 TempLng;
	UINT32 SmplRate;
	
	if (SmplID == 0xFFFF)
		return;
	
	SmplBasePtr = GetGlobalPtr(SMPL_DATA_ID) & 0x7FFFF;
	TempLng = (ReadBE16(&ROMData[SmplBasePtr]) + 1) / 0x10;
	if (SmplID >= TempLng)
		return;
	SmplBasePtr += 0x02 + SmplID * 0x10;
	
	SmplStart =		ReadBE24(&ROMData[SmplBasePtr + 0x00]);
	SmplLen =		ReadBE24(&ROMData[SmplBasePtr + 0x04]);
	SmplLoopSt =	ReadBE24(&ROMData[SmplBasePtr + 0x08]);
	SmplLoopLen =	ReadBE24(&ROMData[SmplBasePtr + 0x0C]);
	
	SmplRate = 44100;
	if (FreqMod & 0x80)
	{
		// Frequency 80..F0 (lower octave)
		while(FreqMod & 0xF0)
		{
			SmplRate /= 2;		// Tone: one octave down
			FreqMod += 0x10;	// subtract from remaining octaves (subtract from negative == add)
		}
	}
	else
	{
		// Frequency 10..70 (higher octave)
		while(FreqMod & 0xF0)
		{
			SmplRate *= 2;		// Tone: one octave up
			FreqMod -= 0x10;	// subtract from remaining octaves
		}
	}
	
	SmplPtr = GetRomPtr(SmplStart);
	if (SmplPtr == NULL)
		return;
	SmplPtr += (SmplStart & 0x1FFFFF);
	//SmplLen += 0x202;	// Melody samples seem to be a bit longer
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", FileName);
		return;
	}
	
	if (0)
	{
		fwrite(SmplPtr, 0x01, SmplLen, hFile);
	}
	else
	{
		UINT8* NewHead;
		UINT8* NewData;
		
		NewHead = (UINT8*)malloc(0x2C);
		memcpy(NewHead, WAVE_Header, 0x2C);
		
		TempLng = SmplLen + 0x24;
		memcpy(&NewHead[0x04], &TempLng, 0x04);		// 'RIFF' chunk length
		memcpy(&NewHead[0x18], &SmplRate, 0x04);	// Sample Rate
		memcpy(&NewHead[0x1C], &SmplRate, 0x04);	// Bytes per Second
		memcpy(&NewHead[0x28], &SmplLen, 0x04);		// 'data' chunk length
		
		NewData = (UINT8*)malloc(SmplLen);
		for (TempLng = 0x00; TempLng < SmplLen; TempLng ++)
			NewData[TempLng] = SmplPtr[TempLng] ^ 0x80;
		
		fwrite(NewHead, 0x01, 0x2C, hFile);
		fwrite(NewData, 0x01, SmplLen, hFile);
		free(NewData);
		free(NewHead);
	}
	
	fclose(hFile);
	
	return;
}


static void CreateSoundfont(const char* FileName)
{
	SF2_DATA* SF2Data;
	UINT16 SmplCnt;
	UINT16 InsCnt;
	UINT8* SmplLoopMask;
	UINT8 DrumMask[0x10];
	UINT8 RetVal;
	
	SF2Data = CreateSF2Base("SCSP Sound Bank");
	
	SmplCnt = GenerateSampleTable(SF2Data, &SmplLoopMask);
	InsCnt = GenerateInstruments(SF2Data, SmplCnt, SmplLoopMask, DrumMask);
	GeneratePresets(SF2Data, InsCnt, DrumMask);
	free(SmplLoopMask);
	
	SortSF2Chunks(SF2Data);
	RetVal = WriteSF2toFile(SF2Data, FileName);
	if (RetVal)
		printf("Save Error: 0x%02X\n", RetVal);
	FreeSF2Data(SF2Data);
	
	return;
}

static UINT16 GenerateSampleTable(SF2_DATA* SF2Data, UINT8** RetLoopMsk)
{
	UINT16 SmplCnt;
	UINT16 CurSmpl;
	
	UINT32 BasePtr;
	UINT32 SmplDBSize;
	INT16* SmplDB;
	UINT32 SmplDBPos;
	UINT32 SmplHdrSize;
	sfSample* SmplHdrs;
	sfSample* TempSHdr;
	
	UINT32 SmplStart;
	UINT32 SmplLen;
	UINT32 SmplLoopSt;
	UINT32 SmplLoopLen;
	UINT32 CurPos;
	const UINT8* SmplPtr;
	
	LIST_CHUNK* LstChk;
	ITEM_CHUNK* ItmChk;
	
	BasePtr = GetGlobalPtr(SMPL_DATA_ID) & 0x7FFFF;
	SmplCnt = ReadBE16(&ROMData[BasePtr]) + 0x01;
	SmplCnt /= 0x10;	// the counter is given in number of bytes, not samples
	BasePtr += 0x02;
	
	// Count all samples
	CurPos = BasePtr + 0x04;	// Data+0x04 = Sample Length
	SmplDBSize = 0x00;
	for (CurSmpl = 0x00; CurSmpl < SmplCnt; CurSmpl ++, CurPos += 0x10)
	{
		SmplLen = ReadBE24(&ROMData[CurPos]);
		//if (! SmplLen)
		//	break;
		SmplDBSize += SmplLen;
	}
	SmplCnt = CurSmpl;
	// according to the SF2 spec., every sample MUST have 46 null-samples at the end.
	SmplDBSize += SmplCnt * 46;
	
	SmplDB = (INT16*)malloc(SmplDBSize * 2);		// We have 8-bit, but need 16-bit samples.
	SmplHdrSize = sizeof(sfSample) * (SmplCnt + 1);	// there's an EOS header
	SmplHdrs = (sfSample*)malloc(SmplHdrSize);
	CurPos = (SmplCnt + 0x07) / 0x08;
	*RetLoopMsk = (UINT8*)malloc(CurPos);
	memset(*RetLoopMsk, 0x00, CurPos);
	
	// fill Sample Structure and generate Sample Database
	SmplDBPos = 0x00;
	for (CurSmpl = 0x00; CurSmpl < SmplCnt; CurSmpl ++, BasePtr += 0x10)
	{
		SmplStart =		ReadBE24(&ROMData[BasePtr + 0x00]);
		SmplLen =		ReadBE24(&ROMData[BasePtr + 0x04]);
		SmplLoopSt =	ReadBE24(&ROMData[BasePtr + 0x08]);
		SmplLoopLen =	ReadBE24(&ROMData[BasePtr + 0x0C]);
		
		TempSHdr = &SmplHdrs[CurSmpl];
		memset(TempSHdr, 0x00, sizeof(sfSample));
		TempSHdr->dwStart = SmplDBPos;
		TempSHdr->dwSampleRate = 44100;
		TempSHdr->byOriginalKey = 60;
		TempSHdr->chCorrection = 0;
		TempSHdr->wSampleLink = 0;
		TempSHdr->sfSampleType = monoSample;
		
		SmplPtr = GetRomPtr(SmplStart);
		if (SmplPtr == NULL || ! SmplLen)
		{
			sprintf(TempSHdr->achSampleName, "Sample %03hX (null)", CurSmpl);
			TempSHdr->dwEnd = SmplDBPos+1;
			TempSHdr->dwStartloop = SmplDBPos;
			TempSHdr->dwEndloop = SmplDBPos;
		}
		else
		{
			SmplPtr += (SmplStart & 0x1FFFFF);
			if (SmplLoopLen)
				(*RetLoopMsk)[CurSmpl >> 3] |= 1 << (CurSmpl & 0x07);
			
			sprintf(TempSHdr->achSampleName, "Sample %03hX", CurSmpl);
			TempSHdr->dwEnd = SmplDBPos + SmplLen;
			CurPos = SmplDBPos + (SmplLoopSt - SmplStart);
			TempSHdr->dwStartloop = CurPos;
			TempSHdr->dwEndloop = CurPos + SmplLoopLen;
			
			for (CurPos = 0x00; CurPos < SmplLen; CurPos ++, SmplDBPos ++)
				SmplDB[SmplDBPos] = SmplPtr[CurPos] << 8;	// 8-bit signed -> 16-bit signed
		}
		// add 46 null-samples
		for (CurPos = 0; CurPos < 46; CurPos ++, SmplDBPos ++)
			SmplDB[SmplDBPos] = 0;
	}
	SmplDBSize = SmplDBPos;	// We probably skipped a few samples, so we need to fix the size.
	
	TempSHdr = &SmplHdrs[CurSmpl];
	memset(TempSHdr, 0x00, sizeof(sfSample));
	strcpy(TempSHdr->achSampleName, "EOS");	// write "End Of Samples" header
	
	// Refresh Sample Count (we probably exited early)
	SmplCnt = CurSmpl;
	SmplHdrSize = sizeof(sfSample) * (SmplCnt + 1);
	
	// --- Add Chunks to SoundFont Data ---
	LstChk = List_GetChunk(SF2Data->Lists, FCC_sdta);
	ItmChk = Item_MakeChunk(FCC_smpl, SmplDBSize * 2, SmplDB, 0x00);
	// Note: with Copy == 0x00, FreeSF2Data will call free(SmplDB)
	List_AddItem(LstChk, ItmChk);
	
	LstChk = List_GetChunk(SF2Data->Lists, FCC_pdta);
	ItmChk = Item_MakeChunk(FCC_shdr, SmplHdrSize, SmplHdrs, 0x00);	// no free() needed either
	List_AddItem(LstChk, ItmChk);
	
	return SmplCnt;
}

static UINT16 GenerateInstruments(SF2_DATA* SF2Data, UINT16 SmplCnt, const UINT8* LoopMsk, UINT8* RetDrmMask)
{
	UINT16 InsCnt;
	UINT16 InsAlloc;
	UINT16 CurIns;
	UINT32 BasePtr;
	UINT32 CurPos;
	
	UINT32 DataSize;
	sfInst* InsData;
	sfInstBag* InsBags;
	sfInstModList InsMod;
	sfInstGenList* InsGen;
	sfInstGenList* TempGen;
	UINT16 InsBagCnt;
	UINT16 InsBagAlloc;
	UINT16 InsGenCnt;
	UINT16 InsGenAlloc;
	
	UINT8 LastNote;
	UINT8 CurNote;
	UINT8 MaxNote;
	SMPL_DEF SmplDef;
	
	LIST_CHUNK* LstChk;
	ITEM_CHUNK* ItmChk;
	
	BasePtr = GetGlobalPtr(INS_DATA_ID) & 0x7FFFF;
	
	InsCnt = ReadBE16(&ROMData[BasePtr]) + 0x01;
	
	InsAlloc = InsCnt + 1;
	InsBagAlloc = InsAlloc * 2;
	InsGenAlloc = InsBagAlloc * 3;
	InsData = (sfInst*)malloc(sizeof(sfInst) * InsAlloc);
	InsBags = (sfInstBag*)malloc(sizeof(sfInstBag) * InsBagAlloc);
	//InsMod = (sfInstModList*)malloc(sizeof(sfInstModList) * 1);
	InsGen = (sfInstGenList*)malloc(sizeof(sfInstGenList) * InsGenAlloc);
	
	InsBagCnt = 0;
	InsGenCnt = 0;
	for (CurIns = 0x00; CurIns < InsCnt; CurIns ++)
	{
		memset(&InsData[CurIns], 0x00, sizeof(sfInst));
		sprintf(InsData[CurIns].achInstName, "Instrument %02hX", CurIns);
		InsData[CurIns].wInstBagNdx = InsBagCnt;
		
		// Instrument Generators format:
		//	[keyRange]
		//	[velRange]
		//	... (more)
		//	sampleID
		CurPos = BasePtr + ReadBE16(&ROMData[BasePtr + 0x02 + CurIns * 0x02]);
		if (ROMData[CurPos] & 0x80)
		{
			// Drum Mode
			RetDrmMask[CurIns >> 3] |= 1 << (CurIns & 0x07);
			
			// set Release Rate for drums
			//AddInsBag(&InsBagAlloc, &InsBagCnt, &InsBags, InsGenCnt, 0);
			
			// 10 seconds, 1200 * Log2(10) = 3986.31
			//AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, releaseVolEnv, 3986);
			
			CurNote = ROMData[CurPos + 0x02];
			MaxNote = ROMData[CurPos + 0x03];
			CurPos += 0x04;
			for (; CurNote <= MaxNote; CurNote ++, CurPos += 0x0C)
			{
				ReadInsData(&ROMData[CurPos], 0xFF, CurNote, &SmplDef, SmplCnt, LoopMsk);
				
				AddInsBag(&InsBagAlloc, &InsBagCnt, &InsBags, InsGenCnt, 0);
				
				AddInsGen_8(&InsGenAlloc, &InsGenCnt, &InsGen, keyRange, SmplDef.MinNote, SmplDef.MaxNote);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, overridingRootKey, SmplDef.RootNote);
				
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, attackVolEnv, SmplDef.AtkRate);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, holdVolEnv, SmplDef.SusRate);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, decayVolEnv, SmplDef.DecRate);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, sustainVolEnv, SmplDef.SusLvl);
				if (SmplDef.LoopMode)
					// Note Off goes to Release Phase
					AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, releaseVolEnv, SmplDef.RelRate);
				else
					// Note Off is ignored
					// 100 seconds, 1200 * Log2(100) = 7972.63
					AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, releaseVolEnv, 7973);
				
				AddInsGen_U16(&InsGenAlloc, &InsGenCnt, &InsGen, sampleModes, SmplDef.LoopMode);
				AddInsGen_U16(&InsGenAlloc, &InsGenCnt, &InsGen, sampleID, SmplDef.SmplID);
			}
		}
		else
		{
			// Melody Instrument Mode
			RetDrmMask[CurIns >> 3] &= ~(1 << (CurIns & 0x07));
			
			LastNote = 0x00;
			do
			{
				CurNote = ROMData[CurPos + 0x00];
				ReadInsData(&ROMData[CurPos], LastNote, CurNote, &SmplDef, SmplCnt, LoopMsk);
				
				AddInsBag(&InsBagAlloc, &InsBagCnt, &InsBags, InsGenCnt, 0);
				
				AddInsGen_8(&InsGenAlloc, &InsGenCnt, &InsGen, keyRange, SmplDef.MinNote, SmplDef.MaxNote);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, overridingRootKey, SmplDef.RootNote);
				
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, attackVolEnv, SmplDef.AtkRate);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, holdVolEnv, SmplDef.SusRate);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, decayVolEnv, SmplDef.DecRate);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, sustainVolEnv, SmplDef.SusLvl);
				AddInsGen_S16(&InsGenAlloc, &InsGenCnt, &InsGen, releaseVolEnv, SmplDef.RelRate);
				
				AddInsGen_U16(&InsGenAlloc, &InsGenCnt, &InsGen, sampleModes, SmplDef.LoopMode);
				AddInsGen_U16(&InsGenAlloc, &InsGenCnt, &InsGen, sampleID, SmplDef.SmplID);
				
				CurPos += 0x0A;
				LastNote = CurNote + 1;
			} while(CurNote < 0x7F);
		}
	}
	memset(&InsData[CurIns], 0x00, sizeof(sfInst));
	strcpy(InsData[CurIns].achInstName, "EOI");
	InsData[CurIns].wInstBagNdx = InsBagCnt;
	
	
	AddInsBag(&InsBagAlloc, &InsBagCnt, &InsBags, InsGenCnt, 0);
	
	if (InsGenCnt >= InsGenAlloc)
	{
		InsGenAlloc = InsGenCnt + 1;
		InsGen = (sfInstGenList*)realloc(InsGen, sizeof(sfInstGenList) * InsGenAlloc);
	}
	TempGen = &InsGen[InsGenCnt];
	TempGen->sfGenOper = 0;	// 'End Of Generators' entry
	TempGen->genAmount.shAmount = 0;
	InsGenCnt ++;
	
	memset(&InsMod, 0x00, sizeof(sfInstModList));
	
	
	LstChk = List_GetChunk(SF2Data->Lists, FCC_pdta);
	
	DataSize = sizeof(sfInst) * InsAlloc;
	ItmChk = Item_MakeChunk(FCC_inst, DataSize, InsData, 0x00);
	List_AddItem(LstChk, ItmChk);
	
	DataSize = sizeof(sfInstBag) * InsBagCnt;
	ItmChk = Item_MakeChunk(FCC_ibag, DataSize, InsBags, 0x00);
	List_AddItem(LstChk, ItmChk);
	
	//DataSize = sizeof(sfInstModList) * 1;
	DataSize = sizeof(sfInstModList);
	ItmChk = Item_MakeChunk(FCC_imod, DataSize, &InsMod, 0x01);
	List_AddItem(LstChk, ItmChk);
	
	DataSize = sizeof(sfInstGenList) * InsGenCnt;
	ItmChk = Item_MakeChunk(FCC_igen, DataSize, InsGen, 0x00);
	List_AddItem(LstChk, ItmChk);
	
	return InsCnt;
}

static void ReadInsData(const UINT8* Data, UINT8 LastNote, UINT8 CurNote, SMPL_DEF* RetSmplDef,
						UINT16 SmplCnt, const UINT8* LoopMask)
{
	INT16 NoteDiff;
	const UINT8* ADSRPtr;
	UINT16 ADSRVal;
	
	if (LastNote < 0x80)
	{
		// Melody Mode
		RetSmplDef->SmplID = ReadBE16(&Data[0x04]);
		
		//CurNote = Data[0x00];
		RetSmplDef->MinNote = LastNote;
		RetSmplDef->MaxNote = CurNote;
		RetSmplDef->RootNote = 96 - (INT8)Data[0x03];
		
		ADSRPtr = &Data[0x06];
	}
	else
	{
		// Drum Mode
		RetSmplDef->SmplID = ReadBE16(&Data[0x00]);
		
		RetSmplDef->MinNote = CurNote;
		RetSmplDef->MaxNote = CurNote;
		
		// high nibble: Octave, low nibble: Note
		NoteDiff = (INT8)Data[0x02] >> 4;	// get octave
		NoteDiff *= 12;						// convert into notes
		NoteDiff += (Data[0x02] & 0x0F);	// add note
		// I'm not sure if the lower bits aren't actually some flags.
		
		// RootKey + NoteDiff (transpose) = RangeKey
		// We got RangeKey, so we need to go the reverse way.
		NoteDiff = CurNote - NoteDiff;
		if (NoteDiff < 0)
			NoteDiff = 0;
		else if (NoteDiff > 192)
			NoteDiff = 192;
		RetSmplDef->RootNote = (UINT8)NoteDiff;
		
		ADSRPtr = &Data[0x08];
	}
	
	if (RetSmplDef->SmplID < SmplCnt)
	{
		// Sample Modes:
		//	0 - no loop
		//	1 - looping
		//	2 - unused (no loop)
		//	3 - looping, leaving loop after KeyOff
		
		// This might look complicated, but it just checks Bit x in LoopMask.
		if (LoopMask[RetSmplDef->SmplID >> 3] & (1 << (RetSmplDef->SmplID & 0x07)))
			RetSmplDef->LoopMode = 0x01;	// Loop on (TODO: 01 or 03?)
		else
			RetSmplDef->LoopMode = 0x00;	// Loop off
	}
	else
	{
		RetSmplDef->SmplID = 0;
		RetSmplDef->LoopMode = 0x00;		// Loop off
	}
	
	// Read ADSR values (SCSP scale)
	ADSRVal = ReadBE16(&ADSRPtr[0x00]);
	RetSmplDef->AtkRate =	(ADSRVal >> 0) & 0x1F;
	RetSmplDef->DecRate =	(ADSRVal >> 6) & 0x1F;
	RetSmplDef->SusRate =	(ADSRVal >> 11) & 0x1F;
	ADSRVal = ReadBE16(&ADSRPtr[0x02]);
	RetSmplDef->RelRate =	(ADSRVal >> 0) & 0x1F;
	RetSmplDef->SusLvl =	(ADSRVal >> 5) & 0x1F;
	
	// convert to SF2 scale
	// SF2 Rate = 1200 * Log2(seconds)
	// SF2 Level = db * 10
	RetSmplDef->AtkRate = SCSPtoSF2Rate(RetSmplDef->AtkRate, 1);
	RetSmplDef->DecRate = SCSPtoSF2Rate(RetSmplDef->DecRate, 0);
	RetSmplDef->SusRate = SCSPtoSF2Rate(RetSmplDef->SusRate, 0);
	RetSmplDef->RelRate = SCSPtoSF2Rate(RetSmplDef->RelRate, 0);
	RetSmplDef->SusLvl = SCSPtoSF2Level(RetSmplDef->SusLvl);
	
	return;
}

static INT16 SCSPtoSF2Rate(UINT16 SCSPRate, UINT8 IsAtk)
{
	// from MAME's scsp.c
	static const double ARTimes[64] = 
	{	100000,100000,8100.0,6900.0,6000.0,4800.0,4000.0,3400.0,3000.0,2400.0,2000.0,1700.0,1500.0,
		1200.0,1000.0,860.0,760.0,600.0,500.0,430.0,380.0,300.0,250.0,220.0,190.0,150.0,130.0,110.0,95.0,
		76.0,63.0,55.0,47.0,38.0,31.0,27.0,24.0,19.0,15.0,13.0,12.0,9.4,7.9,6.8,6.0,4.7,3.8,3.4,3.0,2.4,
		2.0,1.8,1.6,1.3,1.1,0.93,0.85,0.65,0.53,0.44,0.40,0.35,0.0,0.0};
	static const double DRTimes[64] =
	{	100000,100000,118200.0,101300.0,88600.0,70900.0,59100.0,50700.0,44300.0,35500.0,29600.0,25300.0,22200.0,17700.0,
		14800.0,12700.0,11100.0,8900.0,7400.0,6300.0,5500.0,4400.0,3700.0,3200.0,2800.0,2200.0,1800.0,1600.0,1400.0,1100.0,
		920.0,790.0,690.0,550.0,460.0,390.0,340.0,270.0,230.0,200.0,170.0,140.0,110.0,98.0,85.0,68.0,57.0,49.0,43.0,34.0,
		28.0,25.0,22.0,18.0,14.0,12.0,11.0,8.5,7.1,6.1,5.4,4.3,3.6,3.1};
	double RateVal;
	
	if (SCSPRate == 0x00)
		return 32767;	// infinite
	
	if (IsAtk)
		RateVal = ARTimes[SCSPRate * 2];
	else
		RateVal = DRTimes[SCSPRate * 2];
	if (RateVal == 0.0)	// I *can* do that here, since I just copied it from the table,
		return -32768;	// instant
	
	return RoundTo16(1200 * log(RateVal / 1000.0) / log(2.0));
}

static INT16 SCSPtoSF2Level(UINT16 SCSPLevel)
{
	double LinLevel;
	double DBLevel;
	
	if (SCSPLevel >= 0x1F)
		return 32767;
	
	LinLevel = (SCSPLevel ^ 0x1F) / 31.0;
	DBLevel = log(LinLevel) / log(2.0) * 6.0;
	return (INT16)(DBLevel * -10.0 + 0.5);
}

static INT16 RoundTo16(double Value)
{
	if (Value <= -32768.0)
		return -32768;
	else if (Value >= 32767.0)
		return 32767;
	
	if (Value < 0.0)
		return (INT16)(Value - 0.5);
	else
		return (INT16)(Value + 0.5);
}

static void AddInsBag(UINT16* BagAlloc, UINT16* BagCount, sfInstBag** Bags, UINT16 GenIdx, UINT16 ModIdx)
{
	sfInstBag* CurBag;
	if (*BagCount >= *BagAlloc)
	{
		(*BagAlloc) += 0x10;
		*Bags = (sfInstBag*)realloc(*Bags, sizeof(sfInstBag) * *BagAlloc);
	}
	
	CurBag = &(*Bags)[*BagCount];
	CurBag->wInstGenNdx = GenIdx;
	CurBag->wInstModNdx = ModIdx;
	(*BagCount) ++;
	
	return;
}

static void AddInsGen_S16(UINT16* GenAlloc, UINT16* GenCount, sfInstGenList** Gens, UINT16 Type, INT16 Data)
{
	sfInstGenList* CurGen;
	
	if (*GenCount >= *GenAlloc)
	{
		(*GenAlloc) += 0x40;
		*Gens = (sfInstGenList*)realloc(*Gens, sizeof(sfInstGenList) * *GenAlloc);
	}
	
	CurGen = &(*Gens)[*GenCount];
	CurGen ->sfGenOper = Type;
	CurGen ->genAmount.wAmount = Data;
	(*GenCount) ++;
	
	return;
}

static void AddInsGen_U16(UINT16* GenAlloc, UINT16* GenCount, sfInstGenList** Gens, UINT16 Type, UINT16 Data)
{
	AddInsGen_S16(GenAlloc, GenCount, Gens, Type, (INT16)Data);
	
	return;
}

static void AddInsGen_8(UINT16* GenAlloc, UINT16* GenCount, sfInstGenList** Gens, UINT16 Type, UINT8 DataL, UINT8 DataH)
{
	sfInstGenList* CurGen;
	
	if (*GenCount >= *GenAlloc)
	{
		(*GenAlloc) += 0x40;
		*Gens = (sfInstGenList*)realloc(*Gens, sizeof(sfInstGenList) * *GenAlloc);
	}
	
	CurGen = &(*Gens)[*GenCount];
	CurGen ->sfGenOper = Type;
	CurGen ->genAmount.ranges.byLo = DataL;
	CurGen ->genAmount.ranges.byHi = DataH;
	(*GenCount) ++;
	
	return;
}

static void GeneratePresets(SF2_DATA* SF2Data, UINT16 InsCnt, const UINT8* DrumMask)
{
	UINT16 PrsCnt;
	UINT16 PrsAlloc;
	UINT16 CurIns;
	UINT16 CurPrs;
	
	UINT32 DataSize;
	sfPresetHeader* PrsDB;
	sfPresetHeader* TempPHdr;
	sfPresetBag* PrsBags;
	sfModList PrsMod;
	sfGenList* PrsGen;
	
	LIST_CHUNK* LstChk;
	ITEM_CHUNK* ItmChk;
	
	PrsCnt = InsCnt;
	for (CurIns = 0x00; CurIns < InsCnt; CurIns ++)
	{
		if (DrumMask[CurIns >> 3] & (1 << (CurIns & 0x07)))
			PrsCnt ++;
	}
	PrsAlloc = PrsCnt + 1;
	
	PrsDB = (sfPresetHeader*)malloc(sizeof(sfPresetHeader) * PrsAlloc);
	PrsBags = (sfPresetBag*)malloc(sizeof(sfPresetBag) * PrsAlloc);
	//PrsMod = (sfModList*)malloc(sizeof(sfModList) * 1);
	PrsGen = (sfGenList*)malloc(sizeof(sfGenList) * PrsAlloc);
	
	for (CurIns = 0x00, CurPrs = 0x00; CurIns < InsCnt; CurIns ++, CurPrs ++)
	{
		TempPHdr = &PrsDB[CurPrs];
		memset(TempPHdr, 0x00, sizeof(sfPresetHeader));
		sprintf(TempPHdr->achPresetName, "Preset %02hX", CurIns);
		TempPHdr->wPreset = CurIns;			// MIDI Instrument ID
		TempPHdr->wBank = 0x0000;			// Bank MSB 0, Bank LSB 0
		TempPHdr->wPresetBagNdx = CurPrs;
		TempPHdr->dwLibrary = 0;			// must be 0
		TempPHdr->dwGenre = 0;				// must be 0
		TempPHdr->dwMorphology = 0;			// must be 0
		
		PrsBags[CurPrs].wGenNdx = CurPrs;
		PrsBags[CurPrs].wModNdx = 0;
		
		// Preset Generators format:
		//	[keyRange]
		//	[velRange]
		//	... (more)
		//	instrument
		PrsGen[CurPrs].sfGenOper = instrument;
		PrsGen[CurPrs].genAmount.shAmount = CurIns;
		
		if (DrumMask[CurIns >> 3] & (1 << (CurIns & 0x07)))
		{
			CurPrs ++;
			TempPHdr = &PrsDB[CurPrs];
			memset(TempPHdr, 0x00, sizeof(sfPresetHeader));
			sprintf(TempPHdr->achPresetName, "Preset %02hX (drum)", CurIns);
			TempPHdr->wPreset = CurIns;			// MIDI Instrument ID
			TempPHdr->wBank = 0x0080;			// Bank MSB 128, Bank LSB 0
			TempPHdr->wPresetBagNdx = CurPrs;
			TempPHdr->dwLibrary = 0;			// must be 0
			TempPHdr->dwGenre = 0;				// must be 0
			TempPHdr->dwMorphology = 0;			// must be 0
			
			PrsBags[CurPrs].wGenNdx = CurPrs;
			PrsBags[CurPrs].wModNdx = 0;
			PrsGen[CurPrs].sfGenOper = instrument;
			PrsGen[CurPrs].genAmount.shAmount = CurIns;
		}
	}
	TempPHdr = &PrsDB[CurPrs];
	memset(TempPHdr, 0x00, sizeof(sfPresetHeader));
	strcpy(TempPHdr->achPresetName, "EOP");	// write "End Of Presets" header
	TempPHdr->wPresetBagNdx = CurPrs;
	
	PrsBags[CurPrs].wGenNdx = CurPrs;
	PrsBags[CurPrs].wModNdx = 0;
	PrsGen[CurPrs].sfGenOper = 0;		// 'End Of Generators' entry - all 00s
	PrsGen[CurPrs].genAmount.shAmount = 0;
	
	memset(&PrsMod, 0x00, sizeof(sfModList));
	
	LstChk = List_GetChunk(SF2Data->Lists, FCC_pdta);
	
	DataSize = sizeof(sfPresetHeader) * PrsAlloc;
	ItmChk = Item_MakeChunk(FCC_phdr, DataSize, PrsDB, 0x00);
	List_AddItem(LstChk, ItmChk);
	
	DataSize = sizeof(sfPresetBag) * PrsAlloc;
	ItmChk = Item_MakeChunk(FCC_pbag, DataSize, PrsBags, 0x00);
	List_AddItem(LstChk, ItmChk);
	
	//DataSize = sizeof(sfModList) * 1;
	DataSize = sizeof(sfModList);
	ItmChk = Item_MakeChunk(FCC_pmod, DataSize, &PrsMod, 0x01);
	List_AddItem(LstChk, ItmChk);
	
	DataSize = sizeof(sfGenList) * PrsAlloc;
	ItmChk = Item_MakeChunk(FCC_pgen, DataSize, PrsGen, 0x00);
	List_AddItem(LstChk, ItmChk);
	
	return;
}
