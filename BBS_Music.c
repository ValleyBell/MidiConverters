#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef unsigned char	bool;
typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef unsigned long	UINT32;

#define false	0x00
#define true	0x01

bool LetterInArgument(char* Arg, char Letter);

UINT8 DataEast2Mid(UINT32 InLen, UINT8* InData, UINT32 InAddr, UINT32* OutLen, UINT8** OutData);
UINT32 ReadBigEndianL(UINT8* Buffer);
void WriteBigEndianL(UINT8* Buffer, UINT32 Value);
void WriteBigEndianS(UINT8* Buffer, UINT16 Value);
float OPN2DB(UINT8 TL);
UINT8 DB2Mid(float DB);

static bool OldDriver;
static bool InsLimit;

int main(int argc, char* argv[])
{
	FILE* hFile;
	UINT8 PLMode;
	UINT32 SongPos;
	char OutFileBase[0x100];
	char OutFile[0x100];
	char* TempPnt;
	int RetVal;
	
	UINT32 InLen;
	UINT8* InData;
	UINT32 OutLen;
	UINT8* OutData;
	
	UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 TempLng;
	
	printf("Data East Music -> Midi Converter\n---------------------------------\n");
	if (argc < 4)
	{
		printf("Usage: de2mid.exe ROM.bin Options Address(hex) [Song Count]\n");
		printf("\n");
		printf("The 'Options' argument is a collection of one or more of the following letters.\n");
		printf("They mustn't be seperated by space or tab.\n");
		printf("\n");
		printf("Modes:\ts - single file (Address is pointer to song data)\n");
		printf("\tl - music list (Address is pointer to music list)\n");
		printf("e - early driver (as used in Side Pocket)\n");
		printf("i - limit MIDI instrument numbers to 0..15 (may sound better)\n");
		printf("\n");
		printf("Song Count is an optional argument for Music List mode.\n");
		printf("Use it if the song autodetection fails.\n");
		return 0;
	}
	
	TempLng = strlen(argv[2]);
	PLMode = LetterInArgument(argv[2], 'L') && ! LetterInArgument(argv[2], 'S');
	OldDriver = LetterInArgument(argv[2], 'E');
	InsLimit = LetterInArgument(argv[2], 'I');
	
	strcpy(OutFileBase, argv[1]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;
	
	SongPos = strtoul(argv[3], NULL, 0x10);
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	InLen = ftell(hFile);
	if (InLen > 0x800000)	// 8 MB
		InLen = 0x800000;
	
	fseek(hFile, 0x00, SEEK_SET);
	InData = (UINT8*)malloc(InLen);
	fread(InData, 0x01, InLen, hFile);
	
	fclose(hFile);
	
	if (! PLMode)
	{
		RetVal = DataEast2Mid(InLen, InData, SongPos, &OutLen, &OutData);
		if (RetVal)
			return RetVal;
		
		strcpy(OutFile, OutFileBase);
		strcat(OutFile, ".mid");
		
		hFile = fopen(OutFile, "wb");
		if (hFile == NULL)
		{
			printf("Error opening file!\n");
			return 1;
		}
		fwrite(OutData, OutLen, 0x01, hFile);
		
		fclose(hFile);
	}
	else
	{
		if (argc > 4)
			FileCount = (UINT16)strtoul(argv[4], NULL, 0);
		else
			FileCount = 0x00;
		if (! FileCount)
		{
			// Song Count autodetection
			CurFile = 0x00;
			CurPos = SongPos;
			OutLen = ReadBigEndianL(&InData[SongPos]);
			while(CurPos < OutLen)
			{
				TempLng = ReadBigEndianL(&InData[CurPos]);
				if (TempLng & 0x80000000)
					break;
				if (TempLng < OutLen)
					OutLen = TempLng;
				
				if (OldDriver)
					CurPos += 0x04 * 0x06;
				else
					CurPos += 0x04;
				CurFile ++;
			}
			FileCount = CurFile;
			printf("Songs detected: 0x%02X (%u)\n", FileCount, FileCount);
		}
		
		CurPos = SongPos;
		for (CurFile = 0x00; CurFile < FileCount; CurFile ++)
		{
			printf("File %u / %u ...", CurFile + 1, FileCount);
			if (OldDriver)
			{
				TempLng = CurPos;
				CurPos += 0x04 * 0x06;
			}
			else
			{
				TempLng = ReadBigEndianL(&InData[CurPos]);
				CurPos += 0x04;
			}
			RetVal = DataEast2Mid(InLen, InData, TempLng, &OutLen, &OutData);
			if (RetVal)
				return RetVal;
			
			sprintf(OutFile, "%s_%02X.mid", OutFileBase, CurFile);
			
			hFile = fopen(OutFile, "wb");
			if (hFile == NULL)
			{
				free(OutData);	OutData = NULL;
				printf("Error opening file!\n");
				continue;
			}
			fwrite(OutData, OutLen, 0x01, hFile);
			
			fclose(hFile);
			free(OutData);	OutData = NULL;
			printf("\n");
		}
		printf("Done.\n", CurFile + 1, FileCount);
	}
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

bool LetterInArgument(char* Arg, char Letter)
{
	Letter = toupper(Letter);
	
	while(*Arg != '\0')
	{
		if (toupper(*Arg) == Letter)
			return true;
		
		Arg ++;
	}
	
	return false;
}


UINT8 DataEast2Mid(UINT32 InLen, UINT8* InData, UINT32 InAddr, UINT32* OutLen, UINT8** OutData)
{
	UINT8 CurTrk;
	UINT8* DstData;
	UINT32 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	
	UINT8 LoopID;
	UINT8 LoopCount[0x10];
	UINT32 LoopAddr[0x10];
	UINT8 LoopCur[0x10];
	UINT32 TempLng;
	UINT8 TempByt;
	
	*OutLen = 0x10000;	// 64 KB should be enough
	*OutData = (UINT8*)malloc(*OutLen);
	DstData = *OutData;
	
	DstPos = 0x00;
	WriteBigEndianL(&DstData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBigEndianL(&DstData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBigEndianS(&DstData[DstPos + 0x00], 0x0001);	// Format 1
	WriteBigEndianS(&DstData[DstPos + 0x02], 0x0006);	// Tracks: 6
	WriteBigEndianS(&DstData[DstPos + 0x04], 0x000C);	// Ticks per Quarter: 0x18
	DstPos += 0x06;
	
	for (CurTrk = 0x00; CurTrk < 0x06; CurTrk ++)
	{
		InPos = ReadBigEndianL(&InData[InAddr + CurTrk * 0x04]);
		MidChn = (CurTrk == 0x05) ? 0x09 : CurTrk;
		
		WriteBigEndianL(&DstData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		DstData[DstPos] = 0x00;
		DstPos ++;
		
		TrkEnd = false;
		LoopID = 0xFF;
		// check assignment channel 
		if (OldDriver)
		{
			if (InData[InPos] == 0xFF)
				TrkEnd = true;
			InPos += 0x02;
		}
		else
		{
			if (InData[InAddr + 0x06 * 0x04 + CurTrk] == 0xFF)
				TrkEnd = true;
		}
		while(! TrkEnd)
		{
			if (! (InData[InPos] & 0x80))
			{
				TempByt = InData[InPos + 0x00];
				if (OldDriver)
				{
					if (CurTrk < 0x05 && TempByt < 0x7F)
					{
						if ((TempByt & 0x0F) >= 0x0C)
							printf("Invalid Note %02X on track %X\n", TempByt, CurTrk);
						TempByt = (1 + (TempByt & 0xF0) / 0x10) * 12 + (TempByt & 0x0F);
					}
				}
				DstData[DstPos + 0x00] = 0x90 | MidChn;
				DstData[DstPos + 0x01] = TempByt;
				DstData[DstPos + 0x02] = (TempByt == 0x7F) ? 0x00 : 0x7F;
				DstPos += 0x03;
				
				if (InData[InPos + 0x01] & 0x80)
				{
					DstData[DstPos + 0x00] = 0x81;
					DstData[DstPos + 0x01] = InData[InPos + 0x01] & 0x7F;
					DstPos += 0x02;
				}
				else
				{
					DstData[DstPos + 0x00] = InData[InPos + 0x01];
					DstPos += 0x01;
				}
				
				if (TempByt < 0x7F)
				{
					DstData[DstPos + 0x00] = 0x90 | MidChn;
					DstData[DstPos + 0x01] = TempByt;
					DstData[DstPos + 0x02] = 0x00;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
				}
				InPos += 0x02;
			}
			else
			{
				switch(InData[InPos + 0x00])
				{
				case 0x80:	// Set Tempo
					DstData[DstPos + 0x00] = 0xFF;
					DstData[DstPos + 0x01] = 0x51;
					TempLng = 450000 / (InData[InPos + 0x01] / 128.0);
					WriteBigEndianL(&DstData[DstPos + 0x02], TempLng);
					DstData[DstPos + 0x02] = 0x03;
					DstData[DstPos + 0x06] = 0x00;
					DstPos += 0x07;
					InPos += 0x02;
					break;
				case 0x82:	// Instrument Change
					DstData[DstPos + 0x00] = 0xC0 | MidChn;
					if (! InsLimit)
						DstData[DstPos + 0x01] = InData[InPos + 0x01];
					else
						DstData[DstPos + 0x01] = InData[InPos + 0x01] & 0x0F;
					DstData[DstPos + 0x02] = 0x00;
					DstPos += 0x03;
					InPos += 0x02;
					break;
				case 0x83:	// Volume Change
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x07;
#if 1
					DstData[DstPos + 0x02] = 0x7F - InData[InPos + 0x01];
#else
					DstData[DstPos + 0x02] = DB2Mid(OPN2DB(InData[InPos + 0x01]));
#endif
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x86:	// Note Stop value
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x03;
					DstData[DstPos + 0x02] = InData[InPos + 0x01];
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x87:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x01;
					break;
				case 0x88:	// Loop Start
					LoopID ++;
					LoopCount[LoopID] = InData[InPos + 0x01];
					InPos += 0x02;
					LoopAddr[LoopID] = InPos;
					LoopCur[LoopID] = 0x00;
					if (! LoopCount[LoopID])
					{
						DstData[DstPos + 0x00] = 0xB0 | MidChn;
						DstData[DstPos + 0x01] = 0x6F;
						DstData[DstPos + 0x02] = LoopCur[LoopID];
						DstData[DstPos + 0x03] = 0x00;
						DstPos += 0x04;
					}
					break;
				case 0x89:	// Loop End
					if (LoopID == 0xFF)
					{
						InPos += 0x01;
						printf("Warning! Invalid Loop End found!\n");
						break;
					}
					
					LoopCur[LoopID] ++;
					if (! LoopCount[LoopID])
					{
						DstData[DstPos + 0x00] = 0xB0 | MidChn;
						DstData[DstPos + 0x01] = 0x6F;
						DstData[DstPos + 0x02] = LoopCur[LoopID];
						DstData[DstPos + 0x03] = 0x00;
						DstPos += 0x04;
						if (LoopCur[LoopID] >= 0x02)
							TrkEnd = true;
					}
					
					if (! LoopCount[LoopID] || LoopCur[LoopID] < LoopCount[LoopID])
					{
						InPos = LoopAddr[LoopID];
					}
					else
					{
						LoopID --;
						InPos += 0x01;
					}
					break;
				case 0x8D:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x8E:	// set LFO register (022)
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x23;
					DstData[DstPos + 0x02] = InData[InPos + 0x01];
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x8F:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x01;
					break;
				case 0x90:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x03;
					break;
				case 0x91:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x01;
					break;
				case 0x92:	// Panorama
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x0A;
					switch(InData[InPos + 0x01] & 0xC0)
					{
					case 0x40:	// Left Channel
						DstData[DstPos + 0x02] = 0x00;
						break;
					case 0x80:	// Right Channel
						DstData[DstPos + 0x02] = 0x7F;
						break;
					case 0x00:	// No Channel
					case 0xC0:	// Both Channels
						DstData[DstPos + 0x02] = 0x40;
						break;
					}
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x93:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x94:	// ??
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6D;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				case 0x95:	// Track End
					TrkEnd = true;
					InPos += 0x01;
					break;
				case 0x96:	// DAC Volume
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x07;
#if 1
					DstData[DstPos + 0x02] = 0x7F - InData[InPos + 0x01] * 0x20;
#else
					DstData[DstPos + 0x02] = DB2Mid(-6.0f * InData[InPos + 0x01]);
#endif
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				default:
					printf("Unknown event %02X on track %X\n", InData[InPos + 0x00], CurTrk);
					DstData[DstPos + 0x00] = 0xB0 | MidChn;
					DstData[DstPos + 0x01] = 0x6E;
					DstData[DstPos + 0x02] = InData[InPos + 0x00] & 0x7F;
					DstData[DstPos + 0x03] = 0x00;
					DstPos += 0x04;
					InPos += 0x02;
					break;
				}
			}
		}
		DstData[DstPos + 0x00] = 0xFF;
		DstData[DstPos + 0x01] = 0x2F;
		DstData[DstPos + 0x02] = 0x00;
		DstPos += 0x03;
		
		WriteBigEndianL(&DstData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	}
	*OutLen = DstPos;
	
	return 0x00;
}

UINT32 ReadBigEndianL(UINT8* Buffer)
{
	return (Buffer[0x00] << 24) |
			(Buffer[0x01] << 16) |
			(Buffer[0x02] <<  8) |
			(Buffer[0x03] <<  0);
}

void WriteBigEndianL(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value & 0xFF000000) >> 24;
	Buffer[0x01] = (Value & 0x00FF0000) >> 16;
	Buffer[0x02] = (Value & 0x0000FF00) >>  8;
	Buffer[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}

void WriteBigEndianS(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value & 0xFF00) >> 8;
	Buffer[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}

float OPN2DB(UINT8 TL)
{
	return -(TL * 4 / 3.0f);
}

UINT8 DB2Mid(float DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}
