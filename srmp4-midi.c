// Super Real Mahjong PIV Midi Converter
// -------------------------------------
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


#include "midi_funcs.h"


UINT8 Seq2Mid(UINT32 SongLen, const UINT8* SongData);
static UINT8 WriteFileData(UINT32 dataLen, const UINT8* data, const char* fileName);

INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT32 ReadBE32(const UINT8* data);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

int main(int argc, char* argv[])
{
	int argbase;
	FILE* hFile;
	UINT8 retVal;
	
	printf("SRMP4 Midi Converter\n");
	printf("--------------------\n");
	if (argc < 3)
	{
		printf("Usage: srmp4-midi input.bin output.mid\n");
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	argbase = 1;
	
	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	ROMLen = ftell(hFile);
	if (ROMLen > 0x100000)	// 1 MB
		ROMLen = 0x100000;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	retVal = Seq2Mid(ROMLen, ROMData);
	if (! retVal)
		WriteFileData(MidLen, MidData, argv[argbase + 1]);
	free(MidData);	MidData = NULL;
	
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

UINT8 Seq2Mid(UINT32 SongLen, const UINT8* SongData)
{
	UINT32 inPos;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	UINT16 midiFmt;
	UINT16 midiRes;
	UINT16 curTrk;
	UINT32 chkBasePos;
	UINT32 chkLength;
	UINT8 curCmd;
	UINT8 trkEnd;
	UINT8 tempByt;
	UINT16 tempSht;
	INT16 tempPB;
	UINT8 paramBuf[4];
	char tempStr[0x20];
	
	inPos = 0x00;
	if (memcmp(&SongData[inPos], "SIFF", 0x04))
	{
		printf("Invalid file signature!\n");
		return 0x80;
	}
	chkLength = ReadBE32(&SongData[inPos + 0x04]);	// read SIFF chunk length
	inPos += 0x08;
	chkLength &= 0x7FFFFFFF;	// chop off "last chunk" bit
	if (inPos + chkLength < SongLen)
		SongLen = inPos + chkLength;
	
	if (memcmp(&SongData[inPos], "seqs", 0x04))
	{
		printf("Not a SIFF sequence!\n");
		return 0x81;
	}
	chkLength = ReadBE32(&SongData[inPos + 0x04]);	// read seqs chunk length
	inPos += 0x08;
	chkLength &= 0x7FFFFFFF;
	if (inPos + chkLength < SongLen)
		SongLen = inPos + chkLength;
	
	midFInf.alloc = 0x10000;	// 64 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	// yes, these values are Little Endian
	tempSht = ReadLE16(&SongData[inPos + 0x00]);	// read 0x0001
	tempSht = ReadLE16(&SongData[inPos + 0x02]);	// read 0x0016
	tempSht = ReadLE16(&SongData[inPos + 0x04]);	// read 0x0000
	inPos += 0x06;
	
	midiFmt = 1;
	midiRes = 24;
	WriteMidiHeader(&midFInf, midiFmt, 0, midiRes);
	
	for (curTrk = 0; ; curTrk ++)
	{
		if (memcmp(&SongData[inPos], "trck", 0x04))
			break;
		chkLength = ReadBE32(&SongData[inPos + 0x04]) & 0x7FFFFFFF;
		inPos += 0x08;
		chkBasePos = inPos;
		
		WriteMidiTrackStart(&midFInf, &MTS);
		MTS.midChn = 0x00;
		trkEnd = 0;
		while(! trkEnd)
		{
			curCmd = SongData[inPos];	inPos ++;
			switch(curCmd & 0xF0)
			{
			case 0x80:	// Note Off
				WriteEvent(&midFInf, &MTS, curCmd, SongData[inPos], 0x40);
				inPos += 0x01;
				break;
			case 0x90:	// Note On
			case 0xA0:	// unverified
				WriteEvent(&midFInf, &MTS, curCmd, SongData[inPos + 0x00], SongData[inPos + 0x01]);
				inPos += 0x02;
				break;
			case 0xB0:	// Control Change
				tempByt = SongData[inPos + 0x00];
				// controller 2 is volume, so just swap 2 and 7 for now
				if (tempByt == 0x02)
					tempByt = 0x07;
				else if (tempByt == 0x07)
					tempByt = 0x02;
				WriteEvent(&midFInf, &MTS, curCmd, tempByt, SongData[inPos + 0x01]);
				inPos += 0x02;
				break;
			case 0xC0:	// Instrument Change
			case 0xD0:	// unverified
				WriteEvent(&midFInf, &MTS, curCmd, SongData[inPos], 0x40);
				inPos += 0x01;
				break;
			case 0xE0:	// Pitch Bend
				// 0x80 = center, 0x70 = 1 semitone down, 0x90 = 1 semitone up
				tempPB = (INT16)SongData[inPos] - 0x80;
				// scale to default MIDI PitchBend range (center at 0x2000, semitone = +-0x1000)
				tempPB <<= 8;
				tempPB += 0x2000;
				WriteEvent(&midFInf, &MTS, curCmd, (tempPB >> 0) & 0x7F, (tempPB >> 7) & 0x7F);
				inPos += 0x01;
				break;
			case 0xF0:
				switch(curCmd)
				{
				case 0xF0:	// Delay
					MTS.curDly += SongData[inPos];
					inPos += 0x01;
					break;
				case 0xF1:	// Track End
					WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
					trkEnd = 1;
					break;
				case 0xF2:	// Tempo
					tempSht = ReadLE16(&SongData[inPos]);
					inPos += 0x02;
					
					sprintf(tempStr, "Tempo Value: 0x%03X\n", tempSht);
					WriteMetaEvent(&midFInf, &MTS, 0x06, strlen(tempStr), tempStr);
					
					WriteBE32(paramBuf, tempSht * 0x800);
					WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &paramBuf[0x01]);
					break;
				}
				break;
			}
		}
		WriteMidiTrackEnd(&midFInf, &MTS);
		
		inPos = chkBasePos + chkLength;
	}
	if (memcmp(&SongData[inPos], "tend", 0x04))
		printf("Sequence not terminated by tend!\n");
	MidData = midFInf.data;
	MidLen = midFInf.pos;
	
	// rewrite header with correct track count
	midFInf.pos = 0x00;
	WriteMidiHeader(&midFInf, midiFmt, curTrk, midiRes);
	
	return 0x00;
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
	
	return 0x00;
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}
