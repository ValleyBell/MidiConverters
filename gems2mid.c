// GEMS -> Midi Converter
// ----------------------
// Written by Valley Bell, 2012, 2015

// Possible improvements:
//	- new parsing code that emulates all channels and thus integreates other sequences (see X-Men 2)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef unsigned char	bool;
typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef signed short	INT16;
typedef unsigned long	UINT32;

#define false	0x00
#define true	0x01

#define SHOW_DEBUG_MESSAGES


#define MODE_MUS	0x00
#define	MODE_DAC	0x01
#define MODE_INS	0x02

typedef struct running_note
{
	UINT8 MidChn;
	UINT8 Note;
	UINT16 RemLen;
} RUN_NOTE;

typedef struct file_information
{
	UINT32 Alloc;	// allocated bytes
	UINT32 Pos;		// current file offset
	UINT8* Data;	// file data
} FILE_INF;

#pragma pack(1)
#define INSTYPE_FM			 0
#define INSTYPE_DAC			 1
#define INSTYPE_PSG_TONE	 2
#define INSTYPE_PSG_NOISE	 3
#define INSTYPE_DUMMY		99
typedef struct instrument_data
{
	UINT8 InsType;
	UINT8 DataLen;
	//UINT8* Data;
	UINT8 Data[0x26];
} INS_DATA;

typedef struct dac_data
{
	UINT8 V00_Flags;
	UINT16 V01_StartLSB;
	UINT8 V03_StartMSB;
	UINT16 V04_Skip;
	UINT16 V06_DataLen;
	UINT16 V08_Loop;
	UINT16 V0A_End;
	UINT8* Data;
} DAC_DATA;
#pragma pack()


static UINT16 DetectSongCount(UINT32 InLen, const UINT8* InData);
static UINT8 DetectGemsVer(UINT32 InLen, const UINT8* InData);
UINT8 LoadInsData(const char* FileName);

