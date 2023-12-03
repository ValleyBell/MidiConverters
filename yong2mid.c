// YongYong -> Midi Converter
// --------------------------
// Written by Valley Bell, 22/27 July 2018
// based on FMP3 -> Midi Converter

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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


#include "midi_funcs.h"


UINT8 Yong2Mid(UINT16 songID, UINT32 songOfs, UINT32 patListOfs);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);

INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT32 AddrGB2ROM(UINT16 addr, UINT32 romBankOfs);
INLINE double Lin2DB(UINT8 LinVol);
INLINE UINT8 DB2Mid(double DB);
INLINE UINT32 GBTimer2Mid(UINT8 valTMA);
INLINE UINT32 BPM2Mid(UINT8 valBPM);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidAlloc;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 8;
static UINT16 NUM_LOOPS = 2;
static UINT8 DRV_VER = 2;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	UINT8 curSng;
	UINT8 songCnt;
	UINT16 songListOfs;
	UINT16 patListOfs;
	const char* fileExt;
	char* outName;
	char* outExt;
	UINT32 songOfs;
	UINT32 patOfs;
	
	printf("YongYong -> Midi Converter\n--------------------------\n");
	if (argc < 3)
	{
		printf("Usage: yong2mid.exe input.bin output.mid\n");
		printf("Verified games: Sonic 3D Blast 5 (GameBoy)\n");
		return 0;
	}
	
	argbase = 1;
	songCnt = 11;
	songListOfs = 0x4000;
	patListOfs = 0;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! _stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}
		else
			break;
		argbase ++;
	}
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
	ROMLen = ftell(hFile);
	if (ROMLen > 0xFFFF)	// 64 KB
		ROMLen = 0xFFFF;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	fileExt = strrchr(argv[argbase + 1], '.');
	if (fileExt == NULL)
		fileExt = argv[1] + strlen(argv[argbase + 1]);
	outName = (char*)malloc(strlen(argv[argbase + 1]) + 0x10);
	strcpy(outName, argv[argbase + 1]);
	outExt = outName + (fileExt - argv[argbase + 1]);
	
	MidAlloc = 0x10000;	// 64 KB should be enough
	MidData = (UINT8*)malloc(MidAlloc);
	
	if (! patListOfs)
		patListOfs = songListOfs + songCnt * 0x02;
	
	for (curSng = 0x00; curSng < songCnt; curSng ++)
	{
		printf("File %02X / %02X ...", curSng, songCnt);
		
		songOfs = AddrGB2ROM(ReadLE16(&ROMData[songListOfs + curSng * 0x02]), songListOfs);
		patOfs = AddrGB2ROM(ReadLE16(&ROMData[patListOfs + curSng * 0x02]), patListOfs);
		
		retVal = Yong2Mid(curSng, songOfs, patOfs);
		if (retVal)
		{
			printf("Error converting file!\n");
			continue;
		}
		
		sprintf(outExt, "_%02X%s", curSng, fileExt);
		retVal = WriteFileData(MidLen, MidData, outName);
		if (retVal)
			continue;
		printf("\n");
	}
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	free(MidData);	MidData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

