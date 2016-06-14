// Konami -> Midi Converter
// ------------------------
// Written by Valley Bell, 

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <stdtype.h>
#include <stdbool.h>


typedef struct _track_info
{
	UINT16 StartPos;
	UINT16 LoopPos;
	UINT32 TickCnt;
	UINT32 LoopTick;
	//UINT8 Flags;
	UINT16 LoopTimes;
} TRK_INFO;


static UINT16 DetectSongCount(UINT32 DataLen, const UINT8* Data, UINT32 MusBankList, UINT32 MusPtrOfs);
UINT8 Konami2Mid(UINT32 KnmLen, UINT8* KnmData, UINT16 KnmAddr/*, UINT32* OutLen, UINT8** OutData*/);
static void PreparseKnm(UINT32 KnmLen, const UINT8* KnmData, UINT8* KnmBuf, TRK_INFO* TrkInf, UINT8 Mode);
static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf);
static UINT16 ReadLE16(const UINT8* Buffer);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static INT8 GetSignMagByte(UINT8 value);
static INT8 GetTranspByte(UINT8 value);
static UINT32 TickInc2MidiTempo(UINT8 tickInc);
static float OPN2DB(UINT8 TL, UINT8 PanMode);
static float PSG2DB(UINT8 Vol);
static UINT8 DB2Mid(float DB);

void SaveInsAsGYB(const char* FileName, const UINT8* InsData);


static const UINT8 CHN_MASK[0x0A] =
{	0x00, 0x01, 0x02, 0x04, 0x05, 0x06,
	0x80, 0xA0, 0xC0, 0xE0};


#define MODE_MUS	0x00
#define	MODE_DAC	0x01
#define MODE_INS	0x02



UINT32 MidLen;
UINT8* MidData;
UINT16 TickpQrtr;
UINT16 DefLoopCount;
bool OptVolWrites;
bool NoLoopExt;

int main(int argc, char* argv[])
{
	FILE* hFile;
	UINT32 BankPos;
	UINT32 SongPos;
	char OutFileBase[0x100];
	char OutFile[0x100];
	char* TempPnt;
	int RetVal;
	int argbase;
	UINT8 Mode;
	
	UINT32 InLen;
	UINT8* InData;
	//UINT32 OutLen;
	//UINT8* OutData;
	
	UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 BankBase;
	UINT32 BankLen;
	
	printf("Konami -> Midi Converter\n------------------------\n");
	if (argc < 2)
	{
		printf("Usage: Konami2Mid.exe [-Mode] [-Options] ROM.bin MusicListAddr(hex) MusicBankList(hex) [Song Count]\n");
		printf("Modes:\n");
		printf("    -mus        Music Mode (convert sequences to MID)\n");
		printf("    -ins        Instrument Mode (dump instruments to GYB)\n");
		//printf("    -dac        DAC Mode (dump DAC sounds to RAW)\n");
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
	DefLoopCount = 2;
	NoLoopExt = false;
	
	Mode = MODE_MUS;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! _stricmp(argv[argbase] + 1, "Mus"))
			Mode = MODE_MUS;
		else if (! _stricmp(argv[argbase] + 1, "DAC"))
			Mode = MODE_DAC;
		else if (! _stricmp(argv[argbase] + 1, "Ins"))
			Mode = MODE_INS;
		else if (! _stricmp(argv[argbase] + 1, "OptVol"))
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
				if (! DefLoopCount)
					DefLoopCount = 2;
			}
		}
		else if (! _stricmp(argv[argbase] + 1, "NoLpExt"))
			NoLoopExt = true;
		else
			break;
		argbase ++;
	}
	
	if (argc <= argbase + 2)
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
	BankPos = strtoul(argv[argbase + 2], NULL, 0x10);
	
	if (argc > argbase + 3)
		FileCount = (UINT16)strtoul(argv[argbase + 3], NULL, 0);
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
	
	switch(Mode)
	{
	case MODE_MUS:
		if (! FileCount)
			FileCount = DetectSongCount(InLen, InData, BankPos, SongPos);
		
		for (CurFile = 0x00; CurFile < FileCount; CurFile ++)
		{
			BankBase = (InData[BankPos + CurFile] << 15);
			CurPos = (SongPos & 0x7FFF) + CurFile * 0x12;
			printf("File %u / %u ...", CurFile + 1, FileCount);
			
			BankLen = InLen - BankBase;
			if (BankLen > 0x8000)
				BankLen = 0x8000;
			RetVal = Konami2Mid(BankLen, &InData[BankBase], (UINT16)CurPos/*, &OutLen, &OutData*/);
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
		break;
	case MODE_DAC:
		//SaveDACData(OutFileBase);
		break;
	case MODE_INS:
		sprintf(OutFile, "%s.gyb", OutFileBase);
		SaveInsAsGYB(OutFile, &InData[SongPos]);
		break;
	}
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}