UINT8 Gems2Mid(UINT32 GemsLen, UINT8* GemsData, UINT16 GemsAddr/*, UINT32* OutLen, UINT8** OutData*/);
static UINT16 ReadLE16(const UINT8* Buffer);
static UINT32 ReadLE24(const UINT8* Buffer);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);
static void WriteEvent(FILE_INF* fInf, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMetaEvent_Data(FILE_INF* fInf, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(FILE_INF* fInf, UINT32 Value);
static void File_CheckRealloc(FILE_INF* fInf, UINT32 BytesNeeded);
static float OPN2DB(UINT8 TL);
static UINT8 DB2Mid(float DB);

UINT8 LoadDACData(const char* FileName);
void SaveDACData(const char* FileBase);
void SaveInsAsGYB(const char* FileBase);


#define GEMSVER_20	1
#define GEMSVER_28	2
UINT8 GemsVer;

UINT32 MidLen;
UINT8* MidData;
UINT8 RunNoteCnt;
RUN_NOTE RunNotes[0x100];

UINT8 InsCount;
INS_DATA InsData[0x80];

UINT8 DacCount;
DAC_DATA DacData[0x80];

static UINT8 NUM_LOOPS = 2;

int main(int argc, char* argv[])
{
	FILE* hFile;
	//UINT8 PLMode;
	UINT32 SongPos;
	char OutFileBase[0x100];
	char OutFile[0x100];
	char* TempPnt;
	int RetVal;
	UINT8 Mode;
	int argbase;
	
	UINT32 InLen;
	UINT8* InData;
	//UINT32 OutLen;
	//UINT8* OutData;
	
	UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	UINT16 TempSht;
	
	printf("GEMS -> Midi Converter\n----------------------\n");
	if (argc < 2)
	{
		printf("Usage: gems2mid.exe [-mode] [-v#] Input.bin [SongCount [InsFile.bin]]\n");
		printf("\n");
		printf("Modes:\n");
		printf("    Mus - convert GEMS Music to MIDI (default), Input.bin has sequence data\n");
		printf("    DAC - extract DAC sounds, Input.bin has sample data\n");
		printf("    Ins - convert instruments to a GYB file, Input.bin has instrument data\n");
		printf("\n");
		printf("SongCount and InsFile.bin are optional arguments for Music mode.\n");
		printf("SongCount = 0 means autodetection.\n");
		printf("InsFile.bin will be used to map DAC and PSG to the correct channels.\n");
		printf("\n");
		printf("Versions");
		printf("    v1 - GEMS 2.0-2.5 (2-byte track pointers)");
		printf("    v2 - GEMS 2.8 (3-byte track pointers)");
		printf("\n");
		return 0;
	}
	
	Mode = MODE_MUS;
	GemsVer = 0;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		TempPnt = &argv[argbase][1];
		if (! _stricmp(TempPnt, "Mus"))
			Mode = MODE_MUS;
		else if (! _stricmp(TempPnt, "DAC"))
			Mode = MODE_DAC;
		else if (! _stricmp(TempPnt, "Ins"))
			Mode = MODE_INS;
		else if (tolower(TempPnt[0]) == 'v')
		{
			GemsVer = TempPnt[1] - '0';
			if (GemsVer > 2)
				GemsVer = 0;
		}
		argbase ++;
	}
	
	if (argc <= argbase)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	strcpy(OutFileBase, argv[argbase + 0]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;
	
	switch(Mode)
	{
	case MODE_MUS:
		if (argc > argbase + 1)
			FileCount = (UINT16)strtoul(argv[argbase + 1], NULL, 0);
		else
			FileCount = 0x00;
		
		hFile = fopen(argv[argbase + 0], "rb");
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
		
		SongPos = 0x00;
		
		InsCount = 0x00;
		if (argc > argbase + 2)
			LoadInsData(argv[argbase + 2]);
		
		if (! FileCount)
		{
			// Song Count autodetection
			FileCount = DetectSongCount(InLen - SongPos, &InData[SongPos]);
			printf("Songs detected: 0x%02X (%u)\n", FileCount, FileCount);
		}
		if (! GemsVer)
		{
			GemsVer = DetectGemsVer(InLen - SongPos, &InData[SongPos]);
			if (GemsVer)
			{
				printf("Detected GEMS %s (%u-byte track pointers)\n",
					(GemsVer == GEMSVER_28) ? "2.8" : "2.0-2.5", 1 + GemsVer);
			}
			else
			{
				printf("Warning! GEMS Version Autodetection failed!\nPlease report!\n");
				GemsVer = GEMSVER_20;
			}
		}
		
		CurPos = SongPos;
		for (CurFile = 0x00; CurFile < FileCount; CurFile ++, CurPos += 0x02)
		{
			printf("File %u / %u ...", CurFile + 1, FileCount);
			TempSht = ReadLE16(&InData[CurPos]);
			RetVal = Gems2Mid(InLen - SongPos, InData + SongPos, TempSht/*, &OutLen, &OutData*/);
			if (RetVal)
			{
				if (RetVal == 0x01)
				{
					printf(" empty - ignored.\n");
					continue;
				}
				
				return RetVal;
			}
			
			sprintf(OutFile, "%s_%02X.mid", OutFileBase, CurFile);
			
			hFile = fopen(OutFile, "wb");
			if (hFile == NULL)
			{
				free(MidData);	MidData = NULL;
				printf("Error opening file!\n");
				continue;
			}
			fwrite(MidData, MidLen, 0x01, hFile);
			
			fclose(hFile);
			free(MidData);	MidData = NULL;
			printf("\n");
		}
		printf("Done.\n", CurFile + 1, FileCount);
		break;
	case MODE_DAC:
		RetVal = LoadDACData(argv[argbase + 0]);
		if (RetVal)
			break;
		SaveDACData(OutFileBase);
		break;
	case MODE_INS:
		RetVal = LoadInsData(argv[argbase + 0]);
		if (RetVal)
			break;
		SaveInsAsGYB(OutFileBase);
		break;
	}
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

static UINT16 DetectSongCount(UINT32 InLen, const UINT8* InData)
{
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 MaxLen;
	UINT32 SongPtr;
	
	CurFile = 0x00;
	MaxLen = InLen;
	for (CurPos = 0x00, CurFile = 0x00; CurPos < MaxLen; CurPos += 0x02, CurFile ++)
	{
		SongPtr = ReadLE16(&InData[CurPos]);
		if (SongPtr < MaxLen)
			MaxLen = SongPtr;
	}
	return CurFile;
}

static UINT8 DetectGemsVer(UINT32 InLen, const UINT8* InData)
{
	// detect GEMS Sequence type (2-byte or 3-byte pointers)
	UINT16 BasePos;
	UINT16 MinPos1;
	UINT16 MinPos2;
	UINT16 CurPos;
	UINT8 TrkCnt;
	
	MinPos1 = MinPos2 = 0xFFFF;
	for (BasePos = 0x0000; BasePos < 0x200; BasePos += 0x02)
	{
		if (BasePos >= MinPos1)
			break;
		CurPos = ReadLE16(&InData[BasePos]);
		if (! InData[CurPos])	// skip empty songs
			continue;
		
		if (MinPos1 > CurPos)
			MinPos1 = CurPos;
		else if (MinPos2 > CurPos && CurPos > MinPos1)
			MinPos2 = CurPos;
	}
	
	TrkCnt = InData[MinPos1];
	MinPos1 ++;
	
	if (MinPos1 + 0x03 * TrkCnt == MinPos2)
		return GEMSVER_28;
	else if (MinPos1 + 0x02 * TrkCnt == MinPos2)
		return GEMSVER_20;
	else
		return 0;
}

UINT8 LoadInsData(const char* FileName)
{
	FILE* hFile;
	long TempPos;
	UINT8 CurIns;
	UINT16 InsTable[0x80];
	INS_DATA* TempIns;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return 0x80;
	
	fseek(hFile, 0x00, SEEK_END);
	TempPos = ftell(hFile);
	
	fseek(hFile, 0x00, SEEK_SET);
	// Note: This reads to the offset of the first actual instrument data.
	for (CurIns = 0x00; CurIns < 0x80; CurIns ++)
	{
		if (ftell(hFile) >= TempPos)
			break;
		
		InsTable[CurIns]  = fgetc(hFile) << 0;
		InsTable[CurIns] |= fgetc(hFile) << 8;
		if (TempPos > InsTable[CurIns])
			TempPos = InsTable[CurIns];
	}
	InsCount = CurIns;
	
	TempIns = InsData;
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++, TempIns ++)
	{
		fseek(hFile, InsTable[CurIns], SEEK_SET);
		
		TempPos = fgetc(hFile);
		if (TempPos == EOF)
			break;
		TempIns->InsType = (UINT8)TempPos;
		switch(TempIns->InsType)
		{
		case INSTYPE_FM:
			// 26 bytes for various registers
			TempIns->DataLen = 0x26;
			break;
		case INSTYPE_DAC:
			// 1 byte - default playback rate, maybe?
			TempIns->DataLen = 0x01;
			break;
		case INSTYPE_PSG_TONE:
		case INSTYPE_PSG_NOISE:
			// 6 bytes - the first byte is the Noise type
			TempIns->DataLen = 0x06;
			break;
		case INSTYPE_DUMMY:
			TempIns->DataLen = 0x00;
			break;
		default:
			printf("Warning: Instrument %02X uses an unknown instrument type: %02X!\n",
					CurIns, TempIns->InsType);
			TempIns->DataLen = 0x00;
			break;
		}
		//TempIns->Data = (UINT8*)malloc(TempIns->DataLen);
		TempPos = fread(TempIns->Data, 0x01, TempIns->DataLen, hFile);
		if (TempPos < TempIns->DataLen)
			break;	// incomplete data
	}
	InsCount = CurIns;
	
	return 0x00;
}


UINT8 Gems2Mid(UINT32 GemsLen, UINT8* GemsData, UINT16 GemsAddr/*, UINT32* OutLen, UINT8** OutData*/)
{
	UINT8 TrkCnt;
	UINT32* ChnPtrList;
	UINT8 CurTrk;
	UINT32 InPos;
	UINT32 DstPos;
	FILE_INF MidFileInf;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopID;
	UINT8 LoopCount[0x10];
	UINT32 LoopAddr[0x10];
	UINT8 LoopCur[0x10];
	UINT8 TempArr[0x04];
	UINT8 JumpCount;
	UINT32 TempLng;
	UINT16 TempSht;
	INT16 TempOfs;
	UINT8 TempByt;
	UINT32 CurDly;
	UINT8 NoteVol;
	UINT8 ChnVol;
	UINT16 ChnDelay;
	UINT16 ChnDur;
	bool ProcDelay;
	bool WrotePBDepth;
	UINT8 ChnMode;
	UINT8 PanMode;
	
	InPos = GemsAddr;
	
	TrkCnt = GemsData[InPos];
	if (! TrkCnt)
		return 0x01;
	ChnPtrList = (UINT32*)malloc(TrkCnt * sizeof(UINT32));
	InPos ++;
	
	MidLen = 0x10000;	// 64 KB should be enough
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBE32(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBE16(&MidData[DstPos + 0x00], 0x0001);		// Format 1
	WriteBE16(&MidData[DstPos + 0x02], TrkCnt);		// Tracks: TrkCnt
	WriteBE16(&MidData[DstPos + 0x04], 0x0018);		// Ticks per Quarter: 24
	DstPos += 0x06;
	
	if (GemsVer == GEMSVER_28)
	{
		for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, InPos += 0x03)
			ChnPtrList[CurTrk] = ReadLE24(&GemsData[InPos]);
	}
	else
	{
		for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++, InPos += 0x02)
			ChnPtrList[CurTrk] = ReadLE16(&GemsData[InPos]);
	}
	
	MidFileInf.Alloc = MidLen;
	MidFileInf.Data = MidData;
	MidFileInf.Pos = DstPos;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		InPos = ChnPtrList[CurTrk];
		
		WriteBE32(&MidFileInf.Data[MidFileInf.Pos], 0x4D54726B);	// write 'MTrk'
		MidFileInf.Pos += 0x08;
		
		TrkBase = MidFileInf.Pos;
		CurDly = 0x00;
		RunNoteCnt = 0x00;
		ChnMode = 0x00;
		
		TrkEnd = false;
		LoopID = 0xFF;
		JumpCount = 0;
		WrotePBDepth = false;
		
		// This is a nice formula, that skips the drum channel 9.
		MidChn = (CurTrk + (CurTrk + 6) / 15) & 0x0F;
		NoteVol = 0x7F;
		ChnVol = 0x7F;
		PanMode = 0x00;
		ChnDelay = 0x00;
		
		while(! TrkEnd && InPos < GemsLen)
		{
			CurCmd = GemsData[InPos];
			InPos ++;
			ProcDelay = true;
			if (CurCmd < 0x60)	// 00-5F - Note
			{
				TempByt = CurCmd;
				if (ChnMode == 0x01)
					TempByt = (TempByt + 0x30) % 0x60;
				else
					TempByt += 12;	// shift up one octave
				
				WriteEvent(&MidFileInf, &CurDly,
							0x90 | MidChn, TempByt, NoteVol);
				if (RunNoteCnt < 0x100)
				{
					RunNotes[RunNoteCnt].MidChn = MidChn;
					RunNotes[RunNoteCnt].Note = TempByt;
					RunNotes[RunNoteCnt].RemLen = ChnDur;
					RunNoteCnt ++;
				}
			}
			else if (CurCmd >= 0x80)
			{
				// Note: Calling the same command-group multiple times after another
				//       makes larger delays.
				if (CurCmd < 0xC0)	// 80-BF - Note Length
				{
					ChnDur = CurCmd & 0x3F;
					CurCmd = GemsData[InPos];
					while((CurCmd & 0xC0) == 0x80)
					{
						InPos ++;
						ChnDur = (ChnDur << 6) | (CurCmd & 0x3F);
						CurCmd = GemsData[InPos];
					}
				}
				else				// C0-FF - Note Delay
				{
					ChnDelay = CurCmd & 0x3F;
					CurCmd = GemsData[InPos];
					while((CurCmd & 0xC0) == 0xC0)
					{
						InPos ++;
						ChnDelay = (ChnDelay << 6) | (CurCmd & 0x3F);
						CurCmd = GemsData[InPos];
					}
				}
				ProcDelay = false;
			}
			else	// 60-7F - Commands
			{
				switch(CurCmd)
				{
				case 0x60:	// Track End
					TrkEnd = true;
					ProcDelay = false;
					break;
				case 0x61:	// Instrument Change [gemsprogchange]
					TempByt = GemsData[InPos + 0x00];
					TempSht = PanMode;
					if (TempByt < InsCount)
					{
						ChnMode = InsData[TempByt].InsType;
						switch(ChnMode)
						{
						case INSTYPE_FM:
							if (MidChn == 0x09)
								MidChn = 0xFF;
							//TempByt &= 0x0F;
							TempSht = InsData[TempByt].Data[0x03] & 0xC0;
							break;
						case INSTYPE_DAC:
							MidChn = 0x09;
							WriteEvent(&MidFileInf, &CurDly,
										0xD0 | MidChn, InsData[TempByt].Data[0x00], 0x00);
							TempByt = 0x00;
							break;
						case INSTYPE_PSG_TONE:
							if (MidChn == 0x09)
								MidChn = 0xFF;
							WriteEvent(&MidFileInf, &CurDly,
										0xB0 | MidChn, 0x20, TempByt);
							TempByt = 0x50;
							break;
						case INSTYPE_PSG_NOISE:
							MidChn = 0x09;
							WriteEvent(&MidFileInf, &CurDly,
										0xB0 | MidChn, 0x03, InsData[TempByt].Data[0x00]);
							TempByt = 0x00;
							break;
						}
						if (MidChn == 0xFF)
							MidChn = (CurTrk + (CurTrk + 6) / 15) & 0x0F;
					}
					WriteEvent(&MidFileInf, &CurDly,
								0xC0 | MidChn, TempByt, 0x00);
					
					if (PanMode != TempSht)
					{
						PanMode = (UINT8)TempSht;
						switch(PanMode)
						{
						case 0x80:	// Left
							TempByt = 0x00;
							break;
						case 0x40:	// Right
							TempByt = 0x7F;
							break;
						default:	// Center/None
							TempByt = 0x40;
							break;
						}
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x0A, TempByt);
					}
					
					InPos += 0x01;
					break;
				case 0x62:	// Pitch Envelope [gemssetenv]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 80, GemsData[InPos + 0x00]);
					InPos += 0x01;
					break;
				case 0x63:	// no operation
					//WriteEvent(&MidFileInf, &CurDly,
					//			0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					break;
				case 0x64:	// Loop Start [originally MIDI Ctrl 81, value 1..127]
					LoopID ++;
					LoopCount[LoopID] = GemsData[InPos];
					ProcDelay = false;
					InPos += 0x01;
					
					LoopCur[LoopID] = 0x00;
					LoopAddr[LoopID] = InPos;
					if (LoopCount[LoopID] == 0x7F)
					{
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x6F, LoopCur[LoopID]);
					}
					break;
				case 0x65:	// Loop End [originally MIDI Ctrl 81, value 0]
					if (LoopID == 0xFF)
					{
						printf("Warning! Invalid Loop End found!\n");
						break;
					}
					
					LoopCur[LoopID] ++;
					if (LoopCount[LoopID] == 0x7F)
					{
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x6F, LoopCur[LoopID]);
						if (LoopCur[LoopID] >= NUM_LOOPS)
						{
							LoopCur[LoopID] = LoopCount[LoopID];
							TrkEnd = true;
						}
					}
					ProcDelay = false;
					
					if (LoopCur[LoopID] <= LoopCount[LoopID])
						InPos = LoopAddr[LoopID];
					else
						LoopID --;
					break;
				case 0x66:	// Toggle retrigger mode. [gemsretrigenv]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 68, GemsData[InPos]);
					InPos += 0x01;
					break;
				case 0x67:	// Sustain mode [gemssustain]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 64, GemsData[InPos]);
					InPos += 0x01;
					break;
				case 0x68:	// Set Tempo [gemssettempo, originally MIDI Ctrl 16]
					// Parameter byte contains (Tempo - 40)
					//WriteEvent(&MidFileInf, &CurDly,
					//			0xB0 | MidChn, 16, GemsData[InPos]);*/
					
					TempLng = 60000000 / (GemsData[InPos] + 40);
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent_Data(&MidFileInf, &CurDly, 0x51, 0x03, &TempArr[1]);
					InPos += 0x01;
					break;
				case 0x69:	// Mute [gemsmute]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 18, GemsData[InPos]);
					InPos += 0x01;
					break;
				case 0x6A:	// Set Channel Priority [gemspriority]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 19, GemsData[InPos]);
					InPos += 0x01;
					break;
				case 0x6B:	// Play another song [gemsstartsong]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 82, GemsData[InPos]);
					InPos += 0x01;
					break;
				case 0x6C:	// Pitch Bend [gemspitchbend]
					if (! WrotePBDepth)
					{
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x65, 0x00);
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x64, 0x00);
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x06, 0x10);
						WrotePBDepth = true;
					}
					
					// Note: ~256 = 1 semitone
					TempOfs = (INT16)ReadLE16(&GemsData[InPos]);
					TempSht = 0x2000 + (TempOfs * 2);
					WriteEvent(&MidFileInf, &CurDly,
								0xE0 | MidChn, TempSht & 0x7F, (TempSht >> 7) & 0x7F);
					InPos += 0x02;
					break;
				case 0x6D:	// Set song to use SFX timebase - 150 BPM [originally MIDI Ctrl 70 = 0]
					//WriteEvent(&MidFileInf, &CurDly,
					//			0xB0 | MidChn, 70, 0);
					
					WriteEvent(&MidFileInf, &CurDly,
								0xFF, 0x51, 0x00);
					TempLng = 400000;	// 150 BPM
					WriteBE32(TempArr, TempLng);
					WriteMetaEvent_Data(&MidFileInf, &CurDly, 0x51, 0x03, &TempArr[1]);
					break;
				case 0x6E:	// Set DAC sample playback rate [originally MIDI Ctrl 71]
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 71, GemsData[InPos]);
					InPos += 0x01;
					break;
				case 0x6F:	// Jump
					TempLng = InPos;
					TempOfs = (INT16)ReadLE16(&GemsData[InPos]);
					ProcDelay = false;
					InPos += 0x02;
					
					if (TempOfs < 0)
					{
						JumpCount ++;
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x6F, JumpCount);
						if (JumpCount >= NUM_LOOPS)
							break;
					}
					InPos += TempOfs;
					if (InPos >= GemsLen)
					{
						printf("Track %u, Pos 0x%04: Jumping to invalid offset %04X!\n", CurTrk, TempLng, InPos);
						TrkEnd = true;
					}
					break;
				case 0x70:	// Store Value
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 0x63, 0x7F);
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 0x62, GemsData[InPos + 0x00]);
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 0x06, GemsData[InPos + 0x01]);
					InPos += 0x02;
					break;
				case 0x71:	// Conditional Jump
					WriteEvent(&MidFileInf, &CurDly,
								0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					InPos += 0x05;
					break;
				case 0x72:	// More functions
					TempByt = GemsData[InPos + 0x00];
					if (TempByt != 0x04 && TempByt != 0x05)
					{
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x70, TempByt & 0x7F);
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x26, GemsData[InPos + 0x01]);
					}
					switch(TempByt)
					{
					case 0x00:	// Stop Sequence
						break;
					case 0x01:	// Pause Sequence
						break;
					case 0x02:	// Resume Music
						break;
					case 0x03:	// Pause Music
						break;
					case 0x04:	// Set Master Volume
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x27, GemsData[InPos + 0x01] ^ 0x7F);
						break;
					case 0x05:	// Set Channel Volume
						ChnVol = DB2Mid(OPN2DB(GemsData[InPos + 0x01]) + 6);
						WriteEvent(&MidFileInf, &CurDly,
									0xB0 | MidChn, 0x07, ChnVol);
						break;
					}
					InPos += 0x02;
					break;
				default:
					printf("Unknown event %02X on track %X\n", CurCmd, CurTrk);
					//WriteEvent(&MidFileInf, &CurDly,
					//			0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					ProcDelay = false;
					break;
				}
			}
			
			if (ProcDelay)
				CurDly += ChnDelay;
		}
		//CurDly = 0;
		for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
		{
			if (RunNotes[TempByt].RemLen > CurDly)
				CurDly = RunNotes[TempByt].RemLen;
		}
		
		WriteEvent(&MidFileInf, &CurDly, 0xFF, 0x2F, 0x00);
		//if (RunNoteCnt)
		//	*((char*)NULL) = 'x';
		
		WriteBE32(&MidFileInf.Data[TrkBase - 0x04], MidFileInf.Pos - TrkBase);	// write Track Length
	}
	MidData = MidFileInf.Data;
	MidLen = MidFileInf.Pos;
	
	return 0x00;
}

