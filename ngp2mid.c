#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "stdtype.h"

#define INLINE	static __inline

typedef struct bitstream_state
{
	UINT8 bitsLeft;
	UINT8 bitDataCache;
	UINT8 LastDly;
	UINT16 bitStrmPos;
} BITSTRM;


int main(int argc, char* argv[]);
void ConvertNGP2MID(void);
INLINE double PSGVol2DB(UINT8 Vol);
INLINE UINT8 DB2Mid(double DB);
INLINE UINT32 Tempo2Mid(UINT8 TempoVal);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
INLINE UINT16 ReadBits16(BITSTRM* bStrm, UINT8 BitCnt);
INLINE UINT8 ReadBits_Byte(BITSTRM* bStrm, UINT8 BitCnt);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);


#define MIDI_RES	24
#define NUM_LOOPS	2
#define TEMPO_AS_BPM	0x01

#define DRV_VER_09B9	0x00	// NeoGeo Pocket BIOS (v0?)
#define DRV_VER_09F5	0x01	// Sonic Pocket Adventure (v1?)

static const UINT8 DRV_CMDCONV_0to1[0x10] =
{	0x00, 0x03, 0x02, 0x01, 0x04, 0x05, 0x06, 0x07,
	0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F};

static UINT32 SeqSize;
static UINT8* SeqData;
static UINT32 MidSize;
static UINT8* MidData;

static UINT8 DrvVer = DRV_VER_09F5;

int main(int argc, char* argv[])
{
	FILE* hFile;
	//char FileName[0x10];
	
	if (argc <= 2)
	{
		printf("Usage: ngp2mid input.bin output.mid\n");
#ifdef _DEBUG
		_getch();
#endif
		return 0;
	}
	
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
	MidSize = 0x10000;
	MidData = (UINT8*)malloc(MidSize);
	ConvertNGP2MID();
	
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
	//getchar();
#endif
	
	return 0;
}

void ConvertNGP2MID(void)
{
	UINT16 SeqBasePos;
	UINT16 SeqPos;
	UINT32 MidPos;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT32 MidTrkBase;
	UINT32 CurDly;
	UINT8 CurChn;
	UINT8 MidChn;
	UINT8 TrkEnd;
	UINT16 InsMapPos;
	UINT8 MstLoopCnt;
	
	UINT8 IsInSub;
	BITSTRM MainStrm;
	BITSTRM SubStrm;
	BITSTRM* CurStrm;
	
	UINT8 SeqFlag;
	UINT8 SeqDly;
	UINT8 SeqCmd;
	UINT8 SeqParam;
	UINT8 CurNote;
	UINT8 CurNoteVol;
	UINT8 CurMidNote;
	UINT8 LastNote;
	UINT8 LastNoteLen;
	UINT8 RemNoteLen;
	UINT8 CurStkIdx;
	UINT8 LoopCnt[4];	// the driver has only 2 slots
	UINT16 LoopPos[4];
	//UINT8 TempByt;
	UINT16 TempPos;
	UINT32 TempLng;
	
	SeqPos = 0x00;
	TrkCnt = (SeqData[SeqPos + 0x01] >> 6) + 0x01;
	SeqPos += 0x02;
	SeqBasePos = SeqPos;
	
	InsMapPos = SeqBasePos + TrkCnt * 0x02 + 0x01;
	
	MidPos = 0x00;
	WriteBE32(&MidData[MidPos], 0x4D546864);	MidPos += 0x04;	// 'MThd' Signature
	WriteBE32(&MidData[MidPos], 0x00000006);	MidPos += 0x04;	// Header Size
	WriteBE16(&MidData[MidPos], 0x0001);		MidPos += 0x02;	// Format: 1
	WriteBE16(&MidData[MidPos], TrkCnt);		MidPos += 0x02;	// Tracks
	WriteBE16(&MidData[MidPos], MIDI_RES);		MidPos += 0x02;	// Ticks per Quarter
	
#if 0
	WriteBE32(&MidData[MidPos], 0x4D54726B);	MidPos += 0x04;	// 'MTrk' Signature
	WriteBE32(&MidData[MidPos], 0x00000000);	MidPos += 0x04;	// Track Size
	MidTrkBase = MidPos;
	CurDly = 0;
	
	TempLng = Tempo2Mid(120);	// default tempo value
	WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x51, 0x03);
	WriteBE32(&MidData[MidPos - 0x01], TempLng);
	MidData[MidPos - 0x01] = 0x03;	// write again, because the above instruction overwrote it
	MidPos += 0x03;
	
	WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
