// Konami MD -> Midi Converter
// ---------------------------
// Written by Valley Bell, 14 June 2016
// Improved on 23 June 2024 and 07 July 2024
// based on GRC -> Midi Converter

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "stdtype.h"
#include "stdbool.h"

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


#include "midi_funcs.h"

typedef struct _track_info
{
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
} TRK_INF;

#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


static UINT16 DetectSongCount(UINT32 DataLen, const UINT8* Data, UINT32 MusBankList, UINT32 MusPtrOfs);
UINT8 Konami2Mid(UINT32 KnmLen, UINT8* KnmData, UINT16 KnmAddr/*, UINT32* OutLen, UINT8** OutData*/);
static void PreparseKnm(UINT32 KnmLen, const UINT8* KnmData, UINT8* KnmBuf, TRK_INF* TrkInf, UINT8 Mode);
static UINT16 ReadLE16(const UINT8* Buffer);
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



static UINT32 MidLen;
static UINT8* MidData;
static bool HighTickRate;
static UINT16 TickpQrtr;
static UINT16 DefLoopCount;
static bool OptVolWrites;
static bool NoLoopExt;

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
	
	printf("Konami MD -> Midi Converter\n---------------------------\n");
	if (argc < 2)
	{
		printf("Usage: KonamiMD2Mid.exe [-Mode] [-Options] ROM.bin MusicListAddr(hex) MusicBankList(hex) [Song Count]\n");
		printf("Modes:\n");
		printf("    -mus        Music Mode (convert sequences to MID)\n");
		printf("    -ins        Instrument Mode (dump instruments to GYB)\n");
		//printf("    -dac        DAC Mode (dump DAC sounds to RAW)\n");
		printf("Options:\n");
		printf("    -OptVol     Optimize Volume writes (omits redundant ones)\n");
		printf("    -HTR        high tick rate (120 Hz base rate, needed by Rocket Knight Adv.)\n");
		printf("    -TpQ n      Sets the number of Ticks per Quarter to n. (default: 24)\n");
		printf("                Use values like 18 or 32 on songs with broken tempo.\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		return 0;
	}
	
	OptVolWrites = true;
	HighTickRate = false;
	TickpQrtr = 24;
	DefLoopCount = 2;
	NoLoopExt = false;
	
	Mode = MODE_MUS;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Mus"))
			Mode = MODE_MUS;
		else if (! stricmp(argv[argbase] + 1, "DAC"))
			Mode = MODE_DAC;
		else if (! stricmp(argv[argbase] + 1, "Ins"))
			Mode = MODE_INS;
		else if (! stricmp(argv[argbase] + 1, "OptVol"))
			OptVolWrites = true;
		else if (! stricmp(argv[argbase] + 1, "HTR"))
			HighTickRate = true;
		else if (! stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				TickpQrtr = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! TickpQrtr)
					TickpQrtr = 24;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				DefLoopCount = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! DefLoopCount)
					DefLoopCount = 2;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "NoLpExt"))
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
	UINT32 BankBit;
	
	SongPos = (Data[MusBankList] << 15) | (MusPtrOfs & 0x7FFF);
	BankBit = ReadLE16(&Data[SongPos]) & 0x8000;
	for (CurPos = MusBankList; CurPos < DataLen; CurPos ++, MusPtrOfs += 0x12)
	{
		BankBase = (Data[CurPos] << 15);
		SongPos = BankBase | (MusPtrOfs & 0x7FFF);
		if (SongPos >= DataLen)
			break;
		if ((ReadLE16(&Data[SongPos]) & 0x8000) != BankBit)
			break;
	}
	CurPos -= MusBankList;
	
	printf("Songs detected: 0x%02X (%u)\n", CurPos, CurPos);
	return (UINT16)CurPos;
}

static void WriteEvent_Chn(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 chn, UINT8 evt, UINT8 val1, UINT8 val2)
{
	// write event to the channel specified by the "chn" parameter
	UINT8 chnBak = MTS->midChn;
	MTS->midChn = chn;
	WriteEvent(fInf, MTS, evt, val1, val2);
	MTS->midChn = chnBak;
	return;
}

