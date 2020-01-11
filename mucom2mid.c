// Mucom88 -> Midi Converter
// -------------------------
// Written by Valley Bell, 09 March 2015

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory.h>
#include "stdtype.h"

#define INLINE	static __inline

int main(int argc, char* argv[]);
void ConvertMucom2MID(void);
INLINE UINT8 MucomVol2Mid(UINT8 TrkMode, UINT8 Vol, UINT8 PanBoost);
INLINE double FMVol2DB(UINT8 Vol);
INLINE double PSGVol2DB(UINT8 Vol);
INLINE double DeltaTVol2DB(UINT8 Vol);
INLINE UINT8 DB2Mid(double DB);
INLINE UINT32 Tempo2Mid(UINT8 TempoVal);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);


#define USE_VELOCITY	1
#define NO_NOTESTOP		0

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
		printf("Usage: mucom2mid input.bin output.mid\n");
#ifdef _DEBUG
		_getch();
#endif
		return 0;
	}
	
	MIDI_RES = 24;
	//MIDI_RES = 0x20;
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
	MidSize = 0x20000;
	MidData = (UINT8*)malloc(MidSize);
	ConvertMucom2MID();
	
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

typedef struct _track_header
{
	UINT16 StartOfs;
	UINT16 LoopOfs;
} TRK_HDR;
#define SEQ_BASEOFS	0x0005

void ConvertMucom2MID(void)
{
	static const UINT8 NOTE_ARRAY[0x10] =
	//	00  01  02  03  04  05  06  07  08  09  0A  0B  0C  0D  0E  0F
	//	C   C#  D   D#  E   --  F   F#  G   G#  A   A#  B   --  --  --
	{	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11,255,255,255,255};
	static const UINT8 RHYTHM_NOTES[0x06] =
	{0x24, 0x26, 0x33, 0x2A, 0x2D, 0x25};
	UINT8 Mucom88Win;
	UINT16 SeqSize;
	UINT16 TrkHdrPos;
	UINT16 FMInsPos;
	UINT16 SSGInsPos;
	TRK_HDR TrkHdrs[11];
	UINT16 SeqPos;
	UINT32 MidPos;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT32 MidTrkBase;
	UINT32 CurDly;
	UINT8 MidChn;
	UINT8 TrkEnd;
	UINT8 MstLoopCnt;
	UINT8 TrkMode;	// 00 - FM, 01 - SSG
	INT8 NoteMove;
	
	UINT8 CurCmd;
	
	UINT8 CurNote;
	UINT8 CurNoteVol;
	UINT8 CurChnVol;
	UINT8 ChnPanOn;
	UINT8 LastNote;
	UINT8 HoldNote;
	UINT8 NoteStop;
	UINT8 CurRhythmMask;
	UINT8 CurRhythmOn;
	
	UINT8 LoopIdx;
	UINT8 LoopStkIdx;
	UINT8 LoopCnt[0x10];
	
	UINT8 TempByt;
	INT16 TempPos;
	UINT32 TempLng;
	
	TrkHdrPos = 0x0000;
	if (!memcmp(SeqData, "MUB8", 4))
	{
		printf("Detected Mucom88win header.\n");
		Mucom88Win = 1;
		TrkHdrPos = ReadLE16(&SeqData[4]) + 5;
	}
	else
	{
		Mucom88Win = 0;
		for (SeqPos = 0x00; SeqPos < 0x08; SeqPos ++)
		{
			TempPos = ReadLE16(&SeqData[SeqPos + 0x01]);
			if (TempPos == 0x002F)
			{
				TrkHdrPos = SeqPos;
				break;
			}
		}
	}
	if (! TrkHdrPos)
	{
		printf("Unable to find sequence header!\n");
		return;
	}
	printf("Sequence header found at %04X.\n", TrkHdrPos);
	
	TrkCnt = 11;
	SeqPos = 0x01;
	FMInsPos = ReadLE16(&SeqData[TrkHdrPos]);
	SSGInsPos = ReadLE16(&SeqData[TrkHdrPos+2]);
	
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
	
	TempLng = Tempo2Mid(SeqData[SeqPos]);	// default tempo value
	WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x51, 0x03);
	WriteBE32(&MidData[MidPos - 0x01], TempLng);
	MidData[MidPos - 0x01] = 0x03;	// write again, because the above instruction overwrote it
	MidPos += 0x03;
	SeqPos ++;
	
	WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