static UINT16 DetectSongCount(UINT32 DataLen, const UINT8* Data, UINT32 MusBankList, UINT32 MusPtrOfs)
{
	// Song Count autodetection
	UINT32 CurPos;
	UINT32 BankBase;
	UINT32 SongPos;
	
	MusPtrOfs &= 0x7FFF;
	for (CurPos = MusBankList; CurPos < DataLen; CurPos ++, MusPtrOfs += 0x12)
	{
		if ((Data[CurPos] << 15) >= DataLen)
			break;
		BankBase = (Data[CurPos] << 15);
		SongPos = BankBase | (MusPtrOfs & 0x7FFF);
		if (ReadLE16(&Data[SongPos]) & 0x8000)
			break;
	}
	CurPos -= MusBankList;
	
	printf("Songs detected: 0x%02X (%u)\n", CurPos, CurPos);
	return (UINT16)CurPos;
}

UINT8 Konami2Mid(UINT32 KnmLen, UINT8* KnmData, UINT16 KnmAddr/*, UINT32* OutLen, UINT8** OutData*/)
{
	UINT8* TempBuf;
	TRK_INFO TrkInf[0x09];
	TRK_INFO* TempTInf;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	UINT8 ChnMode;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 TempArr[0x04];
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 ChnFlags;
	UINT32 CurDly;
	UINT8 DelayAdd;
	UINT8 VolMult;
	UINT8 ChnVol;
	UINT8 MidChnVol;
	UINT8 NoteVol;
	UINT8 MidNoteVol;
	UINT8 ChnIns;
	UINT8 PanReg;
	INT8 GblTransp;
	INT8 ChnTransp;
	UINT8 CurOctave;
	UINT8 LastNote;
	UINT8 CurNote;
	UINT8 PanMode;
	UINT8 HoldNote;
	UINT8 LpStkIdx;
	UINT16 LoopAddr[2];
	UINT8 LoopCnt[2];
	UINT16 Loop3Start;
	UINT16 Loop3End;
	UINT8 Loop3State;
	UINT16 StackAddr[2];
	UINT16 MstLoopCnt;
	
	TrkCnt = 0x09;
	MidLen = 0x20000;	// 128 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBE32(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBE16(&MidData[DstPos + 0x00], 0x0001);		// Format 1
	WriteBE16(&MidData[DstPos + 0x02], TrkCnt);		// Tracks: TrkCnt
	WriteBE16(&MidData[DstPos + 0x04], TickpQrtr);	// Ticks per Quarter: 24
	DstPos += 0x06;
	
	/*// write Master Track
	WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
	DstPos += 0x08;
	
	TrkBase = DstPos;
	CurDly = 0;
	
	TempLng = TickInc2MidiTempo(0x00);
	WriteBE32(TempArr, TempLng);
	WriteMetaEvent_Data(MidData, &DstPos, &CurDly, 0x51, 0x03, &TempArr[0x01]);
	
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
	
	WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length*/
	
	// Read Header
	TempBuf = (UINT8*)malloc(KnmLen);
	InPos = KnmAddr;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, InPos += 0x02)
	{
		TempTInf = &TrkInf[CurTrk];
		//TempTInf->Flags = 0x00;
		TempTInf->StartPos = ReadLE16(&KnmData[InPos]);
		TempTInf->TickCnt = 0x00;
		TempTInf->LoopTimes = DefLoopCount;
		TempTInf->LoopPos = 0x0000;
		TempTInf->LoopTick = 0x00;
		ChnMode = CHN_MASK[CurTrk] & 0x80;
		
		PreparseKnm(KnmLen, KnmData, TempBuf, TempTInf, ChnMode | 0x00);
		
		// If there is a loop, parse a second time to get the Loop Tick.
		if (TempTInf->LoopPos)
			PreparseKnm(KnmLen, KnmData, TempBuf, TempTInf, ChnMode | 0x01);
	}
	free(TempBuf);	TempBuf = NULL;
	
	if (! NoLoopExt)
		GuessLoopTimes(TrkCnt, TrkInf);
	
	// --- Main Conversion ---
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		
		WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0;
		
		//if (TempTInf->Flags & 0x80)
			TrkEnd = false;
		//else
		//	TrkEnd = true;
		InPos = TempTInf->StartPos;
		
		ChnMode = CHN_MASK[CurTrk];
		if (ChnMode & 0x80)
			MidChn = 0x0A + (CurTrk - 0x06);
		else
			MidChn = CurTrk;
		VolMult = 1;
		ChnVol = 0x00;
		NoteVol = 0x00;
		ChnIns = 0x00;
		PanReg = 0x00;
		DelayAdd = 0;
		HoldNote = 0x00;
		
		ChnTransp = 0;
		CurOctave = 0;
		CurNote = 0x40;
		LastNote = 0xFF;
		ChnFlags = 0x00;
		PanMode = 0x80;
		MidChnVol = 0xFF;
		MidNoteVol = 0x7F;
		LoopAddr[0] = LoopAddr[1] = 0x0000;
		LoopCnt[0] = LoopCnt[1] = 0x00;
		Loop3State = 0x00;
		StackAddr[0] = StackAddr[1] = 0x0000;
		MstLoopCnt = 0xFFFF;
		
		while(! TrkEnd && InPos < KnmLen)
		{
			if (MstLoopCnt == 0xFFFF && InPos == TempTInf->LoopPos)
			{
				MstLoopCnt ++;
				WriteEvent(MidData, &DstPos, &CurDly,
							0xB0 | MidChn, 0x6F, (UINT8)MstLoopCnt);
				MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
			}
			
			CurCmd = KnmData[InPos];
			InPos ++;
			if (CurCmd < 0xD0 || (ChnFlags & 0x80))
			{
				ChnFlags &= ~0x80;
				CurNote = (CurCmd & 0xF0) >> 4;
				if (CurNote == 0x00)
				{
					CurNote = 0xFF;
				}
				else
				{
					CurNote = CurOctave * 12 + ChnTransp + (CurNote - 1);
					if (CurNote >= 0x6C)	// the sound driver has only 108 notes defined
					{
						printf("Warning: Out-of-range note at 0x%04X!\n", InPos - 0x01);
						CurNote = 0x6B;
					}
					if (MidChn == 0x09)
						CurNote += 36;
				}
				
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
				
				if (CurNote != 0xFF)
				{
					if (HoldNote == 0x00)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, CurNote, MidNoteVol);
					}
					else if (HoldNote == 0x02)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x41, 0x7F);	// Portamento On
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, LastNote, 0x00);
						WriteEvent(MidData, &DstPos, &CurDly,
									0x90 | MidChn, CurNote, MidNoteVol);
					}
				}
				
				TempByt = (CurCmd & 0x0F) >> 0;
				if (! TempByt)
					TempByt = 0x10;
				TempByt += DelayAdd;
				DelayAdd = 0;
				
				if (! (ChnFlags & 0x04))
					TempByt *= 3;
				if (! (ChnFlags & 0x02))
					TempByt *= 2;
				if (ChnFlags & 0x08)
				{
					ChnFlags &= ~0x08;
					//TempByt -= TrkRAM3D;
				}
				CurDly += TempByt;
				
				if (HoldNote == 0x02)
				{
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x41, 0x00);	// Portamento Off
				}
				LastNote = CurNote;
				HoldNote = false;
			}
			else
			{
				switch(CurCmd)
				{
				case 0xD0:	// set Volume
				case 0xD1:
				case 0xD2:
				case 0xD3:
				case 0xD4:
				case 0xD5:
				case 0xD6:
				case 0xD7:
					// set Volume from Lookup Table
					NoteVol = CurCmd & 0x07;
					if (VolMult >= 2)
						NoteVol *= VolMult;
					
					if (! (ChnMode & 0x80))
						MidNoteVol = DB2Mid(OPN2DB(NoteVol, 0x00));
					else
						MidNoteVol = DB2Mid(PSG2DB(NoteVol));
					break;
				case 0xD8:	// set Volume Multiplier
					VolMult = KnmData[InPos];
					InPos ++;
					break;
				case 0xD9:	// delay extention
					DelayAdd += 0x10;
					while(KnmData[InPos] == CurCmd)
					{
						DelayAdd += 0x10;
						InPos ++;
					}
					ChnFlags |= 0x80;
					break;
				case 0xDA:	// set Pan
					TempByt = KnmData[InPos];
					InPos ++;
					
					if (! (TempByt & 0x80))
					{
						// write Pan
						PanReg = (TempByt & 0x03) << 6;
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
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, TempByt);
						
						TempByt = (TempByt == 0x40) ? 0x00 : 0x01;
						if (TempByt != PanMode)
						{
							PanMode = TempByt;
							TempByt = DB2Mid(OPN2DB(ChnVol, PanMode));
							if (TempByt != MidChnVol)
							{
								MidChnVol = TempByt;
								WriteEvent(MidData, &DstPos, &CurDly,
											0xB0 | MidChn, 0x07, MidChnVol);
							}
						}
					}
					else
					{
						printf("Warning: Special Pan used!\n");
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, 0x3F);
					}
					break;
				case 0xDB:	// set TrkRAM+38h
					TempByt = -KnmData[InPos];
					InPos ++;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xDC:	// set SSG-EG
					InPos += 0x02;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					break;
				case 0xDD:	// toggle Delay Multiply 3
					ChnFlags ^= 0x04;
					break;
				case 0xDE:	// ??
					ChnFlags |= 0x08;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					break;
				case 0xDF:	// toggle Delay Multiply 2
					ChnFlags ^= 0x02;
					break;
				case 0xE0:	// FM Channel Setup
					ChnMode &= ~0x10;
					if (ChnMode & 0x80)
						MidChn = 0x0A + (CurTrk - 0x06);
					else
						MidChn = CurTrk;
					
					// set Tempo
					TempByt = KnmData[InPos];	InPos ++;
					TempLng = TickInc2MidiTempo(TempByt);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent_Data(MidData, &DstPos, &CurDly, 0x51, 0x03, &TempArr[0x01]);
					
					if (PanMode & 0x80)
					{
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, 0x40);
						PanMode = 0x00;
					}
					
					// set Instrument
					ChnIns = KnmData[InPos];	InPos ++;
					WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, ChnIns & 0x7F, 0x00);
					
					ChnVol = KnmData[InPos];	InPos ++;
					if (! (ChnMode & 0x80))
						TempByt = DB2Mid(OPN2DB(ChnVol, PanMode));
					else
						TempByt = DB2Mid(PSG2DB(ChnVol));
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x07, MidChnVol);
					}
					
					TempByt = KnmData[InPos];	InPos ++;
					break;
				case 0xE1:	// DAC Channel Setup
					ChnMode |= 0x10;
					MidChn = 0x09;
					
					TempByt = KnmData[InPos];	InPos ++;
					TempLng = TickInc2MidiTempo(TempByt);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent_Data(MidData, &DstPos, &CurDly, 0x51, 0x03, &TempArr[0x01]);
					
					if (PanMode & 0x80)
					{
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, 0x40);
						PanMode = 0x00;
					}
					WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, 0x00, 0x00);	// set Drum Kit
					
					TempByt = KnmData[InPos];	InPos ++;
					break;
				case 0xE2:	// set Track Tempo
					TempByt = KnmData[InPos];	InPos ++;
					TempLng = TickInc2MidiTempo(TempByt);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent_Data(MidData, &DstPos, &CurDly, 0x51, 0x03, &TempArr[0x01]);
					break;
				case 0xE3:	// set Instrument
					if (ChnMode & 0x80)
						InPos ++;
					ChnIns = KnmData[InPos];	InPos ++;
					WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, ChnIns & 0x7F, 0x00);
					break;
				case 0xE4:	// set Instrument Volume
					ChnVol = KnmData[InPos];
					InPos ++;
					
					if (! (ChnMode & 0x80))
						TempByt = DB2Mid(OPN2DB(ChnVol, PanMode));
					else
						TempByt = DB2Mid(PSG2DB(ChnVol));
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x07, MidChnVol);
					}
					break;
				case 0xE5:	// Modulation?
					TempByt = KnmData[InPos];
					if (! TempByt)
						InPos ++;
					else
						InPos += 0x02;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xE6:	// 
					TempByt = KnmData[InPos];
					InPos ++;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xE7:	// 
					TempByt = KnmData[InPos];
					InPos ++;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xE8:	// enforce note processing
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					ChnFlags |= 0x80;
					break;
				case 0xE9:	// set LFO Depth
					TempByt = KnmData[InPos];
					if (! TempByt)
					{
						// LFO disable
						InPos ++;
					}
					else
					{
						InPos += 0x02;
					}
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xEA:	// set LFO rate
					TempByt = KnmData[InPos];
					if (TempByt)
						TempByt += 0x07;
					InPos ++;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xEB:	// Global Transpose
					TempByt = KnmData[InPos];
					InPos ++;
					GblTransp = GetTranspByte(TempByt);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xEC:	// Channel Transpose
					TempByt = KnmData[InPos];
					InPos ++;
					ChnTransp = GetTranspByte(TempByt);
					break;
				case 0xED:	// set Detune
					TempByt = KnmData[InPos];
					TempSht = 0x2000 + GetSignMagByte(TempByt) * 64;
					InPos ++;
					
					WriteEvent(MidData, &DstPos, &CurDly,
								0xE0 | MidChn, TempSht & 0x7F, (TempSht >> 7) & 0x7F);
					break;
				case 0xEE:	// 
					InPos += 0x02;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					break;
				case 0xEF:	// some volume stuff
					InPos += 0x02;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					break;
				case 0xF0:	// set Octave??
				case 0xF1:
				case 0xF2:
				case 0xF3:
				case 0xF4:
				case 0xF5:
				case 0xF6:
				case 0xF7:
					CurOctave = CurCmd & 0x07;
					break;
				case 0xF8:	// 
					if (! (ChnMode & 0x80))
					{
						InPos += 0x02;
						break;
					}
					TempByt = KnmData[InPos];
					InPos ++;
					// set TrkRAM+42
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x70, CurCmd & 0x7F);
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt & 0x7F);
					break;
				case 0xF9:	// GoTo
					TempSht = ReadLE16(&KnmData[InPos]);
					
					InPos = TempSht;
					if (InPos >= KnmLen)
						*((char*)NULL) = 'x';
					
					if (InPos == TempTInf->LoopPos)
					{
						if (MstLoopCnt == 0xFFFF)
							MstLoopCnt = 0;
						MstLoopCnt ++;
						if (MstLoopCnt < 0x80)
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | MidChn, 0x6F, (UINT8)MstLoopCnt);
						
						if (MstLoopCnt >= TempTInf->LoopTimes)
							TrkEnd = true;
						
						MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
					}
					break;
				case 0xFA:	// Loop 1
				case 0xFB:	// Loop 2
					LpStkIdx = CurCmd & 0x01;
					if (! LoopAddr[LpStkIdx])
					{
						LoopAddr[LpStkIdx] = InPos;
						LoopCnt[LpStkIdx] = 0x00;
					}
					else
					{
						TempByt = KnmData[InPos];
						InPos ++;
						
						LoopCnt[LpStkIdx] ++;
						if (LoopCnt[LpStkIdx] < TempByt)
							InPos = LoopAddr[LpStkIdx];
						else
							LoopAddr[LpStkIdx] = 0x0000;
					}
					break;
				case 0xFC:	// GoSub/Return 1
				case 0xFD:	// GoSub/Return 2
					LpStkIdx = CurCmd & 0x01;
					if (! StackAddr[LpStkIdx])
					{
						// GoSub
						TempSht = ReadLE16(&KnmData[InPos]);
						InPos += 0x02;
						
						StackAddr[LpStkIdx] = InPos;
						InPos = TempSht;
					}
					else
					{
						// Return from GoSub
						InPos = StackAddr[LpStkIdx];
						StackAddr[LpStkIdx] = 0x0000;
					}
					break;
				case 0xFE:	// 3-part Loop
					// Loop foramt:
					//	FE (start) data FE (marker) [data FE (end x)] data FE FE (final end)
					// The section in [] can be repeated infinitely.
					// The engine plays start..marker, [marker/end x-1..end x, start..marker,] end x-1..final end
					if (Loop3State == 0x00)
					{
						// Loop Start
						Loop3State = 0x01;
						Loop3Start = InPos;
					}
					else if (! (Loop3State & 0x01))
					{
						// take Loop Exit
						Loop3State = 0x00;
						InPos = Loop3End;
					}
					else if (! (Loop3State & 0x02))
					{
						// passing Loop Exit
						Loop3State |= 0x02;
						if (Loop3State & 0x04)
						{
							Loop3State &= ~0x04;
							InPos = Loop3End;
						}
					}
					else
					{
						// passing Loop End
						Loop3State &= ~0x02;
						Loop3State |= 0x04;
						if (KnmData[InPos] == 0xFE)
						{
							InPos ++;
							Loop3State &= ~0x01;	// this is the "real" loop end
						}	// else this is just a "mid loop end" marker
						Loop3End = InPos;
						InPos = Loop3Start;
					}
					break;
				case 0xFF:	// Track End
					TrkEnd = true;
					break;
				default:
					printf("Unknown event %02X on track %X\n", CurCmd, CurTrk);
					TrkEnd = true;
					break;
				/*case 0xF5:	// set YM2612 Timer B
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x70, 0x75);
					InPos ++;
					break;*/
				/*case 0xFE:	// Hold Note ("no attack" mode)
					HoldNote = true;
					break;*/
				}
			}
		}
		if (LastNote != 0xFF)
			WriteEvent(MidData, &DstPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
		
		WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
		
		WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
}