static UINT16 ReadLE16(const UINT8* Buffer)
{
	return	(Buffer[0x00] << 0) |
			(Buffer[0x01] << 8);
}

static UINT32 ReadLE24(const UINT8* Buffer)
{
	return	(Buffer[0x00] <<  0) |
			(Buffer[0x01] <<  8) |
			(Buffer[0x02] << 16);
}

static void WriteBE32(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value & 0xFF000000) >> 24;
	Buffer[0x01] = (Value & 0x00FF0000) >> 16;
	Buffer[0x02] = (Value & 0x0000FF00) >>  8;
	Buffer[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}

static void WriteBE16(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value & 0xFF00) >> 8;
	Buffer[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}

static void WriteEvent(FILE_INF* fInf, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	UINT8* MidData;
	UINT8 CurNote;
	UINT32 TempDly;
	RUN_NOTE* TempNote;
	bool MoreNotes;
	
	do
	{
		MoreNotes = false;
		TempNote = RunNotes;
		TempDly = *Delay;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++, TempNote ++)
		{
			if (TempNote->RemLen < TempDly)
				TempDly = TempNote->RemLen;
		}
		
		TempNote = RunNotes;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++, TempNote ++)
		{
			TempNote->RemLen -= (UINT16)TempDly;
			if (! TempNote->RemLen)
			{
				if (! MoreNotes)
					WriteMidiValue(fInf, TempDly);
				else
					WriteMidiValue(fInf, 0);
				File_CheckRealloc(fInf, 0x03);
				MidData = fInf->Data;
				MidData[fInf->Pos + 0x00] = 0x90 | TempNote->MidChn;
				MidData[fInf->Pos + 0x01] = TempNote->Note;
				MidData[fInf->Pos + 0x02] = 0x00;
				fInf->Pos += 0x03;
				
				MoreNotes = true;
				
				RunNoteCnt --;
				if (RunNoteCnt)
					*TempNote = RunNotes[RunNoteCnt];
				CurNote --;	TempNote --;
			}
		}
		if (MoreNotes)
			(*Delay) -= TempDly;
	} while(MoreNotes);
	
	WriteMidiValue(fInf, *Delay);
	*Delay = 0x00;
	
	File_CheckRealloc(fInf, 0x03);
	MidData = fInf->Data;
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		MidData[fInf->Pos + 0x00] = Evt;
		MidData[fInf->Pos + 0x01] = Val1;
		MidData[fInf->Pos + 0x02] = Val2;
		fInf->Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		MidData[fInf->Pos + 0x00] = Evt;
		MidData[fInf->Pos + 0x01] = Val1;
		fInf->Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		MidData[fInf->Pos + 0x00] = Evt;
		MidData[fInf->Pos + 0x01] = Val1;
		MidData[fInf->Pos + 0x02] = Val2;
		fInf->Pos += 0x03;
		break;
	default:
		break;
	}
	
	return;
}