UINT8 Yong2Mid(UINT16 songID, UINT32 songOfs, UINT32 patListOfs)
{
	static const char* TRK_NAMES[4] = {"Square 1", "Square 2", "Wave", "Noise"};
	UINT8 trkCnt;
	UINT16 trkPtrs[4];
	UINT32 basePos;	// track base offset
	UINT32 inPos;
	UINT16 mstLoopCount;
	
	UINT32 mainPos;
	UINT32 patPos;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	UINT8 curTrk;
	INT8 noteMove;
	
	UINT8 curCmd;
	UINT8 curPat;
	UINT8 trkEnd;
	UINT8 lastNote;
	UINT8 tempByt;
	UINT8 timerVal;
	UINT8 tempBuf[0x04];
	char tempStr[0x20];
	
	inPos = songOfs;
	tempByt = ROMData[inPos];	inPos ++;
	if (tempByt >= 0x10)
	{
		printf("Invalid channel mask!\n");
		return 0x80;
	}
	trkCnt = 0;
	for (curTrk = 0; curTrk < 4; curTrk ++, tempByt >>= 1)
	{
		if (tempByt & 0x01)
		{
			trkPtrs[curTrk] = ReadLE16(&ROMData[inPos]);
			inPos += 0x02;
			trkCnt ++;
		}
		else
		{
			trkPtrs[curTrk] = 0x0000;
		}
	}
	
	midFInf.alloc = MidAlloc;
	midFInf.data = MidData;
	midFInf.pos = 0x00;
	
	WriteMidiHeader(&midFInf, 1, 1 + trkCnt, MIDI_RES);
	
	// dummy tempo track
	WriteMidiTrackStart(&midFInf, &MTS);
	WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFInf, &MTS);
	
	for (curTrk = 0; curTrk < 4; curTrk ++)
	{
		if (! trkPtrs[curTrk])
			continue;
		
		WriteMidiTrackStart(&midFInf, &MTS);
		MTS.midChn = curTrk;
		basePos = AddrGB2ROM(trkPtrs[curTrk], songOfs);
		
		WriteMetaEvent(&midFInf, &MTS, 0x03, strlen(TRK_NAMES[curTrk]), TRK_NAMES[curTrk]);
		if (curTrk == 3)
			WriteEvent(&midFInf, &MTS, 0xC0, 0x75, 0x00);
		else if (curTrk == 2)
			WriteEvent(&midFInf, &MTS, 0xC0, 0x51, 0x00);
		else
			WriteEvent(&midFInf, &MTS, 0xC0, 0x50, 0x00);
		
		lastNote = 0xFF;
		noteMove = +36;
		mstLoopCount = 0;
		mainPos = basePos;
		trkEnd = 0;
		while(! trkEnd)
		{
			curCmd = ROMData[mainPos];	mainPos ++;
			switch(curCmd)
			{
			case 0xFF:	// set master params
				if (DRV_VER == 1)
				{
					mainPos += 0x03;
				}
				else
				{
					if (ROMData[mainPos + 0x03] == 0xFC)
						mainPos += 0x03;	// workaround for "silence" song in Sonic Adventure 7.
					else if (curTrk == 0)
						mainPos += 0x06;	// Square 1 channel
					else
						mainPos += 0x05;	// other channels
				}
				break;
			case 0xFE:	// set Tempo
				// Is the parameter intended to be in BPM?
				tempByt = ROMData[mainPos];
				mainPos ++;
				if (DRV_VER == 1)
				{
					// Sonic 3D Blast 5: the sound driver adds 0x49 (without overflow checking) to get the TMA value
					timerVal = tempByt + 0x49;
					
					sprintf(tempStr, "Tempo %u, TMA = 0x%02X\n", tempByt, timerVal);
					WriteMetaEvent(&midFInf, &MTS, 0x06, strlen(tempStr), tempStr);
				}
				else
				{
					// Sonic Adventure 7: it stores the raw TMA value
					timerVal = tempByt;
					tempByt -= 0x49;
					
					sprintf(tempStr, "TMA = 0x%02X\n", timerVal);
					WriteMetaEvent(&midFInf, &MTS, 0x06, strlen(tempStr), tempStr);
				}
				
				if (1)
					WriteBE32(tempBuf, GBTimer2Mid(timerVal));
				else
					WriteBE32(tempBuf, BPM2Mid(tempByt));
				WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &tempBuf[0x01]);
				break;
			case 0xFD:	// set Volume
				if (DRV_VER == 1)
					tempByt = ROMData[mainPos] & 0x0F;
				else
					tempByt = (ROMData[mainPos] >> 4) & 0x0F;
				mainPos ++;
				
				WriteEvent(&midFInf, &MTS, 0xB0, 0x07, DB2Mid(Lin2DB(tempByt)));
				break;
			case 0xFC:	// Track End
				tempByt = ROMData[mainPos];
				mainPos ++;
				if (tempByt)
				{
					trkEnd = 1;
					break;
				}
				else
				{
					mainPos = basePos;	// loop back
					
					mstLoopCount ++;
					WriteEvent(&midFInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCount);
					if (mstLoopCount >= NUM_LOOPS)
						trkEnd = 1;
				}
				break;
			default:
				curPat = curCmd;
				patPos = AddrGB2ROM(ReadLE16(&ROMData[patListOfs + curPat * 0x02]), patListOfs);
				while(ROMData[patPos] != 0)
				{
					if (! ROMData[patPos + 0x01])
					{
						lastNote = 0xFF;
						MTS.curDly += ROMData[patPos + 0x00];
					}
					else
					{
						lastNote = ROMData[patPos + 0x01] + noteMove;
						WriteEvent(&midFInf, &MTS, 0x90, lastNote, 0x7F);
						MTS.curDly += ROMData[patPos + 0x00];
						WriteEvent(&midFInf, &MTS, 0x90, lastNote, 0x00);
					}
					patPos += 0x02;
				}
				break;
			}
		}
		WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFInf, &MTS);
	}
	MidData = midFInf.data;
	MidLen = midFInf.pos;
	
	return 0x00;
}

static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName)
{
	FILE* hFile;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(data, 0x01, dataLen, hFile);
	fclose(hFile);
	
	return 0;
}


INLINE double Lin2DB(UINT8 LinVol)
{
	//return log(LinVol / 15.0) / log(2.0) * 6.0;
	return log(LinVol / 15.0) * 8.65617024533378;
}

INLINE UINT8 DB2Mid(double DB)
{
	if (DB > 0.0)
		DB = 0.0;
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

INLINE UINT32 GBTimer2Mid(UINT8 valTMA)
{
	double ticksPerSec;
	
	ticksPerSec = 4096.0 / (0x100 - valTMA);	// timer interrupt rate
	ticksPerSec /= 4.0;	// only 1 of 4 tracks is updated each interrupt
	return (UINT32)(1000000 * MIDI_RES / ticksPerSec + 0.5);
}

INLINE UINT32 BPM2Mid(UINT8 valBPM)
{
	return 60000000 / valBPM;
}


INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 AddrGB2ROM(UINT16 addr, UINT32 romBankOfs)
{
	if (addr < 0x4000)
		return addr;
	else
		return (romBankOfs & ~0x3FFF) | (addr & 0x3FFF);
}
