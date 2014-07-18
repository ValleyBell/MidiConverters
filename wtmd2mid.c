// Wolf Team MegaDrive -> Midi Converter
// -------------------------------------
// Written by Valley Bell, 16 July 2014
// Last Update: 18 July 2014

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "stdtype.h"
#include "stdbool.h"

void ConvertAllSongs(UINT16 MusBankList);
UINT8 Wolfteam2Mid(UINT32 SongStartPos);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, UINT8 SongID, const char* Extention);
static double Lin2DB(UINT8 LinVol);
static UINT8 DB2Mid(double DB);
static UINT8 PanBits2MidiPan(UINT8 Pan);

static UINT16 ReadBE16(const UINT8* Data);
static UINT32 ReadBE32(const UINT8* Data);
static UINT16 ReadLE16(const UINT8* Data);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);


static void LZSS_Init(void);
void LZSS_Decode(UINT32 CmpLen, const UINT8* CmpData, UINT32 DecLen, UINT8* DecData);
UINT8 DecompressArcOdyssey(const UINT8* ComprData, UINT32* RetDataLen, UINT8** RetData);

static UINT32 ScanForData(UINT32 DataLen, const UINT8* Data, UINT32 MagicLen,
						  const UINT8* MagicData, UINT32 StartPos);
static UINT32 ReadLEA(const UINT8* Code, UINT32 InstPos);
void WolfTeamDriver_Autodetection(void);


typedef struct running_note
{
	UINT8 MidChn;
	UINT8 Note;
	UINT16 RemLen;
} RUN_NOTE;


#define RAMMODE_NONE	0x00	// don't have Z80 RAM
#define RAMMODE_PTR		0x01	// Z80 RAM data is pointer to ROM data
#define RAMMODE_ALLOC	0x02	// Z80 RAM data was allocated and has to be free'd

UINT32 ROMLen;
UINT8* ROMData;
UINT8 Z80DrvMode;
UINT32 Z80DrvLen;
UINT8* Z80DrvData;
UINT16 Z80MusList;
UINT32 MidLen;
UINT8* MidData;

#define MAX_RUN_NOTES	0x10	// effectively it won't play more than 2 notes at the same time
								// (2 because this does pitch bends)
UINT8 RunNoteCnt;
RUN_NOTE RunNotes[MAX_RUN_NOTES];
bool FixDrumSet;
bool FixVolume;
char OutFileBase[0x100];

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	UINT8 PLMode;
	UINT32 SongPos;
	char* TempPnt;
	
	printf("Wolf Team MegaDrive -> Midi Converter\n-------------------------------------\n");
	if (argc < 3)
	{
		printf("Usage: wtmd2mid.exe Options ROM.bin\n");
		printf("Options: (options can be combined, default setting is 'dv')\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    d   fix Drums (remaps to GM drums)\n");
		printf("    v   fix Volume (convert linear to logarithmic MIDI)\n");
		printf("Supported/verified games: Earnest Evans, El Viento, Arcus Odyssey.\n");
		return 0;
	}
	
	FixDrumSet = true;
	FixVolume = true;
	PLMode = 0x01;
	SongPos = 0x00;
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
	
	fseek(hFile, 0x00, SEEK_END);
	ROMLen = ftell(hFile);
	if (ROMLen > 0x400000)	// 4 MB
		ROMLen = 0x400000;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	Z80DrvMode = RAMMODE_NONE;
	Z80MusList = 0x0000;
	WolfTeamDriver_Autodetection();
	if (PLMode)
	{
		ConvertAllSongs(Z80MusList);
	}
	else
	{
		Wolfteam2Mid(SongPos);
		WriteFileData(MidLen, MidData, 0xFF, "mid");
		free(MidData);	MidData = NULL;
	}
	printf("Done.\n");
	
	if (Z80DrvMode == RAMMODE_ALLOC)
	{
		free(Z80DrvData);	Z80DrvData = NULL;
	}
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

