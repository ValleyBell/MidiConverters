// MID -> SYX Converter
// --------------------
// Written by Valley Bell, 26 June 2023

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stdtype.h"

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE	static __inline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif
#endif	// INLINE


UINT8 Mid2Syx(void);
static UINT32 ReadVarLenValue(UINT32* inPos);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);

INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT32 ReadBE32(const UINT8* Data);


static UINT32 midLen;
static UINT8* midData;
static UINT32 syxLen;
static UINT8* syxData;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	if (argc < 3)
	{
		printf("Usage: mid2syx.exe input.mid output.syx\n");
		printf("The SYX file will contain all SysEx commands from the MID file.\n");
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
	midLen = ftell(hFile);
	if (midLen > 0x100000)
		midLen = 0x100000;
	
	fseek(hFile, 0x00, SEEK_SET);
	midData = (UINT8*)malloc(midLen);
	fread(midData, 0x01, midLen, hFile);
	
	fclose(hFile);
	
	retVal = Mid2Syx();
	if (! retVal)
		WriteFileData(syxLen, syxData, argv[argbase + 1]);
	free(syxData);	syxData = NULL;
	free(midData);	midData = NULL;
	
	return 0;
}

UINT8 Mid2Syx(void)
{
	UINT32 syxPos;
	UINT32 trkLen;
	UINT32 inPos;
	UINT16 trkCnt;
	UINT16 curTrk;
	UINT8 trkEnd;
	
	syxLen = midLen;
	syxData = (UINT8*)malloc(syxLen);
	syxPos = 0x00;
	
	inPos = 0x00;
	if (memcmp(&midData[inPos + 0x00], "MThd", 0x04))
	{
		printf("Not a MID file!\n");
		return 0xFF;
	}
	trkLen = ReadBE32(&midData[inPos + 0x04]);
	trkCnt = ReadBE16(&midData[inPos + 0x0A]);
	inPos += 0x08 + trkLen;
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		UINT32 trkBasePos;
		UINT8 curCmd = 0x00;
		
		if (memcmp(&midData[inPos + 0x00], "MTrk", 0x04))
		{
			printf("Track %u (pos 0x%04X): Invalid track signature!\n", curTrk, inPos);
			return 0xFF;
		}
		trkLen = ReadBE32(&midData[inPos + 0x04]);
		inPos += 0x08;
		trkBasePos = inPos;
		
		trkEnd = 0;
		while(!trkEnd && inPos < trkBasePos + trkLen)
		{
			UINT8 newRS;	// new "running status" byte
			UINT32 cmdLen;
			
			ReadVarLenValue(&inPos);	// skip delay
			
			newRS = (midData[inPos] & 0x80);
			if (newRS)
			{
				curCmd = midData[inPos];
				inPos ++;
			}
			switch(curCmd & 0xF0)
			{
			case 0x80:	// Note Off
			case 0x90:	// Note On
			case 0xA0:	// Note Aftertouch
			case 0xB0:	// Control Change
			case 0xE0:	// Pitch Bend
				inPos += 0x02;
				break;
			case 0xC0:	// Instrument
			case 0xD0:	// Channel Aftertouch
				inPos += 0x01;
				break;
			case 0xF0:	// special
				switch(curCmd)
				{
				case 0xF0:	// SysEx
					cmdLen = ReadVarLenValue(&inPos);
					syxData[syxPos] = 0xF0;	syxPos ++;
					memcpy(&syxData[syxPos], &midData[inPos], cmdLen);
					syxPos += cmdLen;
					inPos += cmdLen;
					break;
				case 0xF7:	// SysEx Continuation
					cmdLen = ReadVarLenValue(&inPos);
					// do NOT insert an additional byte
					memcpy(&syxData[syxPos], &midData[inPos], cmdLen);
					syxPos += cmdLen;
					inPos += cmdLen;
					break;
				case 0xFF:	// Meta Event
					curCmd = midData[inPos];	inPos ++;
					if (curCmd == 0x2F)	// Track End event
						trkEnd = 1;	// normal end
					cmdLen = ReadVarLenValue(&inPos);
					inPos += cmdLen;
					break;
				default:
					printf("Unknown event %02X at %06X\n", curCmd, inPos);
					inPos += 0x01;
					trkEnd = 1;
					break;
				}
				break;
			default:
				printf("Unknown event %02X at %06X\n", curCmd, inPos);
				inPos += 0x01;
				trkEnd = 1;
				break;
			}
		}
		
		inPos = trkBasePos + trkLen;
	}
	
	syxLen = syxPos;
	return 0x00;
}

static UINT32 ReadVarLenValue(UINT32* inPos)
{
	UINT32 pos = *inPos;
	UINT32 value = 0x00;
	while(midData[pos] & 0x80)
	{
		value |= (midData[pos] & 0x7F);	pos ++;
		value <<= 7;
	}
	value |= midData[pos];	pos ++;
	*inPos = pos;
	return value;
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

INLINE UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

INLINE UINT32 ReadBE32(const UINT8* Data)
{
	return	(Data[0x00] << 24) | (Data[0x01] << 16) |
			(Data[0x02] <<  8) | (Data[0x03] <<  0);
}