#endif
	
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++, SeqPos += 0x04)
	{
		TrkHdrs[CurTrk].StartOfs = ReadLE16(&SeqData[SeqPos + 0x00]);
		if (TrkHdrs[CurTrk].StartOfs)
			TrkHdrs[CurTrk].StartOfs += TrkHdrPos;
		TrkHdrs[CurTrk].LoopOfs = ReadLE16(&SeqData[SeqPos + 0x02]);
		if (TrkHdrs[CurTrk].LoopOfs)
			TrkHdrs[CurTrk].LoopOfs += TrkHdrPos;
	}
	SeqSize = ReadLE16(&SeqData[SeqPos]) + TrkHdrPos;
	SeqPos += 0x02;
	
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++)
	{
		WriteBE32(&MidData[MidPos], 0x4D54726B);	// write 'MTrk'
		MidPos += 0x08;
		MidTrkBase = MidPos;
		
		SeqPos = TrkHdrs[CurTrk].StartOfs;
		if (CurTrk < 3)			// FM 1..3
		{
			TrkMode = 0;
			MidChn = CurTrk;
		}
		else if (CurTrk < 6)	// SSG 1..3
		{
			TrkMode = 1;
			MidChn = 10 + CurTrk - 3;
		}
		else if (CurTrk == 6)	// ADPCMA Rhythm
		{
			TrkMode = 3;
			MidChn = 9;
		}
		else if (CurTrk == 10)	// ADPCMB/DeltaT
		{
			TrkMode = 2;
			MidChn = 9;
		}
		else
		{
			TrkMode = 0;
			MidChn = 3 + CurTrk - 7;
		}
		printf("Track %u ...\n", CurTrk);
		
		CurDly = 0;
		TrkEnd = (SeqPos == 0x0000);
		MstLoopCnt = 0x00;
		NoteMove = TrkMode ? +12 : 0;
		//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x65, 0x00);	// RPN MSB: 0
		//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x64, 0x00);	// RPN LSB: 0
		//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x06, 12);	// Data MSB - set Pitch Bend Range
		
		memset(LoopCnt, 0x00, 0x10);
		CurNote = 0xFF;
		LastNote = 0xFF;
		CurNoteVol = 0x7F;
		CurChnVol = 0x00;
		ChnPanOn = 0x00;
		HoldNote = 0x00;
		NoteStop = 0;
		CurRhythmMask = 0x00;
		CurRhythmOn = 0x00;
		LoopStkIdx = 0x00;
		LoopIdx = 0x00;
		while(! TrkEnd)
		{
			if (! MstLoopCnt && SeqPos == TrkHdrs[CurTrk].LoopOfs)
			{
				MstLoopCnt ++;
				WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, 0x00);
			}
			
			CurCmd = SeqData[SeqPos];	SeqPos ++;
			if (CurCmd == 0x00)
			{
				if (TrkHdrs[CurTrk].LoopOfs)
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, MstLoopCnt);
				if (TrkHdrs[CurTrk].LoopOfs && MstLoopCnt < NUM_LOOPS)
				{
					SeqPos = TrkHdrs[CurTrk].LoopOfs;
					MstLoopCnt ++;
				}
				else
				{
					TrkEnd = 0x01;
				}
			}
			else if (CurCmd < 0x80)
			{
				TempByt = SeqData[SeqPos];	SeqPos ++;
				CurNote = NOTE_ARRAY[TempByt & 0x0F];
				if (CurNote == 0xFF)
					printf("Warning: Invalid Note %02X!\n", TempByt);
				CurNote += (TempByt >> 4) * 12;
				CurNote += NoteMove + 12;
				
				if (TrkMode == 3)
				{
					if (! HoldNote)
					{
						for (TempByt = 0x00; TempByt < 0x06; TempByt ++)
						{
							if (CurRhythmOn & (1 << TempByt))
							{
								CurRhythmOn &= ~(1 << TempByt);
								WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, RHYTHM_NOTES[TempByt], 0x00);
							}
						}
						for (TempByt = 0x00; TempByt < 0x06; TempByt ++)
						{
							if (CurRhythmMask & (1 << TempByt))
							{
								CurRhythmOn |= (1 << TempByt);
								WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, RHYTHM_NOTES[TempByt], CurNoteVol);
							}
						}
					}
				}
				else if (LastNote != CurNote || ! HoldNote)
				{
					if (HoldNote)
					{
						if (CurDly >= 1)
						{
							CurDly --;
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);
							CurDly ++;
						}
						else
						{
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);
						}
					}
					
					if (LastNote != 0xFF)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					if (CurNote != 0xFF)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, CurNote, CurNoteVol);
					LastNote = CurNote;
					
					if (HoldNote)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x00);
				}
				HoldNote = 0x00;
				
				CurDly += CurCmd;
				if (NoteStop && CurDly > NoteStop && SeqData[SeqPos] != 0xFD)
				{
					CurDly -= NoteStop;
					if (TrkMode == 3)
					{
						for (TempByt = 0x00; TempByt < 0x06; TempByt ++)
						{
							if (CurRhythmOn & (1 << TempByt))
							{
								CurRhythmOn &= ~(1 << TempByt);
								WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, RHYTHM_NOTES[TempByt], 0x00);
							}
						}
					}
					else if (LastNote != 0xFF)
					{
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
						LastNote = 0xFF;
					}
					CurDly += NoteStop;
				}
			}
			else if (CurCmd < 0xF0)
			{
				if (! HoldNote && LastNote != 0xFF)
				{
					WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					LastNote = 0xFF;
				}
				HoldNote = 0x00;
				
				CurDly += CurCmd & 0x7F;
			}
			else
			{
				switch(CurCmd)
				{
				case 0xF0:	// Set Instrument
					if (TrkMode == 3 && Mucom88Win)
					{
						// set Rhythm Mask
						CurRhythmMask = SeqData[SeqPos];
					}
					else
					{
						TempByt = SeqData[SeqPos] & 0x7F;
						WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, TempByt, 0x00);
					}
					SeqPos ++;
					break;
				case 0xF1:	// Set Volume
					CurChnVol = SeqData[SeqPos];
					TempByt = MucomVol2Mid(TrkMode, CurChnVol, ChnPanOn);
					if (! USE_VELOCITY)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
					else
						CurNoteVol = TempByt;
					SeqPos ++;
					if (TrkMode == 3)
						SeqPos += 0x06;
					break;
				case 0xF2:	// Detune
					//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x0F);
					//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x26, SeqData[SeqPos]);
					TempPos = ReadLE16(&SeqData[SeqPos]);
					TempPos = 0x2000 - (TempPos << 5);
					WriteEvent(MidData, &MidPos, &CurDly, 0xE0 | MidChn, TempPos & 0x7F, (TempPos >> 7) & 0x7F);
					SeqPos += 0x03;
					break;
				case 0xF3:
					if (TrkMode == 3)
					{
						// set Rhythm Mask
						CurRhythmMask = SeqData[SeqPos];
					}
					else
					{
						// Note Stop/Echo Volume?
						NoteStop = SeqData[SeqPos];
						//if (TrkMode == 0 && NoteStop >= 4)
						//	NoteStop = 0;
						if (NO_NOTESTOP)
							NoteStop = 0;
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x29, SeqData[SeqPos]);
					}
					SeqPos ++;
					break;
				case 0xF4:	// Modulation
					if (! SeqData[SeqPos + 0x00])
					{
						// Set Modulation
						TempPos = ReadLE16(&SeqData[SeqPos + 0x03]);
						TempByt = SeqData[SeqPos + 0x05];
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x10, SeqData[SeqPos + 0x01]);
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x11, SeqData[SeqPos + 0x02]);
						if (! TrkMode)
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x12, TempPos & 0x7F);
						else
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x12, (TempPos/8) & 0x7F);
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x13, TempByt & 0x7F);
						
						if (TempPos < 0)
							TempPos = -TempPos;
						TempLng = (TempPos * TempPos) / 8;
						
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x01, TempLng);
						SeqPos += 0x06;
					}
					else
					{
						// Disable Modulation
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x01, 0x00);
						SeqPos ++;
					}
					break;
				case 0xF5:	// Loop Start
					TempPos = ReadLE16(&SeqData[SeqPos]);	// get offset of Loop Count
					LoopStkIdx ++;
					if (SeqPos + TempPos < SeqSize)
						LoopCnt[LoopStkIdx] = SeqData[SeqPos + TempPos];
					else
						LoopCnt[LoopStkIdx] = 0x00;
					SeqPos += 0x02;
					break;
				case 0xF6:	// Loop End
					TempByt = SeqData[SeqPos + 0x01];
					if (! LoopCnt[LoopStkIdx])
						LoopCnt[LoopStkIdx] = TempByt;
					SeqPos += 0x02;
					
					TempPos = ReadLE16(&SeqData[SeqPos]);
					LoopCnt[LoopStkIdx] --;
					if (LoopCnt[LoopStkIdx])
					{
						SeqPos -= TempPos;
					}
					else
					{
						LoopStkIdx --;
						SeqPos += 0x02;
					}
					break;
				case 0xF7: // FM3 special mode
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x0F);
					SeqPos ++;
					break;
				case 0xF8:
				case 0xF9:	// Pan
					if((Mucom88Win && CurCmd == 0xf8) || (!Mucom88Win && CurCmd == 0xf9))
					{
						TempByt = SeqData[SeqPos];
						if (TrkMode == 3)	// rhythm mode works differently
						{
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x26, TempByt);
							TempByt = 0x00;
						}
						TempByt &= 0x03;
						if (TempByt == 0x01)	// right speaker
							TempByt = 0x7F;
						else if (TempByt == 0x02)	// left speaker
							TempByt = 0x00;
						else	// both speakers
							TempByt = 0x40;
						ChnPanOn = (TempByt == 0x40) ? 0x00 : 0x01;

						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x0A, TempByt);
						TempByt = MucomVol2Mid(TrkMode, CurChnVol, ChnPanOn);
						if (! USE_VELOCITY)
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
						else
							CurNoteVol = TempByt;
						SeqPos ++;
						break;
					}
					else
					{
						// seems to just write a communication byte
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x0F);
						SeqPos ++;
						break;
					}
				case 0xFA:	// Register Write
					if (TrkMode == 1)
					{
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x0F);
						SeqPos += 0x06;
					}
					else	// YM2203/2608 Register Write
					{
						if (SeqData[SeqPos + 0x00] == 0x26)	// Timer B - change Tempo
						{
							TempLng = Tempo2Mid(SeqData[SeqPos + 0x01]);
							WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x51, 0x03);
							WriteBE32(&MidData[MidPos - 0x01], TempLng);
							MidData[MidPos - 0x01] = 0x03;	// write again, because the above instruction overwrote it
							MidPos += 0x03;
						}
						else
						{
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 50, SeqData[SeqPos + 0x00]);
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 51, SeqData[SeqPos + 0x01]);
						}
						SeqPos += 0x02;
					}
					break;
				case 0xFB:	// Change Volume
					CurChnVol += SeqData[SeqPos];
					TempByt = MucomVol2Mid(TrkMode, CurChnVol, ChnPanOn);
					if (! USE_VELOCITY)
					{
						if (SeqData[SeqPos] != 0xFB)
							WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, TempByt);
					}
					else
						CurNoteVol = TempByt;
					SeqPos ++;
					break;
				case 0xFC:
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, CurCmd & 0x0F);
					//printf("Unknown command %02X at %04X!\n", CurCmd, SeqPos - 0x01);
					//getchar();
					SeqPos += 0x03;
					break;
				case 0xFD:	// Hold
					HoldNote = 0x01;
					break;
				case 0xFE:	// Loop Exit
					TempPos = ReadLE16(&SeqData[SeqPos]);	// get offset of Loop Exit Counter
					TempByt = SeqData[SeqPos + TempPos];
					if (LoopCnt[LoopStkIdx] == 1)
					{
						LoopCnt[LoopStkIdx] = 0;
						LoopStkIdx --;
						SeqPos += TempPos + 0x04;
					}
					else
					{
						SeqPos += 0x02;
					}
					break;
				case 0xFF:	// Master Loop Start??
					if(Mucom88Win)
					{
						TempByt = SeqData[SeqPos];
						SeqPos ++;
						switch(TempByt)
						{
							case 0xf0: // PCM volume mode ('vm')
								SeqPos++;
								break;
							case 0xf1: // PSG hardware support (mucom88 v1.5 / music lalf 1.0 only)
							case 0xf2:
								SeqPos++;
								break;
							case 0xf3: // Reverb enable
							case 0xf4: // Reverb mode
							case 0xf5: // Reverb switch
								SeqPos++;
								break;
							default:
								printf("unknown extra command %02x at %04x\n", TempByt, SeqPos);
								break;
						}
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x71, TempByt & 0x0F);
					}
					//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, 0x00);
					//SeqPos += 0x02;
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

