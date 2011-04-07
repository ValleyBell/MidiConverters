// MIDI 1 to 0 C++ source file

#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include "stdbool.h"

const unsigned long int FCC_MTHD = 0x6468544D;
const unsigned long int FCC_MTRK = 0x6B72544D;

typedef struct midi_track_info
{
	unsigned long int TrkBase;
	unsigned long int TrkEnd;
	unsigned long int CurPos;
	unsigned long int TickPos;
	unsigned char LastEvent;	// all values are possible
	unsigned char RmbrEvent;	// must not be F0 .. FF
} MIDITRK_INF;

unsigned char MIDI1to0(unsigned long int SrcLen, unsigned char* SrcData,
						unsigned long int* RetDstLen, unsigned char** RetDstData);
static unsigned long int ReadMIDIValue(unsigned char* FileData, unsigned long int* Value);
static unsigned long int WriteMIDIValue(unsigned char* FileData, unsigned long int Value);
static unsigned char CopyMIDIEvent(unsigned char* SrcData, unsigned char* DstData,
								   MIDITRK_INF* TrkSrc, MIDITRK_INF* TrkDst);
static unsigned long int LittleBigEndianCnvL(unsigned long int Value);
static unsigned short int LittleBigEndianCnvS(unsigned short int Value);

unsigned char MIDI1to0(unsigned long int SrcLen, unsigned char* SrcData,
						unsigned long int* RetDstLen, unsigned char** RetDstData)
{
	unsigned long int CurPos;
	unsigned short int TrkCnt;
	unsigned short int CurTrk;
	MIDITRK_INF MstTrk;
	MIDITRK_INF* TrkData;
	MIDITRK_INF* CurTData;
	unsigned long int DstLen;
	unsigned char* DstData;
	unsigned long int DataLen;
	unsigned long int LastTick;
	unsigned char TempByt;
	unsigned short int TempSht;
	unsigned long int TempLng;
	bool WriteDelay;
	// Last Delay Backup Values
	unsigned long int LD_Pos;
	unsigned long int LD_Tick;
	unsigned long int EvtsWritten;
	
	CurPos = 0x00;
	memcpy(&TempLng, &SrcData[CurPos + 0x00], 0x04);
	if (TempLng != FCC_MTHD)
		return 0xF0;
	memcpy(&TempLng, &SrcData[CurPos + 0x04], 0x04);
	DataLen = LittleBigEndianCnvL(TempLng);
	
	DstLen = SrcLen * 2;
	DstData = (unsigned char*)malloc(DstLen);
	memcpy(&DstData[CurPos + 0x00], &FCC_MTHD, 0x04);
	memcpy(&DstData[CurPos + 0x04], &TempLng, 0x04);
	CurPos += 0x08;
	
	// Read Header
	memcpy(&TrkCnt, &SrcData[CurPos + 0x02], 0x02);
	TrkCnt = LittleBigEndianCnvS(TrkCnt);
	// Write MIDI Format 0
	TempSht = LittleBigEndianCnvS(0x0000);
	memcpy(&DstData[CurPos + 0x00], &TempSht, 0x02);
	// Write Track Count 1
	TempSht = LittleBigEndianCnvS(0x0001);
	memcpy(&DstData[CurPos + 0x02], &TempSht, 0x02);
	// Write Resolution Rate (and other bytes, if used)
	memcpy(&DstData[CurPos + 0x04], &SrcData[CurPos + 0x04], DataLen - 0x04);
	CurPos += DataLen;
	
	TrkData = (MIDITRK_INF*)malloc(TrkCnt * sizeof(MIDITRK_INF));
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		memcpy(&TempLng, &SrcData[CurPos + 0x00], 0x04);
		if (TempLng != FCC_MTRK)
			return 0xE0;
		memcpy(&DataLen, &SrcData[CurPos + 0x04], 0x04);
		DataLen = LittleBigEndianCnvL(DataLen);
		
		TrkData[CurTrk].TrkBase = CurPos;
		TrkData[CurTrk].TrkEnd = CurPos + 0x08 + DataLen;
		CurPos += 0x08 + DataLen;
	}
	
	MstTrk.TrkBase = TrkData[0x00].TrkBase;
	MstTrk.CurPos = MstTrk.TrkBase;
	memcpy(&DstData[MstTrk.CurPos + 0x00], &FCC_MTRK, 0x04);
	TempLng = 0x00000000;
	memcpy(&DstData[MstTrk.CurPos + 0x04], &TempLng, 0x04);
	MstTrk.CurPos += 0x08;
	MstTrk.TickPos = 0x00000000;
	MstTrk.LastEvent = 0x00;
	MstTrk.RmbrEvent = 0x00;
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		CurTData = TrkData + CurTrk;
		CurTData->CurPos = CurTData->TrkBase + 0x08;
		CurTData->TickPos = 0x00000000;
		CurTData->LastEvent = 0x00;
		CurTData->RmbrEvent = 0x00;
		
		ReadMIDIValue(SrcData + CurTData->CurPos, &TempLng);
		CurTData->TickPos += TempLng;
	}
	
	LastTick = 0x00000000;
	MstTrk.TickPos = 0x00000000;
	do
	{
		// Search Next Event
		TempLng = MstTrk.TickPos;
		TempSht = 0x0000;
		for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
		{
			CurTData = TrkData + CurTrk;
			if (CurTData->CurPos >= CurTData->TrkEnd)
				continue;
			if (! TempSht || CurTData->TickPos < TempLng)
				TempLng = CurTData->TickPos;
			TempSht ++;
		}
		MstTrk.TickPos = TempLng;
		
		LD_Pos = MstTrk.CurPos;
		LD_Tick = LastTick;
		DataLen = WriteMIDIValue(DstData + MstTrk.CurPos, MstTrk.TickPos - LastTick);
		LastTick = MstTrk.TickPos;
		MstTrk.CurPos += DataLen;
		
		// Write Events
		TempSht = 0x0000;
		EvtsWritten = 0x00000000;
		WriteDelay = false;
		for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
		{
			CurTData = TrkData + CurTrk;
			if (CurTData->CurPos >= CurTData->TrkEnd)
			{
				TempSht ++;
				continue;
			}
			if (CurTData->TickPos > MstTrk.TickPos)
				continue;
			
			ReadMIDIValue(SrcData + CurTData->CurPos, &TempLng);
			CurTData->TickPos -= TempLng;
			while(CurTData->CurPos < CurTData->TrkEnd)
			{
				DataLen = ReadMIDIValue(SrcData + CurTData->CurPos, &TempLng);
				CurTData->TickPos += TempLng;
				if (CurTData->TickPos > MstTrk.TickPos)
					break;
				CurTData->CurPos += DataLen;
				
				if (WriteDelay)
				{
					LD_Pos = MstTrk.CurPos;
					LD_Tick = LastTick;
					DataLen = WriteMIDIValue(DstData + MstTrk.CurPos, 0);
					MstTrk.CurPos += DataLen;
					WriteDelay = false;
				}
				TempByt = CopyMIDIEvent(SrcData, DstData, CurTData, &MstTrk);
				switch(TempByt)
				{
				case 0x00:
					EvtsWritten ++;
					WriteDelay = true;
					break;
				case 0x01:	// Event ignored
					break;
				case 0xFF:	// Error
				default:
					printf("Invalid Event at Pos %lX\n", CurTData->CurPos);
					return 0xFF;
				}
			}
			if (CurTData->CurPos >= CurTData->TrkEnd)
				TempSht ++;
		}
		
		if (! WriteDelay && (TempSht < TrkCnt))
		{
			MstTrk.CurPos = LD_Pos;
			LastTick = LD_Tick;
		}
	} while(TempSht < TrkCnt);
	
	// Write Track End
	if (WriteDelay)
	{
		DstData[MstTrk.CurPos + 0x00] = 0x00;
		MstTrk.CurPos += 0x01;
	}
	DstData[MstTrk.CurPos + 0x00] = 0xFF;
	DstData[MstTrk.CurPos + 0x01] = 0x2F;
	DstData[MstTrk.CurPos + 0x02] = 0x00;
	MstTrk.CurPos += 0x03;
	
	MstTrk.TrkEnd = MstTrk.CurPos;
	TempLng = LittleBigEndianCnvL(MstTrk.TrkEnd - MstTrk.TrkBase - 0x08);
	memcpy(&DstData[MstTrk.TrkBase + 0x04], &TempLng, 0x04);
	DstLen = MstTrk.TrkEnd;
	//DstData = (unsigned char*)realloc(DstData, DstLen);	// Free unused Bytes
	
	*RetDstLen = DstLen;
	*RetDstData = DstData;
	
	return 0x00;
}