void ConvertAllSongs(UINT16 MusBankList)
{
	UINT8 SongCnt;
	UINT8 CurSong;
	UINT16 CurPos;
	UINT32 SongBank;
	UINT8 SongID;
	UINT32 SongOfs;
	
	SongCnt = 0x00;
	CurPos = MusBankList;
	while(CurPos < Z80DrvLen)
	{
		if (Z80DrvData[CurPos + 0x01] != 0xFF)
		{
			if ((UINT32)(Z80DrvData[CurPos + 0x00] << 15) >= ROMLen ||
				Z80DrvData[CurPos + 0x01] >= 0x40)
				break;
		}
		SongCnt ++;
		CurPos += 0x02;
	}
	printf("%u songs found.\n", SongCnt);
	
	CurPos = MusBankList;
	for (CurSong = 0x00; CurSong < SongCnt; CurSong ++, CurPos += 0x02)
	{
		SongID = Z80DrvData[CurPos + 0x00];
		SongBank = Z80DrvData[CurPos + 0x01] << 15;	// Song Bank
		SongOfs = ReadBE32(&ROMData[SongBank | (SongID << 2)]);
		SongOfs = SongBank | (SongOfs & 0x7FFF);
		printf("Song %02X/%02X - Bank %02X, ID %02X, Offset %06X\n", 1 + CurSong, SongCnt,
				SongBank >> 15, SongID, SongOfs);
		
		Wolfteam2Mid(SongOfs);
		if (MidLen)
		{
			WriteFileData(ReadLE16(&ROMData[SongOfs + 0x02]), &ROMData[SongOfs], 1 + CurSong, "bin");
			WriteFileData(MidLen, MidData, 1 + CurSong, "mid");
		}
		free(MidData);	MidData = NULL;
	}
	
	return;
}

