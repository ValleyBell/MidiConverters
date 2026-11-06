// Accolade MegaDrive -> Midi Converter
// ------------------------------------
// Written by Valley Bell, 6 November 2026

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

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


#include "midi_funcs.h"


UINT8 Accolade2Mid(UINT32 songLen, const UINT8* songData);
UINT8 Accolade_InsDump(UINT32 insLen, const UINT8* insData);
static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData, UINT32 maxFileSize);
static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const UINT8* data);
INLINE UINT32 Tempo2Mid(UINT16 bpm);
INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT32 ReadBE32(const UINT8* data);
static const char* GetLastDirSepPos(const char* fileName);
INLINE const char* GetFileTitle(const char* fileName);


#define MODE_MUS	0x00
#define MODE_INS	0x01

static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 120;
static UINT16 NUM_LOOPS = 2;

int main(int argc, char* argv[])
{
	int argbase;
	UINT8 retVal;
	UINT8 convMode;
	int result;
	const char* romFileName;
	const char* outFileName;
	UINT32 dataPos;
	
	printf("Accolade -> Midi Converter\n--------------------------\n");
	if (argc < 3)
	{
		printf("Usage, dump single song:    %s -mus [options] rom.bin output.mid songOffset\n", argv[0]);
		printf("Usage, dump songs:          %s -mus [options] rom.bin output.mid songListOffset songCount\n", argv[0]);
		printf("Usage, dump instruments:    %s -ins [options] rom.bin output.gyb insDataOffset\n", argv[0]);
		printf("Modes:\n");
		printf("    -mus        Music Mode (convert sequences to MID)\n");
		printf("                In song list mode, the tool will add the song number to the file name.\n");
		printf("                Use 0 as songCount to auto-detect the number of songs.\n");
		printf("    -ins        Instrument Mode (dump instruments to GYB)\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: %u)\n", NUM_LOOPS);
		printf("    -TpQ n      Sets the number of Ticks per Quarter to n. (default: %u)\n", MIDI_RES);
		return 0;
	}
	
	argbase = 1;
	convMode = MODE_MUS;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Mus"))
		{
			convMode = MODE_MUS;
		}
		else if (! stricmp(argv[argbase] + 1, "Ins"))
		{
			convMode = MODE_INS;
		}
		else if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				MIDI_RES = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! MIDI_RES)
					MIDI_RES = 120;
			}
		}
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 3)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	romFileName = argv[argbase + 0];
	outFileName = argv[argbase + 1];
	
	retVal = ReadFileData(romFileName, &ROMLen, &ROMData, 0x400000);	// 4 MB
	if (retVal)
		return 1;
	
	result = 0;
	switch(convMode)
	{
	case MODE_MUS:
		dataPos = (UINT32)strtoul(argv[argbase + 2], NULL, 0);
		if (argc <= argbase + 3)
		{
			UINT32 songPos = dataPos;
			UINT16 songLen;
			if (songPos + 0x10 > ROMLen)
			{
				printf("Invalid ROM offset\n");
				result = 1;
				break;
			}
			
			songLen = ReadLE16(&ROMData[songPos]);
			retVal = Accolade2Mid(songLen, &ROMData[songPos]);
			if (retVal)
			{
				result = retVal;
				printf("Conversion error\n");
				break;
			}
			
			retVal = WriteFileData(outFileName, MidLen, MidData);
			if (retVal)
				result = 1;
			free(MidData);	MidData = NULL;
		}
		else
		{
			const char* fileExt;
			char* outName;
			char* outExt;
			UINT32 songListPos;
			UINT16 curFile;
			UINT16 fileCnt;
			
			fileExt = strrchr(GetFileTitle(outFileName), '.');
			if (fileExt == NULL)
				fileExt = outFileName + strlen(outFileName);
			outName = (char*)malloc(strlen(outFileName) + 0x10);
			strcpy(outName, outFileName);
			outExt = outName + (fileExt - outFileName);
			
			songListPos = dataPos;
			if (dataPos + 0x04 > ROMLen)
			{
				printf("Bad offset!\n");
				result = 1;
				break;
			}
			//printf("Song list offset: 0x%06X\n", songListPos);
			
			// detect number of songs
			fileCnt = (UINT16)strtoul(argv[argbase + 3], NULL, 0);
			if (fileCnt == 0)
			{
				for (fileCnt = 0; fileCnt < 0x100; fileCnt ++)
				{
					UINT32 songPos = ReadBE32(&ROMData[songListPos + fileCnt * 0x04]);
					if (songPos + 0x10 > ROMLen)
						break;
					if (ROMData[songPos + 0x02] != 0xFC)
						break;
				}
			}
			
			for (curFile = 0; curFile < fileCnt; curFile ++)
			{
				UINT32 songPos = ReadBE32(&ROMData[songListPos + curFile * 0x04]);
				UINT16 songLen;
				printf("File %u / %u (ofs 0x%06X) ...", 1 + curFile, fileCnt, songPos);
				
				if (songPos + 0x10 > ROMLen)
				{
					printf("  invalid ROM offset\n");
					continue;
				}
				songLen = ReadLE16(&ROMData[songPos]);
				retVal = Accolade2Mid(songLen, &ROMData[songPos]);
				if (retVal)
				{
					result = retVal;
					printf("  conversion error\n");
					continue;
				}
				
				// "Bubsy" uses 1-based indices internally, so I'm using those here as well.
				sprintf(outExt, "%02u%s", 1 + curFile, fileExt);
				if (! retVal)
				{
					retVal = WriteFileData(outName, MidLen, MidData);
					if (retVal)
						result = 1;
				}
				free(MidData);	MidData = NULL;
				printf("\n");
			}
		}
		printf("Done.\n");
		break;
	case MODE_INS:
		dataPos = (UINT32)strtoul(argv[argbase + 2], NULL, 0);
		if (dataPos + 0x20 > ROMLen)
		{
			printf("Invalid ROM offset\n");
			result = 1;
			break;
		}
		retVal = Accolade_InsDump(ROMLen - dataPos, &ROMData[dataPos]);
		if (retVal)
		{
			result = retVal;
			break;
		}
		retVal = WriteFileData(outFileName, MidLen, MidData);
		if (retVal)
			result = 1;
		free(MidData);	MidData = NULL;
		printf("Done.\n");
		break;
	}
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	//getchar();
#endif
	
	return result;
}

