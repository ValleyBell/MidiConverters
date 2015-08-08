// Turbo OutRun -> Midi Converter
// ------------------------------
// Written by Valley Bell, 07 August 2015

#include <stdio.h>
#include <stdlib.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <memory.h>

#ifndef M_PI_2
#define M_PI_2	1.57079632679489661923 
#endif
#ifndef M_SQRT2
#define M_SQRT2	1.41421356237309504880
#endif

#include "stdtype.h"

#define INLINE	static __inline


int main(int argc, char* argv[]);
void ConvertTORun2MID(void);
INLINE UINT8 ArcSmpsVol2Mid(UINT8 TrkMode, UINT8 Vol, UINT8 PanBoost);
INLINE double FMVol2DB(UINT8 Vol);
static UINT8 GetPCMVol(const UINT8 VolL, const UINT8 VolR, const double VolMul);
static UINT8 GetPCMPan(const UINT8 VolL, const UINT8 VolR, double* RetVolFact);
INLINE double SegaPCMVol2DB(UINT8 Vol);
INLINE double Lin2DB(double LinVol);
INLINE UINT8 DB2Mid(double DB);
INLINE UINT32 Tempo2Mid(UINT8 TempoVal);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);


#pragma pack(1)
typedef struct _track_header
{
	UINT8 PbkFlags;
	UINT8 ChnFlags;
	UINT8 TickMul;
	UINT16 TrkPtr;
	UINT8 Transp;
	UINT8 ModEnv;
	UINT8 VolEnv;
	UINT8 Unknown;
} TRK_HDR;
#pragma pack()


#define USE_VELOCITY	1

static UINT32 SeqSize;
static UINT8* SeqData;
static UINT32 MidSize;
static UINT8* MidData;

static UINT16 MIDI_RES;
static UINT16 NUM_LOOPS;

