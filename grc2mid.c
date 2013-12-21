// GRC -> Midi Converter
// ---------------------
// Written by Valley Bell, 8 December 2013
// Improved on 20 December 2013

// TODO:
//		- make loop extention optional
//		- make Chorus thing optional

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef unsigned char	bool;
typedef signed char		INT8;
typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef signed short	INT16;
typedef unsigned long	UINT32;

#define false	0x00
#define true	0x01

#define SHOW_DEBUG_MESSAGES


typedef struct _track_info
{
	UINT16 StartPos;
	UINT16 LoopPos;
	UINT32 TickCnt;
	UINT32 LoopTick;
	UINT8 Flags;
	UINT8 LoopTimes;
	UINT8 MaxVol;
	bool VolBoost;
} TRK_INFO;


//bool LetterInArgument(char* Arg, char Letter);
UINT8 LoadInsData(const char* FileName);

UINT8 GRC2Mid(UINT32 GrcLen, UINT8* GrcData, UINT16 GrcAddr/*, UINT32* OutLen, UINT8** OutData*/);
static void PreparseGrc(UINT32 GrcLen, const UINT8* GrcData, UINT8* GrcBuf, TRK_INFO* TrkInf, UINT8 Mode);
static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf);
static UINT16 ReadLittleEndianS(const UINT8* Buffer);
static void WriteBigEndianL(UINT8* Buffer, UINT32 Value);
static void WriteBigEndianS(UINT8* Buffer, UINT16 Value);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static float OPN2DB(UINT8 TL, UINT8 PanMode, bool VolBoost);
static UINT8 DB2Mid(float DB);
static void CopySMPSModData(const UINT8* RawData, UINT8* MidValData);

UINT8 LoadDACData(const char* FileName);
void SaveDACData(const char* FileBase);
void SaveInsAsGYB(const char* FileBase);


static const UINT8 VOL_TABLE[0x10] =
{	0x7F, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x18,
	0x16, 0x14, 0x12, 0x10, 0x0E, 0x0C, 0x0B, 0x00};
static const UINT8 NOTE_SCALE[0x10] =
{	0, 2, 4, 5, 7,  9, 11, 0xFF,
	1, 3, 4, 6, 8, 10, 11, 0xFF};

// Modulation -> SMPS Modulation conversion table
static const UINT8 MOD_DATA[0x22][4] =
{
	{0x00, 0x00, 0x00, 0x00},	// 00
	{0x08, 0x01, 0x05, 0x05},	// 01
	{0x08, 0x01, 0x04, 0x07},	// 02
	{0x08, 0x01, 0x03, 0x07},	// 03
	{0x00, 0x01, 0x0A, 0x07},	// 04
	{0x0C, 0x01, 0x10, 0x05},	// 05
	{0x0C, 0x01, 0x20, 0x05},	// 06
	{0x00, 0x01, 0xEC, 0xFF},	// 07
	{0x00, 0x01, 0xE2, 0xFF},	// 08
	{0x00, 0x01, 0xD8, 0xFF},	// 09
	{0x00, 0x01, 0xCE, 0xFF},	// 0A
	{0x00, 0x01, 0x34, 0xFF},	// 0B
	{0x00, 0x01, 0xD8, 0xFF},	// 0C
	{0x06, 0x01, 0x08, 0x03},	// 0D
	{0x00, 0x01, 0xFE, 0xFF},	// 0E
	{0x0C, 0x01, 0x06, 0x06},	// 0F
	{0x00, 0x01, 0x1A, 0xFF},	// 10
	{0x05, 0x02, 0x01, 0x05},	// 11
	{0x05, 0x02, 0xFF, 0x05},	// 12
	{0x06, 0x01, 0x03, 0x03},	// 13
	{0x00, 0x01, 0x3C, 0xFF},	// 14
	{0x00, 0x01, 0x46, 0xFF},	// 15
	{0x00, 0x01, 0x50, 0xFF},	// 16
	{0x00, 0x01, 0x64, 0xFF},	// 17
	{0x00, 0x01, 0x96, 0xFF},	// 18
	{0x0C, 0x01, 0x02, 0xFF},	// 19
	{0x00, 0x01, 0xF8, 0xFF},	// 1A
	{0x00, 0x01, 0x0A, 0x06},	// 1B
	{0x00, 0x01, 0x07, 0x09},	// 1C
	{0x0C, 0x01, 0x06, 0x06},	// 1D
	{0x00, 0x01, 0xFF, 0xFF},	// 1E
	{0x00, 0x01, 0xFE, 0xFF},	// 1F
	{0x00, 0x01, 0x05, 0xFF},	// 20
	{0x00, 0x01, 0x07, 0xFF},	// 21
};


