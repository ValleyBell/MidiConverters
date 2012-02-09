#include <stdio.h>
#include <stdlib.h>
#include "stdtype.h"
#include "stdbool.h"

int main(int argc, char* argv[]);
void NiceFileNameCase(char* Str, char* StrEnd);
UINT8 ConvertL3DtoMID(char* InFile, char* OutFile);

int main(int argc, char* argv[])
{
	char* FileName;
	char* MidName;
	char* TempPnt;
	
	if (argc <= 1)
	{
		printf("Lemmings 3D MIDI Converter\n");
		printf("--------------------------\n");
		printf("Usage: Lem3DMid.exe input.snd [output.snd]\n");
		return 0;
	}
	
	FileName = argv[1];
	if (argc <= 2)
	{
		MidName = (char*)malloc(strlen(FileName) + 5);
		strcpy(MidName, FileName);
		TempPnt = strrchr(MidName, '.');
		if (TempPnt == NULL)
			TempPnt = MidName + strlen(MidName);
		
		strcpy(TempPnt, ".mid");
		NiceFileNameCase(MidName, TempPnt);
	}
	else
	{
		MidName = argv[2];
	}
	
	RetVal = ConvertL3DtoMID(FileName, MidFile);
	RetVal |= (RetVal >> 4) | (RetVal >> 3);	// 0x80 -> 0x08, 0x10 -> 0x02
	RetVal &= 0x0F;
	
	return RetVal;
}

void NiceFileNameCase(char* Str, char* StrEnd)
{
	bool IsUpperCase;
	
	IsUpperCase = true;
	while(*Str != '\0')
	{
		if (Str < StrEnd)
		{
			if (islower(Str))
			{
				IsUpperCase = false;
				break;
			}
		}
		else
		{
			if (IsUpperCase)
				*Str = toupper(*Str);
			else
				*Str = tolower(*Str);
		}
		*Str ++;
	}
	
	return;
}

