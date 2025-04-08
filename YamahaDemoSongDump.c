// Yamaha TG/MU Demo Song Dumper
// -----------------------------
#include "stdtype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "midi_funcs.h"

static UINT16 DetectTrackCount(UINT32 ptrListOfs);
static UINT8 ConvertDemoSong(void);
static UINT32 ReadBE32(const UINT8* data);


// TG100 v1.10 (xk731c00.bin)
//	Demo Song offset:	0x01959E

// MU5 v1.00
//	Demo Song track pointers: 0x0103A6 (1 port, offset 0x0106A0)

// MU50 v1.04
//	Demo Song track pointers: 0x00671E (1 port, offset 0x077880, duplicated 3 times)
//
// Code for loading [v1.04]:
//	0069B0 MOV.L	@(0x00671E, er0), er6	0100 7800 6B26 0000 671E
//	0069BA MOV.L	er6, @(0x209498)		0100 6BA6 0020 9498

// MU80 v1.04
//	Demo Song track pointers: 0x02DA60 (2 ports, offsets 0x0776C0, 0x07CB8A)
//
// Code for loading [v1.04]:
//	02DF22 MOV.L	@(0x02DA60, er0), er6	0100 7800 6B26 0002 DA60
//	02DF2C MOV.L	er6, @(0x20CFF6)		0100 6BA6 0020 CFF6
//	02DF34 MOV.L	@(0x02DA64, er0), er6	0100 7800 6B26 0002 DA64
//	02DF3E MOV.L	er6, @(0x20CFFC)		0100 6BA6 0020 CFFC

// MU100 v1.05 (xt71420.ic11)
//	MU100 Demo Song track pointers: 0x00AC6A (2 ports, offsets 0x100D28, 0x119184)
//	MU100R Demo Song track pointers: 0x00AC72 (2 ports, offsets 0x10CF50, 0x11CB25)
// MU100 v1.11 (xu50720.ic11)
//	MU100 Demo Song track pointers: 0x00ACB4 (2 ports, offsets 0x100D28, 0x119184)
//	MU100R Demo Song track pointers: 0x00ACBC (2 ports, offsets 0x10CF50, 0x11CB25)
//
// Code for loading [v1.11]:
//	00B120 MOV.L	@(0x00ACB4, er0), er6	0100 7800 6B26 0000 ACB4
//	00B12A MOV.L	er6, @(0x212702)		0100 6BA6 0021 2702
//	00B132 MOV.L	@(0x00ACB8, er0), er6	0100 7800 6B26 0000 ACB8
//	00B13C MOV.L	er6, @(0x212708)		0100 6BA6 0021 2708

// MU128 v2.00 upgrade data (v200.bin)
//	Demo Song track pointers: [missing] (4 ports, offsets 0x0A8500, 0x0B98E2, 0x0C3643, 0x0C5F5E)
//	Credits Song track pointers: [missing] (4 ports, offsets 0x0A8500, 0x0B98E2, 0x0C9A63, 0x0D4660)

// VL70-m v1.11 (27c160.bin)
//	song track pointers: 0x0EB1F6 (1 pointer per song)
//	song title pointers: 0x0EB21E
//	songs:
//		0x0ABEA2	01 Mad Tube
//		0x0ACEFC	02 JzTrump
//		0x0B01D4	03 Shakuha!
//		0x0B5AF8	04 GuitHero
//		0x0BA804	05 Jazz Sax
//		0x0BE24E	06 Waterphn
//		0x0BE6E4	07 WahUpHp
//		0x0C0496	08 TenorSub
//		0x0C1EBC	09 BowPicol
//		0x0C3114	10 Eastern


#define MIDI_RES	200	// a resolution of 200 (= 400 Hz) results in a reasonable tempo

static UINT32 srcSize;
static UINT8* srcData;
static UINT32 dstSize;
static UINT8* dstData;

static UINT32 ptrListOfs = 0;
static UINT16 trkCount = 0;
static UINT8 ptrMode = 0;

int main(int argc, char* argv[])
{
	FILE* hFile;
	
	printf("Yamaha TG/MU Demo Song Dumper\n-----------------------------\n");
	if (argc < 4)
	{
		printf("Usage: %s rom.bin ptrListOffset out.mid\n", argv[0]);
		return 0;
	}
	
	ptrListOfs = (UINT32)strtoul(argv[2], NULL, 0);
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file %s!\n", argv[1]);
		return 1;
	}
	
	fseek(hFile, 0, SEEK_END);
	srcSize = ftell(hFile);
	fseek(hFile, 0, SEEK_SET);
	srcData = (UINT8*)malloc(srcSize);
	fread(srcData, 1, srcSize, hFile);
	
	fclose(hFile);
	
	trkCount = DetectTrackCount(ptrListOfs);
	if (! trkCount)
	{
		printf("Offset pointing to direct track data.\n");
		ptrMode = 0;
		trkCount = 1;
	}
	else
	{
		printf("Offset pointing to track list.\n");
		ptrMode = 1;
	}
	
	ConvertDemoSong();
	
	hFile = fopen(argv[3], "wb");
	if (hFile == NULL)
	{
		printf("Error opening file %s!\n", argv[2]);
		free(srcData);	srcData = NULL;
		return 1;
	}
	
	fwrite(dstData, 1, dstSize, hFile);
	
	fclose(hFile);
	
	free(srcData);	srcData = NULL;
	free(dstData);	dstData = NULL;
	
	return 0;
}