UINT8 Konami2Mid(UINT32 KnmLen, UINT8* KnmData, UINT16 KnmAddr/*, UINT32* OutLen, UINT8** OutData*/)
{
	UINT8* TempBuf;
	TRK_INF TrkInf[0x09];
	TRK_INF* TempTInf;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 InPos;
	UINT8 ChnMode;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 TempArr[0x04];
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 ChnFlags;
	UINT8 DelayAdd;
	UINT8 GraceLen;
	UINT8 NoteLenFrac;
	UINT8 VolMult;
	UINT8 NoteChn;
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
	UINT8 PortaActive;
	UINT8 LpStkIdx;
	UINT16 LoopAddr[2];
	UINT8 LoopCnt[2];
	UINT16 Loop3Start;
	UINT16 Loop3End;
	UINT8 Loop3State;
	UINT16 StackAddr[2];
	UINT16 MstLoopCnt;
	UINT8 cmdEE_Cntr;
	UINT16 cmdEE_Ptr;
	INT8 cmdEE_VolMod;
	INT8 cmdEE_NoteMod;
	
	TrkCnt = 0x09;
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x0001, TrkCnt, TickpQrtr);
	
#if 0
	// write Master Track
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.midChn = 0x00;
	
	TempLng = TickInc2MidiTempo(0x00);
	WriteBE32(TempArr, TempLng);
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
#endif
	
	// Read Header
	TempBuf = (UINT8*)malloc(KnmLen);
	InPos = KnmAddr;
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++, InPos += 0x02)
	{
		TempTInf = &TrkInf[CurTrk];
		TempTInf->startOfs = ReadLE16(&KnmData[InPos]);
		TempTInf->loopOfs = 0x0000;
		TempTInf->tickCnt = 0;
		TempTInf->loopTick = 0;
		ChnMode = CHN_MASK[CurTrk] & 0x80;
		
		PreparseKnm(KnmLen, KnmData, TempBuf, TempTInf, ChnMode | 0x00);
		
		// If there is a loop, parse a second time to get the Loop Tick.
		if (TempTInf->loopOfs)
			PreparseKnm(KnmLen, KnmData, TempBuf, TempTInf, ChnMode | 0x01);
		TempTInf->loopTimes = TempTInf->loopOfs ? DefLoopCount : 0;
	}
	free(TempBuf);	TempBuf = NULL;
	
	if (! NoLoopExt)
		BalanceTrackTimes(TrkCnt, TrkInf, TickpQrtr / 4, 0xFF);
	
	// --- Main Conversion ---
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		TrkEnd = false;
		InPos = TempTInf->startOfs & 0x7FFF;
		
		ChnMode = CHN_MASK[CurTrk];
		if (ChnMode & 0x80)
			MTS.midChn = 0x0A + (CurTrk - 0x06);
		else
			MTS.midChn = CurTrk;
		NoteChn = MTS.midChn;
		VolMult = 1;
		ChnVol = 0x00;
		NoteVol = 0x00;
		ChnIns = 0x00;
		PanReg = 0x00;
		DelayAdd = 0;
		GraceLen = 0;
		NoteLenFrac = 0;
		PortaActive = 0x00;
		
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
		cmdEE_Cntr = 0;
		cmdEE_VolMod = 0;
		cmdEE_NoteMod = 0;
		StackAddr[0] = StackAddr[1] = 0x0000;
		MstLoopCnt = 0xFFFF;
		
		while(! TrkEnd && InPos < KnmLen)
		{
			if (MstLoopCnt == 0xFFFF && InPos == TempTInf->loopOfs)
			{
				MstLoopCnt ++;
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)MstLoopCnt);
				MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
			}
			
			CurCmd = KnmData[InPos];
			InPos ++;
			if (CurCmd < 0xD0 || (ChnFlags & 0x90))
			{
				UINT8 NoteDelay;
				UINT8 NoteLen;
				
				ChnFlags &= ~0x80;
				CurNote = (CurCmd & 0xF0) >> 4;
				if (CurNote == 0x00)
				{
					CurNote = 0xFF;
				}
				else
				{
					CurNote = CurOctave * 12 + ChnTransp + cmdEE_NoteMod + (CurNote - 1);
					if (CurNote >= 0x6C)	// the sound driver has only 108 notes defined
					{
						printf("\nWarning at 0x%04X: Out-of-range note %u!", InPos - 0x01, CurNote);
						CurNote = 0x6B;
					}
					if (MTS.midChn == 0x09)
						CurNote += 36;
					else
						CurNote += 12;
				}
				
				if (CurNote == 0xFF || MTS.midChn >= 0x09)
				{
					// rest: always turn off
					// drum channels: always retrigger
					if (PortaActive)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);	// Portamento Off
						PortaActive = 0x00;
					}
					if (LastNote != 0xFF)
					{
						WriteEvent_Chn(&midFileInf, &MTS, NoteChn, 0x90, LastNote, 0x00);
					}
					if (CurNote != 0xFF)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, CurNote, MidNoteVol);
						NoteChn = MTS.midChn;
					}
				}
				else
				{
					if (LastNote == 0xFF)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, CurNote, MidNoteVol);
						NoteChn = MTS.midChn;
					}
					else if (LastNote != CurNote)
					{
						if (! PortaActive)
						{
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x7F);	// Portamento On
							PortaActive = 0x01;
						}
						WriteEvent_Chn(&midFileInf, &MTS, NoteChn, 0x90, LastNote, 0x00);
						WriteEvent(&midFileInf, &MTS, 0x90, CurNote, MidNoteVol);
						NoteChn = MTS.midChn;
					}
					// else (CurNote != 0xFF && LastNote == CurNote) -> just continue the existing note
					else
					{
						// write informational Portamento Controller (but in a way that doesn't have an audible effect)
						if (PortaActive)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x41);
						else
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x01);
					}
				}
				LastNote = CurNote;
				
				NoteDelay = (CurCmd & 0x0F) >> 0;
				if (ChnFlags & 0x10)
				{
					// handle grace note
					ChnFlags &= ~0x10;
					ChnFlags |= 0x08;
					GraceLen = NoteDelay;
				}
				else
				{
					if (! NoteDelay)
						NoteDelay = 0x10;
					NoteDelay += DelayAdd;
					DelayAdd = 0;
					
					if (! (ChnFlags & 0x04))
						NoteDelay *= 3;
					if (! (ChnFlags & 0x02))
						NoteDelay *= 2;
					if (ChnFlags & 0x08)
					{
						ChnFlags &= ~0x08;
						NoteDelay -= GraceLen;	// subtract grace note delay
					}
				}
				if (NoteLenFrac == 0)
				{
					NoteLen = NoteDelay;
				}
				else
				{
					UINT8 RemTicksEnd = ((UINT16)NoteDelay * NoteLenFrac) >> 8;
					if (RemTicksEnd == 0)
						RemTicksEnd = 1;
					NoteLen = NoteDelay - RemTicksEnd;
					if (MTS.midChn >= 0x09)
						NoteLen = 0;	// don't stop early for drum/PSG
				}
				if (NoteLen > 0 && NoteLen < NoteDelay)
				{
					// Note: on PSG channels, the "note length" seems to trigger the "release" phase,
					// but this is often very long, so let's just ignore it in this case.
					MTS.curDly += NoteLen;
					NoteDelay -= NoteLen;
					if (PortaActive)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);	// Portamento Off
						PortaActive = 0x00;
					}
					WriteEvent_Chn(&midFileInf, &MTS, NoteChn, 0x90, LastNote, 0x00);
					LastNote = 0xFF;
				}
				MTS.curDly += NoteDelay;
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
				case 0xD9:	// delay extension
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
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, TempByt);
						
						TempByt = (TempByt == 0x40) ? 0x00 : 0x01;
						if (TempByt != PanMode)
						{
							PanMode = TempByt;
							TempByt = DB2Mid(OPN2DB(ChnVol, PanMode));
							if (TempByt != MidChnVol)
							{
								MidChnVol = TempByt;
								WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, MidChnVol);
							}
						}
					}
					else
					{
						printf("\nWarning at 0x%04X: Pan Envelope used!", InPos - 0x01);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, 0x3F);
					}
					break;
				case 0xDB:	// set Note Length fraction
					NoteLenFrac = -KnmData[InPos];
					InPos ++;
					break;
				case 0xDC:	// set SSG-EG
					InPos += 0x02;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					break;
				case 0xDD:	// toggle Delay Multiply 3
					ChnFlags ^= 0x04;
					break;
				case 0xDE:	// enable "grace note" mode
					ChnFlags |= 0x18;
					break;
				case 0xDF:	// toggle Delay Multiply 2
					ChnFlags ^= 0x02;
					break;
				case 0xE0:	// FM Channel Setup
					ChnMode &= ~0x10;
					if (ChnMode & 0x80)
						MTS.midChn = 0x0A + (CurTrk - 0x06);
					else
						MTS.midChn = CurTrk;
					
					// set Tempo
					TempByt = KnmData[InPos];	InPos ++;
					TempLng = TickInc2MidiTempo(TempByt);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
					
					if (PanMode & 0x80)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, 0x40);
						PanMode = 0x00;
					}
					
					// set Instrument
					ChnIns = KnmData[InPos];	InPos ++;
					WriteEvent(&midFileInf, &MTS, 0xC0, ChnIns & 0x7F, 0x00);
					
					ChnVol = KnmData[InPos];	InPos ++;
					if (! (ChnMode & 0x80))
						TempByt = DB2Mid(OPN2DB(ChnVol, PanMode));
					else
						TempByt = DB2Mid(PSG2DB(ChnVol));
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, MidChnVol);
					}
					
					NoteLenFrac = -KnmData[InPos];	InPos ++;
					break;
				case 0xE1:	// Rhythm Channel Setup
					ChnMode |= 0x10;
					MTS.midChn = 0x09;
					
					TempByt = KnmData[InPos];	InPos ++;
					TempLng = TickInc2MidiTempo(TempByt);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
					
					if (PanMode & 0x80)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, 0x40);
						PanMode = 0x00;
					}
					WriteEvent(&midFileInf, &MTS, 0xC0, 0x00, 0x00);	// set Drum Kit
					
					TempByt = KnmData[InPos];	InPos ++;
					break;
				case 0xE2:	// set Track Tempo
					TempByt = KnmData[InPos];	InPos ++;
					TempLng = TickInc2MidiTempo(TempByt);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
					break;
				case 0xE3:	// set Instrument
					if (ChnMode & 0x80)
						InPos ++;
					ChnIns = KnmData[InPos];	InPos ++;
					WriteEvent(&midFileInf, &MTS, 0xC0, ChnIns & 0x7F, 0x00);
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
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, MidChnVol);
					}
					break;
				case 0xE5:	// Frequency Envelope
					TempByt = KnmData[InPos];
					if (! TempByt)
						InPos ++;
					else
						InPos += 0x02;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
					break;
				case 0xE6:	// ?? Envelope
					TempByt = KnmData[InPos];
					InPos ++;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
					break;
				case 0xE7:	// 
					TempByt = -KnmData[InPos];
					InPos ++;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
					break;
				case 0xE8:	// enforce note processing
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
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
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
					break;
				case 0xEA:	// set LFO rate
					TempByt = KnmData[InPos];
					if (TempByt)
						TempByt += 0x07;
					InPos ++;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
					break;
				case 0xEB:	// Global Transpose
					TempByt = KnmData[InPos];
					InPos ++;
					GblTransp = GetTranspByte(TempByt);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
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
					
					WriteEvent(&midFileInf, &MTS, 0xE0, TempSht & 0x7F, (TempSht >> 7) & 0x7F);
					break;
				case 0xEE:	// volume/transpose loop
					if (cmdEE_Cntr == 0)
					{
						cmdEE_Cntr = 1;
						cmdEE_Ptr = InPos;
						cmdEE_VolMod = 0;
						cmdEE_NoteMod = 0;
						InPos += 0x03;
					}
					else if (cmdEE_Cntr < KnmData[cmdEE_Ptr])
					{
						INT8 pitchInc = GetSignMagByte(KnmData[cmdEE_Ptr + 0x01]);
						INT8 volDec = GetSignMagByte(KnmData[cmdEE_Ptr + 0x02]);
						cmdEE_Cntr ++;
						cmdEE_NoteMod += pitchInc;
						cmdEE_VolMod -= volDec;
						InPos = cmdEE_Ptr + 0x03;
						
						if (volDec != 0)
						{
							if (! (ChnMode & 0x80))
								TempByt = DB2Mid(OPN2DB(cmdEE_VolMod, 0x00));
							else
								TempByt = DB2Mid(PSG2DB(cmdEE_VolMod));
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, TempByt);
						}
					}
					else
					{
						if (cmdEE_VolMod != 0)
						{
							if (! (ChnMode & 0x80))
								TempByt = DB2Mid(OPN2DB(cmdEE_VolMod, 0x00));
							else
								TempByt = DB2Mid(PSG2DB(cmdEE_VolMod));
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x0B, TempByt);
						}
						
						// no parameters
						cmdEE_Cntr = 0;
						cmdEE_VolMod = 0;
						cmdEE_NoteMod = 0;
					}
					break;
				case 0xEF:	// set global FM/PSG volume
					// just ignore for now
					InPos += 0x02;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					break;
				case 0xF0:	// set Octave
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
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, CurCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, TempByt & 0x7F);
					break;
				case 0xF9:	// GoTo
					TempSht = ReadLE16(&KnmData[InPos]) ^ (TempTInf->startOfs & 0x8000);
					if (TempSht >= KnmLen)
					{
						printf("\nError at 0x%04X, track %u: Event %02X jumps to invalid offset 0x%04X!",
							InPos - 0x01, CurTrk, CurCmd, TempSht);
						TrkEnd = true;
						break;
					}
					InPos = TempSht;
					
					if (InPos == TempTInf->loopOfs)
					{
						if (MstLoopCnt == 0xFFFF)
							MstLoopCnt = 0;
						MstLoopCnt ++;
						if (MstLoopCnt < 0x80)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)MstLoopCnt);
						
						if (MstLoopCnt >= TempTInf->loopTimes)
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
						TempSht = ReadLE16(&KnmData[InPos]) ^ (TempTInf->startOfs & 0x8000);
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
					printf("\nUnknown event %02X on track %u", CurCmd, CurTrk);
					TrkEnd = true;
					break;
				}
			}
		}
		if (LastNote != 0xFF)
			WriteEvent_Chn(&midFileInf, &MTS, NoteChn, 0x90, LastNote, 0x00);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseKnm(UINT32 KnmLen, const UINT8* KnmData, UINT8* KnmBuf, TRK_INF* TrkInf, UINT8 Mode)
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
	UINT8 cmdEE_Cntr;
	UINT16 cmdEE_Ptr;
	
	if (! Mode)
		TrkInf->loopOfs = 0x0000;
	
	if (! (Mode & 0x01))
		memset(KnmBuf, 0x00, KnmLen);
	InPos = TrkInf->startOfs & 0x7FFF;
	ChnFlags = 0x00;
	DelayAdd = 0;
	StackPos = 0x00;
	Mask = 0x01;
	LoopAddr[0] = LoopAddr[1] = 0x0000;
	LoopCnt[0] = LoopCnt[1] = 0x00;
	Loop3State = 0x00;
	cmdEE_Cntr = 0;
	StackAddr[0] = StackAddr[1] = 0x0000;
	MaskMinPos[StackPos] = InPos;
	MaskMaxPos[StackPos] = InPos;
	
	while(InPos < KnmLen)
	{
		if ((Mode & 0x01) && InPos == TrkInf->loopOfs)
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
				TrkInf->tickCnt += TempByt;
			else
				TrkInf->loopTick += TempByt;
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
			case 0xD9:	// delay extension
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
			case 0xDB:	// set Note Length fraction
				CmdLen = 0x01;
				break;
			case 0xDC:	// set SSG-EG
				CmdLen = 0x02;
				break;
			case 0xDD:	// toggle Delay Multiply 3
				ChnFlags ^= 0x04;
				break;
			case 0xDE:	// enable "grace note" mode
				ChnFlags |= 0x08;
				CmdLen = 0x01;	// just skip the note/delay, as we don't need grace note processing here
				break;
			case 0xDF:	// toggle Delay Multiply 2
				ChnFlags ^= 0x02;
				break;
			case 0xE0:	// FM Channel Setup
				Mode &= ~0x10;
				CmdLen = 0x04;
				break;
			case 0xE1:	// Rhythm Channel Setup
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
			case 0xE4:	// set Instrument Volume
				CmdLen = 0x01;
				break;
			case 0xE5:	// Frequency Envelope
				if (! KnmData[InPos])
					CmdLen = 0x01;
				else
					CmdLen = 0x02;
				break;
			case 0xE6:	// ?? Envelope
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
			case 0xEE:	// volume/transpose loop
				if (cmdEE_Cntr == 0)
				{
					cmdEE_Cntr = 1;
					cmdEE_Ptr = InPos;
					CmdLen = 0x03;
				}
				else if (cmdEE_Cntr < KnmData[cmdEE_Ptr])
				{
					cmdEE_Cntr ++;
					InPos = cmdEE_Ptr;
					CmdLen = 0x03;
				}
				else
				{
					cmdEE_Cntr = 0;
					// no parameters
				}
				break;
			case 0xEF:	// some volume stuff
				CmdLen = 0x02;
				break;
			case 0xF0:	// set Octave
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
				TempSht = ReadLE16(&KnmData[InPos]) ^ (TrkInf->startOfs & 0x8000);
				if (TempSht >= KnmLen)
					return;
				InPos = TempSht;
				
				if (MaskMinPos[StackPos] > InPos)
					MaskMinPos[StackPos] = InPos;
				
				if (! (Mode & 0x01) && (KnmBuf[InPos] & Mask))
				{
					TrkInf->loopOfs = InPos;
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
					TempSht = ReadLE16(&KnmData[InPos]) ^ (TrkInf->startOfs & 0x8000);
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

static UINT16 ReadLE16(const UINT8* Buffer)
{
	return	(Buffer[0x01] << 8) |
			(Buffer[0x00] << 0);
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
	if (HighTickRate)
		baseTempo /= 2;
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
	return TL * 3 / -4.0f;	// 8 steps per 6 db
}

static float PSG2DB(UINT8 Vol)
{
	if (Vol >= 0x0F)	// PSG volume 0x0F == silence
		return -999.9f;
	return Vol * -2.0f;	// 3 PSG steps per 6 db
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
