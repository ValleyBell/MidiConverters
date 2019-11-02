// SYX -> MID Converter
// --------------------
// Written by Valley Bell, 14 October 2019

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdtype.h>

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE	static __inline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif
#endif	// INLINE


typedef struct file_information
{
	UINT32 alloc;	// allocated bytes
	UINT32 pos;		// current file offset
	UINT8* data;	// file data
} FILE_INF;


UINT8 Syx2Mid(void);
static UINT32 GetEventLength(UINT32 startPos);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);
static void WriteMidiHeader(FILE_INF* fInf, UINT16 format, UINT16 tracks, UINT16 resolution);
static void WriteMidiValue(FILE_INF* fInf, UINT32 value);
static void WriteLongEvent(FILE_INF* fInf, UINT32 delay, UINT8 evt, UINT32 dataLen, const void* data);
static void WriteMetaEvent(FILE_INF* fInf, UINT32 delay, UINT8 metaType, UINT32 dataLen, const void* data);

INLINE void WriteBE32(UINT8* buffer, UINT32 value);
INLINE void WriteBE16(UINT8* buffer, UINT16 value);


static UINT32 SyxLen;
static UINT8* SyxData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 480;
static UINT32 MIDI_TEMPO = 500000;	// 120 BPM

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	if (argc < 3)
	{
		printf("Usage: syx2mid.exe input.syx output.mid\n");
		return 0;
	}
	
	argbase = 1;
	if (argc < argbase + 2)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	SyxLen = ftell(hFile);
	if (SyxLen > 0x100000)
		SyxLen = 0x100000;
	
	fseek(hFile, 0x00, SEEK_SET);
	SyxData = (UINT8*)malloc(SyxLen);
	fread(SyxData, 0x01, SyxLen, hFile);
	
	fclose(hFile);
	
	retVal = Syx2Mid();
	if (! retVal)
		WriteFileData(MidLen, MidData, argv[argbase + 1]);
	free(MidData);	MidData = NULL;
	free(SyxData);	SyxData = NULL;
	
	return 0;
}

UINT8 Syx2Mid(void)
{
	FILE_INF midFileInf;
	UINT32 trkBasePos;
	UINT32 inPos;
	UINT32 evtLen;
	UINT8 tempArr[4];
	UINT32 ticksSec;	// ticks per second
	UINT32 delay;
	
	midFileInf.alloc = SyxLen * 3 / 2 + 0x20;
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0, 1, MIDI_RES);
	
	WriteBE32(&midFileInf.data[midFileInf.pos + 0x00], 0x4D54726B);	// write 'MTrk'
	WriteBE32(&midFileInf.data[midFileInf.pos + 0x04], 0x00000000);	// write dummy length
	midFileInf.pos += 0x08;
	trkBasePos = midFileInf.pos;
	
	WriteBE32(tempArr, MIDI_TEMPO);
	WriteMetaEvent(&midFileInf, 0, 0x51, 0x03, &tempArr[0x01]);
	ticksSec = MIDI_RES * 1000000 / MIDI_TEMPO;
	
	inPos = 0x00;
	delay = 0;
	while(inPos < SyxLen)
	{
		evtLen = GetEventLength(inPos);
		if (! evtLen)
			break;
		
		if (SyxData[inPos] < 0xF0)
		{
			WriteMidiValue(&midFileInf, delay);
			memcpy(&midFileInf.data[midFileInf.pos], &SyxData[inPos], evtLen);
			midFileInf.pos += evtLen;
		}
		else
		{
			WriteLongEvent(&midFileInf, delay, SyxData[inPos], evtLen - 1, &SyxData[inPos + 1]);
		}
		inPos += evtLen;
		// wait time [seconds] = evtLen [bytes] * 8 [bits/byte] / 31250 [bits/sec]
		delay = (ticksSec * evtLen * 4 + 15624) / 15625;	// calculate with integer ceil()
	}
	
	WriteMetaEvent(&midFileInf, delay, 0x2F, 0x00, NULL);
	WriteBE32(&midFileInf.data[trkBasePos - 0x04], midFileInf.pos - trkBasePos);	// write Track Length
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	return 0x00;
}

