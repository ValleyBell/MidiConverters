// IMS2MID
// -------
// Programmed by Valley Bell
// January 2015 (original Visual Basic 6 version)
// 25 December 2015 (C version)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "stdtype.h"

#define INLINE	static __inline


int main(int argc, char* argv[]);
static UINT8 OpenFile(const char* FileName, UINT32* retFileLen, UINT8** retFileData);
static UINT8 WriteFile(const char* FileName, const UINT32 FileLen, const UINT8* FileData);
static UINT8 IMS2MID(UINT32 imsLen, const UINT8* imsData, UINT32* refMidLen, UINT8** refmidData);
INLINE UINT32 Tempo2Mid(UINT8 baseBPM, UINT16 factor);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT32 ReadLE32(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);


int main(int argc, char* argv[])
{
	UINT32 ImsLen;
	UINT8* ImsData;
	UINT32 MidLen;
	UINT8* midData;
	int argbase;
	UINT8 retVal;
	
	printf("IMS2MID\n");
	printf("-------\n");
	if (argc < 2)
	{
		printf("Usage: ims2mid.exe input.ims output.mid\n");
		return 0;
	}
	
	argbase = 1;
	/*while(argbase < argc && argv[argbase][0] == '-')
	{
		if (0)
			fixFile = 1;
		else
			break;
		argbase ++;
	}*/
	if (argbase + 1 >= argc)
	{
		printf("Not enough arguments!\n");
		return 1;
	}
	ImsData = NULL;
	retVal = OpenFile(argv[argbase + 0], &ImsLen, &ImsData);
	if (retVal)
	{
		if (ImsData != NULL)
			free(ImsData);
		switch(retVal)
		{
		case 0x01:
			printf("Unable to read complete file!\n");
			break;
		case 0xF0:
			printf("Not enough memory for IMS file!\n");
			break;
		case 0xFF:
			printf("Error opening file!\n");
			break;
		}
		return -1;
	}
	
	MidLen = ImsLen * 2;
	midData = (UINT8*)malloc(MidLen);
	if (midData == NULL)
	{
		free(ImsData);
		printf("Not enough memory for MIDI buffer!\n");
	}
	IMS2MID(ImsLen, ImsData, &MidLen, &midData);
	
	retVal = WriteFile(argv[argbase + 1], MidLen, midData);
	if (retVal == 0xFF)
	{
		free(ImsData);	ImsData = NULL;
		free(midData);	midData = NULL;
		printf("Error writing file!\n");
		return 1;
	}
	printf("File written.\n");
	free(ImsData);	ImsData = NULL;
	free(midData);	midData = NULL;
	
	//_getch();
	return 0;
}

static UINT8 OpenFile(const char* FileName, UINT32* retFileLen, UINT8** retFileData)
{
	FILE* hFile;
	UINT32 FileLen;
	UINT32 bytesWrt;
	UINT8* FileData;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return 0xFF;
	
	fseek(hFile, 0x00, SEEK_END);
	FileLen = ftell(hFile);
	
	// Read Data
	FileData = (UINT8*)malloc(FileLen);
	if (FileData == NULL)
	{
		fclose(hFile);
		return 0xF0;
	}
	rewind(hFile);
	bytesWrt = fread(FileData, 0x01, FileLen, hFile);
	
	fclose(hFile);
	
	*retFileLen = bytesWrt;
	*retFileData = FileData;
	if (bytesWrt < FileLen)
		return 0x01;
	return 0x00;
}

static UINT8 WriteFile(const char* FileName, const UINT32 FileLen, const UINT8* FileData)
{
	FILE* hFile;
	UINT32 bytesWrt;
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
		return 0xFF;
	bytesWrt = fwrite(FileData, 0x01, FileLen, hFile);
	
	fclose(hFile);
	
	if (bytesWrt < FileLen)
		return 0x01;
	return 0x00;
}