INLINE UINT8 MucomVol2Mid(UINT8 TrkMode, UINT8 Vol, UINT8 PanBoost)
{
	double DBVol;
	
	if (TrkMode == 0)
		DBVol = FMVol2DB(Vol);
	else if (TrkMode == 1)
		DBVol = PSGVol2DB(Vol);
	else if (TrkMode == 2)
		DBVol = DeltaTVol2DB(Vol);
	else if (TrkMode == 3)
		DBVol = DeltaTVol2DB(Vol * 4);
	else
		return Vol;
	if (PanBoost)
		DBVol -= 3.0;
	return DB2Mid(DBVol);
}

INLINE double FMVol2DB(UINT8 Vol)
{
	// Mucom uses a FM volume lookup table to map its volume value to 8/3 FM steps. (2 db steps)
	// The table contains 20 values and looks like this:
	// 36 33 30 2D 2A 28 25 22 20 1D 1A 18 15 12 10 0D 0A 08 05 02
#if 0
	UINT8 FmVol;
	
	if (Vol < 20)
		FmVol = (20 - Vol) * 8 / 3;
	else
		FmVol = 0;
	return FmVol * -0.75;
#else
	if (Vol < 20)
		return (20 - Vol) * -2.0;
	else
		return 0;
#endif
}

