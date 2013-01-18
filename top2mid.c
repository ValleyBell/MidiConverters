// Tales of Phantasia SPC -> Midi Converter
// ----------------------------------------
// Written by Valley Bell, 2012

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

//#define CONVERT_VOL


//bool LetterInArgument(char* Arg, char Letter);

UINT8 ToP2Mid(void);
static void WriteBigEndianL(UINT8* Buffer, UINT32 Value);
static void WriteBigEndianS(UINT8* Buffer, UINT16 Value);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static double Lin2DB(UINT8 LinVol);
static UINT8 DB2Mid(double DB);


typedef struct running_note
{
	UINT8 MidChn;
	UINT8 Note;
	UINT16 RemLen;
} RUN_NOTE;


UINT32 SpcLen;
UINT8* SpcData;
UINT32 MidLen;
UINT8* MidData;
UINT8 RunNoteCnt;
RUN_NOTE RunNotes[0x100];
bool FixInsSet;
bool FixVolume;

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
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
	
	printf("ToP SPC -> Midi Converter\n-------------------------\n");
	if (argc < 4)
	{
		printf("Usage: top2mid.exe Options Song.spc Song.mid\n");
		printf("Options: (options can be combined)\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    i   fix Instruments\n");
		printf("    v   fix Volume (convert linear SNES to logarithmic MIDI)\n");
		printf("Supported games: Tales Of Phantasia SFC and Star Ocean.\n");
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
	
	FixInsSet = false;
	FixVolume = false;
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			FixInsSet = false;
			FixVolume = false;
			break;
		case 'I':
			FixInsSet = true;
			break;
		case 'V':
			FixVolume = true;
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
	
	RetVal = ToP2Mid();
	if (RetVal)
		return 3;
	
	hFile = fopen(argv[3], "wb");
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


UINT8 ToP2Mid(void)
{
	const UINT8 DRIVER_SIG[0x14] =
	{	0x20, 0xE8, 0x00, 0xC4, 0xF4, 0xC4, 0xF5, 0xC4,
		0xF6, 0xC4, 0xF7, 0xC4, 0x83, 0x8F, 0x30, 0xF1,
		0xCD, 0xFF, 0xBD, 0x3F};
	UINT16 BasePtr;
	UINT16 ChnPtrList[0x10];
	UINT8 ChnModeList[0x10];
	UINT8 CurTrk;
	UINT16 SegBase;
	UINT16 SegIdx;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopIdx;
	UINT8 LoopCount[0x10];
	UINT16 LoopPos[0x10];
	UINT16 LoopSeg[0x10];
	UINT32 TempLng;
	//UINT16 TempSht;
	UINT8 TempByt;
	UINT8 CurNote;
	UINT8 LastChn;
	UINT32 CurDly;
	UINT8 NoteVol;
	UINT8 ChnVol;
	INT8 NoteMove;
	UINT8 DrmNote;
	UINT8 PBRange;
	
	UINT8 MsgMask;
	UINT8 InitTempo;
	
	if (memcmp(&SpcData[0x0840], DRIVER_SIG, 0x14))
	{
		printf("This SPC uses a wrong sound driver!\n");
		return 0xFF;
	}
	memcpy(&BasePtr, &SpcData[0x0854], 0x02);
	
	MidLen = 0x20000;	// 128 KB should be enough (so-37 has 66.4 KB)
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBigEndianL(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBigEndianS(&MidData[DstPos + 0x00], 0x0001);	// Format 1
	WriteBigEndianS(&MidData[DstPos + 0x02], 0x0010);	// Tracks: 16
	WriteBigEndianS(&MidData[DstPos + 0x04], 0x0018);	// Ticks per Quarter: 48
	DstPos += 0x06;
	
	InPos = BasePtr + 0x0020;
	
	WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
	DstPos += 0x08;
	TrkBase = DstPos;
	CurDly = 0x00;
	
	InPos += 0x02;	// skip file size
	
	InitTempo = SpcData[InPos];
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x51, 0x00);
	TempLng = 60000000 / (InitTempo * 2);	// base guessed
	WriteBigEndianL(&MidData[DstPos - 0x01], TempLng);
	MidData[DstPos - 0x01] = 0x03;
	DstPos += 0x03;
	InPos ++;
	
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBigEndianL(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	
	for (CurTrk = 0x00; CurTrk < 0x0F; CurTrk ++)
	{
		ChnModeList[CurTrk] = SpcData[InPos];
		InPos ++;
		memcpy(&ChnPtrList[CurTrk], &SpcData[InPos], 0x02);
		InPos += 0x02;
	}
	
	for (CurTrk = 0x00; CurTrk < 0x0F; CurTrk ++)
	{
		SegBase = BasePtr + ChnPtrList[CurTrk];
		
		WriteBigEndianL(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0x00;
		
		TrkEnd = ! (ChnModeList[CurTrk] >> 7);
		LoopIdx = 0x00;
		MidChn = CurTrk + (CurTrk + 6) / 15;
		LastChn = MidChn;
		NoteVol = 0x7F;
		ChnVol = 0x64;
		DrmNote = 0x00;
		NoteMove = 0x00;
		RunNoteCnt = 0x00;
		MsgMask = 0x00;
		SegIdx = 0x00;
		InPos = 0x0000;
		PBRange = 0x00;
		while(! TrkEnd)
		{
			if (InPos == 0x0000)
			{
				//if (SegIdx == 0xFFFF)
				//	break;
				memcpy(&InPos, &SpcData[SegBase + SegIdx * 0x02], 0x02);
				if (InPos == 0xFFFF)
					break;
				InPos += BasePtr;
				SegIdx ++;
			}
			
			CurCmd = SpcData[InPos];
			if (CurCmd < 0x90)
			{
				CurNote = CurCmd + NoteMove;
				if (FixVolume)
					NoteVol = DB2Mid(Lin2DB(SpcData[InPos + 0x03]));
				else
					NoteVol = SpcData[InPos + 0x03] >> 1;
				
				if (DrmNote)
					CurNote = DrmNote;
				
				WriteEvent(MidData, &DstPos, &CurDly, 0x00, 0x00, 0x00);
				
				for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
				{
					if (RunNotes[TempByt].Note == CurNote)
					{
						RunNotes[TempByt].RemLen = (UINT16)CurDly + SpcData[InPos + 0x02];
						break;
					}
				}
				if (TempByt >= RunNoteCnt)
				{
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, CurNote, NoteVol);
					if (RunNoteCnt < 0x80)
					{
						RunNotes[RunNoteCnt].MidChn = MidChn;
						RunNotes[RunNoteCnt].Note = CurNote;
						RunNotes[RunNoteCnt].RemLen = SpcData[InPos + 0x02];
						RunNoteCnt ++;
					}
				}
				
				CurDly += SpcData[InPos + 0x01];
				InPos += 0x04;
			}
			else
			{
				switch(CurCmd)
				{
				case 0x90:	// Delay
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0x92:	// Loop Start
					if (! LoopIdx)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, 0x00);
					InPos += 0x01;
					
					LoopSeg[LoopIdx] = SegIdx;
					LoopPos[LoopIdx] = InPos;
					LoopCount[LoopIdx] = 0x00;
					LoopIdx ++;
					break;
				case 0x93:	// Loop End
					TempByt = SpcData[InPos + 0x01];
					InPos += 0x02;
					
					LoopIdx --;
					LoopCount[LoopIdx] ++;
					if (! TempByt)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, LoopCount[LoopIdx]);
					/*if (! TempByt && LoopCount[LoopIdx] >= 0x02)
					{
						SegIdx = 0xFFFF;
						break;
					}*/
					if (LoopCount[LoopIdx] < TempByt ||
						(! TempByt && LoopCount[LoopIdx] < 0x02))
					{
						SegIdx = LoopSeg[LoopIdx];
						InPos = LoopPos[LoopIdx];
						LoopIdx ++;
					}
					
					break;
				case 0x94:	// Pitch Bend
					TempByt = (CurCmd == 0x94) ? 12 : 2;
					if (PBRange != TempByt)
					{
						PBRange = TempByt;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x65, 0x00);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x64, 0x00);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x06, PBRange);
					}
					WriteEvent(MidData, &DstPos, &CurDly,
								0xE0 | MidChn, 0x00, SpcData[InPos + 0x02]);
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x95:	// Set Tempo (relative to initial tempo)
					WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x51, 0x00);
					TempByt = SpcData[InPos + 0x02];
					//TempLng = 60000000 / (InitTempo * 2 * TempByt / 0x40);	// guessed
					TempLng = 0x40 * 30000000 / (InitTempo * TempByt);
					WriteBigEndianL(&MidData[DstPos - 0x01], TempLng);
					MidData[DstPos - 0x01] = 0x03;
					DstPos += 0x03;
					//WriteEvent(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x96:	// Set Instrument
					TempByt = SpcData[InPos + 0x01];
					if (FixInsSet)
					{
						switch(TempByt)
						{
						case 32-1:	// Cymbal
							DrmNote = 0x39;
							break;
						case 44-1:	// Shaker
							DrmNote = 0x39+12;
							break;
						case 30-1:	// Hi-Hat
							DrmNote = 0x2E;
							break;
						case 29-1:	// Hi-Hat
							DrmNote = 0x2A;
							break;
						case 28-1:	// Snare Drum
							DrmNote = 0x26;
							break;
						case 27-1:	// Snare Drum
							DrmNote = 0x28;
							break;
						case 26-1:	// Bass Drum
							DrmNote = 0x24;
							break;
						case 67-1:	// Snare Drum (Clap?)
							DrmNote = 0x27;
							break;
						case 38-1:	// Wood Block H
							DrmNote = 0x4D;
							break;
						case 39-1:	// Wood Block L
							DrmNote = 0x4C;
							break;
						case 33-1:	// Tom Tom
							TempByt = 118-1;
							NoteMove = -12;
							DrmNote = 0x00;
							break;
						case 65-1:	// Jingle Bell
							DrmNote = 0x53;
							break;
						case 21-1:	// Cuica
							//DrmNote = 0x4F;
							TempByt = 114-1;	// Agogo
							NoteMove = 0;
							DrmNote = 0x00;
							break;
						case 22-1:	// Conga?
							DrmNote = 0x40;
							break;
						case 41-1:	// Long Whistle
							DrmNote = 0x48;
							break;
						case 42-1:	// Timbale
							DrmNote = 0x41;
							break;
						case 56-1:	// French Horn
							TempByt = 61-1;
							break;
						case 61-1:
							DrmNote = 56-1;
							break;
						case 34-1:	// Triangle?
							DrmNote = 0x51;
							break;
						case 5-1:	// Sawtooth Lead
							TempByt = 0x51;
							NoteMove = -12;
							DrmNote = 0x00;
							break;
						case 9-1:	// Square Lead
							TempByt = 0x50;
							NoteMove = 0;
							DrmNote = 0x00;
							break;
						default:
							NoteMove = 0;
							DrmNote = 0x00;
							break;
						}
						if (DrmNote)
						{
							MidChn = 0x09;
							//NoteVol = ChnVol ? ChnVol : 0x01;
						}
						else if (MidChn == 0x09)
						{
							MidChn = CurTrk + (CurTrk + 6) / 15;
							//NoteVol = 0x7F;
						}
					}
					
					if (! DrmNote)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xC0 | MidChn, TempByt, 0x00);
					InPos += 0x02;
					break;
				case 0x97:	// another Volume setting?
					if (FixVolume)
						TempByt = DB2Mid(Lin2DB(SpcData[InPos + 0x02]));
					else
						TempByt = SpcData[InPos + 0x02] >> 1;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x07, TempByt);
					InPos += 0x03;
					break;
				case 0x98:	// Set Volume
					if (FixVolume)
						TempByt = DB2Mid(Lin2DB(SpcData[InPos + 0x02]));
					else
						TempByt = SpcData[InPos + 0x02] >> 1;
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x0B, TempByt);
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x99:	// Set Pan
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x0A, SpcData[InPos + 0x02]);
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x9B:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0x9C:
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
				case 0xA2:
					/*WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);*/
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x65, 0x00);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x64, 0x01);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x06, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, 0x11);
					InPos += 0x02;
					break;
				case 0xA3:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xAA:
					//WriteEvent(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x5B, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x5D, SpcData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xAD:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xAE:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xAF:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					InPos += 0x03;
					break;
				case 0xB2:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0xC8:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xF0:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xFD:	// Segment Return
					//InPos += 0x01;
					InPos = 0x0000;
					break;
				case 0xFE:
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x01;
					break;
				case 0xFF:
					TrkEnd = true;
					InPos += 0x01;
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", SpcData[InPos + 0x00], CurTrk, InPos);
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
	UINT8 CurNote;
	UINT32 TempDly;
	RUN_NOTE* TempNote;
	bool MoreNotes;
	
	do
	{
		MoreNotes = false;
		TempNote = RunNotes;
		if (! Evt)
			TempDly = *Delay + 1;
		else
			TempDly = *Delay;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++, TempNote ++)
		{
			if (TempNote->RemLen < TempDly)
				TempDly = TempNote->RemLen;
		}
		if (! Evt && TempDly >= *Delay + 1)
			break;
		
		TempNote = RunNotes;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++, TempNote ++)
		{
			TempNote->RemLen -= (UINT16)TempDly;
			if (! TempNote->RemLen)
			{
				if (! MoreNotes)
					WriteMidiValue(Buffer, Pos, TempDly);
				else
					WriteMidiValue(Buffer, Pos, 0);
				MidData[*Pos + 0x00] = 0x90 | TempNote->MidChn;
				MidData[*Pos + 0x01] = TempNote->Note;
				MidData[*Pos + 0x02] = 0x00;
				*Pos += 0x03;
				
				if (TempNote->MidChn == 0x09 && TempNote->Note == 0x2E)
				{
					WriteMidiValue(Buffer, Pos, 0);
					MidData[*Pos + 0x00] = 0x2C;
					MidData[*Pos + 0x01] = 0x01;
					*Pos += 0x02;
					WriteMidiValue(Buffer, Pos, 0);
					MidData[*Pos + 0x00] = 0x2C;
					MidData[*Pos + 0x01] = 0x00;
					*Pos += 0x02;
				}
				
				MoreNotes = true;
				
				RunNoteCnt --;
				if (RunNoteCnt)
					*TempNote = RunNotes[RunNoteCnt];
				CurNote --;	TempNote --;
			}
		}
		if (MoreNotes)
			(*Delay) -= TempDly;
	} while(MoreNotes);
	if (! Evt)
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