static void WriteMetaEvent_Data(FILE_INF* fInf, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiValue(fInf, *Delay);
	*Delay = 0x00;
	
	File_CheckRealloc(fInf, 0x02 + 0x05 + DataLen);	// worst case: 5 bytes of data length
	MidData = fInf->Data;
	MidData[fInf->Pos + 0x00] = 0xFF;
	MidData[fInf->Pos + 0x01] = MetaType;
	fInf->Pos += 0x02;
	WriteMidiValue(fInf, DataLen);
	memcpy(&MidData[fInf->Pos], Data, DataLen);
	fInf->Pos += DataLen;
	
	return;
}

static void WriteMidiValue(FILE_INF* fInf, UINT32 Value)
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
	
	File_CheckRealloc(fInf, ValSize);
	ValData = &fInf->Data[fInf->Pos];
	CurPos = ValSize;
	TempLng = Value;
	do
	{
		CurPos --;
		ValData[CurPos] = 0x80 | (TempLng & 0x7F);
		TempLng >>= 7;
	} while(TempLng);
	ValData[ValSize - 1] &= 0x7F;
	
	fInf->Pos += ValSize;
	
	return;
}

static void File_CheckRealloc(FILE_INF* FileInf, UINT32 BytesNeeded)
{
#define REALLOC_STEP	0x8000	// 32 KB block
	UINT32 MinPos;
	
	MinPos = FileInf->Pos + BytesNeeded;
	if (MinPos <= FileInf->Alloc)
		return;
	
	while(MinPos > FileInf->Alloc)
		FileInf->Alloc += REALLOC_STEP;
	FileInf->Data = (UINT8*)realloc(FileInf->Data, FileInf->Alloc);
	
	return;
}