#endif
	
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++)
	{
		WriteBE32(&MidData[MidPos], 0x4D54726B);	// write 'MTrk'
		MidPos += 0x08;
		MidTrkBase = MidPos;
		
		SeqPos = SeqBasePos + CurTrk * 0x02;
		TempPos = ReadLE16(&SeqData[SeqPos]);
		SeqPos += 0x02;
		CurChn = TempPos >> 14;
		MidChn = CurChn;
		TempPos &= 0x3FFF;
		printf("Track %u (Channel %u) ...\n", CurTrk, CurChn);
		
		CurDly = 0;
		TrkEnd = 0x00;
		MstLoopCnt = 0;
		WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x65, 0x00);	// RPN MSB: 0
		WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x64, 0x00);	// RPN LSB: 0
		//if (DrvVer == DRV_VER_09B9)
			SeqParam = 12;
		//else if (DrvVer == DRV_VER_09F5)
		//	SeqParam = 10;	// approximate guess
		WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x06, SeqParam);	// Data MSB - set Pitch Bend Range
		
		MainStrm.bitStrmPos = SeqPos + TempPos;
		MainStrm.bitsLeft = 0;
		MainStrm.bitDataCache = 0x00;
		MainStrm.LastDly = 0x00;
		SubStrm.bitStrmPos = 0x00;
		SubStrm.bitsLeft = 0;
		SubStrm.bitDataCache = 0x00;
		SubStrm.LastDly = 0x00;
		
		IsInSub = 0x00;
		CurNote = 0x00;		// The Sound Engine uses Note 0 for rests.
		CurNoteVol = 0x7F;
		LastNote = 0x00;	// I'll use Note 0 for rests, too.
		RemNoteLen = 0x00;
		LastNoteLen = 0x00;
		CurStkIdx = 0x00;
		CurStrm = &MainStrm;
		while(! TrkEnd)
		{
			if (DrvVer == DRV_VER_09F5)
				SeqFlag = ReadBits_Byte(CurStrm, 1);
			// ReadSeqToken:
			if (! ReadBits_Byte(CurStrm, 1))
			{
				if (! ReadBits_Byte(CurStrm, 1))
					CurStrm->LastDly = ReadBits_Byte(CurStrm, 4);
				else
					CurStrm->LastDly = ReadBits_Byte(CurStrm, 8);
			}
			else
			{
				if (SeqFlag && DrvVer == DRV_VER_09F5)
					CurStrm->LastDly = 0x00;
			}
			SeqDly = CurStrm->LastDly;
			
			if (DrvVer == DRV_VER_09B9)
				SeqFlag = ReadBits_Byte(CurStrm, 1);
			if (! SeqFlag)
			{
				if (DrvVer == DRV_VER_09F5)
				{
					//ProcessTrkNote [SPA]
					if (! ReadBits_Byte(CurStrm, 1))
						CurNote = ReadBits_Byte(CurStrm, 7);
				}
				else if (DrvVer == DRV_VER_09B9)
				{
					//ProcessTrkNote [NGP BIOS]
					SeqCmd = ReadBits_Byte(CurStrm, 2);
					if (! (SeqCmd & 0x01))
					{
						CurNote &= 0x0F;
						CurNote |= ReadBits_Byte(CurStrm, 3) << 4;	// read Octave bits
					}
					if (! (SeqCmd & 0x02))
					{
						CurNote &= 0xF0;
						CurNote |= ReadBits_Byte(CurStrm, 4);	// read Note bits
					}
				}
				// high nibble = octave, low nibble = note
				CurMidNote = (CurNote >> 4) * 12;	// convert "octave nibble" into note value
				CurMidNote += CurNote & 0x0F;		// add actual note
				if (CurMidNote)		// if not a rest
					CurMidNote += 36;	// make it start at C3
				
				if (! ReadBits_Byte(CurStrm, 1))
				{
					SeqParam = ReadBits_Byte(CurStrm, 4);
					CurNoteVol = DB2Mid(PSGVol2DB(SeqParam));
					if (! CurNoteVol)
						CurNoteVol = 0x01;
				}
				
				if (! (LastNote == CurMidNote && RemNoteLen))
				{
					// switch to new note if
					//	1. the note has changed or
					//	2. all effects are restarted (RemNoteLen == 0)
					if (RemNoteLen > 0)
					{
						RemNoteLen = (CurDly > 0) ? 1 : 0;
						CurDly -= RemNoteLen;
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x7F);	// Portamento On
						CurDly += RemNoteLen;
						RemNoteLen = 1;
					}
					
					if (LastNote)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					if (CurMidNote)
						WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, CurMidNote, CurNoteVol);
					LastNote = CurMidNote;
					if (RemNoteLen > 0)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x41, 0x00);	// Portamento Off
				}
				
				// Read Note Length
				// ReadSeqToken:
				if (! ReadBits_Byte(CurStrm, 1))
				{
					if (! ReadBits_Byte(CurStrm, 1))
						LastNoteLen = ReadBits_Byte(CurStrm, 4);
					else
						LastNoteLen = ReadBits_Byte(CurStrm, 8);
				}
				RemNoteLen += LastNoteLen;
			}
			else
			{
				//ProcessTrkCmd
				if (DrvVer == DRV_VER_09F5)
				{
					SeqCmd = ReadBits_Byte(CurStrm, 3);
					if (SeqCmd == 0x05)
					{
						// In this revision, the command serves a general "termination" purpose.
						if (IsInSub)
							SeqCmd = 0x08;	// Subroutine Return
						else if (! CurStkIdx)
							SeqCmd = 0x0F;	// Track End
						else
							SeqCmd = 0x05;	// Loop End
					}
				}
				else if (DrvVer == DRV_VER_09B9)
				{
					SeqCmd = ReadBits_Byte(CurStrm, 4);
					SeqCmd = DRV_CMDCONV_0to1[SeqCmd];
				}
				switch(SeqCmd)
				{
				case 0x00:	// Instrument (controls Volume Envelope + Modulation)
					SeqParam = ReadBits_Byte(CurStrm, 5);
					SeqParam = SeqData[InsMapPos + SeqParam];
#if 1
					WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, SeqParam, 0x00);
#else
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x20, SeqParam);
					if (MidChn < 0x03)
						WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, 0x50, 0x00);
					else
						WriteEvent(MidData, &MidPos, &CurDly, 0xC0 | MidChn, 0x7F, 0x00);