typedef struct
{
	UINT8 Ins;
	UINT8 Transp;
} INS_TABLE;
UINT8 Wolfteam2Mid(UINT32 SongStartPos)
{
	const UINT8 DAC_MAP[] = {0x00, 0x24, 0x26, 0x29, 0x2D, 0x30, 0x25, 0x27, 0x2A, 0x00, 0x31};	// X68000 Drum Set
	//const UINT8 DAC_MAP[] = {0x00, 0x24, 0x26, 0x29, 0x2D, 0x30, 0x00, 0x27};	// MegaDrive Drum Set
	const INS_TABLE INS_MAP[] =
	{	{0x21,   0},	// 00 E.Bass
		{0x37,   0},	// 01 Shio.B - El Viento: Orch. Hit
		{0x1E,   0},	// 02 Hard.R
		{0x3D,   0},	// 03 NewBrs [verify]
		{0xFF,   0},	// 04 KAZAN
		{0x0C, +24},	// 05 Marimb
		{0x05, +12},	// 06 EPian3
		{0x2E,   0},	// 07 Hrap [verify]
		{0x31,   0},	// 08 Str2 [verify]
		{0x06, +12},	// 09 Cemba1
		{0x3E, +12},	// 0A NewBrs
		{0x26,   0},	// 0B DnaBas
		{0x07,   0},	// 0C CLAVI [verify]
		{0x3C, +12},	// 0D NewHrn
		{0xFF,   0},	// 0E NOBISI
		{0x01, +12},	// 0F APiano
		{0x08, +12},	// 10 TubBel
		{0x10,   0},	// 11 EOrg1 [verify]
		{0xFF,   0},	// 12 BiYoon
		{0x34, +12},	// 13 Chorus
		{0xFF,   0},	// 14 KARAN
		{0x23, +12},	// 15 Paon (&#xFF8A;&#xFF9F;&#xFF75;&#xFF70;&#xFF9D;)
		{0x49, +12},	// 16 FLUTE
		{0xFF,   0},	// 17 N.Bass
		{0x22,   0},	// 18 H.EBas
		{0x46,   0},	// 19 Basson
		{0x16, +12},	// 1A Uno Harm (&#xFF73;&#xFF89;Harm)
		{0x33,   0},	// 1B SynStr [verify]
		{0x47, +12},	// 1C Clarin
		{0x20,   0},	// 1D KBASS1 [verify]
		{0x27,   0},	// 1E SBASS2 [verify]
		{0x1D,   0},	// 1F DGUITA [verify]
		{0x25,   0},	// 20 SBASS [verify]
	};
	UINT16 SongLen;
	const UINT8* SongData;
	
	struct
	{
		UINT8 Mode;
		UINT8 Pan;	// Stereo Bits
		UINT16 Ptr;
	} ChnList[0x10];
	
	UINT16 TickpQrt;
	UINT8 LoopCnt;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 SegBase;
	UINT16 SegIdx;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	UINT8 DrumMode;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopIdx;
	UINT8 LoopCount;
	UINT16 LoopSeg;
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 CurNote;
	UINT32 CurDly;
	UINT8 ChnVol;
	INT8 NoteMove;
	UINT8 TrkState;
	UINT8 PBRange;
	
	UINT8 CurNoteLen;
	UINT8 CurNoteDly;
	UINT8 NewNoteLen;
	UINT8 NewNoteDly;
	UINT8 PrevNoteLen;
	UINT8 PrevNoteDly;
	
	UINT8 MsgMask;
	UINT8 InitTempo;
	
	SongData = &ROMData[SongStartPos];
	SongLen = ReadLE16(&SongData[0x02]);
	TrkCnt = 0x09;
	TickpQrt = 48;
	TempSht = ReadLE16(&SongData[0x04]);
	if (TempSht <= 0x10)
		LoopCnt = 1;
	else
		LoopCnt = 2;
	
	MidLen = 0x20000;	// 128 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	if (SongData[0x01] != 'F')
	{
		printf("Invalid Song Mode %c!\n", SongData[0x01]);
		MidLen = 0;
		return 1;
	}
	
	DstPos = 0x00;
	WriteBE32(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBE16(&MidData[DstPos + 0x00], 0x0001);		// Format 1
	WriteBE16(&MidData[DstPos + 0x02], 1 + TrkCnt);
	WriteBE16(&MidData[DstPos + 0x04], TickpQrt);	// Ticks per Quarter: 48
	DstPos += 0x06;
	
	InPos = 0x00;
	
	WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
	DstPos += 0x08;
	TrkBase = DstPos;
	CurDly = 0x00;
	
	InPos += 0x06;	// skip main header
	
	TempByt = 0x10;	// Sequence Name Length
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x03, TempByt);
	memcpy(&MidData[DstPos], &SongData[InPos], TempByt);
	InPos += TempByt;	DstPos += TempByt;
	
	InitTempo = SongData[InPos];
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x51, 0x03);
	if (1)
	{
		TempLng = 60000000 / InitTempo;	// base guessed (seems to be a pretty good guess though)
	}
	else
	{
		// 1. normal formula with all steps
		//TimerBVal = 13000 / (InitTempo * 3.0f);	// Timer B value (without 0x100-b)
		//TempSng = (TempSng * 0x10) * (6 * 24) / 7670454;	// Period
		//TempLng = 1000000 * TickpQrt * TempSng;	// Microseconds per Quarter (MIDI value)
		
		// 2. slightly optimized formula
		//TempSng = 13000 * 0x10 * 48 * TickpQrt / (float)(7670454 * InitTempo);
		//TempLng = 1000000 * TempSng;
		
		// 3. optimized + rounding + no floating point
		TempLng = 7670454 * InitTempo;
		TempLng = (UINT32)( ((UINT64)1000000 * 9984000 * TickpQrt + TempLng / 2) / TempLng );
	}
	WriteBE32(&MidData[DstPos - 0x01], TempLng);
	MidData[DstPos - 0x01] = 0x03;	// write again, because the above instruction overwrote it
	DstPos += 0x03;
	
	LoopSeg = 0x01;
	InPos += 0x08;	// skip second header
	
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, InPos += 0x04)
	{
		ChnList[CurTrk].Mode = SongData[InPos + 0x00];
		ChnList[CurTrk].Pan = SongData[InPos + 0x01];
		memcpy(&ChnList[CurTrk].Ptr, &SongData[InPos + 0x02], 0x02);
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		SegBase = ChnList[CurTrk].Ptr;
		
		WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0x00;
		
		TrkEnd = (ChnList[CurTrk].Mode == 0x00);
		LoopIdx = 0x00;
		LoopCount = 0;
		MidChn = 1 + CurTrk;	// Channel 0 is reserved for SFX, and so track 8 ends up on the Drum channel
		DrumMode = (CurTrk == 8);
		ChnVol = 0x7F;
		TrkState = 0x00;
		NoteMove = 0x00;
		RunNoteCnt = 0x00;
		MsgMask = 0x00;
		SegIdx = 0x00;
		InPos = 0x0000;
		PBRange = 0x00;
		if (! TrkEnd || ChnList[CurTrk].Pan)
		{
			TempByt = PanBits2MidiPan(ChnList[CurTrk].Pan);
			WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, TempByt);
			if (DrumMode)	// Drum Channel - write Instrument Change
			{
				WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, 0x08, 0x00);
				WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x07, ChnVol);
			}
		}
		
		CurNoteDly = NewNoteDly = PrevNoteDly = 0x00;
		CurNoteLen = NewNoteLen = PrevNoteLen = 0x00;
		while(! TrkEnd)
		{
			if (InPos == 0x0000)
			{
				//if (SegIdx == 0xFFFF)
				//	break;
				memcpy(&InPos, &SongData[SegBase + SegIdx * 0x02], 0x02);
				if (InPos == 0xFFFF)
				{
					if (LoopSeg >= SegIdx)
						break;
					LoopCount ++;
					if (LoopCount >= LoopCnt)
					{
						if (LoopCnt > 1)
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x6F, LoopCount);
						break;
					}
					
					SegIdx = LoopSeg;
					InPos = 0x00;
					continue;
				}
				if (SegIdx == LoopSeg && LoopCnt > 1)
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x6F, LoopCount);
				SegIdx ++;
			}
			
			CurCmd = SongData[InPos];
			if (CurCmd < 0xEC || (CurCmd >= 0xF0 && CurCmd < 0xFC))
			{
				InPos ++;
				if (CurCmd == 0xDC)
				{
					// reuse PrevNoteDly/Len
					CurCmd = SongData[InPos];
					CurNoteDly = PrevNoteDly;
					CurNoteLen = PrevNoteLen;
					InPos ++;
				}
				else	// 00..7F, 80..DF
				{
					if (! (CurCmd & 0x80))
					{
						PrevNoteDly = NewNoteDly;
						PrevNoteLen = NewNoteLen;
						if (DrumMode)
						{
							WriteEvent(MidData, &DstPos, &CurDly, 0x7F, 0x00, 0x00);	// flush DAC notes
							// The DAC track is special.
							NewNoteDly = SongData[InPos];
							//NewNoteLen = NewNoteDly ? (NewNoteDly - 1) : 1;
							NewNoteLen = NewNoteDly;
							InPos ++;
						}
						else
						{
							NewNoteDly = SongData[InPos + 0x00];
							NewNoteLen = SongData[InPos + 0x01];
							InPos += 0x02;
						}
					}
					CurNoteDly = NewNoteDly;
					CurNoteLen = NewNoteLen;
				}
				if (CurTrk != 8)
				{
					// FM channels
					CurNote = CurCmd & 0x0F;
					CurNote += ((CurCmd & 0x70) >> 4) * 12;
					CurNote += NoteMove;
					if (! CurNoteLen)
						CurNote = 0x00;
				}
				else
				{
					// DAC channel
					CurNote = CurCmd & 0x7F;
					if (FixDrumSet && CurNote)
					{
						if (CurNote < (sizeof(DAC_MAP) / sizeof(UINT8)))
							CurNote = DAC_MAP[CurCmd];
						else
							CurNote = 0x00;
						if (! CurNote)
							printf("Ununsed drum %02X found!\n", CurCmd);
					}
					if (! CurNote)
						CurNote = (CurCmd & 0x7F) + NoteMove;
				}
				
				WriteEvent(MidData, &DstPos, &CurDly, 0x00, 0x00, 0x00);
				
				for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
				{
					if (RunNotes[TempByt].Note == CurNote)
					{
						RunNotes[TempByt].RemLen = (UINT16)CurDly + CurNoteLen;
						break;
					}
				}
				if (TempByt >= RunNoteCnt && CurNote > 0x00)
				{
					// handle Note Portamento
					if (RunNoteCnt)
					{
						if (! (TrkState & 0x01))
						{
							if (CurDly > 0)
							{
								TrkState |= 0x04;	// write the Portamento Controls one tick earlier
								CurDly --;
							}
							if (! (TrkState & 0x02))
							{
								WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x05, 0x18);
								TrkState |= 0x02;
							}
							WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);
							TrkState |= 0x01;
							if (TrkState & 0x04)
							{
								TrkState &= ~0x04;
								CurDly ++;
							}
						}
					}
					else
					{
						if (TrkState & 0x01)
						{
							WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x41, 0x00);
							TrkState &= ~0x01;
						}
					}
					
					WriteEvent(MidData, &DstPos, &CurDly, 0x90 | MidChn, CurNote, 0x7F);
					if (RunNoteCnt < MAX_RUN_NOTES)
					{
						RunNotes[RunNoteCnt].MidChn = MidChn;
						RunNotes[RunNoteCnt].Note = CurNote;
						RunNotes[RunNoteCnt].RemLen = CurNoteLen;
						RunNoteCnt ++;
					}
				}
				
				CurDly += CurNoteDly;
			}
			else
			{
				switch(CurCmd)
				{
				case 0xEC:	// Set Instrument
					CurCmd = SongData[InPos + 0x02];
					if (1)
					{
						if (CurCmd < (sizeof(INS_MAP) / sizeof(INS_TABLE)))
						{
							TempByt = INS_MAP[CurCmd].Ins;
							NoteMove = INS_MAP[CurCmd].Transp;
						}
						else
						{
							TempByt = 0xFF;
						}
						if (TempByt == 0xFF)
						{
							printf("Warning! Unmapped instrument %02X! (Chn %u)\n", CurCmd, 1 + MidChn);
							TempByt = CurCmd;
							NoteMove = 0;
						//	WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x20, 0x01);
						}
						//else
						//	WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x20, 0x00);
					}
					WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, TempByt, 0x00);
					CurDly += SongData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0xED:	// Set Volume
					if (FixVolume)
						TempByt = DB2Mid(Lin2DB(SongData[InPos + 0x02]) * 0.5);
					else
						TempByt = SongData[InPos + 0x02];
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
					InPos += 0x03;
					break;
				case 0xEE:	// Set Pan
					TempByt = PanBits2MidiPan(SongData[InPos + 0x02]);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, TempByt);
					CurDly += SongData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0xEF:	// Pitch Bend
					if (! PBRange)
					{
						// write Pitch Bend Range
						PBRange = 16;
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x65, 0x00);
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x64, 0x00);
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x06, PBRange);
					}
					// Note: 32 steps = 1 semitone
					TempSht = ReadLE16(&SongData[InPos + 0x02]);
					TempSht *= (8192 / PBRange / 32);
					TempSht += 0x2000;
					WriteEvent(MidData, &DstPos, &CurDly, 0xE0 | MidChn, TempSht & 0x7F, TempSht >> 7);
					CurDly += SongData[InPos + 0x01];
					InPos += 0x04;
					break;
				case 0xFD:	// Segment Return
					//InPos += 0x01;
					InPos = 0x0000;
					break;
				/*case 0xFF:
					TrkEnd = true;
					InPos += 0x01;
					break;*/
				default:
					printf("Unknown event %02X on track %X at %04X\n", SongData[InPos + 0x00], CurTrk, InPos);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					InPos += 0x01;
					TrkEnd = true;
					break;
				}
			}
		}
		for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
		{
			if (RunNotes[TempByt].RemLen > CurDly)
				CurDly = RunNotes[TempByt].RemLen;
		}
		WriteEvent(MidData, &DstPos, &CurDly, 0x7F, 0x00, 0x00);	// flush all notes
		
		WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
		
		WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);		// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
}

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
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
		if (Evt != 0x7F)
		{
			if (TempDly >= *Delay)
				break;	// not beyond the timeout - do the event
		}
		else
		{
			// 7F is the 'flush all' command
			if (TempDly > *Delay)
				break;
		}
		
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
				WriteMidiValue(Buffer, Pos, TempDly);
				TempDly = 0;
				
				MidData[*Pos + 0x00] = 0x90 | TempNote->MidChn;
				MidData[*Pos + 0x01] = TempNote->Note;
				MidData[*Pos + 0x02] = 0x00;
				*Pos += 0x03;
				
				RunNoteCnt --;
				if (RunNoteCnt)
					*TempNote = RunNotes[RunNoteCnt];
				CurNote --;
			}
		}
	}
	if (! (Evt & 0x80))
		return;
	
	WriteMidiValue(Buffer, Pos, *Delay);
	if (*Delay)
	{
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
			RunNotes[CurNote].RemLen -= (UINT16)*Delay;
		*Delay = 0x00;
	}
	
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
	//return log(LinVol / 127.0) / log(2.0) * 6.0;
	return log(LinVol / 127.0) * 8.65617024533378;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
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