static float OPN2DB(UINT8 TL)
{
	return -(TL * 3 / 4.0f);
}

static UINT8 DB2Mid(float DB)
{
	if (DB > 0.0f)
		DB = 0.0f;
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}



UINT8 LoadDACData(const char* FileName)
{
	FILE* hFile;
	UINT32 TempPos;
	UINT32 SmplPos;
	UINT8 CurDAC;
	DAC_DATA* TempDAC;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return 0x80;
	
	fseek(hFile, 0x00, SEEK_END);
	TempPos = (UINT32)ftell(hFile);
	
	fseek(hFile, 0x00, SEEK_SET);
	TempDAC = DacData;
	for (CurDAC = 0x00; CurDAC < 0x80; CurDAC ++, TempDAC ++)
	{
		if ((UINT32)ftell(hFile) >= TempPos)
			break;
		
		fread(TempDAC, 0x0C, 0x01, hFile);
		
		SmplPos = (TempDAC->V03_StartMSB << 16) | (TempDAC->V01_StartLSB << 0);
		if (TempDAC->V06_DataLen && TempPos > SmplPos)
			TempPos = SmplPos;
	}
	DacCount = CurDAC;
	
	TempDAC = DacData;
	for (CurDAC = 0x00; CurDAC < DacCount; CurDAC ++, TempDAC ++)
	{
		if (! TempDAC->V06_DataLen)
		{
			TempDAC->Data = NULL;
			continue;
		}
		
		SmplPos = (TempDAC->V03_StartMSB << 16) | (TempDAC->V01_StartLSB << 0);
		fseek(hFile, SmplPos, SEEK_SET);
		TempDAC->Data = (UINT8*)malloc(TempDAC->V06_DataLen);
		fread(TempDAC->Data, 0x01, TempDAC->V06_DataLen, hFile);
	}
	
	fclose(hFile);
	
	return 0x00;
}