#endif
					break;
				case 0x01:	// Detune
					SeqParam = ReadBits_Byte(CurStrm, 7);
					WriteEvent(MidData, &MidPos, &CurDly, 0xE0 | MidChn, 0x00, SeqParam);
					break;
				case 0x02:	// Pan
					SeqParam = ReadBits_Byte(CurStrm, 4);
					
					if (DrvVer == DRV_VER_09F5)
					{
						// The Pan modifier is: 00 00 01 10 02 20 03 30 04 40 05 50 06 60 0F F0
						// The low nibble softens the right channel,
						// the high nibble softens the left channel.
						SeqCmd = SeqParam & 0x01;
						SeqParam >>= 1;	// strip nibble bit
						SeqParam <<= 3;	// 00..07 -> 00..38
						if (SeqParam >= 0x38)
							SeqParam = 0x3F;
						
						if (SeqCmd & 0x01)
							SeqParam = 0x40 + SeqParam;
						else
							SeqParam = 0x40 - SeqParam;
					}
					else if (DrvVer == DRV_VER_09B9)
					{
						// The Pan modifier is: 0F 06 05 04 03 02 01 00 00 10 20 30 40 50 60 F0
						SeqParam <<= 3;	// 00..0F -> 00..78
						if (SeqParam < 0x40)
						{
							// 00..38 -> 01,10..40
							if (SeqParam == 0x00)
								SeqParam = 0x01;
							else
								SeqParam += 0x08;
						}
						else
						{
							// 40..78 -> 40..70,7F
							if (SeqParam == 0x78)
								SeqParam = 0x7F;
						}
					}
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x0A, SeqParam);
					break;
				case 0x03:	// Volume
					SeqParam = ReadBits_Byte(CurStrm, 4);
					SeqParam = DB2Mid(PSGVol2DB(SeqParam));
					WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x07, SeqParam);
					break;
				case 0x04:	// Loop Start
					SeqParam = ReadBits_Byte(CurStrm, 5);
					if (SeqParam == 0x00)
						SeqParam = 0xFF;
					LoopCnt[CurStkIdx] = SeqParam;
					LoopPos[CurStkIdx] = CurStrm->bitStrmPos;
					CurStrm->bitsLeft = 0;
					if (LoopCnt[CurStkIdx] == 0xFF)
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, MstLoopCnt);
					CurStkIdx ++;
					break;
				case 0x05:	// Loop End
					//SeqParam = ReadBits_Byte(CurStrm, 0);
					CurStkIdx --;
					if (LoopCnt[CurStkIdx] == 0xFF)
					{
						// Master Loop
						MstLoopCnt ++;
						WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6F, MstLoopCnt);
						if (MstLoopCnt >= NUM_LOOPS)
							TrkEnd = 0x02;
					}
					else
					{
						LoopCnt[CurStkIdx] --;
						if (! LoopCnt[CurStkIdx])
							break;	// quit loop
					}
					// loop back
					CurStrm->bitStrmPos = LoopPos[CurStkIdx];
					CurStrm->bitsLeft = 0;
					CurStkIdx ++;
					break;
				case 0x06:	// Set Tempo
					SeqParam = ReadBits_Byte(CurStrm, 8);
					//WriteEvent(MidData, &MidPos, &CurDly, 0xB0 | MidChn, 0x6E, SeqParam);
					
					TempLng = Tempo2Mid(SeqParam);	// default tempo value
					WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x51, 0x03);
					WriteBE32(&MidData[MidPos - 0x01], TempLng);
					MidData[MidPos - 0x01] = 0x03;	// write again, because the above instruction overwrote it
					MidPos += 0x03;
					break;
				case 0x07:	// Subroutine
					TempPos = ReadBits16(CurStrm, 11);
					SubStrm.bitStrmPos = CurStrm->bitStrmPos + TempPos;
					
					IsInSub = 0x01;
					SeqDly = 0x00;
					CurStrm = &SubStrm;
					break;
				case 0x08:	// Subroutine Return
					CurStrm->bitsLeft = 0;
					CurStrm->bitDataCache = 0x00;
					SubStrm.bitStrmPos = 0x0000;
					
					IsInSub = 0x00;
					SeqDly = 0x00;
					CurStrm = &MainStrm;
					break;
				case 0x0F:	// Track End
					TrkEnd = 0x01;
					break;
				}
			}
			
			if (RemNoteLen >= SeqDly)
			{
				RemNoteLen -= SeqDly;
				CurDly += SeqDly;
			}
			else
			{
				CurDly += RemNoteLen;
				if (LastNote)
				{
					WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
					LastNote = 0x00;
				}
				CurDly += SeqDly - RemNoteLen;
				RemNoteLen = 0;
			}
			SeqDly = 0;
		}
		if (LastNote)
		{
			CurDly += RemNoteLen;
			WriteEvent(MidData, &MidPos, &CurDly, 0x90 | MidChn, LastNote, 0x00);
		}
		
		WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
		WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
	}
	MidSize = MidPos;
	
	return;
}