static UINT8 IMS2MID(UINT32 imsLen, const UINT8* imsData, UINT32* refMidLen, UINT8** refmidData)
{
	UINT32 midLen;
	UINT8* midData;
	UINT32 midTrkBase;
	UINT32 curPos;
	UINT32 midPos;
	UINT32 ticksPerBeat;
	UINT32 curDelay;
	UINT8 curEvt;
	UINT8 fullEvtCmd;
	UINT8 curChn;
	UINT8 fileEnd;
	UINT8 baseBPM;
	UINT16 tempoFactor;
	INT16 pbValue;
	UINT32 midTempoVal;
	UINT32 dataSize;
	UINT8 tempArr[4];
	UINT8 chnNote[0x10];
	
	midLen = *refMidLen;
	midData = *refmidData;
	
	ticksPerBeat = imsData[0x24];
	baseBPM = imsData[0x3C];
	
	midPos = 0x00;
	WriteBE32(&midData[midPos], 0x4D546864);	midPos += 0x04;	// 'MThd' Signature
	WriteBE32(&midData[midPos], 0x00000006);	midPos += 0x04;	// Header Size
	WriteBE16(&midData[midPos], 0x0000);		midPos += 0x02;	// Format: 0
	WriteBE16(&midData[midPos], 0x0001);		midPos += 0x02;	// 1 Track
	WriteBE16(&midData[midPos], ticksPerBeat);	midPos += 0x02;	// Ticks per Quarter
	
	WriteBE32(&midData[midPos], 0x4D54726B);	// write 'MTrk'
	midPos += 0x08;
	midTrkBase = midPos;
	
	// write default Tempo
	WriteMidiValue(midData, &midPos, 0);	// write delay
	midTempoVal = Tempo2Mid(baseBPM, 0);
	WriteBE32(tempArr, midTempoVal);
	WriteMetaEvent_Data(midData, &midPos, 0x51, 0x03, &tempArr[1]);
	
	memset(chnNote, 0x00, 0x10);
	fileEnd = 0;
	curPos = 0x46;
	while(! fileEnd && curPos < imsLen)
	{
		// process delay
		curDelay = 0;
		while(imsData[curPos] == 0xF8)
		{
			curDelay += ticksPerBeat;
			curPos ++;
		}
		if (imsData[curPos] >= 0xF0)
			printf("Unknown delay 0x%02X found at offset %04X!\n", imsData[curPos], curPos);
		curDelay += imsData[curPos];
		curPos ++;
		WriteMidiValue(midData, &midPos, curDelay);
		
		fullEvtCmd = (imsData[curPos] & 0x80);
		if (fullEvtCmd)
		{
			curEvt = imsData[curPos];	curPos ++;
			midData[midPos] = curEvt;	midPos ++;
			curChn = curEvt & 0x0F;
		}
		switch(curEvt & 0xF0)
		{
		case 0x80:	// Note Retrigger
			if (! fullEvtCmd)
			{
				midData[midPos] = 0x80 | curChn;
				midPos ++;
			}
			midData[midPos + 0x00] = chnNote[curChn];
			midData[midPos + 0x01] = 0x00;
			midPos += 0x02;
			WriteMidiValue(midData, &midPos, 0);
			
			chnNote[curChn] = imsData[curPos + 0x00];
			midData[midPos + 0x00] = 0x90 | curChn;
			midData[midPos + 0x01] = imsData[curPos + 0x00];
			if (imsData[curPos + 0x01])
				midData[midPos + 0x02] = imsData[curPos + 0x01];
			else
				midData[midPos + 0x02] = 0x01;
			curPos += 0x02;	midPos += 0x03;
			break;
		case 0x90:	// Note On/Off
			if (imsData[curPos + 0x01])
				chnNote[curChn] = imsData[curPos + 0x00];
			else
				chnNote[curChn] = 0x00;
			midData[midPos + 0x00] = imsData[curPos + 0x00];
			midData[midPos + 0x01] = imsData[curPos + 0x01];
			curPos += 0x02;	midPos += 0x02;
			break;
		case 0xA0:	// Note Velocity Change
			if (fullEvtCmd)
				midData[midPos - 0x01] = 0xB0 | curChn;
			midData[midPos + 0x00] = 0x27;
			midData[midPos + 0x01] = imsData[curPos + 0x00];
			curPos += 0x01;	midPos += 0x02;
			break;
		case 0xC0:	// Set Instrument
			midData[midPos + 0x00] = imsData[curPos + 0x00];
			curPos += 0x01;	midPos += 0x01;
			break;
		case 0xE0:	// Pitch Bend
			pbValue =	((imsData[curPos + 0x00] & 0x7F) << 0) |
						((imsData[curPos + 0x01] & 0x7F) << 7);
			// IMS files (like ROL) support only a semitone range for pitch bends.
			// MIDI files default to 2 semitones, so I need fix the value.
			// 0x0000..0x2000..0x3FFF -> 0x1000..0x2000..0x2FFF
			pbValue = (pbValue / 2) + 0x1000;
			midData[midPos + 0x00] = (pbValue >> 0) & 0x7F;
			midData[midPos + 0x01] = (pbValue >> 7) & 0x7F;
			curPos += 0x02;	midPos += 0x02;
			break;
		case 0xB0:	// unused event types
		case 0xD0:
			printf("Unknown event 0x%02X found at offset %04X!\n", curEvt, curPos - 0x01);
			curPos += 0x02;
			break;
		case 0xF0:
			switch(curEvt)
			{
			case 0xF0:	// Tempo
				dataSize = 0x00;
				while(curPos + dataSize < imsLen && imsData[curPos + dataSize] != 0xF7)
					dataSize ++;
				dataSize ++;	// skip F7 terminator
				
				if (imsData[curPos + 0x00] == 0x7F && imsData[curPos + 0x01] == 0x00)
				{
					if (fullEvtCmd)
						midPos --;	// remove F0 command
					tempoFactor =	((imsData[curPos + 0x02] & 0x7F) << 7) |
									((imsData[curPos + 0x03] & 0x7F) << 0);
					midTempoVal = Tempo2Mid(baseBPM, tempoFactor);
					WriteBE32(tempArr, midTempoVal);
					WriteMetaEvent_Data(midData, &midPos, 0x51, 0x03, &tempArr[1]);
				}
				else
				{
					WriteMidiValue(midData, &midPos, dataSize);
					memcpy(&midData[midPos], &imsData[curPos], dataSize);
					midPos += dataSize;
				}
				curPos += dataSize;
				break;
			case 0xFC:	// Track End
				midPos --;
				WriteEvent(midData, &midPos, 0xFF, 0x2F, 0x00);
				fileEnd = 1;
				break;
			default:
				printf("Unknown event 0x%02X found at offset %04X!\n", curEvt, curPos - 0x01);
				break;
			}
		}	// end switch(curEvt & 0xF0)
	}	// end while(! fileEnd)
	
	WriteBE32(&midData[midTrkBase - 0x04], midPos - midTrkBase);	// write Track Length
	midLen = midPos;
	
	*refMidLen = midLen;
	*refmidData = midData;
	
	return 0x00;
}

INLINE UINT32 Tempo2Mid(UINT8 baseBPM, UINT16 factor)
{
	double finalBPM;
	
	if (! factor)
		factor = 0x80;
	finalBPM = baseBPM * factor / 128.0;
	if (finalBPM >= 1.0)
		return (UINT32)(60000000.0 / finalBPM + 0.5);
	else
		return 0;
}

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	if (! (Evt & 0x80))
		return;
	
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

static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
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