void SaveDACData(const char* FileBase)
{
	FILE* hFile;
	UINT8 CurDAC;
	DAC_DATA* TempDAC;
	char OutFile[0x100];
	
	printf("Saving %02X DAC samples ...", DacCount);
	TempDAC = DacData;
	for (CurDAC = 0x00; CurDAC < DacCount; CurDAC ++, TempDAC ++)
	{
		if (! TempDAC->V06_DataLen)
		{
			TempDAC->Data = NULL;
			continue;
		}
		
		sprintf(OutFile, "%s_%02X.raw", FileBase, CurDAC);
		
		hFile = fopen(OutFile, "wb");
		if (hFile == NULL)
		{
			printf("Error opening %s!\n", OutFile);
			continue;
		}
		
		fwrite(TempDAC->Data, 0x01, TempDAC->V06_DataLen, hFile);
		
		fclose(hFile);
	}
	printf("  Done.\n");
	
	return;
}

void SaveInsAsGYB(const char* FileBase)
{
	const UINT8 GYB_GEMS[0x20] =
	{	0x04, 0x0A, 0x10, 0x16,	0x05, 0x0B, 0x11, 0x17,	0x06, 0x0C, 0x12, 0x18,	// 30-5C
		0x07, 0x0D, 0x13, 0x19,	0x08, 0x0E, 0x14, 0x1A,	0x09, 0x0F, 0x15, 0x1B,	// 60-8C
		0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x03, 0xFF, 0xFF};	// 90-9C, B0, B4, Extra
	const char* INST_STRS[] = {"FM", "DAC", "PSG T", "PSG N", "???"};
	FILE* hFile;
	UINT8 CurIns;
	UINT8 CurReg;
	INS_DATA* TempIns;
	char OutFile[0x100];
	char TempStr[0x80];
	const char* InsTypeStr;
	char* TempPtr;
	
	sprintf(OutFile, "%s.gyb", FileBase);
	
	hFile = fopen(OutFile, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", OutFile);
		return;
	}
	
	// Write Header
	fputc(26, hFile);	// Signature Byte 1
	fputc(12, hFile);	// Signature Byte 2
	fputc(0x02, hFile);	// Version
	fputc(InsCount, hFile);	// Melody Instruments
	fputc(0x00, hFile);		// Drum Instruments
	
	// Write Mappings
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++)
	{
		fputc(CurIns, hFile);	// GM Mapping: Melody
		fputc(0xFF, hFile);		// GM Mapping: Drum
	}
	for (; CurIns < 0x80; CurIns ++)
	{
		fputc(0xFF, hFile);
		fputc(0xFF, hFile);
	}
	
	fputc(0x00, hFile);	// LFO Value
	
	// Write Instrument Data
	TempIns = InsData;
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++, TempIns ++)
	{
		if (TempIns->InsType == 0x00)
		{
			// Type FM
			for (CurReg = 0x00; CurReg < 0x20; CurReg ++)
			{
				if (GYB_GEMS[CurReg] == 0xFF)
					fputc(0x00, hFile);
				else
					fputc(TempIns->Data[GYB_GEMS[CurReg]], hFile);
			}
		}
		else
		{
			// Type DAC/PSG
			for (CurReg = 0x00; CurReg < 0x20; CurReg ++)
			{
				fputc(0x00, hFile);
			}
		}
	}
	
	// Write Instrument Names
	TempIns = InsData;
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++, TempIns ++)
	{
		if (TempIns->InsType < 0x04)
			InsTypeStr = INST_STRS[TempIns->InsType];
		else
			InsTypeStr = INST_STRS[0x04];
		sprintf(TempStr, "%02X %s", CurIns, InsTypeStr);
		
		switch(TempIns->InsType)
		{
		case INSTYPE_FM:
			strcat(TempStr, ": ");
			if (((TempIns->Data[0x03] >> 7) ^ (TempIns->Data[0x03] >> 6)) & 0x01)	// 0x80 != 0x40
			{
				if (TempIns->Data[0x02] & 0x80)
					strcat(TempStr, "L");
				else
					strcat(TempStr, "R");
				strcat(TempStr, " only, ");
			}
			if (TempIns->Data[0x00])
			{
				TempPtr = TempStr + strlen(TempStr);
				sprintf(TempPtr, "R22 = %02X, ", TempIns->Data[0x00]);
			}
			if (TempIns->Data[0x01])
			{
				strcat(TempPtr, "Spc Ch3 Mode, ");
			}
			TempPtr = TempStr + strlen(TempStr) - 2;
			*TempPtr = '\0';
			break;
		case INSTYPE_DAC:
			TempPtr = TempStr + strlen(TempStr);
			sprintf(TempPtr, ": Pitch %02X", TempIns->Data[0x00]);
			break;
		case INSTYPE_PSG_TONE:
			break;
		case INSTYPE_PSG_NOISE:
			TempPtr = TempStr + strlen(TempStr);
			sprintf(TempPtr, ": Noise Mode %02X", TempIns->Data[0x00]);
			break;
		}
		
		CurReg = (UINT8)strlen(TempStr);
		fputc(CurReg, hFile);
		fwrite(TempStr, 0x01, CurReg, hFile);
	}
	
	fputc(0x00, hFile);	// Fake Checksum
	fputc(0x00, hFile);
	fputc(0x00, hFile);
	fputc(0x00, hFile);
	
	fclose(hFile);
	printf("Done.\n");
	
	return;
}