static UINT32 GetEventLength(UINT32 startPos)
{
	UINT8 evtType;
	UINT32 curPos;
	
	curPos = startPos;
	evtType = SyxData[curPos];	curPos ++;
	if (evtType < 0x80)
		return 0;
	if (evtType < 0xF0)
		return ((evtType & 0xE0) == 0xC0) ? 0x02 : 0x03;
	switch(evtType)
	{
	case 0xF0:
	case 0xF7:
		while(curPos < SyxLen && ! (SyxData[curPos] & 0x80))
			curPos ++;
		if (curPos < SyxLen && SyxData[curPos] == 0xF7)
			curPos ++;	// skip last F7 byte
		return curPos - startPos;
	default:
		return 0;
	}
}

static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName)
{
	FILE* hFile;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(data, 0x01, dataLen, hFile);
	fclose(hFile);
	
	return 0;
}

static void WriteMidiHeader(FILE_INF* fInf, UINT16 format, UINT16 tracks, UINT16 resolution)
{
	WriteBE32(&fInf->data[fInf->pos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&fInf->data[fInf->pos + 0x04], 0x00000006);	// Header Length
	fInf->pos += 0x08;
	
	WriteBE16(&fInf->data[fInf->pos + 0x00], format);		// MIDI Format (0/1/2)
	WriteBE16(&fInf->data[fInf->pos + 0x02], tracks);		// number of tracks
	WriteBE16(&fInf->data[fInf->pos + 0x04], resolution);	// Ticks per Quarter
	fInf->pos += 0x06;
	
	return;
}

static void WriteMidiValue(FILE_INF* fInf, UINT32 value)
{
	UINT8 valSize;
	UINT8* valData;
	UINT32 tempLng;
	UINT32 curPos;
	
	valSize = 0x00;
	tempLng = value;
	do
	{
		tempLng >>= 7;
		valSize ++;
	} while(tempLng);
	
	valData = &fInf->data[fInf->pos];
	curPos = valSize;
	tempLng = value;
	do
	{
		curPos --;
		valData[curPos] = 0x80 | (tempLng & 0x7F);
		tempLng >>= 7;
	} while(tempLng);
	valData[valSize - 1] &= 0x7F;
	
	fInf->pos += valSize;
	
	return;
}

static void WriteLongEvent(FILE_INF* fInf, UINT32 delay, UINT8 evt, UINT32 dataLen, const void* data)
{
	WriteMidiValue(fInf, delay);
	
	fInf->data[fInf->pos + 0x00] = evt;
	fInf->pos += 0x01;
	WriteMidiValue(fInf, dataLen);
	memcpy(&fInf->data[fInf->pos], data, dataLen);
	fInf->pos += dataLen;
	
	return;
}

static void WriteMetaEvent(FILE_INF* fInf, UINT32 delay, UINT8 metaType, UINT32 dataLen, const void* data)
{
	WriteMidiValue(fInf, delay);
	
	fInf->data[fInf->pos + 0x00] = 0xFF;
	fInf->data[fInf->pos + 0x01] = metaType;
	fInf->pos += 0x02;
	WriteMidiValue(fInf, dataLen);
	memcpy(&fInf->data[fInf->pos], data, dataLen);
	fInf->pos += dataLen;
	
	return;
}

INLINE void WriteBE32(UINT8* buffer, UINT32 value)
{
	buffer[0x00] = (value >> 24) & 0xFF;
	buffer[0x01] = (value >> 16) & 0xFF;
	buffer[0x02] = (value >>  8) & 0xFF;
	buffer[0x03] = (value >>  0) & 0xFF;
	
	return;
}

INLINE void WriteBE16(UINT8* buffer, UINT16 value)
{
	buffer[0x00] = (value >> 8) & 0xFF;
	buffer[0x01] = (value >> 0) & 0xFF;
	
	return;
}