UINT8 DecompressArcOdyssey(const UINT8* ComprData, UINT32* RetDataLen, UINT8** RetData)
{
	UINT32 CmpLen;
	const UINT8* CmpData;
	UINT32 DecLen;
	UINT8* DecData;
	
	CmpLen = ReadBE32(&ComprData[0x00]);
	DecLen = ReadBE32(&ComprData[0x04]);
	CmpData = &ComprData[0x08];
	DecData = (UINT8*)malloc(DecLen + 0x10);
	
	LZSS_Init();	// 01E7AE - Initialize Buffer Memory
	LZSS_Decode(CmpLen, CmpData, DecLen, DecData);	// 01E816 - Decompression
	
	*RetData = DecData;
	*RetDataLen = DecLen;
	return 0x00;
}

#define N		 4096	/* size of ring buffer */
#define F		   18	/* upper limit for match_length */
#define THRESHOLD	2   /* encode string into position and length
						   if match_length is greater than this */
UINT8	text_buf[N + F - 1];	/* ring buffer of size N,
			with extra F-1 bytes to facilitate string comparison */

static void LZSS_Init(void)	// ported from the Arcus Odyssey ROM
{
	UINT16 BufPos;
	UINT16 RegD0;
	UINT16 RegD1;
	
	// Important Note: These are non-standard values and ARE used by the compressed data.
	BufPos = 0x0000;
	// 01E7B2 - A000..ACFF
	for (RegD0 = 0x00; RegD0 < 0x100; RegD0 ++)
	{
		for (RegD1 = 0; RegD1 < 0x0D; RegD1 ++, BufPos ++)
			text_buf[BufPos] = (UINT8)RegD0;
	}
	// 01E7D2 - AD00..ADFF
	for (RegD0 = 0x00; RegD0 < 0x100; RegD0 ++, BufPos ++)
		text_buf[BufPos] = (UINT8)RegD0;
	// 01E7DE - AE00..AEFF
	do
	{
		text_buf[BufPos] = (UINT8)RegD0;
		RegD0 --; BufPos ++;
	} while(RegD0 > 0x00);
	// 01E7E8 - AF00..AF7F
	for (RegD0 = 0x00; RegD0 < 0x80; RegD0 ++, BufPos ++)
		text_buf[BufPos] = 0x00;
	// 01E800 - AF80..AFED
	for (RegD0 = 0x00; RegD0 < 0x80 - F; RegD0 ++, BufPos ++)
		text_buf[BufPos] = 0x20;
	//RegD7 = 0x80;	RegD6 = 0x00;	RegA4 = 0xB000;
	
	return;
}