int main(int argc, char* argv[])
{
	FILE* hFile;
	//char FileName[0x10];
	
	if (argc <= 2)
	{
		printf("Usage: toutrun2mid epr-12300.88 output.mid\n");
#ifdef _DEBUG
		_getch();
#endif
		return 0;
	}
	
	MIDI_RES = 48;
	NUM_LOOPS = 2;
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
		return 1;
	
	fseek(hFile, 0, SEEK_END);
	SeqSize = ftell(hFile);
	
	SeqData = (UINT8*)malloc(SeqSize);
	fseek(hFile, 0, SEEK_SET);
	fread(SeqData, 0x01, SeqSize, hFile);
	
	fclose(hFile);
	
	printf("Converting %s ...\n", argv[1]);
	MidSize = 0x100000;
	MidData = (UINT8*)malloc(MidSize);
	ConvertTORun2MID();
	
	hFile = fopen(argv[2], "wb");
	if (hFile == NULL)
	{
		printf("Error saving %s!\n", argv[2]);
	}
	else
	{
		fwrite(MidData, 0x01, MidSize, hFile);
		fclose(hFile);
	}
	free(MidData);	MidData = NULL;
	free(SeqData);	SeqData = NULL;
	
	printf("Done.\n");
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

void ConvertTORun2MID(void)
{
	UINT8 DRUM_MAP[0x20];
	static const UINT16 SEQ_TABLE = 0x073A;
	static const UINT8 SONG_ID = 0x96;
	UINT16 TrkHdrPos;
	TRK_HDR* TrkHdrs;
	UINT16 SeqPos;
	UINT32 MidPos;
	UINT8 DefTempo;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT32 MidTrkBase;
	UINT32 CurDly;
	UINT8 MidChn;
	//UINT8 TrkEnd;
	UINT8 MstLoopCnt;
	UINT8 TrkMode;	// 00 - FM, 01 - PCM
	INT8 NoteMove;
	UINT8 TrkPbFlags;
	
	UINT8 CurCmd;
	UINT8 CurNote;
	UINT8 CurNoteVol;
	UINT8 CurChnVol;
	UINT8 CurPcmVolL;
	UINT8 CurPcmVolR;
	double CurPcmVolMul;
	UINT8 ChnPanOn;
	UINT8 CurTickMul;
	UINT8 CurIns;
	UINT8 LastNote;
	UINT8 HoldNote;
	
	UINT8 LoopIdx;
	UINT8 LoopCnt[0x10];
	UINT8 GosubID;
	UINT16 GosubPtr[0x10];
	
	UINT8 TempByt;
	INT16 TempPos;
	UINT32 TempLng;
	UINT8 TempArr[0x20];
	
	for (TempByt = 0x00; TempByt < 0x20; TempByt ++)
		DRUM_MAP[TempByt] = 0xFF;
	DRUM_MAP[0x01] = 0x31;	// Cymbal
	DRUM_MAP[0x02] = 0x37;	// Splash Cymbal
	DRUM_MAP[0x03] = 0x33;	// Open Hi-Hat 2
	DRUM_MAP[0x04] = 0x2E;	// Open Hi-Hat
	DRUM_MAP[0x05] = 0x28;	// Snare
	DRUM_MAP[0x06] = 0x26;	// Dance Snare
	DRUM_MAP[0x07] = 0x23;	// Bass Drum
	DRUM_MAP[0x08] = 0x24;	// Bass Drum (with reverb)
	DRUM_MAP[0x09] = 0x45;	// Shaker
	
	DRUM_MAP[0x0A] = 0x2B;	// Tom L
	DRUM_MAP[0x0B] = 0x2D;	// Tom M
	DRUM_MAP[0x0C] = 0x30;	// Tom H
	DRUM_MAP[0x0D] = 0x2A;	// Closed Hi-Hat
	DRUM_MAP[0x0E] = 0x36;	// Cowbell
	DRUM_MAP[0x0F] = 0x27;	// Clap
	
	SeqPos = SEQ_TABLE + 2 * (SONG_ID - 0x80);
	TrkHdrPos = ReadLE16(&SeqData[SeqPos]);
	SeqPos = TrkHdrPos;
	if (SONG_ID >= 0x90 && SONG_ID <= 0x98)
	{
		DefTempo = SeqData[SeqPos];	SeqPos ++;
	}
	TrkCnt = SeqData[SeqPos];	SeqPos ++;
	TrkHdrs = (TRK_HDR*)&SeqData[SeqPos];
	/*if (! TrkHdrPos)
	{
		printf("Unable to find sequence header!\n");
		return;
	}*/
	
	SeqPos = TrkHdrPos;
	
	MidPos = 0x00;
	WriteBE32(&MidData[MidPos], 0x4D546864);	MidPos += 0x04;	// 'MThd' Signature
	WriteBE32(&MidData[MidPos], 0x00000006);	MidPos += 0x04;	// Header Size
	WriteBE16(&MidData[MidPos], 0x0001);		MidPos += 0x02;	// Format: 1
	WriteBE16(&MidData[MidPos], 1+TrkCnt);		MidPos += 0x02;	// Tracks (+1 for tempo)
	WriteBE16(&MidData[MidPos], MIDI_RES);		MidPos += 0x02;	// Ticks per Quarter
	
#if 1
	WriteBE32(&MidData[MidPos], 0x4D54726B);	MidPos += 0x04;	// 'MTrk' Signature
	WriteBE32(&MidData[MidPos], 0x00000000);	MidPos += 0x04;	// Track Size
	MidTrkBase = MidPos;
	CurDly = 0;
	
	TempLng = Tempo2Mid(DefTempo);	// default tempo value
	WriteBE32(TempArr, TempLng);
	WriteMetaEvent_Data(MidData, &MidPos, &CurDly, 0x51, 0x03, &TempArr[1]);
	
	WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
#endif
	
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++)
	{
		WriteBE32(&MidData[MidPos], 0x4D54726B);	// write 'MTrk'
		MidPos += 0x08;
		MidTrkBase = MidPos;
		
		SeqPos = TrkHdrs[CurTrk].TrkPtr;
		TrkMode = TrkHdrs[CurTrk].ChnFlags;
		if (TrkHdrs[CurTrk].ChnFlags & 0x40)		// SegaPCM track
		{
			TrkMode = 1;
			if ((CurTrk & 0x07) < 3)
				MidChn = 10 + (CurTrk & 0x07);
			else
				MidChn = 9;
		}
		else	// FM track
		{
			TrkMode = 0;
			MidChn = 0 + (CurTrk & 0x07);
		}
		printf("Track %u ...\n", CurTrk);
		
		CurDly = 0;
		//TrkEnd = ! (TrkHdrs[CurTrk].PbkFlags & 0x80);
		TrkPbFlags = TrkHdrs[CurTrk].PbkFlags;
		MstLoopCnt = 0x00;
		//NoteMove = TrkMode ? +12 : 0;
		NoteMove = TrkHdrs[CurTrk].Transp*0;
		if (CurTrk == 9 || CurTrk == 10)
			NoteMove += 24;
		else if (CurTrk == 8)
			NoteMove -= 12;
		//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x65, 0x00);	// RPN MSB: 0
		//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x64, 0x00);	// RPN LSB: 0
		//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x06, 12);	// Data MSB - set Pitch Bend Range
		
		memset(LoopCnt, 0x00, 0x10);
		CurNote = 0xFF;
		LastNote = 0xFF;
		CurNoteVol = 0x7F;
		CurChnVol = 0x00;
		CurTickMul = TrkHdrs[CurTrk].TickMul;
		ChnPanOn = 0x00;
		CurPcmVolMul = 1.0;
		CurIns = 0xFF;
		HoldNote = 0x00;
		LoopIdx = 0x00;
		GosubID = 0x00;
		while(TrkPbFlags & 0x80)
		{
			/*if (! MstLoopCnt && SeqPos == TrkHdrs[CurTrk].LoopOfs)
			{
				MstLoopCnt ++;
				WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, 0x00);
			}*/
			
			CurCmd = SeqData[SeqPos];	SeqPos ++;
			if (CurCmd == 0x00)	// Rest
			{
				if (! HoldNote && LastNote != 0xFF)
				{
					WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					LastNote = 0xFF;
				}
				HoldNote = 0x00;
				
				CurDly += SeqData[SeqPos] * CurTickMul;
				SeqPos ++;
			}
			else if (CurCmd < 0x80)
			{
				CurNote = CurCmd - 0x01;
				//if (CurNote == 0xFF)
				//	printf("Warning: Invalid Note %02X!\n", TempByt);
				if (MidChn == 9 && CurIns < 0x20)
				{
					TempByt = DRUM_MAP[CurIns];
					if (TempByt != 0xFF)
						CurNote = TempByt;
					else
						_getch();
				}
				else
					CurNote += NoteMove;
				
				if (TrkPbFlags & 0x20)
				{
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x54, CurNote);	// write Portamento Note
					CurNote = SeqData[SeqPos] - 0x01;
					SeqPos ++;
					CurNote += NoteMove;
					
					TempByt = SeqData[SeqPos];
					/*if (TempByt == 0)
						TempByt = 0xFF;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x05, 0x7F / TempByt);*/
					TempByt = TempByt * 4;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x05, TempByt);
				}
				
				if (LastNote != CurNote || ! HoldNote)
				{
					if (HoldNote)
					{
						if (CurDly >= 1)
						{
							CurDly --;
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x60);
							CurDly ++;
						}
						else
						{
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x60);
						}
					}
					
					if (LastNote != 0xFF)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					if (CurNote != 0xFF)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, CurNote, CurNoteVol);
					LastNote = CurNote;
					
					if (HoldNote && !(TrkPbFlags & 0x20))
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x00);
				}
				HoldNote = 0x00;
				TrkPbFlags &= ~0x02;
				
				CurDly += SeqData[SeqPos] * CurTickMul;
				SeqPos ++;
			}
			else
			{
				CurCmd &= 0x9F;
				switch(CurCmd)
				{
				case 0x80:	// dummy
					break;
				case 0x81:	// set Tick Multiplier
					CurTickMul = SeqData[SeqPos];
					SeqPos ++;
					break;
				case 0x82:	// set PCM volume
					if (! TrkMode)
					{
						SeqPos ++;
						break;	// FM - do nothing
					}
					CurPcmVolR = SeqData[SeqPos];	SeqPos ++;
					if (CurPcmVolR >= 0x40)
						CurPcmVolR = 0x00;
					CurPcmVolL = SeqData[SeqPos];	SeqPos ++;
					if (CurPcmVolL >= 0x40)
						CurPcmVolL = 0x00;
					TempByt = GetPCMPan(CurPcmVolL, CurPcmVolR, &CurPcmVolMul);
					if (MidChn == 9 && CurIns < 0x20)
					{
						if (DRUM_MAP[CurIns] != 0xFF)
						{
							// write NRPN: Drum Pan
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x63, 0x1C);
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x62, DRUM_MAP[CurIns]);
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x06, TempByt);
						}
					}
					else
					{
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x0A, TempByt);
					}
					
					TempByt = GetPCMVol(CurPcmVolL, CurPcmVolR, CurPcmVolMul);
					if (! USE_VELOCITY)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
					else
						CurNoteVol = TempByt;
					break;
				case 0x83:	// dummy
					break;
				case 0x84:	// Track End
					/*if (TrkHdrs[CurTrk].LoopOfs)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, MstLoopCnt);
					if (TrkHdrs[CurTrk].LoopOfs && MstLoopCnt < NUM_LOOPS)
					{
						SeqPos = TrkHdrs[CurTrk].LoopOfs;
						MstLoopCnt ++;
					}
					else*/
					{
						TrkPbFlags &= ~0x80;
					}
					break;
				case 0x85:	// Noise Mode Enable
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x86:	// set Volume Envelope
					/*CurChnVol = SeqData[SeqPos];
					TempByt = ArcSmpsVol2Mid(TrkMode, CurChnVol, ChnPanOn);
					if (! USE_VELOCITY)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
					else
						CurNoteVol = TempByt;*/
					TempByt = SeqData[SeqPos];
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x27, TempByt);
					SeqPos ++;
					break;
				case 0x87:	// set Modulation Envelope
					TempByt = SeqData[SeqPos];
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x21, TempByt);
					SeqPos ++;
					break;
				case 0x88:	// GoSub
					TempPos = ReadLE16(&SeqData[SeqPos]);
					SeqPos += 0x02;
					if (GosubID < 0x10)
					{
						GosubPtr[GosubID] = SeqPos;
						GosubID ++;
						SeqPos = TempPos;
					}
					else
					{
						printf("Error on Track %u, Pos 0x%04X: Subroutine skipped (buffer too small)!\n",
								CurTrk, SeqPos);
					}
					break;
				case 0x89:	// Return;
					if (GosubID)
					{
						GosubID --;
						SeqPos = GosubPtr[GosubID];
					}
					else
					{
						printf("Error on Track %u, Pos 0x%04X: Invalid Subroutine-Return found!\n",
								CurTrk, SeqPos);
						TrkPbFlags &= ~0x80;
					}
					break;
				case 0x8A:	// GoTo
					TempPos = ReadLE16(&SeqData[SeqPos]);
					SeqPos = TempPos;
					
					MstLoopCnt ++;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, MstLoopCnt);
					if (MstLoopCnt >= NUM_LOOPS)
						TrkPbFlags &= ~0x80;
					break;
				case 0x8B:	// Transpose
					NoteMove += SeqData[SeqPos];
					SeqPos ++;
					break;
				case 0x8C:	// Loop
					LoopIdx = SeqData[SeqPos + 0x00];
					TempByt = SeqData[SeqPos + 0x01];
					TempPos = ReadLE16(&SeqData[SeqPos + 0x02]);
					if (! LoopCnt[LoopIdx])
						LoopCnt[LoopIdx] = TempByt;
					
					LoopCnt[LoopIdx] --;
					if (LoopCnt[LoopIdx])
						SeqPos = TempPos;
					else
						SeqPos += 0x04;
					break;
				case 0x8D:	// Pitch Slide On
					TrkPbFlags |= 0x20;
					TrkPbFlags &= ~0x02;
					HoldNote = 0x00;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);
					//printf("Warning! Pitch Slide Enable!\n");
					break;
				case 0x8E:	// Pitch Slide Off
					TrkPbFlags &= ~0x22;
					HoldNote = 0x00;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x00);
					break;
				case 0x8F:	// Raw Frequency On
					TrkPbFlags |= 0x08;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					printf("Warning! Raw Frequency Enable!\n");
					TrkPbFlags &= ~0x80;
					break;
				case 0x90:	// Raw Frequency Off
					TrkPbFlags &= ~0x08;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x91:	// Set FM Instrument
					CurIns = SeqData[SeqPos];
					WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, CurIns & 0x7F, 0x00);
					SeqPos ++;
					break;
				case 0x92:	// FM Noise Off
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x93:	// PCM related
					if (! TrkMode)
						break;
					// ??
					TrkPbFlags &= ~0x08;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x94:	// Hold
					HoldNote = 0x01;
					break;
				case 0x95:	// Pan L
				case 0x96:	// Pan R
				case 0x97:	// Pan C
					if (CurCmd == 0x95)	// left speaker
						TempByt = 0x00;
					else if (CurCmd == 0x96)	// right speaker
						TempByt = 0x7F;
					else if (CurCmd == 0x97)	// both speakers
						TempByt = 0x40;
					ChnPanOn = (TempByt == 0x40) ? 0x00 : 0x01;
					
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x0A, TempByt);
					TempByt = ArcSmpsVol2Mid(TrkMode, CurChnVol, ChnPanOn);
					if (! USE_VELOCITY)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
					else
						CurNoteVol = TempByt;
					break;
				case 0x98:	// broken
					TrkPbFlags &= ~0x80;
					break;
				case 0x99:	// PCM related
					if (! TrkMode)
						break;
					// ??
					TrkPbFlags |= 0x08;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x9A:	// set PCM Instrument
					CurIns = SeqData[SeqPos];
					if (MidChn != 9)
						WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, CurIns & 0x7F, 0x00);
					SeqPos ++;
					break;
				case 0x9B:	// unknown On
					TrkPbFlags |= 0x10;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x9C:	// unknown Off
					TrkPbFlags &= ~0x10;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x9D:	// PCM Track End?
					TrkPbFlags = 0x00;
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					break;
				case 0x9E:	// invalid
				case 0x9F:	// invalid
					TrkPbFlags &= ~0x80;
					break;
				}
			}
		}
		if (LastNote != 0xFF)
			WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
		
		WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
		WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
	}
	MidSize = MidPos;
	
	return;
}