static void PreparseKnm(UINT32 KnmLen, const UINT8* KnmData, UINT8* KnmBuf, TRK_INFO* TrkInf, UINT8 Mode)
{
	// Note: KnmBuf is a temporary buffer with a size of KnmLen bytes.
	//       It is used to find loops by marking processed bytes.
	//       A loop is found when a GoTo jumps to an already processed byte.
	//
	//       The buffer has to be allocated by the calling function to speed the program
	//       up by saving a few mallocs.
	
	UINT16 InPos;
	UINT8 CurCmd;
	UINT8 ChnFlags;
	UINT8 DelayAdd;
	UINT8 StackPos;
	UINT8 LpStkIdx;
	UINT16 LoopAddr[2];
	UINT8 LoopCnt[2];
	UINT16 Loop3Start;
	UINT16 Loop3End;
	UINT8 Loop3State;
	UINT16 StackAddr[2];
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 CmdLen;
	UINT8 Mask;
	UINT16 MaskMinPos[0x04];
	UINT16 MaskMaxPos[0x04];
	
	if (! Mode)
		TrkInf->LoopPos = 0x0000;
	//if (! (TrkInf->Flags & 0x80))
	//	return;	// Track inactive - return
	
	if (! (Mode & 0x01))
		memset(KnmBuf, 0x00, KnmLen);
	InPos = TrkInf->StartPos;
	ChnFlags = 0x00;
	DelayAdd = 0;
	StackPos = 0x00;
	Mask = 0x01;
	LoopAddr[0] = LoopAddr[1] = 0x0000;
	LoopCnt[0] = LoopCnt[1] = 0x00;
	Loop3State = 0x00;
	StackAddr[0] = StackAddr[1] = 0x0000;
	MaskMinPos[StackPos] = InPos;
	MaskMaxPos[StackPos] = InPos;
	
	while(InPos < KnmLen)
	{
		if ((Mode & 0x01) && InPos == TrkInf->LoopPos)
			return;
		
		CurCmd = KnmData[InPos];
		KnmBuf[InPos] |= Mask;
		InPos ++;
		if (CurCmd < 0xD0 || (ChnFlags & 0x80))
		{
			ChnFlags &= ~0x80;
			
			TempByt = (CurCmd & 0x0F) >> 0;
			if (! TempByt)
				TempByt = 0x10;
			TempByt += DelayAdd;
			DelayAdd = 0;
			
			if (! (ChnFlags & 0x04))
				TempByt *= 3;
			if (! (ChnFlags & 0x02))
				TempByt *= 2;
			if (ChnFlags & 0x08)
			{
				ChnFlags &= ~0x08;
				//TempByt -= TrkRAM3D;
			}
			if (! (Mode & 0x01))
				TrkInf->TickCnt += TempByt;
			else
				TrkInf->LoopTick += TempByt;
		}
		else
		{
			CmdLen = 0x00;
			switch(CurCmd)
			{
			case 0xD0:	// set Volume
			case 0xD1:
			case 0xD2:
			case 0xD3:
			case 0xD4:
			case 0xD5:
			case 0xD6:
			case 0xD7:
				break;
			case 0xD8:	// set Volume Multiplier
				CmdLen = 0x01;
				break;
			case 0xD9:	// delay extention
				DelayAdd += 0x10;
				while(KnmData[InPos] == CurCmd)
				{
					KnmBuf[InPos] |= Mask;
					DelayAdd += 0x10;
					InPos ++;
				}
				ChnFlags |= 0x80;
				break;
			case 0xDA:	// set Pan
				CmdLen = 0x01;
				break;
			case 0xDB:	// set TrkRAM+38h
				CmdLen = 0x01;
				break;
			case 0xDC:	// set SSG-EG
				CmdLen = 0x02;
				break;
			case 0xDD:	// toggle Delay Multiply 3
				ChnFlags ^= 0x04;
				break;
			case 0xDE:	// ??
				ChnFlags |= 0x08;
				break;
			case 0xDF:	// toggle Delay Multiply 2
				ChnFlags ^= 0x02;
				break;
			case 0xE0:	// FM Channel Setup
				Mode &= ~0x10;
				CmdLen = 0x04;
				break;
			case 0xE1:	// DAC Channel Setup
				Mode |= 0x10;
				CmdLen = 0x02;
				break;
			case 0xE2:	// set Track Tempo
				CmdLen = 0x01;
				break;
			case 0xE3:	// set Instrument
				if (Mode & 0x80)
					CmdLen = 0x02;
				else
					CmdLen = 0x01;
				break;
			case 0xE4:	// set Instrument ??
				CmdLen = 0x01;
				break;
			case 0xE5:	// Modulation?
				if (! KnmData[InPos])
					CmdLen = 0x01;
				else
					CmdLen = 0x02;
				break;
			case 0xE6:	// 
				CmdLen = 0x01;
				break;
			case 0xE7:	// 
				CmdLen = 0x01;
				break;
			case 0xE8:	// enforce note processing
				ChnFlags |= 0x80;
				break;
			case 0xE9:	// set LFO Depth
				if (! KnmData[InPos])
					CmdLen = 0x01;
				else
					CmdLen = 0x02;
				break;
			case 0xEA:	// set LFO rate
				CmdLen = 0x01;
				break;
			case 0xEB:	// Global Transpose
				CmdLen = 0x01;
				break;
			case 0xEC:	// Channel Transpose
				CmdLen = 0x01;
				break;
			case 0xED:	// set Detune
				CmdLen = 0x01;
				break;
			case 0xEE:	// 
				CmdLen = 0x02;
				break;
			case 0xEF:	// some volume stuff
				CmdLen = 0x02;
				break;
			case 0xF0:	// set Octave??
			case 0xF1:
			case 0xF2:
			case 0xF3:
			case 0xF4:
			case 0xF5:
			case 0xF6:
			case 0xF7:
				break;
			case 0xF8:	// 
				if (! (Mode & 0x80))
					CmdLen = 0x02;
				else
					CmdLen = 0x01;
				break;
			case 0xF9:	// GoTo
				if (MaskMaxPos[StackPos] < InPos)
					MaskMaxPos[StackPos] = InPos;
				TempSht = ReadLE16(&KnmData[InPos]);
				
				InPos = TempSht;
				if (InPos >= KnmLen)
					return;
				if (MaskMinPos[StackPos] > InPos)
					MaskMinPos[StackPos] = InPos;
				
				if (! (Mode & 0x01) && (KnmBuf[InPos] & Mask))
				{
					TrkInf->LoopPos = InPos;
					return;
				}
				break;
			case 0xFA:	// Loop 1
			case 0xFB:	// Loop 2
				LpStkIdx = CurCmd & 0x01;
				if (! LoopAddr[LpStkIdx])
				{
					LoopAddr[LpStkIdx] = InPos;
					LoopCnt[LpStkIdx] = 0x00;
				}
				else
				{
					TempByt = KnmData[InPos];
					InPos ++;
					
					LoopCnt[LpStkIdx] ++;
					if (LoopCnt[LpStkIdx] < TempByt)
						InPos = LoopAddr[LpStkIdx];
					else
						LoopAddr[LpStkIdx] = 0x0000;
				}
				break;
			case 0xFC:	// GoSub/Return 1
			case 0xFD:	// GoSub/Return 2
				LpStkIdx = CurCmd & 0x01;
				if (! StackAddr[LpStkIdx])
				{
					// GoSub
					TempSht = ReadLE16(&KnmData[InPos]);
					InPos += 0x02;
					
					StackAddr[LpStkIdx] = InPos;
					StackPos ++;
					Mask <<= 1;
					InPos = TempSht;
					
					MaskMinPos[StackPos] = InPos;
					MaskMaxPos[StackPos] = InPos;
				}
				else
				{
					// Return from GoSub
					if (MaskMaxPos[StackPos] < InPos)
						MaskMaxPos[StackPos] = InPos;
					for (InPos = MaskMinPos[StackPos]; InPos < MaskMaxPos[StackPos]; InPos ++)
						KnmBuf[InPos] &= ~Mask;	// remove usage mask of this subroutine
					Mask >>= 1;
					StackPos --;
					
					InPos = StackAddr[LpStkIdx];
					StackAddr[LpStkIdx] = 0x0000;
				}
				break;
			case 0xFE:	// 3-part Loop
				if (Loop3State == 0x00)
				{
					// Loop Start
					Loop3State = 0x01;
					Loop3Start = InPos;
				}
				else if (! (Loop3State & 0x01))
				{
					// take Loop Exit
					Loop3State = 0x00;
					InPos = Loop3End;
				}
				else if (! (Loop3State & 0x02))
				{
					// passing Loop Exit
					Loop3State |= 0x02;
					if (Loop3State & 0x04)
					{
						Loop3State &= ~0x04;
						InPos = Loop3End;
					}
				}
				else
				{
					// passing Loop End
					Loop3State &= ~0x02;
					Loop3State |= 0x04;
					if (KnmData[InPos] == 0xFE)
					{
						InPos ++;
						Loop3State &= ~0x01;
					}
					Loop3End = InPos;
					InPos = Loop3Start;
				}
				break;
			case 0xFF:	// Track End
				return;
			default:
				return;
			}
			for (TempByt = 0x00; TempByt < CmdLen; TempByt ++, InPos ++)
				KnmBuf[InPos] |= Mask;
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
			
			TempTInf->LoopTimes = (UINT16)((TrkLen + TrkLoopLen / 3) / TrkLoopLen);
			printf("\nTrk %u: Extended loop to %u times", CurTrk, TempTInf->LoopTimes);
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

static UINT16 ReadLE16(const UINT8* Buffer)
{
	return	(Buffer[0x01] << 8) |
			(Buffer[0x00] << 0);
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

static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0;
	
	Buffer[*Pos + 0x00] = 0xFF;
	Buffer[*Pos + 0x01] = MetaType;
	*Pos += 0x02;
	WriteMidiValue(Buffer, Pos, DataLen);
	memcpy(Buffer + *Pos, Data, DataLen);
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

// read byte in Sign-Magnitude format
static INT8 GetSignMagByte(UINT8 value)
{
	if (value & 0x80)
		return 0x00 - (value & 0x7F);
	else
		return 0x00 + (value & 0x7F);
}

// read byte for Note Transposition (bit 7 = sign, bits 4-6 = octave, bits 0-3 = note)
static INT8 GetTranspByte(UINT8 value)
{
	INT8 note;
	INT8 octave;
	
	note = (value & 0x0F) >> 0;
	octave = (value & 0x70) >> 4;
	if (! (value & 0x80))
		return octave * 12 + note;
	else
		return -(octave * 12 + note);
}

static UINT32 TickInc2MidiTempo(UINT8 tickInc)
{
	UINT32 baseTempo;
	
	// Note: Timing is 1 tick = 1 frame (60 Hz)
	// BPM = 3600 Ticks/min / 24 Ticks/Quarter
	// 3600 / 24 = 150 BPM
	// 150 BPM == MIDI Tempo 400 000
	baseTempo = 50000 * TickpQrtr / 3;	// 1 000 000 * Tick/Qrtr / 60
	if (! tickInc)	// actually this would cause the song to hang
		return baseTempo;
	return baseTempo * 0x100 / tickInc;
}

static float OPN2DB(UINT8 TL, UINT8 PanMode)
{
	if (TL >= 0x7F)
		return -999.9f;
	if (PanMode & 0x01)
		TL += 0x04;
	if (false)
	{
		if (TL >= 8)
			TL -= 8;
		else
			TL = 0;
	}
	return -(TL * 3 / 4.0f);	// 8 steps per 6 db
}

static float PSG2DB(UINT8 Vol)
{
	if (Vol >= 0x0F)	// 0x78-0x7F -> PSG volume 0x0F == silence
		return -999.9f;
	//return -(Vol / 8 * 2.0f);
	return -(Vol / 4.0f);	// 3 PSG steps per 6 db, makes 24 internal steps per 6 db
}

static UINT8 DB2Mid(float DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}


void SaveInsAsGYB(const char* FileName, const UINT8* InsData)
{
	const UINT8 INS_REG_MAP[0x20] =
	{	0x01, 0x0D, 0x07, 0x13,		// 30-3C
		0x02, 0x0E, 0x08, 0x14,		// 40-4C
		0x03, 0x0F, 0x09, 0x15,		// 50-5C
		0x04, 0x10, 0x0A, 0x16,		// 60-6C
		0x05, 0x11, 0x0B, 0x17,		// 70-7C
		0x06, 0x12, 0x0C, 0x18,		// 80-8C
		0xFF, 0xFF, 0xFF, 0xFF,		// 90-9C
		0x00, 0xFF, 0xFF, 0xFF};	// B0, B4, Extra, Padding
	FILE* hFile;
	UINT8 InsCount;
	UINT8 CurIns;
	UINT8 CurReg;
	const UINT8* InsPtr;
	char TempStr[0x80];
	UINT8 GybIns[0x20];	// GYB instrument data buffer
	
	InsPtr = InsData;
	for (InsCount = 0x00; InsCount < 0xFF; InsCount ++, InsPtr += 0x19)
	{
		if (InsPtr[0x02] >= 0x80)	// check unused bits in 40 register
			break;	// if set - exit
	}
	printf("Instruments counted: 0x%02X\n", InsCount);
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", FileName);
		return;
	}
	
	// Write Header
	fputc(26, hFile);	// Signature Byte 1
	fputc(12, hFile);	// Signature Byte 2
	fputc(0x02, hFile);	// Version
	fputc(InsCount, hFile);	// Melody Instruments
	fputc(0x00, hFile);		// Drum Instruments
	
	// Write Mappings
	for (CurIns = 0x00; CurIns < InsCount && CurIns < 0x80; CurIns ++)
	{
		fputc(CurIns, hFile);	// GM Mapping: Melody
		fputc(0xFF, hFile);		// GM Mapping: Drum
	}
	for (; CurIns < 0x80; CurIns ++)
	{
		fputc(0xFF, hFile);
		fputc(0xFF, hFile);
	}
	
	fputc(0x00, hFile);	// LFO Value
	
	// Write Instrument Data
	InsPtr = InsData;
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++, InsPtr += 0x19)
	{
		for (CurReg = 0x00; CurReg < 0x20; CurReg ++)
		{
			if (INS_REG_MAP[CurReg] == 0xFF)
				GybIns[CurReg] = 0x00;
			else
				GybIns[CurReg] = InsPtr[INS_REG_MAP[CurReg]];
		}
		fwrite(GybIns, 0x01, 0x20, hFile);
	}
	
	// Write Instrument Names
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++)
	{
		sprintf(TempStr, "Instrument %02X", CurIns);
		
		CurReg = (UINT8)strlen(TempStr);
		fputc(CurReg, hFile);
		fwrite(TempStr, 0x01, CurReg, hFile);
	}
	
	fputc(0x00, hFile);	// Fake Checksum
	fputc(0x00, hFile);
	fputc(0x00, hFile);
	fputc(0x00, hFile);
	
	fclose(hFile);
	printf("Done.\n");
	
	return;
}