void LZSS_Decode(UINT32 CmpLen, const UINT8* CmpData, UINT32 DecLen, UINT8* DecData)	// from LZSS.C by Haruhiko Okumura
{
	int  i, j, k, r;
	unsigned char c;
	unsigned int  flags;
	UINT32 CmpPos;
	UINT32 DecPos;
	
	//for (i = 0; i < N - F; i++) text_buf[i] = ' ';
	r = N - F;  flags = 0;
	CmpPos = 0;
	DecPos = 0;
	for ( ; ; ) {
		if (((flags >>= 1) & 256) == 0) {
			if (CmpPos >= CmpLen) break;
			c = CmpData[CmpPos ++];
			flags = c | 0xff00;		/* uses higher byte cleverly */
		}							/* to count eight */
		if (flags & 1) {
			if (CmpPos >= CmpLen) break;
			c = CmpData[CmpPos ++];
			if (DecPos >= DecLen) break;
			DecData[DecPos ++] = c;
			text_buf[r++] = c;  r &= (N - 1);
		} else {
			if (CmpPos >= CmpLen) break;
			i = CmpData[CmpPos ++];
			if (CmpPos >= CmpLen) break;
			j = CmpData[CmpPos ++];
			i |= ((j & 0xf0) << 4);  j = (j & 0x0f) + THRESHOLD;
			for (k = 0; k <= j; k++) {
				c = text_buf[(i + k) & (N - 1)];
				if (DecPos >= DecLen) break;
				DecData[DecPos ++] = c;
				text_buf[r++] = c;  r &= (N - 1);
			}
		}
	}
	return;
}