INLINE UINT8 ArcSmpsVol2Mid(UINT8 TrkMode, UINT8 Vol, UINT8 PanBoost)
{
	double DBVol;
	
	if (TrkMode == 0)
		DBVol = FMVol2DB(Vol + 8);
	else if (TrkMode == 1)
		DBVol = SegaPCMVol2DB(Vol);
	else
		return Vol;
	if (PanBoost)
		DBVol -= 3.0;
	return DB2Mid(DBVol);
}

INLINE double FMVol2DB(UINT8 Vol)
{
	return Vol * -0.75;
}

static UINT8 GetPCMVol(const UINT8 VolL, const UINT8 VolR, const double VolMul)
{
	double DBVol;
	UINT8 FinVol;
	
	DBVol = Lin2DB((VolL + VolR) * VolMul);
	FinVol = DB2Mid(DBVol);
	if (FinVol <= 0)
		FinVol = 1;
	else if (FinVol > 0x7F)
		FinVol = 0x7F;
	return FinVol;
}

static UINT8 GetPCMPan(const UINT8 VolL, const UINT8 VolR, double* RetVolFact)
{
	// GM Pan Formula:
	//	PanAmount = (PanCtrlVal - 1) / 126
	//	Left  Channel Gain [dB] = 20 * log10(cos(Pi / 2 * PanAmount))
	//	Right Channel Gain [dB] = 20 * log10(sin(Pi / 2 * PanAmount))
	double VolDiff;
	double VolBoost;
	double PanAngle;
	double PanVal;
	UINT8 FinPan;
	
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
}

