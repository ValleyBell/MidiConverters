#include <stdtype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "midi_funcs.h"

static UINT8 ConvertDemoSong(void);


#define SONG_OFFSET	0x01959E
#define ROM_SIZE	0x020000

static UINT32 srcSize;
static UINT8* srcData;
static UINT32 dstSize;
static UINT8* dstData;

int main(int argc, char* argv[])
{
	FILE* hFile;
	
	if (argc < 3)
	{
		printf("Usage: %s xk731c00.bin out.mid\n");
		return 0;
	}
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file %s!\n", argv[1]);
		return 1;
	}
	
	fseek(hFile, SONG_OFFSET, SEEK_SET);
	srcSize = ROM_SIZE - SONG_OFFSET;
	srcData = (UINT8*)malloc(srcSize);
	fread(srcData, 1, srcSize, hFile);
	
	fclose(hFile);
	
	ConvertDemoSong();
	
	hFile = fopen(argv[2], "wb");
	if (hFile == NULL)
	{
		printf("Error opening file %s!\n", argv[2]);
		return 1;
	}
	
	fwrite(dstData, 1, dstSize, hFile);
	
	fclose(hFile);
	
	free(srcData);	srcData = NULL;
	free(dstData);	dstData = NULL;
	
	return;
}

static UINT8 ConvertDemoSong(void)
{
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT32 curPos;
	UINT32 syxLen;
	UINT8 curCmd;
	UINT8 trkEnd;
	
	midFileInf.alloc = 0x8000;
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x00, 1, 0xC8);
	
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.curDly = 0;
	MTS.midChn = 0x00;
	
	trkEnd = 0;
	curPos = 0x00;
	while(curPos < srcSize && ! trkEnd)
	{
		curCmd = srcData[curPos];
		switch(curCmd & 0xF0)
		{
		// usual MIDI events
		case 0x80:
			WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], 0x40);
			curPos += 0x02;
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
			case 0xF2:	// song end
				trkEnd = 1;
				break;
			case 0xF3:	// 8-bit delay
				MTS.curDly += srcData[curPos + 0x01];
				curPos += 0x02;
				break;
			case 0xF4:	// 16-bit delay
				MTS.curDly += (srcData[curPos + 0x01] << 0) | (srcData[curPos + 0x02] << 8);
				curPos += 0x03;
				break;
			default:
				printf("Encountered unknown event 0x%02X!\n", curCmd);
				trkEnd = 1;
				break;
			}
			break;
		default:
			printf("Encountered unknown event 0x%02X!\n", curCmd);
			trkEnd = 1;
			break;
		}
	}
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	dstData = midFileInf.data;
	dstSize = midFileInf.pos;
	
	return 0x00;
}