static UINT32 ScanForData(UINT32 DataLen, const UINT8* Data, UINT32 MagicLen,
						  const UINT8* MagicData, UINT32 StartPos)
{
	UINT32 CurPos;
	
	for (CurPos = StartPos; CurPos < DataLen; CurPos ++)
	{
		if (! memcmp(Data + CurPos, MagicData, MagicLen))
			return CurPos;
	}
	
	return (UINT32)-1;
}

static UINT32 ReadLEA(const UINT8* Code, UINT32 InstPos)
{
	UINT8 LeaMode;
	
	LeaMode = Code[InstPos + 0x01];
	InstPos += 0x02;
	
	if (LeaMode == 0xF8)		// LEA	Ax, addr.w
		return (INT16)ReadBE16(&Code[InstPos]) & 0xFFFFFF;
	else if (LeaMode == 0xF9)	// LEA	Ax, addr.l
		return ReadBE32(&Code[InstPos]) & 0xFFFFFF;
	else if (LeaMode == 0xFA)	// LEA	Ax, addr(PC)
		return InstPos + (INT16)ReadBE16(&Code[InstPos]);
	else if (LeaMode == 0xFB)	// LEA	Ax, addr(PC, Dx.w)
		return InstPos + (INT16)ReadBE16(&Code[InstPos]);
	
	return 0x00;
}