UINT32 MidLen;
UINT8* MidData;
UINT16 TickpQrtr;
UINT8 DefLoopCount;
bool OptVolWrites;
bool NoLoopExt;

int main(int argc, char* argv[])
{
	FILE* hFile;
	//UINT8 PLMode;
	UINT32 SongPos;
	char OutFileBase[0x100];
	char OutFile[0x100];
	char* TempPnt;
	int RetVal;
	int argbase;
	
	UINT32 InLen;
	UINT8* InData;
	UINT32 OutLen;
	//UINT8* OutData;
	
	UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 TempLng;
	UINT16 TempSht;
	
	printf("GRC -> Midi Converter\n---------------------\n");
	if (argc < 2)
	{
		printf("Usage: grc2mid.exe [-Options] ROM.bin MusicListAddr(hex) [Song Count]\n");
		printf("Options:\n");
		printf("    -OptVol     Optimize Volume writes (omits redundant ones)\n");
		printf("    -TpQ n      Sets the number of Ticks per Quarter to n. (default: 24)\n");
		printf("                Use values like 18 or 32 on songs with broken tempo.\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extention\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		return 0;
	}
	
	OptVolWrites = true;
	TickpQrtr = 24;
	DefLoopCount = 0x02;
	NoLoopExt = false;
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! _stricmp(argv[argbase] + 1, "OptVol"))
			OptVolWrites = true;
		else if (! _stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				TickpQrtr = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! TickpQrtr)
					TickpQrtr = 24;
			}
		}
		else if (! _stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				DefLoopCount = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! TickpQrtr)
					DefLoopCount = 2;
			}
		}
		else if (! _stricmp(argv[argbase] + 1, "NoLpExt"))
			NoLoopExt = true;
		else
			break;
		argbase ++;
	}
	
	//if (argc <= argbase)
	if (argc <= argbase + 1)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	strcpy(OutFileBase, argv[argbase + 0]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;
	
	SongPos = strtoul(argv[argbase + 1], NULL, 0x10);
	
	if (argc > argbase + 2)
		FileCount = (UINT16)strtoul(argv[argbase + 2], NULL, 0);
	else
		FileCount = 0x00;

	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	InLen = ftell(hFile);
	if (InLen > 0x800000)	// 8 MB
		InLen = 0x800000;
	
	fseek(hFile, 0x00, SEEK_SET);
	InData = (UINT8*)malloc(InLen);
	fread(InData, 0x01, InLen, hFile);
	
	fclose(hFile);
	
	if (! FileCount)
	{
		// Song Count autodetection
		CurFile = 0x00;
		CurPos = SongPos;
		OutLen = SongPos + ReadLittleEndianS(&InData[SongPos]);
		while(CurPos < OutLen)
		{
			TempLng = SongPos + ReadLittleEndianS(&InData[CurPos]);
			if (TempLng < OutLen)
				OutLen = TempLng;
			
			CurPos += 0x02;
			CurFile ++;
		}
		FileCount = CurFile;
		printf("Songs detected: 0x%02X (%u)\n", FileCount, FileCount);
	}
	
	CurPos = SongPos;
	for (CurFile = 0x00; CurFile < FileCount; CurFile ++)
	{
		printf("File %u / %u ...", CurFile + 1, FileCount);
		TempSht = ReadLittleEndianS(&InData[CurPos]);
		CurPos += 0x02;
		RetVal = GRC2Mid(InLen - SongPos, InData + SongPos, TempSht/*, &OutLen, &OutData*/);
		if (RetVal)
		{
			if (RetVal == 0x01)
			{
				printf(" empty - ignored.\n");
				continue;
			}
			
			return RetVal;
		}
		
		sprintf(OutFile, "%s_%02X.mid", OutFileBase, CurFile);
		
		hFile = fopen(OutFile, "wb");
		if (hFile == NULL)
		{
			free(MidData);	MidData = NULL;
			printf("Error opening file!\n");
			continue;
		}
		fwrite(MidData, MidLen, 0x01, hFile);
		
		fclose(hFile);
		free(MidData);	MidData = NULL;
		printf("\n");
	}
	printf("Done.\n");
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

/*bool LetterInArgument(char* Arg, char Letter)
{
	Letter = toupper(Letter);
	
	while(*Arg != '\0')
	{
		if (toupper(*Arg) == Letter)
			return true;
		
		Arg ++;
	}
	
	return false;
}*/


UINT8 GRC2Mid(UINT32 GrcLen, UINT8* GrcData, UINT16 GrcAddr/*, UINT32* OutLen, UINT8** OutData*/)
{
	UINT8* TempBuf;
	TRK_INFO TrkInf[0x09];
	TRK_INFO* TempTInf;
	UINT8 CurTrk;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 StackPos;
	UINT16 StackAddr[0x10];
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT32 CurDly;
	UINT8 ChnVol;
	UINT8 MidChnVol;
	UINT8 PanReg;
	UINT8 DefNoteLen;
	UINT8 CurOctave;
	UINT8 LastNote;
	UINT8 CurNote;
	UINT8 PanMode;
	UINT8 HoldNote;
	UINT8 LoopCnt;
	UINT8 LastModType;
	UINT8 ModDataMem[5];
	
	MidLen = 0x20000;	// 128 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBigEndianL(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBigEndianS(&MidData[DstPos + 0x00], 0x0001);		// Format 1
	WriteBigEndianS(&MidData[DstPos + 0x02], 0x0007);		// Tracks: TrkCnt
	WriteBigEndianS(&MidData[DstPos + 0x04], TickpQrtr);	// Ticks per Quarter: 24
	DstPos += 0x06;
	
	// write Master Track
	WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
	DstPos += 0x08;
	
	TrkBase = DstPos;
	CurDly = 0;
	
	// Note: Timing is 1 tick = 1 frame (60 Hz)
	// BPM = 3600 Ticks/min / 24 Ticks/Quarter
	// 3600 / 24 = 150 BPM
	// 150 BPM == MIDI Tempo 400 000
	//TempLng = 400000;
	TempLng = 50000 * TickpQrtr / 3;	// 1 000 000 * Tick/Qrtr / 60
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x51, 0x00);
	WriteBigEndianL(&MidData[DstPos - 0x01], TempLng);
	MidData[DstPos - 0x01] = 0x03;
	DstPos += 0x03;
	
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
	
	WriteBigEndianL(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	
	// Read Header
	TempBuf = (UINT8*)malloc(GrcLen);
	InPos = GrcAddr;
	for (CurTrk = 0x00; CurTrk < 0x09; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		TempTInf->Flags = GrcData[InPos + 0x00];
		TempTInf->StartPos = ReadLittleEndianS(&GrcData[InPos + 0x01]);
		TempTInf->TickCnt = 0x00;
		TempTInf->LoopTimes = DefLoopCount;
		TempTInf->LoopPos = 0x0000;
		TempTInf->LoopTick = 0x00;
		TempTInf->MaxVol = 0x7F;
		TempTInf->VolBoost = false;
		
		PreparseGrc(GrcLen, GrcData, TempBuf, TempTInf, 0x00);
		if (TempTInf->MaxVol < 0x08)
			TempTInf->VolBoost = true;
		
		// If there is a loop, parse a second time to get the Loop Tick.
		if (TempTInf->LoopPos)
			PreparseGrc(GrcLen, GrcData, TempBuf, TempTInf, 0x01);
		
		InPos += 0x03;
	}
	free(TempBuf);	TempBuf = NULL;
	
	if (! NoLoopExt)
		GuessLoopTimes(0x09, TrkInf);
	
	// --- Main Conversion ---
	for (CurTrk = 0x00; CurTrk < 0x06; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		
		WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0;
		
		if (TempTInf->Flags & 0x80)
			TrkEnd = false;
		else
			TrkEnd = true;
		InPos = TempTInf->StartPos;
		
		MidChn = CurTrk;
		//MidChn = (CurTrk == 0x05) ? 0x09 : CurTrk;
		ChnVol = 0x7F;
		PanReg = 0x00;
		DefNoteLen = 0x00;
		CurOctave = 0;
		StackPos = 0x00;
		HoldNote = 0x00;
		
		LastNote = 0xFF;
		PanMode = 0x00;
		MidChnVol = 0xFF;
		LoopCnt = 0xFF;
		LastModType = 0xFF;
		CopySMPSModData(MOD_DATA[0], ModDataMem);
		
		WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x21, 0x01);
		MidData[DstPos] = 0x04;	DstPos ++;
		if (TempTInf->VolBoost)
			WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | CurTrk, 93, 0x08);
		
		while(! TrkEnd && InPos < GrcLen)
		{
			if (LoopCnt == 0xFF && InPos == TempTInf->LoopPos)
			{
				LoopCnt ++;
				WriteEvent(MidData, &DstPos, &CurDly,
							0xB0 | MidChn, 0x6F, LoopCnt);
				MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
				LastModType = 0xFF;
			}
			
			CurCmd = GrcData[InPos];
			if (! (CurCmd & 0x80))
			{
				//	Bits 0-3 (0F): Note Value (07/0F = rest)
				//	Bit   4  (10): use custom delay
				//	Bits 5-6 (60): Stereo Mask
				//
				//	Note 0 is a B. (YM2612 FNum 0x26A)
				TempByt = NOTE_SCALE[CurCmd & 0x0F];
				if (TempByt == 0xFF)
					CurNote = 0xFF;
				else
					CurNote = CurOctave * 12 + TempByt - 1;
				
				if (HoldNote && LastNote != CurNote)
				{
					if (CurNote == 0xFF)
					{
						printf("Warning: Ignoring command 0xFE!\n");
						HoldNote = 0x00;
					}
					else
					{
						//printf("Warning: Note Portamento!\n");
						HoldNote = 0x02;
					}
				}
				if (LastNote != 0xFF && ! HoldNote)
				{
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, LastNote, 0x00);
				}
				
				TempByt = NOTE_SCALE[CurCmd & 0x0F];
				if (CurNote != 0xFF)
				{
					// Pan has only an effect if a Note is played.
					TempByt = (CurCmd & 0x60) << 1;
					if (TempByt != PanReg)
					{
						// write Pan
						PanReg = TempByt;
						switch(PanReg & 0xC0)
						{
						case 0x40:	// Left Channel
							TempByt = 0x00;
							break;
						case 0x80:	// Right Channel
							TempByt = 0x7F;
							break;
						case 0x00:	// No Channel
						case 0xC0:	// Both Channels
							TempByt = 0x40;
							break;
						}
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x0A, TempByt);
						
						TempByt = ! (TempByt == 0x40);
						if (TempByt != PanMode)
						{
							PanMode = TempByt;
							TempByt = DB2Mid(OPN2DB(ChnVol, PanMode, TempTInf->VolBoost));
							if (! OptVolWrites || TempByt != MidChnVol)
							{
								MidChnVol = TempByt;
								WriteEvent(MidData, &DstPos, &CurDly,
											0xB0 | MidChn, 0x07, MidChnVol);
							}
						}
					}
				}
				
				if (CurNote != 0xFF)
				{
					if (HoldNote == 0x00)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, CurNote, 0x7F);
					}
					else if (HoldNote == 0x02)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x41, 0x7F);	// Portamento On
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, LastNote, 0x00);
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, CurNote, 0x7F);
					}
				}
				
				if (! (CurCmd & 0x10))
				{
					CurDly += DefNoteLen;
				}
				else
				{
					InPos ++;
					CurDly += GrcData[InPos];
				}
				InPos ++;
				
				if (HoldNote == 0x02)
				{
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x41, 0x00);	// Portamento Off
				}
				LastNote = CurNote;
				HoldNote = false;
			}
			else if ((CurCmd & 0xE0) == 0x80)
			{
				if ((CurCmd & 0xF0) == 0x80)
				{
					// set Volume from Lookup Table
					ChnVol = VOL_TABLE[CurCmd & 0x0F];
					TempByt = DB2Mid(OPN2DB(ChnVol, PanMode, TempTInf->VolBoost));
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x07, MidChnVol);
					}
				}
				else //if ((CurCmd & 0xF0) == 0x90)
				{
					CurOctave = CurCmd & 0x0F;
				}
				InPos ++;
			}
			else
			{
				InPos ++;
				switch(CurCmd)
				{
				case 0xEF:	// set Detune
					TempSht = 0x2000 + (INT8)GrcData[InPos] * 64;
					InPos ++;
					
					WriteEvent(MidData, &DstPos, &CurDly,
								0xE0 | MidChn, TempSht & 0x7F, (TempSht >> 7) & 0x7F);
					break;
				case 0xF0:	// reset SFX ID
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x70);
					break;
				case 0xF1:	// set Volume
					ChnVol = GrcData[InPos] & 0x7F;
					TempByt = DB2Mid(OPN2DB(ChnVol, PanMode, TempTInf->VolBoost));
					InPos ++;
					
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x07, MidChnVol);
					}
					break;
				case 0xF2:	// Enable/Disable DAC
					TempByt = GrcData[InPos];
					InPos ++;
					if (TempByt)
					{
						// enable DAC
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x00, 0x7F);
					}
					else
					{
						// disable DAC
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x00, 0x00);
					}
					break;
				case 0xF3:	// set Fade Speed
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x73);
					InPos ++;
					break;
				case 0xF4:	// synchronize all tracks
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x74);
					break;
				case 0xF5:	// set YM2612 Timer B
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x75);
					InPos ++;
					break;
				case 0xF6:	// set AMS/FMS
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x76);
					InPos ++;
					break;
				case 0xF7:	// set LFO rate
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x77);
					InPos ++;
					break;
				case 0xF8:	// Return from GoSub
					if (! StackPos)
					{
						printf("Error: Return without GoSub! (Pos 0x%04X)\n", InPos - 0x01);
						TrkEnd = true;
						break;
					}
					
					StackPos --;
					InPos = StackAddr[StackPos];
					break;
				case 0xF9:	// GoSub
					TempSht = ReadLittleEndianS(&GrcData[InPos]);
					InPos --;
					
					StackAddr[StackPos] = InPos + 0x03;
					StackPos ++;
					
					InPos += TempSht;
					break;
				case 0xFA:	// GoTo
					TempSht = ReadLittleEndianS(&GrcData[InPos]);
					InPos --;
					
					InPos += TempSht;
					if (InPos >= GrcLen)
						*((char*)NULL) = 'x';
					
					if (LoopCnt == 0xFF)
						LoopCnt = 0x00;
					LoopCnt ++;
					if (LoopCnt < 0x80)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, LoopCnt);
					
					if (LoopCnt >= TempTInf->LoopTimes)
						TrkEnd = true;
					
					MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
					LastModType = 0xFF;
					break;
				case 0xFB:	// Set Modulation
					TempByt = GrcData[InPos];
					InPos ++;
					
					if (TempByt & 0x80)
						printf("Warning: Modulation Type %02X used!\n", TempByt);
					if (TempByt)
					{
						if (LastModType != TempByt)
						{
							CopySMPSModData(MOD_DATA[TempByt], ModDataMem);
							LastModType = TempByt;
							
							for (TempByt = 0x00; TempByt < 0x04; TempByt ++)
								WriteEvent(MidData, &DstPos, &CurDly,
											0xB0 | MidChn, 0x10 | TempByt, ModDataMem[TempByt]);
							
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | MidChn, 0x21, LastModType);
						}
						
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x01, ModDataMem[0x04]);
					}
					else
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x01, TempByt);
					}
					break;
				case 0xFC:	// set Instrument
					TempByt = GrcData[InPos];
					InPos ++;
					
					WriteEvent(MidData, &DstPos, &CurDly,
								0xC0 | MidChn, TempByt, 0x00);
					break;
				case 0xFD:	// set Default Note Length
					DefNoteLen = GrcData[InPos];
					InPos ++;
					break;
				case 0xFE:	// Hold Note ("no attack" mode)
					HoldNote = true;
					break;
				case 0xFF:	// Track End
					TrkEnd = true;
					break;
				default:
					printf("Unknown event %02X on track %X\n", CurCmd, CurTrk);
					TrkEnd = true;
					break;
				}
			}
		}
		if (LastNote != 0xFF)
			WriteEvent(MidData, &DstPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
		
		WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
		
		WriteBigEndianL(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
}

static void PreparseGrc(UINT32 GrcLen, const UINT8* GrcData, UINT8* GrcBuf, TRK_INFO* TrkInf, UINT8 Mode)
{
	// Note: GrcBuf is a temporary buffer with a size of GrcLen bytes.
	//       It is used to find loops by marking processed bytes.
	//       A loop is found when a GoTo jumps to an already processed byte.
	//
	//       The buffer has to be allocated by the calling function to speed the program
	//       up by saving a few mallocs.
	
	UINT16 InPos;
	UINT8 CurCmd;
	UINT8 StackPos;
	UINT16 StackAddr[0x10];
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 DefNoteLen;
	
	if (! Mode)
	{
		TrkInf->LoopPos = 0x0000;
		TrkInf->MaxVol = 0x7F;
	}
	if (! (TrkInf->Flags & 0x80))
		return;	// Track inactive - return
	
	if (! Mode)
		memset(GrcBuf, 0x00, GrcLen);
	InPos = TrkInf->StartPos;
	StackPos = 0x00;
	DefNoteLen = 0x00;
	
	while(InPos < GrcLen)
	{
		if (Mode && InPos == TrkInf->LoopPos)
			return;
		
		CurCmd = GrcData[InPos];
		GrcBuf[InPos] = 0x01;
		InPos ++;
		if (! (CurCmd & 0x80))
		{
			if (! (CurCmd & 0x10))
			{
				TempByt = DefNoteLen;
			}
			else
			{
				TempByt = GrcData[InPos];
				GrcBuf[InPos] = 0x01;
				InPos ++;
			}
			if (! Mode)
				TrkInf->TickCnt += TempByt;
			else
				TrkInf->LoopTick += TempByt;
		}
		else if ((CurCmd & 0xE0) == 0x80)
		{
			if ((CurCmd & 0xF0) == 0x80)
			{
				TempByt = VOL_TABLE[CurCmd & 0x0F];
				if (TrkInf->MaxVol > TempByt)
					TrkInf->MaxVol = TempByt;
			}
		}
		else
		{
			switch(CurCmd)
			{
			case 0xEF:	// set Detune
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xF0:	// reset SFX ID
				break;
			case 0xF1:	// set Volume
				TempByt = GrcData[InPos] & 0x7F;
				if (TrkInf->MaxVol > TempByt)
					TrkInf->MaxVol = TempByt;
				
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xF2:	// Enable/Disable DAC
				break;
			case 0xF3:	// set Fade Speed
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xF4:	// synchronize all tracks
				break;
			case 0xF5:	// set YM2612 Timer B
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xF6:	// set AMS/FMS
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xF7:	// set LFO rate
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xF8:	// Return from GoSub
				if (! StackPos)
					return;
				
				StackPos --;
				InPos = StackAddr[StackPos];
				break;
			case 0xF9:	// GoSub
				TempSht = ReadLittleEndianS(&GrcData[InPos]);
				InPos --;
				
				StackAddr[StackPos] = InPos + 0x03;
				StackPos ++;
				
				InPos += TempSht;
				break;
			case 0xFA:	// GoTo
				TempSht = ReadLittleEndianS(&GrcData[InPos]);
				InPos --;
				
				InPos += TempSht;
				if (InPos >= GrcLen)
					return;
				
				if (GrcBuf[InPos])
				{
					TrkInf->LoopPos = InPos;
					return;
				}
				break;
			case 0xFB:	// Set Modulation
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xFC:	// set Instrument
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xFD:	// set Default Note Length
				DefNoteLen = GrcData[InPos];
				GrcBuf[InPos] = 0x01;
				InPos ++;
				break;
			case 0xFE:	// Hold Note ("no attack" mode)
				break;
			case 0xFF:	// Track End
				return;
			default:
				return;
			}
		}
	}
	
	return;
}

static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf)
{
	UINT8 CurTrk;
	TRK_INFO* TempTInf;
	UINT32 TrkLen;
	UINT32 TrkLoopLen;
	UINT32 MaxTrkLen;
	
	MaxTrkLen = 0x00;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		if (TempTInf->LoopPos)
			TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		else
			TrkLoopLen = 0x00;
		
		TrkLen = TempTInf->TickCnt + TrkLoopLen * (TempTInf->LoopTimes - 1);
		if (MaxTrkLen < TrkLen)
			MaxTrkLen = TrkLen;
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		if (TempTInf->LoopPos)
			TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		else
			TrkLoopLen = 0x00;
		if (TrkLoopLen < 0x20)
			continue;
		
		TrkLen = TempTInf->TickCnt + TrkLoopLen * (TempTInf->LoopTimes - 1);
		if (TrkLen * 5 / 4 < MaxTrkLen)
		{
			// TrkLen = desired length of the loop
			TrkLen = MaxTrkLen - TempTInf->LoopTick;
			
			TempTInf->LoopTimes = (TrkLen + TrkLoopLen / 3) / TrkLoopLen;
			printf("Trk %u: Extended loop to %u times\n", CurTrk, TempTInf->LoopTimes);
		}
	}
	
	return;
}