static UINT16 DetectTrackCount(UINT32 ptrListOfs)
{
	UINT32 curPos;
	UINT32 tempOfs;
	
	for (curPos = ptrListOfs; curPos < srcSize; curPos += 0x04)
	{
		tempOfs = ReadBE32(&srcData[curPos]);
		if (tempOfs >= srcSize)
			break;
	}
	
	return (curPos - ptrListOfs) / 0x04;
}

static UINT8 ConvertDemoSong(void)
{
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT32 curPos;
	UINT32 syxLen;
	UINT8 curCmd;
	UINT8 trkEnd;
	UINT16 curTrk;
	UINT8 off3;	// 3-byte note off event
	
	midFileInf.alloc = 0x8000;
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	if (trkCount <= 1)
		WriteMidiHeader(&midFileInf, 0, trkCount, MIDI_RES);	// format 0 for single-track MIDIs
	else
		WriteMidiHeader(&midFileInf, 1, trkCount, MIDI_RES);
	
	off3 = 0;
	for (curTrk = 0; curTrk < trkCount; curTrk ++)
	{
	
	printf("Converting track %u / %u ...\n", 1 + curTrk, trkCount);
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.curDly = 0;
	MTS.midChn = 0x00;
	
	if (trkCount > 1)
		WriteMetaEvent(&midFileInf, &MTS, 0x21, 0x01, &curTrk);	// track ID == port number
	
	trkEnd = 0;
	if (! ptrMode)
		curPos = ptrListOfs;
	else
		curPos = ReadBE32(&srcData[ptrListOfs + curTrk * 0x04]);
	while(curPos < srcSize && ! trkEnd)
	{
		curCmd = srcData[curPos];
		switch(curCmd & 0xF0)
		{
		// usual MIDI events
		case 0x80:
			// velocity being 0x40 internally was verified using a MU100 ROM
			if (! off3)
			{
				WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], 0x40);
				curPos += 0x02;
			}
			else
			{
				// The MU5 demo song uses this format.
				// ... and even then the MU5 demo code assumes the "no velocity" format and just
				// skips over all unknown command codes.
				WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], srcData[curPos + 0x02]);
				curPos += 0x03;
			}
			break;
		case 0x90:
		case 0xA0:
		case 0xB0:
		case 0xE0:
			WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], srcData[curPos + 0x02]);
			curPos += 0x03;
			break;
		case 0xC0:
		case 0xD0:
			WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], 0x00);
			curPos += 0x02;
			break;
		// special events
		case 0xF0:
			switch(curCmd)
			{
			case 0xF0:	// SysEx Event
				for (syxLen = 0x01; curPos + syxLen < srcSize - 1; syxLen ++)
				{
					if (srcData[curPos + syxLen] == 0xF7)
						break;
				}
				WriteLongEvent(&midFileInf, &MTS, 0xF0, syxLen, &srcData[curPos + 0x01]);
				curPos += 0x01 + syxLen;
				break;
			case 0xF1:	// unknown
				// found in MU5 demo song, but ignored by playback routine
				printf("Warning: Unknown event %02X at position 0x%06X\n", curCmd, curPos);
				off3 = 1;
				curPos += 0x02;
				break;
			case 0xF2:	// song end
				trkEnd = 1;
				break;
			case 0xF3:	// 8-bit delay
				{
					// Note: Internally on the MU100, all delays are divided by 4 before being
					//       used for timing. I omit this behaviour here, as I haven't checked
					//       how other modules behave and all delays were multiples of 4 anyway.
					UINT8 dly = srcData[curPos + 0x01];
					MTS.curDly += dly;
				}
				curPos += 0x02;
				break;
			case 0xF4:	// 16-bit delay
				{
					// formula verified with MU100 ROM disassembly
					UINT16 dly = (srcData[curPos + 0x01] & 0x7F) | (srcData[curPos + 0x02] << 7);
					MTS.curDly += dly;
				}
				curPos += 0x03;
				break;
			case 0xF5:	// ?? (copied to straight to MIDI command buffer)
				// Is this a "port select" flag? (The serial protocol allows this.)
				printf("Warning: Unhandled event %02X %02X at position 0x%06X\n",
					curCmd, srcData[curPos + 0x01], curPos);
				curPos += 0x02;
				break;
			case 0xF9:	// unknown
				// found in MU5 demo song, but ignored by playback routine
				printf("Warning: Unknown event %02X at position 0x%06X\n", curCmd, curPos);
				curPos += 0x03;
				break;
			default:
				printf("Encountered unknown event 0x%02X at position 0x%06X\n", curCmd, curPos);
				trkEnd = 1;
				break;
			}
			break;
		default:
			printf("Encountered unknown event 0x%02X at position 0x%06X\n", curCmd, curPos);
			trkEnd = 1;
			break;
		}
	}
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	}	// end for (curTrk)
	
	dstData = midFileInf.data;
	dstSize = midFileInf.pos;
	printf("Done.\n");
	
	return 0x00;
}

static UINT32 ReadBE32(const UINT8* data)
{
	return	(data[0x00] << 24) | (data[0x01] << 16) |
			(data[0x02] <<  8) | (data[0x03] <<  0);
}