INLINE double SegaPCMVol2DB(UINT8 Vol)
{
	//return log(Vol / 63.0) / log(2.0) * 6.0;
	return log(Vol / 63.0) * 8.65617024533378;
}

INLINE double Lin2DB(double LinVol)
{
	//return log(LinVol / 126.0) / log(2.0) * 6.0;
	return log(LinVol / 126.0) * 8.65617024533378;
}

INLINE UINT8 DB2Mid(double DB)
{
	//DB += 6.0;
	if (DB > 0.0)
		DB = 0.0;
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

INLINE UINT32 Tempo2Mid(UINT8 TempoVal)
{
	// Note: The tempo value is the value of YM Timer B.
	// higher value = higher tick frequency = higher tempo
	
	// Base Clock = 3579545 Hz
	// Prescaler: 64
	// internal Timer Countdown: (400h - value)
	// Timer Frequency: Clock / (Countdown * Prescaler)
	double TicksPerSec;
	UINT16 TmrVal;
	
	TmrVal = TempoVal << 2;
	TmrVal = 0x400 - TempoVal;
	TicksPerSec = 3579545.0 / (64 * TmrVal);
	return (UINT32)(450000 * MIDI_RES / TicksPerSec + 0.5);
}



static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	if (! (Evt & 0x80))
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

static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0x00;
	
	MidData[*Pos + 0x00] = 0xFF;
	MidData[*Pos + 0x01] = MetaType;
	*Pos += 0x02;
	WriteMidiValue(Buffer, Pos, DataLen);
	memcpy(MidData + *Pos, Data, DataLen);
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
