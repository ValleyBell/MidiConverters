/*
	Taito FX-1B MIDI converter by superctr

	These games use fairly standard MIDI datastream similar to format 0 MIDI files.
	Command 0xf0-0xff are proprietary.
*/
#include "stdtype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "midi_funcs.h"
#define SONG_OFFSET 0x7FF8

static UINT8 ConvertSong(UINT32);
static void GetSongTable(UINT32);

#define ROM_SIZE	0x080000

static UINT32 srcSize;
static UINT8* srcData;
static UINT32 dstSize;
static UINT8* dstData;

#define SONG_MAX 256
static UINT16 songCount;
static UINT32 songTable[SONG_MAX];// for now, assume that's the max amt of sequences.

static UINT8 temp_bytes[8];

int main(int argc, char* argv[])
{
	FILE* hFile;
	
	if (argc < 2)
	{
		printf("Usage: %s soundrom.bin [song_id out.mid]\n");
		printf("leave song_id blank to just get song count\n");
		return 0;
	}
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file %s!\n", argv[1]);
		return 1;
	}
	
	fseek(hFile, 0, SEEK_SET);
	srcSize = ROM_SIZE - SONG_OFFSET;
	srcData = (UINT8*)malloc(srcSize);
	fread(srcData, 1, srcSize, hFile);
	
	fclose(hFile);
	
	GetSongTable(0x7ff8);
	
	if (argc < 4)
	{
		// convert all songs ?
		printf("Song count = %d\n",songCount);
		return 1;
	}
	else
	{
		int songid = strtoul(argv[2],NULL,0);
		
		if(songid < songCount)
		{
			ConvertSong(songTable[songid]);
			
			hFile = fopen(argv[3], "wb");
			if (hFile == NULL)
			{
				printf("Error opening file %s!\n", argv[2]);
				return 1;
			}
			
			fwrite(dstData, 1, dstSize, hFile);
			
			fclose(hFile);
			free(dstData);	dstData = NULL;
		}
		else
		{
			printf("Song count = %d\n",songCount);
			return 1;
		}
	}
	
	free(srcData);	srcData = NULL;
	
	return 0;
}

static inline UINT32 ReadUINT32(UINT32 offset)
{
	return (srcData[offset] << 0) | (srcData[offset + 0x01] << 8) | (srcData[offset + 0x02] << 16) | (srcData[offset + 0x03] << 24);
}

static inline UINT32 ReadUINT16(UINT32 offset)
{
	return (srcData[offset] << 0) | (srcData[offset + 0x01] << 8);
}

static void GetSongTable(UINT32 start)
{
	int i;
	UINT32 songtab_pos = ReadUINT32(start) - 0x80000;
	
	songCount = ReadUINT16(songtab_pos);
	//songtab_pos += 2;

	for(i=0; i<songCount; i++)
	{
		if(i == SONG_MAX)
			break;

		songTable[i] = ReadUINT32(songtab_pos + 2 + (i*4)) + songtab_pos;
	}
}

static UINT8 ConvertSong(UINT32 songpos)
{
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT32 curPos;
	UINT32 syxLen;
	UINT8 curCmd;
	UINT8 trkEnd;
	UINT32 temp;
	
	midFileInf.alloc = 0x100000;
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x00, 1, 192);
	
	WriteMidiTrackStart(&midFileInf, &MTS);

	MTS.curDly = 0;
	MTS.midChn = 0x00;
	
	trkEnd = 0;
	curPos = songpos; //0x00;
	
	//temp = srcData[curPos + 0x00];	// initial tempo
	//WriteBE32(temp_bytes,60000000/temp);
	//WriteMetaEvent(&midFileInf,&MTS,0x51,0x03,temp_bytes+1);	
	
	// initial delay to make it look better synced ?
	MTS.curDly += 96;
	
	curPos += 1;
	while(curPos < srcSize && ! trkEnd)
	{
		/* read a deltatime */
		temp = 0;
		while(srcData[curPos] == 0xf8)
		{
			temp += srcData[curPos++];
			//printf("%02x ", srcData[curPos-1]);
		}
		temp += srcData[curPos++];	
		MTS.curDly += temp;
		
		/* read command */
		curCmd = srcData[curPos];
		switch(curCmd & 0xF0)
		{
		// usual MIDI events
		case 0x80:
			WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], 0x40);
			curPos += 0x02;
			break;
		case 0x90:
		case 0xB0:
			WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], srcData[curPos + 0x01], srcData[curPos + 0x02]);
			curPos += 0x03;
			break;
		case 0xA0: // A0 sends a modulation controller
			WriteEvent(&midFileInf, &MTS, 0xb0 | (srcData[curPos + 0x00]&0x0f), 0x01, srcData[curPos + 0x01]);
			curPos += 0x02;
			break;
		case 0xE0: // pitch bend parameter is only 1 byte
			WriteEvent(&midFileInf, &MTS, srcData[curPos + 0x00], 0x00, srcData[curPos + 0x01]);
			curPos += 0x02;
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
			case 0xF1:
				printf("command F1 data %02x %02x\n", srcData[curPos + 0x01],srcData[curPos + 0x02]);
				curPos += 0x03;
				break;
			case 0xF2: // change tempo
				temp = (srcData[curPos + 0x01] << 0);
				WriteBE32(temp_bytes,60000000/temp);
				WriteMetaEvent(&midFileInf,&MTS,0x51,0x03,temp_bytes+1);
				curPos += 0x02;
				break;
			case 0xF3: // Loop (not implemented yet)
				//    this byte might be a loop ID actually
				//    |
				// f3 01 00 sets a loop point
				// f3 01 nn is a finite loop
				// f3 01 ff is infinite loop
				printf("command F3 data %02x %02x\n", srcData[curPos + 0x01],srcData[curPos + 0x02]);
				curPos += 0x03;
				break;
			case 0xF8:
				break;
			case 0xFF:
				trkEnd = 1;
				break;
			default:
				printf("Encountered unknown event 0x%02X @ %06x!\n", curCmd, curPos);
				trkEnd = 1;
				break;
			}
			break;
		default:
			printf("Encountered unknown event 0x%02X @ %06x!\n", curCmd, curPos);
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