/*static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf)
{
	UINT8 CurTrk;
	TRK_INFO* TempTInf;
	UINT32 TrkLoopLen;
	UINT32 MaxLoopLen;
	
	MaxLoopLen = 0x00;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		
		TrkLoopLen *= TempTInf->LoopTimes;
		if (MaxLoopLen < TrkLoopLen)
			MaxLoopLen = TrkLoopLen;
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		if (TrkLoopLen < 0x20)
			continue;
		
		if (TrkLoopLen * TempTInf->LoopTimes * 5 / 4 < MaxLoopLen)
		{
			TempTInf->LoopTimes = (MaxLoopLen + TrkLoopLen / 4) / TrkLoopLen;
		}
	}
	
	return;
}*/

static UINT16 ReadLittleEndianS(const UINT8* Buffer)
{
	return	(Buffer[0x01] << 8) |
			(Buffer[0x00] << 0);
}

static void WriteBigEndianL(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value & 0xFF000000) >> 24;
	Buffer[0x01] = (Value & 0x00FF0000) >> 16;
	Buffer[0x02] = (Value & 0x0000FF00) >>  8;
	Buffer[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}

static void WriteBigEndianS(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value & 0xFF00) >> 8;
	Buffer[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0;
	
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		Buffer[*Pos + 0x00] = Evt;
		Buffer[*Pos + 0x01] = Val1;
		Buffer[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		Buffer[*Pos + 0x00] = Evt;
		Buffer[*Pos + 0x01] = Val1;
		*Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		Buffer[*Pos + 0x00] = Evt;
		Buffer[*Pos + 0x01] = Val1;
		Buffer[*Pos + 0x02] = Val2;
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

static float OPN2DB(UINT8 TL, UINT8 PanMode, bool VolBoost)
{
	if (PanMode)
		TL += 0x04;
	if (! VolBoost)
	{
		if (TL >= 8)
			TL -= 8;
		else
			TL = 0;
	}
	return -(TL * 4 / 3.0f);
}

static UINT8 DB2Mid(float DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

static void CopySMPSModData(const UINT8* RawData, UINT8* MidVals)
{
	INT16 ModDelta;
	
	MidVals[0x00] = RawData[0x00];
	MidVals[0x01] = RawData[0x01];
	MidVals[0x02] = RawData[0x02];
	MidVals[0x03] = RawData[0x03];
	
	if (MidVals[0x02] >= 0x40 && MidVals[0x02] < 0xC0)
		MidVals[0x01] |= 0x20;
	MidVals[0x02] &= 0x7F;
	
	if (MidVals[0x03] >= 0x40 && MidVals[0x03] < 0xC0)
		MidVals[0x01] |= 0x40;
	MidVals[0x03] &= 0x7F;
	
	ModDelta = (INT8)RawData[0x02] * RawData[0x03];
	if (ModDelta < 0)
		ModDelta = -ModDelta;
	
	ModDelta *= 2;
	if (ModDelta < 0x08)
		MidVals[0x04] = 0x08;
	else if (ModDelta > 0x7F)
		MidVals[0x04] = 0x7F;
	else
		MidVals[0x04] = (UINT8)ModDelta;
	
	return;
}