// Find Music Bank List by searching for:
//	26 00 6F 29 11 xx xx
// Find Z80 Driver:
//	41F8 xxxx 43F9 00A0 0000 (Arcus Odyssey - compressed)
//	41FA xxxx 4E71 43F9 00A0 0000 (Earnest Evans, El Viento - uncomrpessed)
void WolfTeamDriver_Autodetection(void)
{
	const UINT8 MAGIC_DRVLOAD[0x06] = {0x43, 0xF9, 0x00, 0xA0, 0x00, 0x00};
	const UINT8 MAGIC_NOP[0x02] = {0x4E, 0x71};
	const UINT8 MAGIC_MUSLIST[0x05] = {0x26, 0x00, 0x6F, 0x29, 0x11};
	UINT32 BasePos;
	UINT32 DrvInsPos;	// Instruction Offset for Z80 Driver
	UINT32 DrvPos;
	UINT16 Instr;
	
	BasePos = ScanForData(ROMLen, ROMData, 0x06, MAGIC_DRVLOAD, 0x00);
	if (BasePos == (UINT32)-1)
		goto NoDrvFoundError;
	
	DrvInsPos = BasePos;
	while(DrvInsPos > 0x00 && ! memcmp(ROMData + DrvInsPos - 0x02, MAGIC_NOP, 0x02))
		DrvInsPos -= 0x02;
	
	if ((ReadBE16(&ROMData[DrvInsPos - 0x04]) & ~0x0E03) == 0x41F8)
		DrvInsPos -= 0x04;
	else if ((ReadBE16(&ROMData[DrvInsPos - 0x06]) & ~0x0E03) == 0x41F8)
		DrvInsPos -= 0x06;
	else
		goto NoDrvFoundError;
	
	DrvPos = ReadLEA(ROMData, DrvInsPos);
	if (ReadBE32(&ROMData[DrvPos]) == 0xF3C30000)	// for El Viento
		DrvPos += 0x04;	// skip the null-driver, the real driver starts 4 bytes later (loaded by an earlier LEA)
	
	BasePos += 0x06;
	Instr = ReadBE16(&ROMData[BasePos]);
	if ((Instr & ~0x0E00) == 0x303C)	// MOVE.W len, Dx (Earnest Evans/El Viento - load driver length)
	{
		Z80DrvMode = RAMMODE_PTR;
		Z80DrvLen = ReadBE16(&ROMData[BasePos + 0x02]) + 1;	// DBF loops execute once more
		Z80DrvData = ROMData + DrvPos;
		printf("Uncompressed driver found at %06X (size: %04X)\n", DrvPos, Z80DrvLen);
	}
	else if (Instr == 0x4EB9)	// JSR ... (Arcus Odyssey - call decompression routine)
	{
		Z80DrvMode = RAMMODE_PTR;
		Z80DrvLen = ReadBE32(&ROMData[DrvPos]);	// DBF loops execute once more
		printf("Compressed driver found at %06X (size: %04X)\n", DrvPos, Z80DrvLen);
		DecompressArcOdyssey(ROMData + DrvPos, &Z80DrvLen, &Z80DrvData);
		printf("Decompressed size: %04X\n", Z80DrvLen);
	}
	else
	{
		Z80DrvMode = RAMMODE_PTR;
		Z80DrvLen = 0x2000;
		Z80DrvData = ROMData + DrvPos;
		printf("Uncompressed driver found at %06X\n", DrvPos);
	}
	
	BasePos = ScanForData(Z80DrvLen, Z80DrvData, 0x05, MAGIC_MUSLIST, 0x00);
	if (BasePos == (UINT32)-1)
	{
		printf("Failed to find Music List!\n");
		Z80MusList = 0x0000;
		return;
	}
	BasePos += 0x05;
	
	Z80MusList = ReadLE16(&Z80DrvData[BasePos]);
	printf("Music List Offset: %04X\n", Z80MusList);
	
	return;

NoDrvFoundError:
	printf("Z80 Driver not found!\n");
	
	return;
}