UINT8 Accolade2Mid(UINT32 songLen, const UINT8* songData)
{
	UINT32 inPos;
	UINT32 loopPos;
	UINT8 loopHasDelay;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 tempArr[0x04];
	UINT16 mstLoopCnt;
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	inPos = ReadLE16(&songData[0x04]);
	
	WriteMidiHeader(&midFileInf, 0x0000, 1, MIDI_RES);
	
	WriteMidiTrackStart(&midFileInf, &MTS);
	
	MTS.midChn = 0x00;
	WriteBE32(tempArr, Tempo2Mid(150));
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
	
	mstLoopCnt = 0;
	loopPos = 0x00;
	loopHasDelay = 0x00;
	trkEnd = 0;
	while(inPos < songLen && ! trkEnd)
	{
		UINT8 curCmd;
		UINT8 hasDelay;
		
		curCmd = songData[inPos];	inPos ++;
		hasDelay = (curCmd & 0x80);
		curCmd |= 0x80;	// convert to MIDI command ID (the sound driver does this as well!)
		switch(curCmd & 0xF0)
		{
		case 0x80:	// Note Off
			WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x40);
			inPos += 0x01;
			break;
		case 0x90:	// Note On
			WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], songData[inPos + 0x01]);
			inPos += 0x02;
			break;
		case 0xA0:	// Loop Marker
		{
			UINT8 numLoops = songData[inPos];	inPos += 0x01;
			// This value is the intended number of loops.
			// However the sound driver loops infinitely regardless of the actual value, so we do this as well.
			if (numLoops == 0)
			{
				// loop start
				mstLoopCnt = 0;
				loopPos = inPos;
				loopHasDelay = hasDelay;
			}
			else
			{
				// loop end (jumps back to loop start)
				mstLoopCnt ++;
				if (mstLoopCnt < NUM_LOOPS)
				{
					inPos = loopPos;
					hasDelay = loopHasDelay;	// important! (else Bubsy song 1 breaks)
				}
			}
			WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)mstLoopCnt);
		}
			break;
		case 0xB0:	// unused
			printf("Unknown event %02X at offset %04X\n", curCmd, inPos - 0x01);
			WriteEvent(&midFileInf, &MTS, curCmd, 0x10, 0x00);
			//inPos += 0x00;
			break;
		case 0xC0:	// set instrument
			WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x00);
			inPos += 0x01;
			break;
		case 0xD0:	// ??
			printf("Unknown event %02X at offset %04X\n", curCmd, inPos - 0x01);
			WriteEvent(&midFileInf, &MTS, curCmd, 0x00, 0x00);
			//inPos += 0x00;
			break;
		case 0xE0:	// Pitch Bend? (unused)
			printf("Unknown event %02X at offset %04X\n", curCmd, inPos - 0x01);
			WriteEvent(&midFileInf, &MTS, curCmd, songData[inPos + 0x00], 0x00);
			inPos += 0x01;
			break;
		case 0xF0:	// Track End
			if (curCmd != 0xFC)
				printf("Unusual track end command 0x%02X at offset %04X!\n", curCmd, inPos - 0x01);
			trkEnd = 1;
			hasDelay = 0;
			break;
		}
		
		if (hasDelay)
		{
			UINT16 cmdDelay = songData[inPos];	inPos ++;
			if (cmdDelay & 0x80)
			{
				cmdDelay &= 0x007F;
				cmdDelay |= (songData[inPos] << 7);	inPos ++;
			}
			MTS.curDly += cmdDelay;
		}
	}
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static UINT8 ReadFileData(const char* fileName, UINT32* retSize, UINT8** retData, UINT32 maxFileSize)
{
	FILE* hFile = fopen(fileName, "rb");
	if (hFile == NULL)
	{
		printf("Error reading %s!\n", fileName);
		return 0xFF;
	}
	
	fseek(hFile, 0, SEEK_END);
	*retSize = ftell(hFile);
	if (*retSize > maxFileSize)
		*retSize = maxFileSize;
	
	*retData = (UINT8*)malloc(*retSize);
	fseek(hFile, 0x00, SEEK_SET);
	fread(*retData, 0x01, *retSize, hFile);
	
	fclose(hFile);
	
	return 0x00;
}