INLINE double PSGVol2DB(UINT8 Vol)
{
	if (Vol > 0x0F)
		return 0.0;
	else if (Vol > 0x00)
		return (0x0F - Vol) * -3.0;	// AY8910 volume is 3 db per step
	else
		return -999;
}

INLINE double DeltaTVol2DB(UINT8 Vol)
{
	//return log(Vol / 255.0) / log(2.0) * 6.0;
	return log(Vol / 255.0) * 8.65617024533378 + 6.0;	// boost its volume
}

INLINE UINT8 DB2Mid(double DB)
{
	DB += 6.0;
	if (DB > 0.0)
		DB = 0.0;
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

INLINE UINT32 Tempo2Mid(UINT8 TempoVal)
{
	// Note: The tempo value is the value of YM Timer B.
	// higher value = higher tick frequency = higher tempo
	
	// Base Clock = 2 MHz
	// Prescaler: 6 * 12
	// internal Timer Countdown: (100h - value) * 10h
	// Timer Frequency: Clock / (Countdown * Prescaler)
	double TicksPerSec;
	UINT16 TmrVal;
	
	TmrVal = (0x100 - TempoVal) << 4;
	TicksPerSec = 2000000.0 / (6 * 12 * TmrVal);
	return (UINT32)(500000 * MIDI_RES / TicksPerSec + 0.5);
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
