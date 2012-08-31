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


//bool LetterInArgument(char* Arg, char Letter);

UINT8 SBM52Mid(void);
static void WriteBigEndianL(UINT8* Buffer, UINT32 Value);
static void WriteBigEndianS(UINT8* Buffer, UINT16 Value);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static double Lin2DB(UINT8 LinVol);
static UINT8 DB2Mid(double DB);


//static bool OldDriver;
//static bool InsLimit;

UINT32 SpcLen;
UINT8* SpcData;
UINT32 MidLen;
UINT8* MidData;

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
	
	printf("SBM5 SPC -> Midi Converter\n--------------------------\n");
	if (argc < 2)
	{
		printf("Usage: sbm52mid.exe Song.spc Song.mid\n");
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
	
	RetVal = SBM52Mid();
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


UINT8 SBM52Mid(void)
{
#define TICK_P_QUARTER	0x30
	UINT16 HdrPos;
	UINT8 ChnMask;
	UINT16 ChnPtrList[0x08];
	UINT8 CurTrk;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopID;
	UINT8 StkID;	// Stack Pointer
	UINT8 LoopCount[0x10];
	UINT16 LoopAddr[0x10];
	UINT16 StkAddr[0x10];
	UINT8 LoopCur[0x10];
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 CurOct;
	UINT8 CurNote;
	UINT8 CurVol;
	INT8 CurDisplc;
	UINT8 LastNote;
	UINT8 LastChn;
	UINT32 CurDly;
	UINT8 NoteVol;
	UINT8 ChnVol;
	INT8 NoteMove;
	UINT8 PtmtOn;
	INT8 PtmtDTone;
//	UINT8 PitchToNote;
//	INT16 PitchRange;
//	UINT8 PbStpCnt;
//	UINT8 LastPbRPN;
//	UINT8 NoAtk;
	
	UINT8 MsgMask;
	
	MidLen = 0x100000;	// 64 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBigEndianL(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBigEndianS(&MidData[DstPos + 0x00], 0x0001);	// Format 1
	WriteBigEndianS(&MidData[DstPos + 0x02], 0x0008);	// Tracks: 8
	WriteBigEndianS(&MidData[DstPos + 0x04], TICK_P_QUARTER);	// Ticks per Quarter
	DstPos += 0x06;
	
	memcpy(&InPos, &SpcData[0x3000], 0x02);
	memcpy(&HdrPos, &SpcData[InPos], 0x02);
	
	InPos = HdrPos;
	ChnMask = SpcData[InPos + 0x01];
	InPos += 0x02;
	
	for (CurTrk = 0x00; CurTrk < 0x08; CurTrk ++)
	{
		if (ChnMask & (1 << CurTrk))
		{
			memcpy(&ChnPtrList[CurTrk], &SpcData[InPos], 0x02);
			InPos += 0x02;
		}
		else
		{
			ChnPtrList[CurTrk] = 0x0000;
		}
	}
	
	for (CurTrk = 0x00; CurTrk < 0x08; CurTrk ++)
	{
		InPos = ChnPtrList[CurTrk];
		
		WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0x00;
		
		TrkEnd = (InPos == 0x0000);
		LastNote = 0xFF;
		LoopID = 0xFF;
		StkID = 0xFF;
		LoopCur[0x0F] = 0x00;
		MidChn = CurTrk;
		LastChn = MidChn;
		NoteVol = 0x7F;
		ChnVol = 0x7F;
		NoteMove = 0x00;
//		PitchToNote = 0xFF;
//		LastNote = 0xFF;
//		NoAtk = 0x00;
//		LastPbRPN = 0x00;
		CurOct = 0x00;
		CurVol = 0x4F;
		CurDisplc = 0x00;
		PtmtOn = 0x00;
		PtmtDTone = 0x00;
		MsgMask = 0x00;
		while(! TrkEnd && DstPos < MidLen - 0x100)
		{
			CurCmd = SpcData[InPos];
			if (CurCmd < 0xD0)
			{
				TempByt = CurCmd >> 4;
				if (! TempByt)
				{
					// Delay
					CurNote = 0xFF;
				}
				else if (TempByt <= 12)
				{
					CurNote = (12 + CurOct * 12) + (TempByt - 1) + NoteMove;
					if (MidChn == 0x09)
						CurNote = (CurNote + 24) & 0x7F;
					else
						CurNote += CurDisplc;
				}
				
				// turn Note off
				if (LastNote < 0x80)
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | LastChn, LastNote, 0x00);
				
				// write the current Note
				if (CurNote < 0x80 && LastNote != 0x80)
				{
					if (PtmtOn & 0x01)
						WriteEvent(MidData, &DstPos, &CurDly,
									//0xB0 | MidChn, 0x54, (CurNote + PtmtDTone) & 0x7F);
									0xB0 | MidChn, 0x54, 12 + PtmtDTone);
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, CurNote, NoteVol);
				}
				
				if (CurCmd & 0x08)
					LastNote = 0x80;	// Surpress New Note
				else
					LastNote = CurNote;
				LastChn = MidChn;
				
				TempByt = CurCmd & 0x07;
				if (TempByt)
				{
					TempByt --;
					CurDly += (TICK_P_QUARTER * 4) >> TempByt;
				}
				else
				{
					InPos ++;
					TempByt = SpcData[InPos];
					CurDly += TempByt * (TICK_P_QUARTER / 24);
				}
				
			//	NoAtk >>= 1;
			//	PitchToNote = 0xFF;
				InPos ++;
			}
			else
			{
				switch(CurCmd)
				{
				case 0xD1:	// Set Tempo
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
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
				case 0xD2:	// Set Octave
					CurOct = SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0xD3:	// Octave Up
					CurOct ++;
					if (CurOct > 0x05)	// verified with real driver
						CurOct = 0x05;	// Chn 3 of "Where it All Began" needs this
					InPos += 0x01;
					break;
				case 0xD4:	// Octave Down
					CurOct --;
					if (CurOct & 0x80)	// the driver actually uses DEC and BPL
						CurOct = 0x00;
					InPos += 0x01;
					break;
				case 0xD5:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xD6:	// Set Instrument
					if (MidChn == 0x09)
						MidChn = CurTrk;
					WriteEvent(MidData, &DstPos, &CurDly,
							0xC0 | MidChn, SpcData[InPos + 0x01], 0x00);
					InPos += 0x02;
					break;
				case 0xD9:	// Set Volume (00-3F)
					CurVol = SpcData[InPos + 0x01];
					if (CurVol > 0x4F)
						printf("Volume Set: 0x%02X\n", CurVol);
					if (CurVol & 0x80)
						CurVol = 0x00;
					else if (CurVol > 0x4F)
						CurVol = 0x4F;
					
					ChnVol = DB2Mid(Lin2DB(CurVol));
					//WriteEvent(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x07, ChnVol);
					NoteVol = ChnVol ? ChnVol : 0x01;
					InPos += 0x02;
					break;
				case 0xDA:	// Set Pan (0..15..30)
					TempByt = SpcData[InPos + 0x01];
					if (TempByt > 30)
						printf("Pan Set: %u\n", TempByt);
					
					TempByt = (TempByt + 1) * 4;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x0A, TempByt);
					InPos += 0x02;
					break;
				case 0xDB:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xDC:	// Change Volume
					if (SpcData[InPos + 0x01] < 0x80 &&
						CurVol + SpcData[InPos + 0x01] >= 0x100)
						printf("Volume Error at 0x%02hX!\n", InPos);
					CurVol += SpcData[InPos + 0x01];
					if (CurVol & 0x80)
						CurVol = 0x00;
					else if (CurVol > 0x4F)
						CurVol = 0x4F;
					
					ChnVol = DB2Mid(Lin2DB(CurVol));
					//WriteEvent(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x07, ChnVol);
					NoteVol = ChnVol ? ChnVol : 0x01;
					InPos += 0x02;
					break;
				case 0xDD:	// Loop Start
					LoopID ++;
					LoopCount[LoopID] = SpcData[InPos + 0x01];
					InPos += 0x02;
					LoopCur[LoopID] = 0x00;
					LoopAddr[LoopID] = InPos;
					break;
				case 0xDE:	// Loop End
					if (LoopID == 0xFF)
					{
						InPos += 0x01;
						printf("Warning! Invalid Loop End found!\n");
						break;
					}
					
					LoopCur[LoopID] ++;
					if (LoopCur[LoopID] < LoopCount[LoopID])
					{
						InPos = LoopAddr[LoopID];
					}
					else
					{
						LoopID --;
						InPos += 0x01;
					}
					break;
				case 0xDF:	// GoSub
					memcpy(&TempSht, &SpcData[InPos + 0x01], 0x02);
					
					StkID ++;
					StkAddr[StkID] = InPos + 0x03;
					InPos = TempSht;
					break;
				case 0xE1:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xE2:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x03]);
					InPos += 0x04;
					break;
				case 0xE3:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xE6:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE7:	// Set Key Displacement
					CurDisplc = (INT8)SpcData[InPos + 0x01];
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x68, 0x40 + CurDisplc);
					InPos += 0x02;
					break;
				case 0xE8:	// Change Key Displacement
					CurDisplc += SpcData[InPos + 0x01];
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x68, 0x40 + CurDisplc);
					InPos += 0x02;
					break;
				case 0xEB:	// Master Loop Start
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6F, 0x00);
					LoopCur[0x0F] = 0x00;
					InPos += 0x01;
					LoopAddr[0x0F] = InPos;
					break;
				case 0xEC:	// Master Loop End
					LoopCur[0x0F] ++;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6F, LoopCur[0x0F]);
					if (LoopCur[0x0F] < 0x02)
					{
						InPos = LoopAddr[0x0F];
					}
					else
					{
						InPos += 0x01;
						TrkEnd = true;
					}
					break;
				case 0xEF:	// Set Portamento from Delta-Tone
					/*WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);*/
					PtmtOn &= ~0x01;
					if (SpcData[InPos + 0x02])
					{
						PtmtOn |= 0x01;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x05, 0x40 - SpcData[InPos + 0x02]);
					}
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x41, PtmtOn ? (0x40 | PtmtOn) : 0x00);
					InPos += 0x03;
					break;
				case 0xF0:	// Set Portamento Delta-Tone
					/*WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);*/
					PtmtDTone = (INT8)SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0xF1:
					/*WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);*/
					PtmtOn &= ~0x02;
					if (SpcData[InPos + 0x01])
					{
						PtmtOn |= 0x02;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x05, 0x40 - SpcData[InPos + 0x01]);
					}
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x41, PtmtOn ? (0x40 | PtmtOn) : 0x00);
					InPos += 0x03;
					break;
				case 0xFE:
					if (SpcData[InPos + 0x01]==3)
						MidChn = 0x09;
					//else if (MidChn == 0x09)
					//	MidChn = CurTrk;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xFF:
					if (StkID == 0xFF)
					{
						TrkEnd = true;
					}
					else
					{
						InPos = StkAddr[StkID];
						StkID --;
					}
					break;
				/*
				case 0xD2:	// Volume?
					ChnVol = DB2Mid(Lin2DB(SpcData[InPos + 0x01]));
					//if (! DrmNote)
					//	WriteEvent(MidData, &DstPos, &CurDly,
					//				0xB0 | MidChn, 0x07, ChnVol);
					//else
						NoteVol = ChnVol ? ChnVol : 0x01;
					InPos += 0x02;
					break;
				case 0xD3:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x03;
					break;
				case 0xD4:	// Panorama?
					TempByt = SpcData[InPos + 0x01] >> 1;	// change range from 00.80.FF to 00.40.7F
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x0A, TempByt);
					InPos += 0x02;
					break;
				case 0xD6:	// Note Slide
					if (LastNote < 0x80)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, LastNote, 0x00);
						
						WriteEvent(MidData, &DstPos, &CurDly,	// Portamento On
									0xB0 | MidChn, 0x41, 0x7F);
						
						CurNote = SpcData[InPos + 0x01];
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, CurNote, NoteVol);
						LastNote = CurNote;
						
						WriteEvent(MidData, &DstPos, &CurDly,	// Portamento On
									0xB0 | MidChn, 0x41, 0x00);
					}
					InPos += 0x03;
					break;
				case 0xD7:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x03]);
					InPos += 0x04;
					break;
				case 0xD8:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xDB:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xDC:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE2:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE3:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xE7:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xE9:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xEB:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xEC:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xED:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xEE:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					if (DrmNote == 0x2A)
						DrmNote = 0x2E;
					InPos += 0x02;
					break;
				case 0xEF:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					if (DrmNote == 0x2E)
						DrmNote = 0x2A;
					InPos += 0x01;
					break;
				case 0xF2:
					TrkEnd = true;
					InPos += 0x01;
					break;
				case 0xF5:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xF7:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xF8:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xF9:	// Loop Exit
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					//WriteEvent(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					if (LoopCur[LoopID] < SpcData[InPos + 0x01])
					{
						InPos += 0x04;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, 0x00);
					}
					else
					{
						memcpy(&TempSht, &SpcData[InPos + 0x02], 0x02);
						InPos = BasePtr + TempSht;
						WriteEvent(MidData, &DstPos, &CurDly,
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
				*/
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
					WriteEvent(MidData, &DstPos, &CurDly,
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
	return log(LinVol / 79.0) * 8.65617024533378;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}
