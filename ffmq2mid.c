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
#define WRITE_DEBUG_CTRLS
#define USE_VOL_CTRLS


//bool LetterInArgument(char* Arg, char Letter);

UINT8 FFMQ2Mid(void);
static void WriteBigEndianL(UINT8* Buffer, UINT32 Value);
static void WriteBigEndianS(UINT8* Buffer, UINT16 Value);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static double Lin2DB(UINT8 LinVol);
static UINT8 DB2Mid(double DB);


//static bool OldDriver;
//static bool InsLimit;

#ifdef WRITE_DEBUG_CTRLS
#define WriteDgbEvt	WriteEvent
#else
#define WriteDgbEvt
#endif

UINT32 SpcLen;
UINT8* SpcData;
UINT32 MidLen;
UINT8* MidData;
UINT8 FileVer = 0x00;

int main(int argc, char* argv[])
{
	FILE* hFile;
	/*UINT8 PLMode;
	UINT32 SongPos;
	char OutFileBase[0x100];
	char OutFile[0x100];*/
	char TempArr[0x08];
	//char* TempPnt;
	int RetVal;
	
	/*UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 TempLng;*/
	
	printf("FFMQ SPC -> Midi Converter\n--------------------------\n");
	if (argc < 2)
	{
		printf("Usage: ffmq2mid.exe Song.spc Song.mid\n");
		/*printf("Usage: de2mid.exe ROM.bin Options Address(hex) [Song Count]\n");
		printf("\n");
		printf("The 'Options' argument is a collection of one or more of the following letters.\n");
		printf("They mustn't be seperated by space or tab.\n");
		printf("\n");
		printf("Modes:\ts - single file (Address is pointer to song data)\n");
		printf("\tl - music list (Address is pointer to music list)\n");
		printf("e - early driver (as used in Side Pocket)\n");
		printf("i - limit MIDI instrument numbers to 0..15 (may sound better)\n");
		printf("\n");
		printf("Song Count is an optional argument for Music List mode.\n");
		printf("Use it if the song autodetection fails.\n");*/
		return 0;
	}
	
	/*TempLng = strlen(argv[2]);
	PLMode = LetterInArgument(argv[2], 'L') && ! LetterInArgument(argv[2], 'S');
	OldDriver = LetterInArgument(argv[2], 'E');
	InsLimit = LetterInArgument(argv[2], 'I');*/
	
/*	strcpy(OutFileBase, argv[1]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;*/
	
	hFile = fopen(argv[1], "rb");
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
	
	RetVal = FFMQ2Mid();
	if (RetVal)
		return 3;
	
	hFile = fopen(argv[2], "wb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fwrite(MidData, 0x01, MidLen, hFile);
	
	fclose(hFile);
	
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


UINT8 FFMQ2Mid(void)
{
	const UINT32 DELAY_TABLE[0x0F] =
	{//	  0     1     2     3     4     5     6     7     8     9    10    11    12    13    14
	//	 1/1   3/2   1/2   1/3   3/8   1/4   1/6  3/16   1/8  1/12  1/16  1/24  1/32  1/48  1/64
		0xC0, 0x90, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0C, 0x08, 0x06, 0x04, 0x03};
	const UINT32 DELAY_TABLE_L[0x0E] =
	{//	  0           1     2     3     4     5     6     7     8     9    10    11    12    13
	//	 1/1         1/2   1/3   3/8   1/4   1/6  3/16   1/8  1/12  1/16  1/24  1/32  1/48  1/64
		0xC0,       0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0C, 0x08, 0x06, 0x04, 0x03};
	UINT16 BasePtr;
	UINT16 ChnPtrList[0x08];
	UINT8 FILE_FMT;
	UINT8 CMD_BASE;
	UINT8 CurTrk;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopID;
	UINT8 LoopCount[0x10];
	UINT16 LoopAddr[0x10];
	UINT8 LoopCur[0x10];
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 CurOct;
	UINT8 CurNote;
	UINT8 LastNote;
	UINT8 LastChn;
	UINT32 CurDly;
	UINT8 NoteVol;
	UINT8 ChnVol;
	INT8 NoteMove;
//	UINT8 PitchToNote;
//	INT16 PitchRange;
//	UINT8 PbStpCnt;
//	UINT8 LastPbRPN;
//	UINT8 NoAtk;
	UINT8 DrmNote;
	UINT8 PortamntOn;
	INT8 PortamntNote;
	
	UINT8 MsgMask;
	
	MidLen = 0x10000;	// 64 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBigEndianL(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBigEndianS(&MidData[DstPos + 0x00], 0x0001);	// Format 1
	WriteBigEndianS(&MidData[DstPos + 0x02], 0x0008);	// Tracks: 8
	WriteBigEndianS(&MidData[DstPos + 0x04], 0x0030);	// Ticks per Quarter: 48
	DstPos += 0x06;
	
	if (! FileVer)
	{
		memcpy(ChnPtrList, &SpcData[0x1C00], 0x02 * 0x03);
		if (ChnPtrList[0] == ChnPtrList[1])
			FileVer = 0x11;
		else if (ChnPtrList[0] < ChnPtrList[1] && ChnPtrList[2] < ChnPtrList[1])
			FileVer = 0x20;
		else
			FileVer = 0x10;
	}
	
	if (FileVer == 0x10 || ! FileVer)
	{
		memcpy(ChnPtrList, &SpcData[0x1C00], 0x02 * 0x08);
		BasePtr = 0x1C12 - ChnPtrList[0];
		FILE_FMT = 0x10;
	}
	else if (FileVer == 0x11)
	{
		memcpy(&TempSht, &SpcData[0x1C00], 0x02);
		memcpy(ChnPtrList, &SpcData[0x1C02], 0x02 * 0x08);
		BasePtr = 0x1C14 - TempSht;
		FILE_FMT = 0x11;
	}
	else if (FileVer == 0x20)
	{
		memcpy(&TempSht, &SpcData[0x1C00], 0x02);
		memcpy(ChnPtrList, &SpcData[0x1C04], 0x02 * 0x08);
		BasePtr = 0x1C24 - TempSht;
		FILE_FMT = 0x20;
	}
	else
	{
		FILE_FMT = 0x00;
	}
	
	switch(FILE_FMT >> 4)
	{
	case 0x01:
		CMD_BASE = 0xD2;
		break;
	case 0x02:
		CMD_BASE = 0xC4;
		break;
	default:
		printf("Invalid format!\n");
		return 0xFF;
	}
	
	for (CurTrk = 0x00; CurTrk < 0x08; CurTrk ++)
	{
		InPos = BasePtr + ChnPtrList[CurTrk];
		
		WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0x00;
		
		TrkEnd = false;
		LastNote = 0xFF;
		LoopID = 0xFF;
		LoopCur[0x0F] = 0x00;
		MidChn = CurTrk;
		LastChn = MidChn;
		NoteVol = 0x7F;
		ChnVol = 0x64;
		DrmNote = 0x00;
		NoteMove = 0x00;
//		PitchToNote = 0xFF;
//		LastNote = 0xFF;
//		NoAtk = 0x00;
//		LastPbRPN = 0x00;
		PortamntOn = 0x00;
		CurOct = 0x04;
		MsgMask = 0x00;
		while(! TrkEnd)
		{
			CurCmd = SpcData[InPos];
			if (CurCmd < CMD_BASE)
			{
				if ((FILE_FMT >> 4) == 0x01)
					TempByt = CurCmd / 15;
				else
					TempByt = CurCmd / 14;
				if (TempByt < 12)
				{
					// Normal Note
					if (! DrmNote)
						CurNote = (CurOct * 12) + TempByt + NoteMove;
					else
						CurNote = DrmNote;
				}
				else if (TempByt == 12)
				{
					// Hold Note
					CurNote = LastNote;
					LastNote = 0x80;	// Surpress New Note
				}
				else if (TempByt == 13)
				{
					// Delay
					CurNote = 0xFF;
				}
				
				// turn Note off
				if (LastNote < 0x80)
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | LastChn, LastNote, 0x00);
				
				if (PortamntOn == 0x01)
				{
					PortamntOn ++;
					if (CurNote < 0x80 && LastNote != 0x80)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x54, CurNote);	// write Portamento Control
						LastNote = 0x80;
					}
				}
				else if (PortamntOn == 0x02)
				{
					WriteEvent(MidData, &DstPos, &CurDly,	// Portamento Off
								0xB0 | MidChn, 0x41, 0x00);
					PortamntOn = 0x00;
				}
				
				// write the current Note
				if (CurNote < 0x80 && LastNote != 0x80)
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, CurNote, /*0x7F*/NoteVol);
				
				LastNote = CurNote;
				LastChn = MidChn;
				
				if (PortamntOn == 0x02 && CurNote < 0x80)
				{
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | LastChn, LastNote, 0x00);	// Turn old note off
					
					if ((INT16)CurNote + PortamntNote < 0x00)
						CurNote = 0x00;
					else if ((INT16)CurNote + PortamntNote > 0x7F)
						CurNote = 0x7F;
					else
						CurNote += PortamntNote;
					PortamntNote = 0x00;
					
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, CurNote, /*0x7F*/NoteVol);
					LastNote = CurNote;
				}
				
				if ((FILE_FMT >> 4) == 0x01)
				{
					TempByt = CurCmd % 15;
					CurDly += DELAY_TABLE[TempByt];
				}
				else
				{
					TempByt = CurCmd % 14;
					CurDly += DELAY_TABLE_L[TempByt];
				}
				
			//	NoAtk >>= 1;
			//	PitchToNote = 0xFF;
				InPos ++;
			}
			else
			{
				if ((FILE_FMT >> 4) == 0x02)
				{
					// convert old commands values to new ones
					if (CurCmd < 0xE4)
						CurCmd += 14;	// C4..E3 -> D2...F1
					else if (CurCmd >= 0xF0 && CurCmd <= 0xF8)
						CurCmd += 3;	// F0..F8 -> F3..FB
					else
					{
						printf("Unknown event %02X on track %X\n", SpcData[InPos + 0x00], CurTrk);
						break;
					}
				}
				
				switch(CurCmd)
				{
				case 0xD2:	// Volume
					ChnVol = DB2Mid(Lin2DB(SpcData[InPos + 0x01]));
#ifdef USE_VOL_CTRLS
					if (! DrmNote)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x07, ChnVol);
					else
#endif
						NoteVol = ChnVol ? ChnVol : 0x01;
					InPos += 0x02;
					break;
				case 0xD3:	// Volume Slide
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					ChnVol = DB2Mid(Lin2DB(SpcData[InPos + 0x02]));
					NoteVol = ChnVol ? ChnVol : 0x01;
					InPos += 0x03;
					break;
				case 0xD4:	// Panorama
					TempByt = SpcData[InPos + 0x01] >> 1;	// change range from 00.80.FF to 00.40.7F
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x0A, TempByt);
					InPos += 0x02;
					break;
				case 0xD6:	// Note Slide
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, LastNote, 0x00);
					
					TempByt = SpcData[InPos + 0x01];
					//TempByt = (UINT8)(0x60 * (1.0 - pow(1 - TempByt / 256.0, 4.0)));
					TempByt = (UINT8)(0x20 + 0x60 * (TempByt / 256.0));
					WriteEvent(MidData, &DstPos, &CurDly,	// Portamento Time
								0xB0 | MidChn, 0x05, TempByt);
					WriteEvent(MidData, &DstPos, &CurDly,	// Portamento On
								0xB0 | MidChn, 0x41, 0x7F);
					PortamntOn = 0x01;
					
					PortamntNote = SpcData[InPos + 0x02];
					InPos += 0x03;
					break;
				case 0xD7:	// Modulation
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x03]);
					TempByt = SpcData[InPos + 0x03] & 0x3F;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x01, TempByt * 2);
					InPos += 0x04;
					break;
				case 0xD8:	// Modulation Off
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x01, 0x00);
					InPos += 0x01;
					break;
				case 0xDB:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xDC:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE2:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE3:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE4:	// Set Octave
					CurOct = SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0xE5:	// Octave Up
					CurOct ++;
					InPos += 0x01;
					break;
				case 0xE6:	// Octave Down
					CurOct --;
					InPos += 0x01;
					break;
				case 0xE7:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xE8:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xE9:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xEA:	// Instrument Change
					TempByt = SpcData[InPos + 0x01];
					if (FileVer == 0x20)
					{
					switch(TempByt)	// Live-A-Live
					{
					case 37-1:
						TempByt = 62-1;
						NoteMove = +12;
						DrmNote = 0x00;
						break;
					case 38-1:
						TempByt = 35-1;
						NoteMove = -12;
						DrmNote = 0x00;
						break;
					case 41-1:
						TempByt = 20-1;
						NoteMove = +12;
						DrmNote = 0x00;
						break;
					case 39-1:
						TempByt = 31-1;
						NoteMove = 0;
						DrmNote = 0x00;
						break;
					case 40-1:
						TempByt = 30-1;
						NoteMove = 0;
						DrmNote = 0x00;
						break;
					case 35-1:	// Tambourine
						DrmNote = 0x36;
						break;
					case 34-1:	// Hi-Hat
						DrmNote = 0x2A;
						break;
					case 36-1:	// Snare Drum
						DrmNote = 0x26;
						break;
					case 33-1:	// Tom Tom
						TempByt = 118-1;
						NoteMove = -12;
						DrmNote = 0x00;
						break;
					default:
						NoteMove = 0;
						DrmNote = 0x00;
						break;
					}
					}
					else if (FileVer == 0x11)
					{
					switch(TempByt)
					{
					case 0x20:
						TempByt = 18-1;
						NoteMove = 0;
						DrmNote = 0x00;
						break;
					case 0x21:
						TempByt = 57-1;
						NoteMove = 0;
						DrmNote = 0x00;
						break;
					case 0x22:
						TempByt = 0x22;
						NoteMove = -36;
						DrmNote = 0x00;
						break;
					case 0x23:
						TempByt = 30-1;
						NoteMove = -12;
						DrmNote = 0x00;
						break;
					case 37-1:	// Cymbal
						DrmNote = 0x39;
						break;
					case 38-1:	// Hi-Hat
						DrmNote = 0x2A;
						break;
					case 39-1:	// Hi-Hat
						DrmNote = 0x2E;
						break;
					case 40-1:	// Bass Drum
						DrmNote = 0x24;
						break;
					case 41-1:	// Snare Drum
						DrmNote = 0x26;
						break;
					case 42-1:	// Tom Tom
						TempByt = 118-1;
						NoteMove = -12;
						DrmNote = 0x00;
						break;
					}
					}
					else if (FileVer == 0x10)
					{
					switch(TempByt)	// FF Mystic Quest
					{
					case 0x20:	// Bass Drum
						DrmNote = 0x24;
						break;
					case 0x21:	// Snare Drum
						DrmNote = 0x26;
						break;
					case 0x22:	// Hi-Hat
						DrmNote = 0x2A;
						break;
					case 0x23:	// Tom Tom
						DrmNote = 0x2D;
						break;
					//case 0x24:	// Cymbal
					//	DrmNote = 0x31;
					//	break;
					case 0x25:	// Cymbal
						DrmNote = 0x37;
						break;
					default:
						DrmNote = 0x00;
						switch(TempByt)
						{
						case  1-1:
							TempByt = 6-1;
							NoteMove = +12;
							break;
						case  2-1:
							TempByt = 5-1;
							NoteMove = +12;
							break;
						case  3-1:
							TempByt = 81-1;
							NoteMove = +12;
							break;
						case  5-1:
							TempByt = 82-1;
							NoteMove = +12;
							break;
						case  8-1:
							TempByt = 9-1;
							NoteMove = +12;
							break;
						case 37-1:
							TempByt = 48-1;
							NoteMove = -12;
							break;
						case 39-1:
							TempByt = 39-1;
							NoteMove = -12;
							break;
						case 40-1:
							TempByt = 17-1;
							NoteMove = +12;
							break;
						case 41-1:
							TempByt = 30-1;
							NoteMove = 0;
							break;
						case 42-1:
							TempByt = 49-1;
							NoteMove = +12;
							break;
						case 43-1:
							TempByt = 57-1;
							NoteMove = +12;
							break;
						case 44-1:
							TempByt = 73-1;
							NoteMove = +12;
							break;
						default:
							NoteMove = 0;
							printf("Unmapped Instrument: %u!\n", TempByt + 1);
							break;
						}
						break;
					}
					}
					if (DrmNote)
					{
						MidChn = 0x09;
#ifdef USE_VOL_CTRLS
						NoteVol = ChnVol ? ChnVol : 0x01;
#endif
					}
					else if (MidChn == 0x09)
					{
						MidChn = CurTrk;
#ifdef USE_VOL_CTRLS
						NoteVol = 0x7F;
#endif
					}
					
					if (! DrmNote)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xC0 | MidChn, TempByt, 0x00);
					
					InPos += 0x02;
					break;
				case 0xEB:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xEC:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xED:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xEE:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					if (DrmNote == 0x2A || DrmNote == 0x2E)
					{
						if (SpcData[InPos + 0x01] < 0x04)
							DrmNote = 0x2E;
						else
							DrmNote = 0x2A;
					}
					InPos += 0x02;
					break;
				case 0xEF:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					if (DrmNote == 0x2E)
						DrmNote = 0x2A;
					InPos += 0x01;
					break;
				case 0xF0:	// Loop Start
					LoopID ++;
					LoopCount[LoopID] = SpcData[InPos + 0x01];
					InPos += 0x02;
					LoopCur[LoopID] = 0x01;
					LoopAddr[LoopID] = InPos;
					break;
				case 0xF1:	// Loop End
					if (LoopID == 0xFF)
					{
						InPos += 0x01;
						printf("Warning! Invalid Loop End found in Track %X!\n", CurTrk);
						WriteDgbEvt(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, 0x7F);
						// The sound driver seems to ignore invalid Loop End commands.
						// This way, Final Fantasy Mystic Quest, Song 27 (Ending) behaves correctly.
						break;
					}
					
					if (LoopCur[LoopID] <= LoopCount[LoopID])
					{
						LoopCur[LoopID] ++;
						InPos = LoopAddr[LoopID];
					}
					else
					{
						if (LoopCount[LoopID] == 0x00)
						{
							printf("Warning! 1x-Loop found in Track %X, forcing track end!\n", CurTrk);
							TrkEnd = true;
						}
						LoopID --;
						InPos += 0x01;
					}
					break;
				case 0xF2:
					TrkEnd = true;
					InPos += 0x01;
					break;
				case 0xF3:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					
					WriteEvent(MidData, &DstPos, &CurDly,
								0xFF, 0x51, 0x00);
					//TempLng = 1000000 / (SpcData[InPos + 0x01] / 60.0);
					TempLng = 60000000 / SpcData[InPos + 0x01];
					WriteBigEndianL(&MidData[DstPos - 0x01], TempLng);
					MidData[DstPos - 0x01] = 0x03;
					DstPos += 0x03;
					
					InPos += 0x02;
					break;
				case 0xF5:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xF7:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xF8:
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xF9:	// Loop Exit
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					//WriteDgbEvt(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					if (LoopCur[LoopID] < SpcData[InPos + 0x01])
					{
						InPos += 0x04;
						WriteDgbEvt(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, 0x00);
					}
					else
					{
						// Note: In the old format, this jumps TO the Loop End command.
						//       In the new format, this jumps AFTER the Loop End command. (and pops the loop stack)
						if ((FILE_FMT >> 4) == 0x02)
							LoopID --;
						memcpy(&TempSht, &SpcData[InPos + 0x02], 0x02);
						InPos = BasePtr + TempSht;
						WriteDgbEvt(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, 0x7F);
					}
					break;
				case 0xFA:	// Jump
					LoopCur[0x0F] ++;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6F, LoopCur[0x0F]);
					if (LoopCur[0x0F] < 0x02)
					{
						memcpy(&TempSht, &SpcData[InPos + 0x01], 0x02);
						InPos = BasePtr + TempSht;
					}
					else
					{
						InPos += 0x01;
						TrkEnd = true;
					}
					break;
				//case 0xFF:	// Track End
				//	TrkEnd = true;
				//	InPos += 0x01;
				//	break;
				/*case 0x8D:	// Portatemento/Pitch Bend
					// pitch next note to aa
#ifdef SHOW_DEBUG_MESSAGES
					if (! (MsgMask & 0x02))
					{
						printf("Channel %u: Pitch Bend\n", CurTrk);
						MsgMask |= 0x02;
					}
#endif
					PitchToNote = SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0x94:	// Frequency Displacement
					if (! (MsgMask & 0x10))
					{
						printf("Channel %u: Command %02X\n", CurTrk, CurCmd);
						MsgMask |= 0x10;
					}
					MidData[DstPos + 0x00] = 0xB0 | MidChn;
					MidData[DstPos + 0x01] = 0x6D;
					MidData[DstPos + 0x02] = CurCmd & 0x7F;
					MidData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;*/
				default:
					printf("Unknown event %02X on track %X\n", SpcData[InPos + 0x00], CurTrk);
					WriteDgbEvt(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					InPos += 0x02;
					TrkEnd = true;
					break;
				}
			}
		}
		if (LastNote < 0x80)
			WriteEvent(MidData, &DstPos, &CurDly,
						0x90 | MidChn, LastNote, 0x00);
		
		WriteEvent(MidData, &DstPos, &CurDly,
					0xFF, 0x2F, 0x00);
		
		WriteBigEndianL(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
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

static double Lin2DB(UINT8 LinVol)
{
	//return log(LinVol / 255.0) / log(2.0) * 6.0;
	return log(LinVol / 255.0) * 8.65617024533378;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}