INLINE double PSGVol2DB(UINT8 Vol)
{
	if (DrvVer ==  DRV_VER_09B9)
		Vol ^= 0x0F;
	if (Vol >= 0x0F)
		return -999;
	else
		return Vol * -2.0;
}

INLINE UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

INLINE UINT32 Tempo2Mid(UINT8 TempoVal)
{
	// Note: The tempo value is some sort of step size.
	// It is linear, i.e. double value = double speed.
	
	// Interrupt Frequency = 6144000 (main CPU clock) / (0x62 ticks * 0x80 cycles/tick) = ~490 Hz
	// The sound driver has an additional divider of 271h.
	double TicksPerSec;
	
	if (! TEMPO_AS_BPM)
		TicksPerSec = 6144000.0 / (0x62 * 0x80 * 0x271) * TempoVal;
	else
		TicksPerSec = 48.0 / 60.0 * TempoVal;
	return (UINT32)(1000000 * MIDI_RES / TicksPerSec + 0.5);
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

INLINE UINT16 ReadBits16(BITSTRM* bStrm, UINT8 BitCnt)
{
	UINT8 LowData;
	UINT8 HighData;
	
	if (BitCnt <= 8)
		return ReadBits_Byte(bStrm, BitCnt);
	
	LowData = ReadBits_Byte(bStrm, 8);
	BitCnt -= 8;
	HighData = ReadBits_Byte(bStrm, BitCnt);
	return (HighData << 8) | (LowData << 0);
}

INLINE UINT8 ReadBits_Byte(BITSTRM* bStrm, UINT8 BitCnt)
{
	UINT8 RemBits;	// Register A
	UINT16 BitData;	// Register HL
	
	BitCnt &= 0x0F;
	
	// Note: The high 8 bit of BitData contain the result data.
	//       The low 8 bit contain the remaining bits of the last byte read from the file.
	RemBits = bStrm->bitsLeft;
	BitData = bStrm->bitDataCache;
	do
	{
		if (! RemBits)
		{
			if (bStrm->bitStrmPos >= SeqSize)
			{
				printf("Error: Reading from invalid file offset %04X! (size: %04X)\n",
						bStrm->bitStrmPos, SeqSize);
				bStrm->bitStrmPos = 0;
				RemBits = 0xFF;
			}
			BitData &= 0xFF00;
			BitData |= SeqData[bStrm->bitStrmPos];
			bStrm->bitStrmPos ++;
			if (RemBits != 0xFF)
				RemBits = 8;
		}
		BitData <<= 1;
		RemBits --;
		BitCnt --;
	} while(BitCnt);
	bStrm->bitsLeft = RemBits;
	bStrm->bitDataCache = BitData & 0x00FF;
	
	return BitData >> 8;
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