static unsigned long int ReadMIDIValue(unsigned char* FileData, unsigned long int* Value)
{
	unsigned char* DataPnt;
	unsigned long int TempLng;
	
	DataPnt = FileData;
	TempLng = 0x00000000;
	while(*DataPnt & 0x80)
	{
		TempLng <<= 7;
		TempLng |= *DataPnt & 0x7F;
		DataPnt ++;
	}
	TempLng <<= 7;
	TempLng |= *DataPnt & 0x7F;
	DataPnt ++;
	
	*Value = TempLng;
	return DataPnt - FileData;
}

static unsigned long int WriteMIDIValue(unsigned char* FileData, unsigned long int Value)
{
	unsigned char* DataPnt;
	unsigned char ByteCount;
	unsigned long int TempLng;
	
	TempLng = Value;
	ByteCount = 0x00;
	do
	{
		TempLng >>= 7;
		ByteCount ++;
	} while(TempLng);
	
	TempLng = Value;
	DataPnt = FileData + (ByteCount - 0x01);
	*DataPnt = 0x00 | ((unsigned char)TempLng & 0x7F);
	TempLng >>= 7;
	
	while(TempLng)
	{
		DataPnt --;
		*DataPnt = 0x80 | ((unsigned char)TempLng & 0x7F);
		TempLng >>= 7;
	}
	
	return ByteCount;
}

