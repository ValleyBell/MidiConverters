// MIDI Output Routines
// --------------------
// written by Valley Bell
// to be included as header file

#include <stdlib.h>
#include <string.h>

#include <stdtype.h>


typedef struct _midi_track_state
{
	UINT32 TrkBase;
	UINT32 CurDly;	// delay until next event
	UINT8 MidChn;
} MID_TRK_STATE;

typedef struct file_information
{
	UINT32 Alloc;	// allocated bytes
	UINT32 Pos;		// current file offset
	UINT8* Data;	// file data
} FILE_INF;


static void WriteMidiDelay(FILE_INF* fInf, UINT32* Delay);
static void WriteEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteLongEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT32 DataLen, const UINT8* Data);
static void WriteMetaEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(FILE_INF* fInf, UINT32 Value);
static void File_CheckRealloc(FILE_INF* FileInf, UINT32 BytesNeeded);
static void WriteMidiHeader(FILE_INF* fInf, UINT16 Format, UINT16 Tracks, UINT16 Resolution);
static void WriteMidiTrackStart(FILE_INF* fInf, MID_TRK_STATE* MTS);
static void WriteMidiTrackEnd(FILE_INF* fInf, MID_TRK_STATE* MTS);

static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);


static void WriteMidiDelay(FILE_INF* fInf, UINT32* Delay)
{
	WriteMidiValue(fInf, *Delay);
	if (*Delay)
	{
		UINT8 CurNote;
		
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
			RunNotes[CurNote].RemLen -= (UINT16)*Delay;
		*Delay = 0x00;
	}
	
	return;
}

static void WriteEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	WriteMidiDelay(fInf, &MTS->CurDly);
	
	File_CheckRealloc(fInf, 0x03);
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		fInf->Data[fInf->Pos + 0x00] = Evt | MTS->MidChn;
		fInf->Data[fInf->Pos + 0x01] = Val1;
		fInf->Data[fInf->Pos + 0x02] = Val2;
		fInf->Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		fInf->Data[fInf->Pos + 0x00] = Evt | MTS->MidChn;
		fInf->Data[fInf->Pos + 0x01] = Val1;
		fInf->Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		fInf->Data[fInf->Pos + 0x00] = Evt;
		fInf->Data[fInf->Pos + 0x01] = Val1;
		fInf->Data[fInf->Pos + 0x02] = Val2;
		fInf->Pos += 0x03;
		break;
	default:
		break;
	}
	
	return;
}

static void WriteLongEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 Evt, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiDelay(fInf, &MTS->CurDly);
	
	File_CheckRealloc(fInf, 0x01 + 0x04 + DataLen);	// worst case: 4 bytes of data length
	fInf->Data[fInf->Pos + 0x00] = Evt;
	fInf->Pos += 0x01;
	WriteMidiValue(fInf, DataLen);
	memcpy(&fInf->Data[fInf->Pos], Data, DataLen);
	fInf->Pos += DataLen;
	
	return;
}

static void WriteMetaEvent(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiDelay(fInf, &MTS->CurDly);
	
	File_CheckRealloc(fInf, 0x02 + 0x05 + DataLen);	// worst case: 5 bytes of data length
	fInf->Data[fInf->Pos + 0x00] = 0xFF;
	fInf->Data[fInf->Pos + 0x01] = MetaType;
	fInf->Pos += 0x02;
	WriteMidiValue(fInf, DataLen);
	memcpy(&fInf->Data[fInf->Pos], Data, DataLen);
	fInf->Pos += DataLen;
	
	return;
}

static void WriteMidiValue(FILE_INF* fInf, UINT32 Value)
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
	
	File_CheckRealloc(fInf, ValSize);
	ValData = &fInf->Data[fInf->Pos];
	CurPos = ValSize;
	TempLng = Value;
	do
	{
		CurPos --;
		ValData[CurPos] = 0x80 | (TempLng & 0x7F);
		TempLng >>= 7;
	} while(TempLng);
	ValData[ValSize - 1] &= 0x7F;
	
	fInf->Pos += ValSize;
	
	return;
}

static void File_CheckRealloc(FILE_INF* FileInf, UINT32 BytesNeeded)
{
#define REALLOC_STEP	0x8000	// 32 KB block
	UINT32 MinPos;
	
	MinPos = FileInf->Pos + BytesNeeded;
	if (MinPos <= FileInf->Alloc)
		return;
	
	while(MinPos > FileInf->Alloc)
		FileInf->Alloc += REALLOC_STEP;
	FileInf->Data = (UINT8*)realloc(FileInf->Data, FileInf->Alloc);
	
	return;
}

static void WriteMidiHeader(FILE_INF* fInf, UINT16 Format, UINT16 Tracks, UINT16 Resolution)
{
	File_CheckRealloc(fInf, 0x08 + 0x06);
	
	WriteBE32(&fInf->Data[fInf->Pos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&fInf->Data[fInf->Pos + 0x04], 0x00000006);	// Header Length
	fInf->Pos += 0x08;
	
	WriteBE16(&fInf->Data[fInf->Pos + 0x00], Format);		// MIDI Format (0/1/2)
	WriteBE16(&fInf->Data[fInf->Pos + 0x02], Tracks);		// number of tracks
	WriteBE16(&fInf->Data[fInf->Pos + 0x04], Resolution);	// Ticks per Quarter
	fInf->Pos += 0x06;
	
	return;
}

static void WriteMidiTrackStart(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	File_CheckRealloc(fInf, 0x08);
	
	WriteBE32(&fInf->Data[fInf->Pos + 0x00], 0x4D54726B);	// write 'MTrk'
	WriteBE32(&fInf->Data[fInf->Pos + 0x04], 0x00000000);	// write dummy length
	fInf->Pos += 0x08;
	
	MTS->TrkBase = fInf->Pos;
	MTS->CurDly = 0;
	
	return;
}

static void WriteMidiTrackEnd(FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT32 TrkLen;
	
	TrkLen = fInf->Pos - MTS->TrkBase;
	WriteBE32(&fInf->Data[MTS->TrkBase - 0x04], TrkLen);	// write Track Length
	
	return;
}


static void WriteBE32(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value >> 24) & 0xFF;
	Buffer[0x01] = (Value >> 16) & 0xFF;
	Buffer[0x02] = (Value >>  8) & 0xFF;
	Buffer[0x03] = (Value >>  0) & 0xFF;
	
	return;
}

static void WriteBE16(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value >> 8) & 0xFF;
	Buffer[0x01] = (Value >> 0) & 0xFF;
	
	return;
}