UINT8 ConvertL3DtoMID(char* InFile, char* OutFile)
{
	UINT32 InSize;
	UINT8* InData;
	UINT32 OutSize;
	UINT8* OutData;
	UINT32 InPos;
	UINT32 OutPos;
	
	UINT16 TrkCount;
	UINT32* TrkTOC;
	UINT8 Tempo;
	bool TrkEnd;
	
	UINT32 TempLng;
	UINT32 MTrkBase;
	UINT16 CurTrk;
	UINT8 SelChn;
	UINT8 LastNote;
	bool IsSBL;
	
	FILE* hFile;
	
	hFile = fopen(InFile, "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 0x80;
	}
	
	fseek(hFile, 0, SEEK_END);
	InSize = ftell(hFile);
	if (InSize > 0x10000)
		InSize = 0x10000;	// the file's pointers are only 16-bit
	
	fseek(hFile, 0, SEEK_SET);
	InData = (UINT8*)malloc(InSize);
	fread(InData, 0x01, InData, hFile);
	
	fclose(hFile);
	
	if (! InSize || InData[0x00] != 0x01)
	{
		printf("Invalid file!\n");
		return 0x10;
	}
	
	OutSize = InSize * 0x04;
	OutData = (UINT8*)malloc(OutSize);
	OutData[0x00] = 0x4D;
	OutData[0x01] = 0x54;
	OutData[0x02] = 0x68;
	OutData[0x03] = 0x64;
	OutData[0x04] = 0x00;
	OutData[0x05] = 0x00;
	OutData[0x06] = 0x00;
	OutData[0x07] = 0x06;
	OutData[0x08] = 0x00;
	OutData[0x09] = 0x01;
	
	// Jump to Footer (pointer at 0001h points to "footer-pointer")
	InPos = (InData[0x02] << 8) | (InData[0x01] << 0);
	if (InPos >= InSize)
	{
		printf("Invalid file!\n");
		return 0x10;
	}
	
	// Read Footer
	OutData[0x0C] = 0x00;
	OutData[0x0D] = InData[InPos + 0x00];	// Ticks per Quarter
	Tempo = InData[InPos + 0x01];
	TrkCount = InData[InPos + 0x02];
	OutData[0x0A] = 0x00;
	OutData[0x0B] = TrkCount;
	InPos += 0x03;
	
	TrkTOC = (UINT32*)malloc(TrkCount * sizeof(UINT32));
	for (CurTrk = 0x00; CurTrk < TrkCount; CurTrk ++)
	{
		TrkTOC[CurTrk] = (InData[InPos + 0x01] << 8) | (InData[InPos + 0x00] << 0);
		InPos += 0x02;
	}
	
	IsSBL = false;
	OutPos = 0x0E;
	for (CurTrk = 0x00; CurTrk < TrkCount; CurTrk ++)
	{
		OutData[OutPos + 0x00] = 0x4D;
		OutData[OutPos + 0x01] = 0x54;
		OutData[OutPos + 0x02] = 0x72;
		OutData[OutPos + 0x03] = 0x6B;
		OutPos += 0x08;
		MTrkBase = OutPos;
		
		if (! CurTrk)
		{
			// Write Tempo
			TempLng = 1000000 * 60 / Tempo;
			OutData[OutPos] = 0x00;	// Delay
			OutPos = OutPos + 0x01;
			OutData[OutPos + 0x00] = 0xFF;
			OutData[OutPos + 0x01] = 0x51;
			OutData[OutPos + 0x02] = 0x03;
			OutData[OutPos + 0x03] = (TempLng & 0x00FF0000) >> 16;
			OutData[OutPos + 0x04] = (TempLng & 0x0000FF00) >>  8;
			OutData[OutPos + 0x05] = (TempLng & 0x000000FF) >>  0;
			OutPos += 0x06;
		}
		
		InPos = TrkTOC[CurTrk];
		TrkEnd = false;
		LastNote = 0xFF;
		while(! TrkEnd)
		{
			// delay (in standard MIDI format)
			while(InData[InPos] & 0x80)
			{
				OutData[OutPos] = InData[InPos];
				InPos ++;	OutPos ++;
			}
			OutData[OutPos] = InData[InPos];
			InPos ++;	OutPos ++;
			
			if (! (InData[InPos + 0x00] & 0x80))
			{
				if (LastNote == InData[InPos + 0x00])
				{
					OutData[OutPos + 0x00] = 0x90 | SelChn;
					OutData[OutPos + 0x01] = LastNote;
					OutData[OutPos + 0x02] = 0x00;
					OutData[OutPos + 0x03] = 0x00;
					OutPos += 0x04;
				}
				LastNote = InData[InPos + 0x00];
				OutData[OutPos + 0x00] = 0x90 | SelChn;
				OutData[OutPos + 0x01] = LastNote;
				OutData[OutPos + 0x02] = InData[InPos + 0x01];
				InPos += 0x02;
				OutPos += 0x03;
			}
			else
			{
				switch(InData[InPos + 0x00])
				{
				case 0x80 To 0x8F:	// Select Channel
					SelChn = InData[InPos + 0x00] & 0x0F;
					OutData[OutPos + 0x00] = 0xFF;
					OutData[OutPos + 0x01] = 0x20;
					OutData[OutPos + 0x02] = 0x01;
					OutData[OutPos + 0x03] = SelChn;
					InPos += 0x01;
					OutPos += 0x04;
					break;
				case 0x97:	// Drum Select (only present in Sound Blaster files)
					IsSBL = true;
					OutData[OutPos + 0x00] = 0xB0 | SelChn;
					OutData[OutPos + 0x01] = 0x67;
					OutData[OutPos + 0x02] = InData[InPos + 0x01];
					InPos += 0x02;
					OutPos += 0x03;
					break;
				case 0x9C:	// Loop Start
					OutData[OutPos + 0x00] = 0xB0 | SelChn;
					OutData[OutPos + 0x01] = 0x6F;
					OutData[OutPos + 0x02] = 0x00;
					InPos += 0x01;
					OutPos += 0x03;
					break;
				case 0x98:	// Loop Back
					OutData[OutPos + 0x00] = 0xB0 | SelChn;
					OutData[OutPos + 0x01] = 0x6F;
					OutData[OutPos + 0x02] = 0x01;
					InPos += 0x01;
					OutPos += 0x03;
					
					OutData[OutPos] = 0x00;	// Add a Delay
					OutPos += 0x01;
					TrkEnd = true;
					break;
				case 0x91:	// Track End
					InPos += 0x01,
					TrkEnd = true;
					break;
				case 0x99:	// Turn Last Note Off
					if (LastNote == 0xFF)
					{
						printf("Warning: Trying to turn off a note 2 times! "
								"(Track %hu, Pos 0x%04)\n", CurTrk, InPos);
					}
					OutData[OutPos + 0x00] = 0x90 | SelChn;
					OutData[OutPos + 0x01] = LastNote;
					OutData[OutPos + 0x02] = 0x00;
					LastNote = 0xFF;
					InPos += 0x01;
					OutPos += 0x03;
					break;
				case 0x90:	// Turn Note Off
					OutData[OutPos + 0x00] = 0x90 | SelChn;
					OutData[OutPos + 0x01] = InData[InPos + 0x01];
					OutData[OutPos + 0x02] = 0x00;
					InPos += 0x02;
					OutPos += 0x03;
					break;
				case 0x92:	// Instrument Change
					OutData[OutPos + 0x00] = 0xC0 | SelChn;
					OutData[OutPos + 0x01] = InData[InPos + 0x01];
					InPos += 0x02;
					OutPos += 0x02;
					break;
				case 0x95:	// Pitch Bend
					OutData[OutPos + 0x00] = 0xE0 | SelChn;
					OutData[OutPos + 0x01] = 0x00;
					OutData[OutPos + 0x02] = InData[InPos + 0x01];
					InPos += 0x02;
					OutPos += 0x03;
					break;
				case 0x96:	// Volume Controller
					OutData[OutPos + 0x00] = 0xB0 | SelChn;
					OutData[OutPos + 0x01] = 0x07;
					OutData[OutPos + 0x02] = InData[InPos + 0x01];
					InPos += 0x02;
					OutPos += 0x03;
					break;
				case 0x9B:	// Panorama Controller
					OutData[OutPos + 0x00] = 0xB0 | SelChn;
					OutData[OutPos + 0x01] = 0x0A;
					OutData[OutPos + 0x02] = InData[InPos + 0x01];
					InPos += 0x02;
					OutPos += 0x03;
					break;
				case 0x9D:	// Controller (any)
					OutData[OutPos + 0x00] = 0xB0 | SelChn;
					OutData[OutPos + 0x01] = InData[InPos + 0x01];
					OutData[OutPos + 0x02] = InData[InPos + 0x02];
					InPos += 0x03;
					OutPos += 0x03;
					break;
				default:
					printf("Unknown Command: %02X\n", InData[InPos + 0x00]);
					break;
				}
			}
		}
		OutData[OutPos + 0x00] = 0xFF;
		OutData[OutPos + 0x01] = 0x2F;
		OutData[OutPos + 0x02] = 0x00;
		OutPos += 0x03;
		
		TempLng = OutPos - MTrkBase;
		OutData[MTrkBase - 0x04] = (TempLng & 0xFF000000) >> 24;
		OutData[MTrkBase - 0x03] = (TempLng & 0x00FF0000) >> 16;
		OutData[MTrkBase - 0x02] = (TempLng & 0x0000FF00) >>  8;
		OutData[MTrkBase - 0x01] = (TempLng & 0x000000FF) >>  0;
	}
	if (OutPos > OutSize)
		printf("Critical Warning: Wrote beyond the buffer!\n");
	OutSize = OutPos;
	
	hFile = fopen(OutFile, "wb");
	if (hFile == NULL)
	{
		printf("Error opening output file!\n");
		return 0x81;
	}
	
	fwrite(OutData, 0x01, OutSize, hFile);
	
	fclose(hFile);
	
	return 0x00;
}