static unsigned char CopyMIDIEvent(unsigned char* SrcData, unsigned char* DstData,
								   MIDITRK_INF* TrkSrc, MIDITRK_INF* TrkDst)
{
	bool ShortEvt;
	unsigned long int DataLen;
	unsigned long int TempLng;
	
	if (SrcData[TrkSrc->CurPos] & 0x80)
	{
		TrkSrc->LastEvent = SrcData[TrkSrc->CurPos];
		if ((SrcData[TrkSrc->CurPos] & 0xF0) < 0xF0)
			TrkSrc->RmbrEvent = SrcData[TrkSrc->CurPos];
		TrkSrc->CurPos ++;
		ShortEvt = false;
	}
	else
	{
		if (! TrkSrc->RmbrEvent)
			return 0xFF;
		TrkSrc->LastEvent = TrkSrc->RmbrEvent;	// it's not valid to have short F? events
		ShortEvt = true;
	}
	
	if (! ShortEvt || (ShortEvt && TrkDst->LastEvent != TrkSrc->LastEvent))
	{
		DstData[TrkDst->CurPos] = TrkSrc->LastEvent;
		TrkDst->LastEvent = TrkSrc->LastEvent;
		TrkDst->CurPos ++;
	}
	
	switch(TrkSrc->LastEvent & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		// Copy 2 Bytes
		memcpy(&DstData[TrkDst->CurPos], &SrcData[TrkSrc->CurPos], 0x02);
		TrkSrc->CurPos += 0x02;	TrkDst->CurPos += 0x02;
		break;
	case 0xC0:
	case 0xD0:
		// Copy 1 Byte
		DstData[TrkDst->CurPos] = SrcData[TrkSrc->CurPos];
		TrkSrc->CurPos += 0x01;	TrkDst->CurPos += 0x01;
		break;
	case 0xF0:
		switch(TrkSrc->LastEvent)
		{
		case 0xF0:	// SysEx Data
			// Read Data Length
			TempLng = ReadMIDIValue(SrcData + TrkSrc->CurPos, &DataLen);
			DataLen += TempLng;
			// Copy Bytes
			memcpy(&DstData[TrkDst->CurPos], &SrcData[TrkSrc->CurPos], DataLen);
			TrkSrc->CurPos += DataLen;	TrkDst->CurPos += DataLen;
			break;
		case 0xFF:	// Meta Event
			// Check & Copy Meta Event ID
			switch(SrcData[TrkSrc->CurPos])
			{
			case 0x20:
			case 0x21:
			case 0x2F:
				// Ignore Event
				TrkSrc->CurPos += 0x01;
				TempLng = ReadMIDIValue(SrcData + TrkSrc->CurPos, &DataLen);
				TrkSrc->CurPos += DataLen + TempLng;
				TrkDst->CurPos -= 0x01;
				return 0x01;
			}
			DstData[TrkDst->CurPos] = SrcData[TrkSrc->CurPos];
			TrkSrc->CurPos += 0x01;	TrkDst->CurPos += 0x01;
			// Copy Meta Event Data
			TempLng = ReadMIDIValue(SrcData + TrkSrc->CurPos, &DataLen);
			DataLen += TempLng;
			memcpy(&DstData[TrkDst->CurPos], &SrcData[TrkSrc->CurPos], DataLen);
			TrkSrc->CurPos += DataLen;	TrkDst->CurPos += DataLen;
			break;
		}
		break;
	}
	
	return 0x00;
}

static unsigned long int LittleBigEndianCnvL(unsigned long int Value)
{
	return ((Value & 0xFF000000) >> 24) |
			((Value & 0x00FF0000) >> 8) |
			((Value & 0x0000FF00) << 8) |
			((Value & 0x000000FF) << 24);
}

static unsigned short int LittleBigEndianCnvS(unsigned short int Value)
{
	return ((Value & 0xFF00) >> 8) |
			((Value & 0x00FF) << 8);
}