static UINT8 WriteFileData(const char* fileName, UINT32 dataLen, const UINT8* data)
{
	FILE* hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(data, 0x01, dataLen, hFile);
	fclose(hFile);
	
	return 0x00;
}

INLINE UINT32 Tempo2Mid(UINT16 bpm)
{
	// formula: (60 000 000 / bpm) * (MIDI_RES / 120)
	return 500000 * MIDI_RES / bpm;
}

UINT8 Accolade_InsDump(UINT32 insLen, const UINT8* insData)
{
	// GYB order is: 30..3C, 40..4C, ... , 90..9C, B0, B4, transposition, padding
	// Accolade order is: 30, 40, 50, 60, 70, 80, 90, 34, 44, ..., 8C, 9C, A4, A0, B0, B4
	static const UINT8 INS_REG_MAP[0x1E] =	// GYB register -> Accolade register mapping
	{	0x00, 0x07, 0x0E, 0x15,	// 30-3C
		0x01, 0x08, 0x0F, 0x16,	// 40-4C
		0x02, 0x09, 0x10, 0x17,	// 50-5C
		0x03, 0x0A, 0x11, 0x18,	// 60-6C
		0x04, 0x0B, 0x12, 0x19,	// 70-7C
		0x05, 0x0C, 0x13, 0x1A,	// 80-8C
		0x06, 0x0D, 0x14, 0x1B,	// 90-9C
		0x1E, 0x1F};			// B0, B4
	UINT32 gybAlloc;
	UINT8* gybData;
	UINT8 insCnt;
	UINT8 curIns;
	UINT8 curReg;
	char tempStr[0x80];
	UINT32 insPos;
	UINT32 gybPos;
	
	insPos = 0x00;
	if (memcmp((const char*)&insData[0x00], "SGAINSTR", 8))
	{
		printf("Bad instrument data offset: Signature missing\n");
		return 0x80;
	}
	insPos += 0x08;
	
#if 0
	for (insCnt = 0; insCnt < 0xF00/0x20; insCnt ++)
	{
		const UINT8* insPtr = &insData[insCnt * 0x20];
		if ((insPtr[0x1F] & 0xC0) == 0x00)	// 
			break;
	}
	printf("Instruments counted: 0x%02X\n", insCnt);
#else
	// "Bubsy" always copies 0xF00 bytes, so let's just be sure for now.
	insCnt = 0xF00/0x20;
#endif
	
	// required space: 0x06 + 0x100 + 0x80*[insDataLen==0x20 + 0x01 + nameMax==0x32] + 0x04 = 0x2A8A
	gybAlloc = 0x4000;	// This is be more than enough space
	gybData = (UINT8*)malloc(gybAlloc);
	
	// write header
	gybPos = 0x00;
	gybData[gybPos + 0x00] = 26;		// signature byte 1
	gybData[gybPos + 0x01] = 12;		// signature byte 2
	gybData[gybPos + 0x02] = 0x02;		// version
	gybData[gybPos + 0x03] = insCnt;	// melody instruments
	gybData[gybPos + 0x04] = 0x00;		// drum instruments
	gybPos += 0x05;
	
	// write MIDI instrument mappings (melody + drums)
	memset(&gybData[gybPos], 0xFF, 0x80 * 2);	// default: not assigned (0xFF)
	// order is: melody 0, drum 0, melody 1, drum 1, ..., drum 127
	for (curIns = 0; curIns < insCnt; curIns ++)
	{
		gybData[gybPos + curIns * 0x02 + 0x00] = curIns;	// GM Mapping: melody
	}
	gybPos += 0x100;
	
	// write LFO Value
	gybData[gybPos] = 0x00;
	gybPos += 0x01;
	
	// write instrument data
	for (curIns = 0; curIns < insCnt; curIns ++, insPos += 0x20, gybPos += 0x20)
	{
		UINT8 panLfoReg;
		for (curReg = 0x00; curReg < 0x1D; curReg ++)
			gybData[gybPos + curReg] = insData[insPos + INS_REG_MAP[curReg]];
		panLfoReg = insData[insPos + INS_REG_MAP[0x1D]];
		gybData[gybPos + 0x1D] = panLfoReg & 0x3F;	// strip stereo flags
		
		if (panLfoReg & 0xC0)
			gybData[gybPos + 0x1E] = -12;	// transposition
		else
			gybData[gybPos + 0x1E] = 0;		// no transposition for (probably) unused instrument
		gybData[gybPos + 0x1F] = 0x00;	// padding
	}
	
	// write instrument names
	for (curIns = 0; curIns < insCnt; curIns ++)
	{
		int nameLen = sprintf(tempStr, "Instrument %u", 1 + curIns);
		if (nameLen > 0xFF)
			nameLen = 0xFF;
		
		gybData[gybPos + 0x00] = (UINT8)nameLen;
		memcpy(&gybData[gybPos + 0x01], tempStr, nameLen);
		gybPos += 0x01 + nameLen;
	}
	
	// "dummy" checksum
	gybData[gybPos + 0x00] = 0x00;
	gybData[gybPos + 0x01] = 0x00;
	gybData[gybPos + 0x02] = 0x00;
	gybData[gybPos + 0x03] = 0x00;
	gybPos += 0x04;
	
	MidData = gybData;
	MidLen = gybPos;
	
	return 0x00;
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return	(data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}

static const char* GetLastDirSepPos(const char* fileName)
{
	const char* sepPos;
	const char* wSepPos;	// Windows separator
	
	sepPos = strrchr(fileName, '/');
	wSepPos = strrchr(fileName, '\\');
	if (wSepPos == NULL)
		return sepPos;
	else if (sepPos == NULL)
		return wSepPos;
	return (wSepPos > sepPos) ? wSepPos : sepPos;
}

INLINE const char* GetFileTitle(const char* fileName)
{
	const char* sepPos = GetLastDirSepPos(fileName);
	return (sepPos == NULL) ? fileName : (sepPos + 1);
}
