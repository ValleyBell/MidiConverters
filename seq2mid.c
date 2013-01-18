// PlayStation SEQ2MID Converter
// -----------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stdtype.h"
#include "stdbool.h"


#define OPTIMIZE_NRPN


#define INS_MAX_INSTRUMENTS	0x80
#define INS_DATA_BLK_LINES	0x10
#define INS_LINE_CNT		(INS_MAX_INSTRUMENTS * INS_DATA_BLK_LINES)
typedef struct instrument_header
{
	UINT32 fccSignature;
	UINT32 dwUnknown04;
	UINT32 dwUnknown08;
	UINT32 dwInsSize;	// Instrument Definition + Data/Samples
	UINT16 wUnknown10;
	UINT16 wInsCount;	// Number of Instument Data Blocks
	UINT16 wInsLineUsed;
	UINT16 wInsIDCount;
	UINT8 bMasterVol;
	UINT8 bMasterPan;
	UINT8 bUnknown1A;
	UINT8 bUnknown1B;
	UINT32 dwPadding;
	// -> 20 Bytes
} INS_HEADER;
typedef struct instrument_description
{
	UINT8 bDataCount;	// Number of Lines
	UINT8 bInsVolume;
	UINT16 wUnknown02;
	UINT8 bInsPanorama;
	UINT8 bUnknown05;
	UINT8 bUnknown06;
	UINT8 bUnknown07;
	UINT32 dwPadding08;
	UINT32 dwPadding0C;
	// -> 10 Bytes
} INS_DESCRIPTION;
typedef struct instrument_data_block
{
	UINT8 bUnknown00;
	UINT8 bUnknown01;
	UINT8 bToneVol;
	UINT8 bTonePan;
	UINT8 bUnknown04;
	UINT8 bUnknown05;
	UINT8 bNoteLow;
	UINT8 bNoteHigh;
	UINT32 dwUnknown08;
	UINT8 bPbDepthMSB;
	UINT8 bPbDepthLSB;
	UINT16 wUnknown0E;
	UINT16 wUnknown10;
	UINT16 wUnknown12;
	UINT16 wInsNumber;
	UINT16 wInsID;
	UINT16 wUnknown18;
	UINT16 wUnknown1A;
	UINT16 wUnknown1C;
	UINT16 wUnknown1E;
	// -> 20 Bytes
} INS_DATA_BLOCK;
typedef struct ins_data
{
	INS_DATA_BLOCK DataBlk[INS_DATA_BLK_LINES];
} INS_DATA;
typedef struct instrument_data
{
	INS_HEADER Head;
	INS_DESCRIPTION Desc[INS_MAX_INSTRUMENTS];
	INS_DATA Data[INS_MAX_INSTRUMENTS];
} INSTRUMENT_DATA;


typedef struct instrument_setup
{
	UINT8 MidiIns;
	INT8 NoteMove;
	UINT8 DrumBase;
} INS_SETUP;

typedef struct channel_data_seq
{
	UINT8 Ins;
	INT8 Move;
	UINT8 DrmNote;
	UINT8 NewChn;
} CHN_DATA_SEQ;

typedef struct channel_data_midi
{
	UINT16 Pitch;
	UINT8 PBRange;
	UINT8 RPN_MSB;
	UINT8 RPN_LSB;
} CHN_DATA_MID;
typedef struct channel_data_drums
{
	UINT8 Pitch;
	UINT8 Volume;
	UINT8 Pan;
} CHN_DATA_DRM;


// FourCharCode Constants
#define FCC_VABp	0x56414270
#define FCC_SEQp	0x53455170


int main(int argc, char* argv[]);
static void StripFileTitle(char* FilePath);
static void StripFilePath(char* FilePath);
static void StripFileExtention(char* FilePath);
void ReadConvData(const char* FileName);
void LoadInsData(const char* FileName, INSTRUMENT_DATA* RetIns);
INS_SETUP* GetInsSetupData(UINT8 Ins, UINT8 Note, const INSTRUMENT_DATA* InsData);
UINT32 Seq2MidConversion(UINT32* PosStart, UINT8** RetData, INSTRUMENT_DATA* InsData);
UINT32 Seq2InsConversion(UINT32 PosStart, UINT8** RetData, INSTRUMENT_DATA* RetIns);
void GetInsData(UINT32 InsLen, const UINT8* InsData, INSTRUMENT_DATA* RetIns);
void WriteBE32(UINT8* Data, UINT32 Value);
void WriteBE16(UINT8* Data, UINT16 Value);
UINT16 ReadBE16(const UINT8* Data);
void WriteLE32(UINT8* Data, UINT32 Value);
//void ReverseBytes(UINT32 ByteCount, void* DstData, void* SrcData);
static bool IsNoteOn(UINT8 Event, UINT8 Volume);
static UINT8 MidiVolConv(UINT8 Volume);
void MakeInstrumentTestMIDI(const INSTRUMENT_DATA* InsData, const char* OutFileName);


UINT8 FileNameType;
bool FixMidi;
bool InsertPB;
bool FixInsSet;
bool FixVolume;
UINT32 SeqLen;
UINT8* SeqFile;
char InsFileName[0x100];
INS_SETUP InsSetup[INS_LINE_CNT];

int main(int argc, char* argv[])
{
	char FilePath[_MAX_PATH];
	char FileName[_MAX_PATH];
	char OutFileName[_MAX_PATH];
	char* StrPtr;
	UINT32 MidAllCnt;
	UINT32 MidMnCnt;
	UINT32 MidSubCnt;
	UINT32 CurPos;
	UINT32 MidLen;
	UINT8* MidFile;
	UINT32 TempLng;
	char TempFile[0x10];
	INSTRUMENT_DATA InsData;
	FILE* hFile;
	//char* FileList_Name;
	
	if (argc <= 2)
	{
		printf("Usage: seq2mid Options File.seq [InsMap.ini]\n");
		printf("Options: (options can be combined)\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    p   insert Pitch Bend resets\n");
		printf("    i   fix Instruments (needs InsMap.ini)\n");
		printf("    v   fix Volume (convert linear PSX to logarithmic MIDI)\n");
		return 9;
	}
	
	FixMidi = true;
	InsertPB = false;
	FixInsSet = false;
	FixVolume = false;
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			FixMidi = false;
			break;
		case 'P':
			InsertPB = true;
			break;
		case 'I':
			FixInsSet = true;
			break;
		case 'V':
			FixVolume = true;
			break;
		}
		StrPtr ++;
	}
	InsertPB &= FixMidi;
	FixInsSet &= FixMidi;
	
	FileNameType = 0x00;
	//FileNameType = 0x02;
	
//#define BASE_PATH	"D:\\VStudio-Programme\\VBasic\\PSFCnv\\ToP/"
//#define INS_SET		"ToP"
	// Works also with uncompressed PSFs
	//FileList_Name = "ToD\\ToD_lib.EXE.bin";
	//FileList_Name = "ToE\\InsSetA\\toe 026 mt. farlos.seq";
	//FileList_Name = "ToE\\InsSetB\\toe 099 mid boss 2.seq";
//	FileList_Name = "top 101 decisive.seq";
		
	if (FixInsSet)
	{
//		sprintf(FileName, "%s%s", BASE_PATH, INS_SET ".ini");
//		ReadConvData(FileName);
		if (argc <= 3)
		{
			printf("Insufficient arguments!\n");
			return 9;
		}
		
		ReadConvData(argv[3]);
		
		LoadInsData(InsFileName, &InsData);
		//sprintf(FileName, "%s%s", BASE_PATH, "ToP\\" INS_SET "_test.seq");
		//MakeInstrumentTestMIDI(&InsData ,FileName);
	}
	
	//sprintf(FileName, "%s%s", BASE_PATH, FileList_Name);
	strcpy(FileName, argv[2]);
	
	printf("Loading ...");
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0, SEEK_END);
	SeqLen = ftell(hFile);
	
	SeqFile = (UINT8*)malloc(SeqLen);
	fseek(hFile, 0, SEEK_SET);
	fread(SeqFile, 0x01, SeqLen, hFile);
	
	fclose(hFile);
	printf("  OK\n");
	
	strcpy(FilePath, FileName);
	StripFileTitle(FilePath);
	StripFilePath(FileName);
	StripFileExtention(FileName);
	
	printf("Converting ...\n");
	MidAllCnt = 0;
	MidMnCnt = -1;
	MidSubCnt = 0;
	CurPos = 0x00;
	while(true)
	{
		do
		{
			if (SeqFile[CurPos] != 'p')
			{
				CurPos ++;
				continue;
			}
			
			//memcpy(&TempLng, &SeqFile[CurPos], 0x04);
			TempLng = *((UINT32*)(SeqFile + CurPos));
			if (TempLng == FCC_VABp)
			{
				memcpy(&TempLng, &SeqFile[CurPos + 0x08], 0x04);
				if (! TempLng)
				{
					MidLen = Seq2InsConversion(CurPos, &MidFile, &InsData);
					
					MidMnCnt ++;
					MidSubCnt = 0;
					switch(FileNameType)
					{
					case 0x00:
						strcpy(TempFile, "");
						break;
					case 0x01:
						sprintf(TempFile, "_%02", MidAllCnt);
						break;
					case 0x02:
						sprintf(TempFile, "_%02", MidMnCnt);
						break;
					}
					sprintf(OutFileName, "%s%s%s.ins", FilePath, FileName, TempFile);
					
					hFile = fopen(OutFileName, "wb");
					
					fwrite(MidFile, 0x01, MidLen, hFile);
					
					fclose(hFile);
					
					CurPos += TempLng;
				}
			}
			else if (TempLng == FCC_SEQp)
			{
				memcpy(&TempLng, &SeqFile[CurPos + 0x04], 0x04);
				if (TempLng == 0x01000000 && SeqFile[CurPos + 0x07] == 0x01)
					break;
				else
					CurPos += 0x04;
			}
			else
			{
				CurPos ++;
			}
		} while(CurPos < SeqLen);
		if (CurPos >= SeqLen)
			break;
		
		if (MidMnCnt == -1 && FileNameType == 0x01)
			FileNameType = 0x00;
		switch(FileNameType)
		{
		case 0x00:
			strcpy(TempFile, "");
			break;
		case 0x01:
			sprintf(TempFile, "_%02u", MidAllCnt);
			break;
		case 0x02:
			sprintf(TempFile, "_%02u-%02u", MidMnCnt, MidSubCnt);
			break;
		}
		printf("File %u: %s%s\n", MidAllCnt + 1, FileName, TempFile);
		sprintf(OutFileName, "%s%s%s.mid", FilePath, FileName, TempFile);
		
		MidLen = Seq2MidConversion(&CurPos, &MidFile, &InsData);
		
		hFile = fopen(OutFileName, "wb");
		if (hFile != NULL)
		{
			fwrite(MidFile, 0x01, MidLen, hFile);
			
			fclose(hFile);
		}
		else
		{
			printf("Error opening file!\n");
		}
		
		MidAllCnt ++;
		MidSubCnt ++;
	}
	
	printf("%u Files saved.\n", MidAllCnt);
#ifdef _DEBUG
	//_getch();
#endif
	
	return 0;
}

static void StripFileTitle(char* FilePath)
{
	char* ChrPos1;
	char* ChrPos2;
	
	ChrPos1 = strrchr(FilePath, '\\');
	ChrPos2 = strrchr(FilePath, '/');
	if (ChrPos1 < ChrPos2)
		ChrPos1 = ChrPos2;
	if (ChrPos1 == NULL)
	{
		*FilePath = 0x00;
	}
	else
	{
		ChrPos1 ++;
		*ChrPos1 = '\0';
	}
	
	return;
}

static void StripFilePath(char* FilePath)
{
	char* ChrPos1;
	char* ChrPos2;
	
	ChrPos1 = strrchr(FilePath, '\\');
	ChrPos2 = strrchr(FilePath, '/');
	if (ChrPos1 < ChrPos2)
		ChrPos1 = ChrPos2;
	if (ChrPos1 != NULL)
	{
		ChrPos1 ++;
		strcpy(FilePath, ChrPos1);
	}
	
	return;
}

static void StripFileExtention(char* FilePath)
{
	char* PntPos;
	char* ChrPos1;
	char* ChrPos2;
	
	PntPos = strrchr(FilePath, '.');
	if (PntPos != NULL)
	{
		ChrPos1 = strrchr(PntPos, '\\');
		ChrPos2 = strrchr(PntPos, '/');
		if (ChrPos1 < ChrPos2)
			ChrPos1 = ChrPos2;
		if (ChrPos1 != NULL)
			PntPos = NULL;
	}
	
	if (PntPos != NULL)
		*PntPos = '\0';
	
	return;
}

void ReadConvData(const char* FileName)
{
	FILE* hFile;
	char TempStr[0x100];
	char* TempPnt;
	char* EndPnt;
	UINT8 InsNum;
	UINT8 InsLine;
	INS_SETUP* TempIns;
	UINT8 FirstLine;
	
	hFile = fopen(FileName, "rt");
	if (hFile == NULL)
	{
		printf("Error opening %s\n", FileName);
		return;
	}
	
	TempIns = InsSetup;
	for (InsNum = 0x00; InsNum < INS_MAX_INSTRUMENTS; InsNum ++)
	{
		for (InsLine = 0x00; InsLine < INS_DATA_BLK_LINES; InsLine ++, TempIns ++)
		{
			TempIns->MidiIns = 0xFF/*InsNum*/;
			TempIns->NoteMove = 0x00;
			TempIns->DrumBase = 0xFF;
		}
	}
	
	strcpy(InsFileName, FileName);
	StripFileTitle(InsFileName);
	
	FirstLine = 0x01;
	while(! feof(hFile))
	{
		TempPnt = fgets(TempStr, 0x100, hFile);
		if (TempPnt == NULL)
			break;
		if (TempStr[0x00] == '\n' || TempStr[0x00] == '\0')
			continue;
		if (TempStr[0x00] == ';')
		{
			// skip comment lines
			// fgets has set a null-terminator at char 0xFF
			while(TempStr[strlen(TempStr) - 1] != '\n')
			{
				fgets(TempStr, 0x100, hFile);
				if (TempStr[0x00] == '\0')
					break;
			}
			continue;
		}
		
		if (FirstLine)
		{
			FirstLine = 0x00;
			TempPnt = strchr(TempStr, '\n');
			if (TempPnt != NULL)
				*TempPnt = '\0';
			strcat(InsFileName, TempStr);
			continue;
		}
		
		TempPnt = strchr(TempStr, '\t');
		if (TempPnt == NULL || TempPnt == TempStr)
			continue;	// invalid line
		
		InsNum = (UINT8)strtoul(TempStr, &EndPnt, 0x10);
		if (EndPnt == TempStr || InsNum >= INS_MAX_INSTRUMENTS)
			continue;
		
		if (EndPnt != NULL && *EndPnt == '-')
			InsLine = (UINT8)strtoul(EndPnt + 1, NULL, 0x10);
		else
			InsLine = 0x00;
		InsLine &= 0x0F;
		
		TempIns = &InsSetup[(InsNum << 4) | InsLine];
		TempPnt ++;
		if (*TempPnt != 'D')
		{
			TempIns->MidiIns = (UINT8)strtoul(TempPnt, NULL, 0x10);
			TempPnt = strchr(TempPnt, '\t');
			if (TempPnt != NULL)
			{
				TempPnt ++;
				TempIns->NoteMove = (UINT8)strtoul(TempPnt, NULL, 10);
			}
			else
			{
				TempIns->NoteMove = 0x00;
			}
			TempIns->DrumBase = 0xFF;
		}
		else
		{
			TempPnt ++;
			TempIns->MidiIns = 0x80;
			TempIns->NoteMove = (UINT8)strtoul(TempPnt, NULL, 0x10);
			TempPnt = strchr(TempPnt, '\t');
			if (TempPnt != NULL)
			{
				TempPnt ++;
				TempIns->DrumBase = (UINT8)strtoul(TempPnt, NULL, 0x10);
			}
			else
			{
				TempIns->DrumBase = 0x00;
			}
			if (! TempIns->DrumBase)
				TempIns->DrumBase = TempIns->NoteMove;
		}
	}
	
	fclose(hFile);
	
	return;
}

void LoadInsData(const char* FileName, INSTRUMENT_DATA* RetIns)
{
	FILE* hFile;
	UINT32 InsLen;
	UINT8* InsData;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return;
	}
	
	fseek(hFile, 0, SEEK_END);
	InsLen = ftell(hFile);
	
	InsData = (UINT8*)malloc(InsLen);
	fseek(hFile, 0, SEEK_SET);
	fread(InsData, 0x01, InsLen, hFile);
	
	fclose(hFile);
	
	GetInsData(InsLen, InsData, RetIns);
	
	free(InsData);
	
	return;
}

INS_SETUP* GetInsSetupData(UINT8 Ins, UINT8 Note, const INSTRUMENT_DATA* InsData)
{
	const INS_DESCRIPTION* TempDesc;
	const INS_DATA_BLOCK* TempBlk;
	INS_SETUP* TempIS;
	INS_SETUP* ISBase;
	UINT8 CurLin;
	
	TempDesc = &InsData->Desc[Ins];
	if (TempDesc->bDataCount && Ins < INS_MAX_INSTRUMENTS)
	{
		TempBlk = InsData->Data[Ins].DataBlk;
		TempIS = &InsSetup[Ins << 4];
		ISBase = NULL;
		for (CurLin = 0x00; CurLin < TempDesc->bDataCount; CurLin ++, TempIS ++, TempBlk ++)
		{
			if (ISBase == NULL && TempIS->MidiIns != 0xFF)
				ISBase = TempIS;
			
			if (Note >= TempBlk->bNoteLow && Note <= TempBlk->bNoteHigh)
			{
				if (TempIS->MidiIns != 0xFF)
					return TempIS;
				else
					break;
			}
		}
		
		if (ISBase == NULL)
			ISBase = &InsSetup[Ins << 4];
		if (ISBase->MidiIns == 0xFF)
		{
			printf("Warning: Unmapped instrument %02X!\n", Ins);
			ISBase->MidiIns = Ins;
		}
		return ISBase;
	}
	else
	{
		printf("Illegal Instrument: %02X\n", Ins);
		return &InsSetup[0x00];
	}
}

void WriteRPN(UINT8* MidFile, UINT32* MidPos, UINT8 RpnMSB, UINT8 RpnLSB, UINT8 RpnData,
			  CHN_DATA_MID* MidChnData)
{
	UINT8 RPNMsk;
	UINT8 RPNCtrl;
	
	RPNMsk = RpnMSB & 0x80;
	if (! RPNMsk)
		RPNCtrl = 0x64;	// RPN
	else
		RPNCtrl = 0x62;	// NRPN
	RpnMSB &= 0x7F;
	
#ifdef OPTIMIZE_NRPN
	if (MidChnData->RPN_MSB != (RPNMsk | RpnMSB))
	{
		MidFile[*MidPos + 0x00] = RPNCtrl | 0x01;
		MidFile[*MidPos + 0x01] = RpnMSB;
		MidFile[*MidPos + 0x02] = 0x00;
		*MidPos += 0x03;
		MidChnData->RPN_MSB = RPNMsk | RpnMSB;
	}
	if (MidChnData->RPN_LSB != (RPNMsk | RpnLSB))
	{
		MidFile[*MidPos + 0x00] = RPNCtrl | 0x00;
		MidFile[*MidPos + 0x01] = RpnLSB;
		MidFile[*MidPos + 0x02] = 0x00;
		*MidPos += 0x03;
		MidChnData->RPN_LSB = RPNMsk | RpnLSB;
	}
#else
	if (MidChnData->RPN_MSB != (RPNMsk | RpnMSB) ||
		MidChnData->RPN_LSB != (RPNMsk | RpnLSB))
	{
		MidFile[*MidPos + 0x00] = RPNCtrl | 0x01;
		MidFile[*MidPos + 0x01] = RpnMSB;
		MidFile[*MidPos + 0x02] = 0x00;
		MidFile[*MidPos + 0x03] = RPNCtrl | 0x00;
		MidFile[*MidPos + 0x04] = RpnLSB;
		MidFile[*MidPos + 0x05] = 0x00;
		*MidPos += 0x06;
		
		MidChnData->RPN_MSB = RPNMsk | RpnMSB;
		MidChnData->RPN_LSB = RPNMsk | RpnLSB;
	}
#endif
	
	if (! (RpnData & 0x80))
	{
		MidFile[*MidPos + 0x00] = 0x06;
		MidFile[*MidPos + 0x01] = RpnData;
		MidFile[*MidPos + 0x02] = 0x00;
		*MidPos += 0x03;
	}
	
	return;
}

UINT32 Seq2MidConversion(UINT32* PosStart, UINT8** RetData, INSTRUMENT_DATA* InsData)
{
	UINT32 MidLen;
	UINT8* MidFile;
	UINT32 CurPos;
	UINT32 MidPos;
	UINT32 TempLng;
	INT16 TempSht;
	UINT8 TempByt;
	//UINT8* TempArr;
	UINT8 LastEvt;
	bool ShortEvt;
	bool WriteFullEvt;
	bool MidiEnd;
	UINT16 MidRes;
	bool IntroOn;
	UINT32 IntroDelay;
	UINT32 Delay;
	CHN_DATA_SEQ ChnSeq[0x10];
	CHN_DATA_MID ChnMid[0x10];
	CHN_DATA_DRM ChnDrm[0x80];
	UINT8 ChnDrmPtchBak[0x80];
	UINT8 CurChn;
	UINT8 MidChn;
	INS_DESCRIPTION* TempDesc;
	INS_DATA_BLOCK* TempBlk;
	INS_SETUP* TempIS;
	CHN_DATA_SEQ* TempChS;
	CHN_DATA_MID* TempChM;
	CHN_DATA_DRM* TempChD;
	UINT8 CurDrmPtch;
	
	MidLen = 0x40000;
	MidFile = (UINT8*)malloc(MidLen);
	
	CurPos = *PosStart;
	MidiEnd = false;
	
	CurPos += 0x04;
	WriteBE32(	&MidFile[0x00],	0x4D546864);	// MThd
	WriteBE32(	&MidFile[0x04],	0x06);			// Header Length
	memcpy(		&MidFile[0x08],	&SeqFile[CurPos + 0x00], 0x02);	// Format
	memcpy(		&MidFile[0x0A],	&SeqFile[CurPos + 0x02], 0x02);	// Tracks
	memcpy(		&MidFile[0x0C],	&SeqFile[CurPos + 0x04], 0x02);	// Tick/Quarter
	//If SeqFile[CurPos + 0x03) <> 0x01 Then Stop
	
	WriteBE32(	&MidFile[0x0E],	0x4D54726B);	//MTrk
	WriteBE32(	&MidFile[0x12],	0xFFFFFFFF);
	MidRes = ReadBE16(&SeqFile[CurPos + 0x04]);
	MidPos = 0x16;
	CurPos += 0x06;
	
	for (CurChn = 0x00; CurChn < 0x10; CurChn ++)
	{
		TempChS = &ChnSeq[CurChn];
		TempChM = &ChnMid[CurChn];
		
		TempChS->Ins = 0xFF;
		TempChS->Move = 0;
		TempChS->DrmNote = 0xFF;
		TempChS->NewChn = 0xFF;
		
		TempChM->Pitch = 0x4000;
		TempChM->PBRange = 0x00;
		TempChM->RPN_MSB = 0x7F;	// RPN Null
		TempChM->RPN_LSB = 0x7F;
	}
	for (CurChn = 0x00; CurChn < 0x80; CurChn ++)
	{
		TempChD = &ChnDrm[CurChn];
		
		TempChD->Pitch = 0x40;
		TempChD->Volume = 0x7F;
		TempChD->Pan = 0x40;
		ChnDrmPtchBak[CurChn] = 0xFF;
	}
	CurDrmPtch = 0xFF;
	
	// Tempo
	MidFile[MidPos + 0x00] = 0x00;
	MidFile[MidPos + 0x01] = 0xFF;
	MidFile[MidPos + 0x02] = 0x51;
	MidFile[MidPos + 0x03] = 0x03;
	memcpy(&MidFile[MidPos + 0x04], &SeqFile[CurPos], 0x03);
	MidPos += 0x07;
	CurPos += 0x03;
	
	if (FixMidi)
	{
		// Time for Initialiation
		IntroDelay = (4 * MidRes >> SeqFile[CurPos + 0x01]) * SeqFile[CurPos + 0x00];
		IntroOn = true;
	}
	else
	{
		IntroOn = false;
	}
	MidFile[MidPos + 0x00] = 0x00;
	MidFile[MidPos + 0x01] = 0xFF;
	MidFile[MidPos + 0x02] = 0x58;
	MidFile[MidPos + 0x03] = 0x04;
	MidFile[MidPos + 0x04] = SeqFile[CurPos + 0x00];
	MidFile[MidPos + 0x05] = SeqFile[CurPos + 0x01];
	MidFile[MidPos + 0x06] = 0x06 << SeqFile[CurPos + 0x01];
	MidFile[MidPos + 0x07] = 0x08;
	MidPos += 0x08;
	CurPos += 0x02;
	
	WriteFullEvt = false;
	do
	{
		if (MidPos + 0x020 >= MidLen)
		{
			MidLen += 0x40000;
			MidFile = (UINT8*)realloc(MidFile, MidLen);
		}
		
		if (IntroOn)
		{
			Delay = 0x00;
			while(SeqFile[CurPos] & 0x80)
			{
				Delay <<= 7;
				Delay |= SeqFile[CurPos] & 0x7F;
				CurPos ++;
			}
			Delay <<= 7;
			Delay |= SeqFile[CurPos] & 0x7F;
			CurPos ++;
			
			// Finish Initialisation Measure
			if (Delay >= IntroDelay)
			{
				TempLng = IntroDelay;
				TempByt = 0;
				while(TempLng >= 0x80)
				{
					TempLng >>= 7;
					TempByt += 7;
				}
				
				while(TempByt)
				{
					MidFile[MidPos] = 0x80 | ((IntroDelay >> TempByt) & 0x7F);
					TempByt -= 7;
					MidPos ++;
				}
				MidFile[MidPos] = 0x00 | (IntroDelay & 0x7F);
				MidPos ++;
				
				// Measure: TempByt / 2 ^ TempSht
				TempByt = 0x04;
				TempSht = 0x02;
				MidFile[MidPos + 0x00] = 0xFF;
				MidFile[MidPos + 0x01] = 0x58;
				MidFile[MidPos + 0x02] = 0x04;
				MidFile[MidPos + 0x03] = TempByt;
				MidFile[MidPos + 0x04] = (UINT8)TempSht;
				MidFile[MidPos + 0x05] = 0x06 << TempSht;
				MidFile[MidPos + 0x06] = 0x08;
				MidPos += 0x07;
				WriteFullEvt = true;
				
				Delay -= IntroDelay;
				IntroDelay = 0;
				IntroOn = false;
			}
			else
			{
				IntroDelay -= Delay;
			}
			
			TempLng = Delay;
			TempByt = 0;
			while(TempLng >= 0x80)
			{
				TempLng >>= 7;
				TempByt += 7;
			}
			
			while(TempByt)
			{
				MidFile[MidPos] = 0x80 | ((Delay >> TempByt) & 0x7F);
				TempByt -= 7;
				MidPos ++;
			}
			MidFile[MidPos] = 0x00 | (Delay & 0x7F);
			MidPos ++;
		}
		else
		{
			while(SeqFile[CurPos] & 0x80)
			{
				MidFile[MidPos] = SeqFile[CurPos];
				CurPos ++;
				MidPos ++;
			}
			MidFile[MidPos] = SeqFile[CurPos];
			CurPos ++;
			MidPos ++;
		}
		
		ShortEvt = ! (SeqFile[CurPos] & 0x80);
		if (! ShortEvt)
		{
			LastEvt = SeqFile[CurPos];
			CurPos ++;
		}
		if (WriteFullEvt)
		{
			ShortEvt = false;
			WriteFullEvt = false;
		}
		
		CurChn = LastEvt & 0x0F;
		if (ChnSeq[CurChn].NewChn != 0xFF)
			MidChn = ChnSeq[CurChn].NewChn;
		else
			MidChn = CurChn;
		switch(LastEvt & 0xF0)
		{
		case 0xC0:
			if (FixInsSet && InsData->Head.wInsCount)
			{
				TempChS = &ChnSeq[CurChn];
				TempChS->Ins = SeqFile[CurPos + 0x00];
				TempDesc = &InsData->Desc[TempChS->Ins];
				
				TempIS = GetInsSetupData(TempChS->Ins, 0xFF, InsData);
				SeqFile[CurPos + 0x00] = TempIS->MidiIns & 0x7F;
				
				if ((TempIS->MidiIns & 0x80) && TempIS->DrumBase != 0xFF)
				{
					TempChS->DrmNote = (UINT8)TempIS->NoteMove;
					TempChS->NewChn = 0x09;
					if (CurChn != 0x09 && ChnSeq[0x09].NewChn == 0xFF)
						ChnSeq[0x09].NewChn = CurChn;
					MidChn = 0x09;
					
					if (CurDrmPtch == 0xFF)
					{
						MidFile[MidPos + 0x00] = 0xC0 | MidChn;
						MidFile[MidPos + 0x01] = 0x00;
						MidFile[MidPos + 0x02] = 0x00;
						MidPos += 0x03;
						MidFile[MidPos + 0x00] = 0xB0 | MidChn;
						MidFile[MidPos + 0x01] = 0x07;
						MidFile[MidPos + 0x02] = 0x7F;
						MidFile[MidPos + 0x03] = 0x00;
						MidPos += 0x04;
						CurDrmPtch = 0x00;
					}
					
					TempChM = &ChnMid[MidChn];
					TempIS = &InsSetup[TempChS->Ins << 4];
					TempBlk = InsData->Data[TempChS->Ins].DataBlk;
					ShortEvt = false;
					for (TempByt = 0x00; TempByt < TempDesc->bDataCount; TempByt ++, TempBlk ++, TempIS ++)
					{
						if (TempIS->DrumBase == 0xFF)
							continue;
						TempChD = &ChnDrm[TempIS->NoteMove];
						
						if (TempChD->Volume != TempBlk->bToneVol)
						{
							if (! ShortEvt)
							{
								MidFile[MidPos] = 0xB0 | MidChn;
								MidPos ++;
								ShortEvt = true;
							}
							// Write Drum Volume Level
							WriteRPN(MidFile, &MidPos, 0x9A, (UINT8)TempIS->NoteMove,
									TempBlk->bToneVol, TempChM);
							TempChD->Volume = TempBlk->bToneVol;
						}
						
						if (TempChD->Pan != TempBlk->bTonePan)
						{
							if (! ShortEvt)
							{
								MidFile[MidPos] = 0xB0 | MidChn;
								MidPos ++;
								ShortEvt = true;
							}
							// Write Drum Panorama
							WriteRPN(MidFile, &MidPos, 0x9C, (UINT8)TempIS->NoteMove,
									TempBlk->bTonePan, TempChM);
							TempChD->Pan = TempBlk->bTonePan;
						}
					}
					ShortEvt = false;
					
					MidChn = CurChn;
					if (MidChn == 0x09)
					{
						for (MidChn = 0x00; MidChn < 0x0F; MidChn ++)
						{
							if (MidChn == 0x09)
								continue;
							if (ChnSeq[MidChn].NewChn == 0x09)
								break;
						}
						// if no free channel found, use 0x0F
					}
					SeqFile[CurPos + 0x00] = 0x7F;
				}
				else
				{
					if ((TempIS->MidiIns & 0x80) && TempIS->DrumBase == 0xFF)
					{
						TempChS->Move = 0;
						TempChS->DrmNote = (UINT8)TempIS->NoteMove;
						TempChS->NewChn = 0x09;
						
						MidChn = CurChn;
						SeqFile[CurPos + 0x00] = 0x00;
					}
					else
					{
						if (CurChn == 0x09 && TempChS->NewChn == 0xFF)
						{
							TempChS->NewChn = 0x0F;
							MidChn = TempChS->NewChn;
						}
						else if (CurChn != 0x09 && TempChS->NewChn == 0x09)
						{
							TempChS->NewChn = CurChn;
							MidChn = TempChS->NewChn;
						}
						TempChS->Move = TempIS->NoteMove;
						TempChS->DrmNote = 0xFF;
					}
					MidFile[MidPos + 0x00] = 0xB0 | MidChn;
					MidFile[MidPos + 0x01] = 0x0B;	//0x27
					MidFile[MidPos + 0x02] = TempDesc->bInsVolume;
					MidFile[MidPos + 0x03] = 0x00;
					MidFile[MidPos + 0x04] = 0x0A;	//0x2A
					MidFile[MidPos + 0x05] = TempDesc->bInsPanorama;
					MidFile[MidPos + 0x06] = 0x00;
					MidPos += 0x07;
					ShortEvt = false;
				}
				
				if (TempDesc->bDataCount)
				{
					TempBlk = InsData->Data[TempChS->Ins].DataBlk;
					TempChM = &ChnMid[MidChn];
					if (TempBlk->bPbDepthMSB && TempChM->PBRange != TempBlk->bPbDepthMSB)
					{
						// Note: PbDepth MSB and LSB are always the same
						MidFile[MidPos + 0x00] = 0xB0 | MidChn;
						MidPos += 0x01;
						// Write Pitch Bend Range
						WriteRPN(MidFile, &MidPos, 0x00, 0x00, TempBlk->bPbDepthMSB, TempChM);
						ShortEvt = false;
						
						TempChM->PBRange = TempBlk->bPbDepthMSB;
					}
				}
			}
			// fall through
		case 0xD0:
			if (! ShortEvt)
			{
				MidFile[MidPos] = (LastEvt & 0xF0) | MidChn;
				MidPos ++;
			}
			MidFile[MidPos + 0x00] = SeqFile[CurPos + 0x00];
			MidPos += 0x01;
			CurPos += 0x01;
			break;
		case 0x80:
		case 0x90:
		case 0xA0:
		case 0xB0:
		case 0xE0:
			if (FixMidi)
			{
				TempChS = &ChnSeq[CurChn];
				if (TempChS->DrmNote != 0xFF)
					MidChn = 0x09;
				TempChM = &ChnMid[MidChn];
				
				if ((LastEvt & 0xE0) == 0x80)	// Event 0x80 and 0x90
				{
					if (TempChS->DrmNote != 0xFF)
						TempIS = GetInsSetupData(TempChS->Ins, SeqFile[CurPos + 0x00], InsData);
					
					if (IsNoteOn(LastEvt, SeqFile[CurPos + 0x01]))
					{
						if (InsertPB && TempChS->DrmNote == 0xFF && TempChM->Pitch != 0x4000)
						{
							MidFile[MidPos + 0x00] = 0xE0 | MidChn;
							MidFile[MidPos + 0x01] = 0x00;
							MidFile[MidPos + 0x02] = 0x40;
							MidFile[MidPos + 0x03] = 0x00;
							MidPos += 0x04;
							
							ShortEvt = false;
							TempChM->Pitch = 0x4000;
						}
						if (TempChS->DrmNote != 0xFF)
						{
							TempChS->DrmNote = (UINT8)TempIS->NoteMove;
							if (TempIS->DrumBase != 0xFF)
							{
								TempChD = &ChnDrm[TempChS->DrmNote];
								
								TempSht = SeqFile[CurPos + 0x00] - TempIS->DrumBase;
								if (TempSht < -0x40)
									TempSht = -0x40;
								else if (TempSht > 0x3F)
									TempSht = 0x3F;
								TempByt = (UINT8)(0x40 + TempSht);
								
								if (TempChD->Pitch != TempByt)
								{
									MidFile[MidPos + 0x00] = 0xB0 | MidChn;
									MidPos ++;
									WriteRPN(MidFile, &MidPos, 0x98, TempChS->DrmNote, TempByt, TempChM);
									TempChD->Pitch = TempByt;
									if (ChnDrmPtchBak[TempChS->DrmNote] == 0xFF)
										ChnDrmPtchBak[TempChS->DrmNote] = 0x80;
									
									ShortEvt = false;
								}
								// ChnDrmPtchBak Values:
								//	  FF  - not used
								//	  80  - first drum note sets pitch
								//	00-7F - drum pitch was set before and needs reset on loop
								if (ChnDrmPtchBak[TempChS->DrmNote] == 0xFF)
									ChnDrmPtchBak[TempChS->DrmNote] = TempByt;
							}
						}
					}
					
					if (TempChS->DrmNote == 0xFF)
					{
						TempSht = SeqFile[CurPos + 0x00] + TempChS->Move;
						if (TempSht < 0x00)
							SeqFile[CurPos + 0x00] = 0x00;
						else if (TempSht > 0x7F)
							SeqFile[CurPos + 0x00] = 0x7F;
						else
							SeqFile[CurPos + 0x00] = (UINT8)TempSht;
					}
					else
					{
						TempIS = GetInsSetupData(TempChS->Ins, SeqFile[CurPos + 0x00], InsData);
						if (TempIS->DrumBase == 0xFF)
						{
							// Use lookup-table to correct drums
						}
						else
						{
							SeqFile[CurPos + 0x00] = (UINT8)TempIS->NoteMove;
						}
					}
					SeqFile[CurPos + 0x01] = MidiVolConv(SeqFile[CurPos + 0x01]);
				}
				else if ((LastEvt & 0xF0) == 0xB0)
				{
					switch(SeqFile[CurPos + 0x00])
					{
					case 0x07:	// Volume
					case 0x0A:	// Pan
						if (SeqFile[CurPos + 0x00] == 0x07)
							SeqFile[CurPos + 0x01] = MidiVolConv(SeqFile[CurPos + 0x01]);
						
						if (TempChS->DrmNote != 0xFF)
						{
							MidFile[MidPos + 0x00] = 0xB0 | MidChn;
							MidPos ++;
							
							TempDesc = &InsData->Desc[TempChS->Ins];
							TempIS = &InsSetup[TempChS->Ins << 4];
							ShortEvt = false;
							for (TempByt = 0x00; TempByt < TempDesc->bDataCount; TempByt ++, TempIS ++)
							{
								if (TempIS->DrumBase == 0xFF)
									continue;
								TempChD = &ChnDrm[TempIS->NoteMove];
								
								if (SeqFile[CurPos + 0x00] == 0x07)
								{
									// Drum Volume
									if (TempChD->Volume != SeqFile[CurPos + 0x01])
									{
										TempChD->Volume = SeqFile[CurPos + 0x01];
										WriteRPN(MidFile, &MidPos, 0x9A, (UINT8)TempIS->NoteMove,
												TempChD->Volume, TempChM);
										ShortEvt = true;
									}
								}
								else if (SeqFile[CurPos + 0x00] == 0x0A)
								{
									// Drum Pan
									if (TempChD->Pan != SeqFile[CurPos + 0x01])
									{
										TempChD->Pan = SeqFile[CurPos + 0x01];
										WriteRPN(MidFile, &MidPos, 0x9C, (UINT8)TempIS->NoteMove,
												TempChD->Pan, TempChM);
										ShortEvt = true;
									}
								}
							}
							if (ShortEvt)
							{
								MidPos -= 0x03;	// undo last event
							}
							else
							{
								if (SeqFile[CurPos + 0x00] == 0x07)
									TempByt = 0x9A;
								else if (SeqFile[CurPos + 0x00] == 0x0A)
									TempByt = 0x9C;
								WriteRPN(MidFile, &MidPos, TempByt, TempChS->DrmNote, 0xFF, TempChM);
							}
							SeqFile[CurPos + 0x00] = 0x06;	// Data MSB
							ShortEvt = true;
						}
						break;
					case 0x65:	// RPN MSB
					case 0x64:	// RPN LSB
					case 0x63:	// NRPN MSB
					case 0x62:	// NRPN LSB
						TempByt = (SeqFile[CurPos + 0x00] & 0x02) << 6;
						if (SeqFile[CurPos + 0x00] & 0x01)
							TempChM->RPN_MSB = TempByt | SeqFile[CurPos + 0x01];
						else
							TempChM->RPN_LSB = TempByt | SeqFile[CurPos + 0x01];
						
						if (SeqFile[CurPos + 0x00] == 0x63)
						{
							// NRPN 20 - Loop Start (Data MSB: Loop Count)
							// NRPN 30 - Loop End
							if (SeqFile[CurPos + 0x01] == 20)
							{
								for (TempByt = 0x00; TempByt < 0x80; TempByt ++)
									ChnDrmPtchBak[TempByt] = 0xFF;
							}
							else if (SeqFile[CurPos + 0x01] == 30)
							{
								TempChM = &ChnMid[0x09];
								TempChD = &ChnDrm[0x00];
								WriteFullEvt = false;
								for (TempByt = 0x00; TempByt < 0x80; TempByt ++, TempChD ++)
								{
									if (ChnDrmPtchBak[TempByt] < 0x80 &&
										TempChD->Pitch != ChnDrmPtchBak[TempByt])
									{
										if (! WriteFullEvt)
										{
											MidFile[MidPos + 0x00] = 0xB9;
											MidPos ++;
											WriteFullEvt = true;
										}
										WriteRPN(MidFile, &MidPos, 0x98, TempByt, ChnDrmPtchBak[TempByt], TempChM);
										TempChD->Pitch = ChnDrmPtchBak[TempByt];
										
										ShortEvt = false;
									}
								}
								WriteFullEvt = false;
							}
						}
						break;
					case 0x06:
						// prevent the Loop Controller from changing something different
						if (TempChM->RPN_MSB & 0x80)
						{
							TempByt = TempChM->RPN_MSB & 0x7F;
							if (TempByt == 20 || TempByt == 30)
								SeqFile[CurPos + 0x00] = 0x26;
						}
						break;
					}	// end switch(SeqFile[CurPos + 0x00])
				}
				else if ((LastEvt & 0xF0) == 0xE0)
				{
					memcpy(&TempChM->Pitch, &SeqFile[CurPos + 0x00], 0x02);
				}	// end if (LastEvt & 0xF0)
			}	// end if (FixMidi)
			
			if (! ShortEvt)
			{
				MidFile[MidPos] = (LastEvt & 0xF0) | MidChn;
				MidPos ++;
			}
			MidFile[MidPos + 0x00] = SeqFile[CurPos + 0x00];
			MidFile[MidPos + 0x01] = SeqFile[CurPos + 0x01];
			MidPos += 0x02;
			CurPos += 0x02;
			break;
		case 0xF0:
			switch(LastEvt)
			{
			case 0xF0:
				printf("SysEx Event ?!\n");
				CurPos ++;
				break;
			case 0xFF:
				MidFile[MidPos] = LastEvt;
				MidPos ++;
				
				MidFile[MidPos + 0x00] = SeqFile[CurPos + 0x00];
				switch(SeqFile[CurPos + 0x00])
				{
				case 0x51:
					TempByt = 0x03;
					break;
				case 0x2F:
					TempByt = 0x00;
					MidiEnd = true;
					break;
				default:
					printf("MetaEvent: %02X ?!\n", SeqFile[CurPos + 0x01]);
					TempByt = 0x00;
					break;
				}
				MidFile[MidPos + 0x01] = TempByt;
				if (TempByt)
					memcpy(&MidFile[MidPos + 0x02], &SeqFile[CurPos + 0x01], TempByt);
				MidPos += 0x02 + TempByt;
				CurPos += 0x01 + TempByt;
				break;
			default:
				printf("???\n");
				MidPos ++;
				MidiEnd = true;
				break;
			}
			break;
		default:
			printf("???\n");
			MidPos ++;
			MidiEnd = true;
			break;
		}
	} while(! MidiEnd);
	MidLen = MidPos;
	
	TempLng = MidLen - 0x16;
	WriteBE32(	&MidFile[0x12],	TempLng);
	
	*RetData = MidFile;
	*PosStart = CurPos;
	
	return MidLen;
}

UINT32 Seq2InsConversion(UINT32 PosStart, UINT8** RetData, INSTRUMENT_DATA* RetIns)
{
	UINT32 CurPos;
	UINT32 InsLen;
	UINT16 InsCnt;
	
	CurPos = PosStart;
	
	// Header
	memcpy(&InsCnt, &SeqFile[CurPos + 0x12], 0x02);
	CurPos += 0x20;
	
	// Instrument Description
	CurPos += 0x10 * INS_MAX_INSTRUMENTS;
	
	// Instrument Data
	CurPos += 0x20 * INS_DATA_BLK_LINES * InsCnt;
	
	
	InsLen = CurPos - PosStart;
	*RetData = (UINT8*)malloc(InsLen);
	memcpy(*RetData, &SeqFile[PosStart], InsLen);
	
	GetInsData(InsLen, *RetData, RetIns);
	
	return InsLen;
}

void GetInsData(UINT32 InsLen, const UINT8* InsData, INSTRUMENT_DATA* RetIns)
{
	UINT32 CurPos;
	UINT8 CurIns;
	UINT8 CurLin;
	UINT8 InsNo;
	UINT16 TempSht;
	INS_HEADER* IHd;
	INS_DESCRIPTION* IDsc;
	INS_DATA_BLOCK* IDBlk;
	
	CurPos = 0x00;
	
	//ReDim RetIns.Desc = ((0x00 To INS_MAX_INSTRUMENTS - 1)
	//ReDim RetIns.Data(0x00 To INS_MAX_INSTRUMENTS - 1)
	
	IHd = &RetIns->Head;
	memcpy(&IHd->fccSignature,	&InsData[CurPos + 0x00], 0x04);
	memcpy(&IHd->dwUnknown04,	&InsData[CurPos + 0x04], 0x04);
	memcpy(&IHd->dwUnknown08,	&InsData[CurPos + 0x08], 0x04);
	memcpy(&IHd->dwInsSize,		&InsData[CurPos + 0x0C], 0x04);
	memcpy(&IHd->wUnknown10,	&InsData[CurPos + 0x10], 0x02);
	memcpy(&IHd->wInsCount,		&InsData[CurPos + 0x12], 0x02);
	memcpy(&IHd->wInsLineUsed,	&InsData[CurPos + 0x14], 0x02);
	memcpy(&IHd->wInsIDCount,	&InsData[CurPos + 0x16], 0x02);
	IHd->bMasterVol =			 InsData[CurPos + 0x18];
	IHd->bMasterPan =			 InsData[CurPos + 0x19];
	IHd->bUnknown1A =			 InsData[CurPos + 0x1A];
	IHd->bUnknown1B =			 InsData[CurPos + 0x1B];
	memcpy(&IHd->dwPadding,		&InsData[CurPos + 0x1C], 0x04);
	CurPos += 0x20;
	
	for (CurIns = 0x00; CurIns < INS_MAX_INSTRUMENTS; CurIns ++)
	{
		//ReDim RetIns.Data(CurIns).DataBlk(0x00 To INS_DATA_BLK_LINES - 1)
		
		IDsc = &RetIns->Desc[CurIns];
		IDsc->bDataCount =			 InsData[CurPos + 0x00];
		IDsc->bInsVolume =			 InsData[CurPos + 0x01];
		IDsc->bInsVolume = MidiVolConv(IDsc->bInsVolume);
		memcpy(&IDsc->wUnknown02,	&InsData[CurPos + 0x02], 0x02);
		IDsc->bInsPanorama =		 InsData[CurPos + 0x04];
		IDsc->bUnknown05 =			 InsData[CurPos + 0x05];
		IDsc->bUnknown06 =			 InsData[CurPos + 0x06];
		IDsc->bUnknown07 =			 InsData[CurPos + 0x07];
		memcpy(&IDsc->dwPadding08,	&InsData[CurPos + 0x08], 0x04);
		memcpy(&IDsc->dwPadding0C,	&InsData[CurPos + 0x0C], 0x04);
		CurPos += 0x10;
	}
	
	for (CurIns = 0x00; CurIns < IHd->wInsCount; CurIns ++)
	{
		for (CurLin = 0x00; CurLin < INS_DATA_BLK_LINES; CurLin ++)
		{
			memcpy(&TempSht, &InsData[CurPos + 0x016], 0x02);
			if (TempSht)
			{
				InsNo = InsData[CurPos + 0x014];
				IDBlk = &RetIns->Data[InsNo].DataBlk[CurLin];
				
				IDBlk->bUnknown00 =			 InsData[CurPos + 0x00];
				IDBlk->bUnknown01 =			 InsData[CurPos + 0x01];
				IDBlk->bToneVol =			 InsData[CurPos + 0x02];
				IDBlk->bToneVol = MidiVolConv(IDBlk->bToneVol);
				IDBlk->bTonePan =			 InsData[CurPos + 0x03];
				IDBlk->bUnknown04 =			 InsData[CurPos + 0x04];
				IDBlk->bUnknown05 =			 InsData[CurPos + 0x05];
				IDBlk->bNoteLow =			 InsData[CurPos + 0x06];
				IDBlk->bNoteHigh =			 InsData[CurPos + 0x07];
				if (IDBlk->bNoteLow > IDBlk->bNoteHigh)
					IDBlk->bNoteLow = 0x00;
				memcpy(&IDBlk->dwUnknown08,	&InsData[CurPos + 0x08], 0x04);
				IDBlk->bPbDepthMSB =		 InsData[CurPos + 0x0C];
				IDBlk->bPbDepthLSB =		 InsData[CurPos + 0x0D];
				memcpy(&IDBlk->wUnknown0E,	&InsData[CurPos + 0x0E], 0x02);
				memcpy(&IDBlk->wUnknown10,	&InsData[CurPos + 0x10], 0x02);
				memcpy(&IDBlk->wUnknown12,	&InsData[CurPos + 0x12], 0x02);
				memcpy(&IDBlk->wInsNumber,	&InsData[CurPos + 0x14], 0x02);
				memcpy(&IDBlk->wInsID,		&InsData[CurPos + 0x16], 0x02);
				memcpy(&IDBlk->wUnknown18,	&InsData[CurPos + 0x18], 0x02);
				memcpy(&IDBlk->wUnknown1A,	&InsData[CurPos + 0x1A], 0x02);
				memcpy(&IDBlk->wUnknown1C,	&InsData[CurPos + 0x1C], 0x02);
				memcpy(&IDBlk->wUnknown1E,	&InsData[CurPos + 0x1E], 0x02);
			}
			CurPos += 0x20;
		}
	}
	
	return;
}

void WriteBE32(UINT8* Data, UINT32 Value)
{
	Data[0x00] = (Value & 0xFF000000) >> 24;
	Data[0x01] = (Value & 0x00FF0000) >> 16;
	Data[0x02] = (Value & 0x0000FF00) >>  8;
	Data[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}

void WriteBE16(UINT8* Data, UINT16 Value)
{
	Data[0x00] = (Value & 0xFF00) >> 8;
	Data[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}

UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

void WriteLE32(UINT8* Data, UINT32 Value)
{
	Data[0x00] = (Value & 0x000000FF) >>  0;
	Data[0x01] = (Value & 0x0000FF00) >>  8;
	Data[0x02] = (Value & 0x00FF0000) >> 16;
	Data[0x03] = (Value & 0xFF000000) >> 24;
	
	return;
}

// now replaced by WriteBExx and ReadBExx
/*void ReverseBytes(UINT32 ByteCount, void* DstData, void* SrcData)
{
	UINT32 CurPos;
	UINT8* SrcPos;
	UINT8* DstPos;
	
	SrcPos = (UINT8*)SrcData;
	DstPos = (UINT8*)DstData + ByteCount;
	for (CurPos = 0x00; CurPos < ByteCount; CurPos ++)
	{
		DstPos --;
		*DstPos = *SrcPos;
		SrcPos ++;
	}
	
	return;
}*/

static bool IsNoteOn(UINT8 Event, UINT8 Volume)
{
	return (Event & 0x10) && Volume;
}

static UINT8 MidiVolConv(UINT8 Volume)
{
	double DBVol;
	double MidVol;
	
	if (! FixVolume || ! Volume)
		return Volume;
	
	DBVol = log(Volume / (double)0x7F) / log(2.0) * 6.0;
	MidVol = pow(10.0, DBVol / 40.0);
	
	return (UINT8)(MidVol * (double)0x7F + 0.5);
}


#define TEST_TEMPO	120
void MakeInstrumentTestMIDI(const INSTRUMENT_DATA* InsData, const char* OutFileName)
{
	UINT32 MidLen;
	UINT8* MidFile;
	UINT32 CurPos;
	UINT32 TempLng;
	UINT8 CurIns;
	UINT8 CurLin;
	const INS_DATA_BLOCK* IDBlk;
	UINT8 TempNote;
	FILE* hFile;
	
	// INS_MAX_INSTRUMENTS * INS_DATA_BLK_LINES * 0x07 = 0x3800
	// INS_MAX_INSTRUMENTS * 0x03 = 0x0180
	//	-> 0x3980
	MidLen = 0x4000;
	MidFile = (UINT8*)malloc(MidLen);
	
	WriteLE32(	&MidFile[0x00],	FCC_SEQp);		// SEQp
	WriteBE16(	&MidFile[0x04],	0x0000);		// Format
	WriteBE16(	&MidFile[0x06],	0x0001);		// Tracks
	WriteBE16(	&MidFile[0x08],	0x0004);		// Tick/Quarter
	TempLng = 60000000 / TEST_TEMPO;			// Tempo
	MidFile[0x0A] = (TempLng & 0x00FF0000) >> 16;
	MidFile[0x0B] = (TempLng & 0x0000FF00) >>  8;
	MidFile[0x0C] = (TempLng & 0x000000FF) >>  0;
	// write 4/4
	MidFile[0x0D] = 0x04;			// Beats
	MidFile[0x0E] = 0x02;			// 2^x Beats per Measure
	CurPos = 0x0F;
	
	for (CurIns = 0x00; CurIns < InsData->Head.wInsCount; CurIns ++)
	{
		if (! InsData->Desc[CurIns].bDataCount)
			continue;
		
		MidFile[CurPos + 0x00] = 0x00;
		MidFile[CurPos + 0x01] = 0xC0;
		MidFile[CurPos + 0x02] = CurIns;
		CurPos += 0x03;
		
		IDBlk = InsData->Data[CurIns].DataBlk;
		for (CurLin = 0x00; CurLin < InsData->Desc[CurIns].bDataCount; CurLin ++, IDBlk ++)
		{
			if (! IDBlk->wInsID)
				continue;
			
			TempNote = (IDBlk->bNoteLow + IDBlk->bNoteHigh) / 2;
			MidFile[CurPos + 0x00] = 0x10;
			MidFile[CurPos + 0x01] = 0x90;
			MidFile[CurPos + 0x02] = TempNote;
			MidFile[CurPos + 0x03] = 0x7F;
			CurPos += 0x04;
			
			MidFile[CurPos + 0x00] = 0x10;
			MidFile[CurPos + 0x01] = TempNote;
			MidFile[CurPos + 0x02] = 0x00;
			CurPos += 0x03;
		}
	}
	
	MidFile[CurPos + 0x00] = 0x00;
	MidFile[CurPos + 0x01] = 0xFF;
	MidFile[CurPos + 0x02] = 0x2F;
	MidFile[CurPos + 0x03] = 0x00;
	CurPos += 0x04;
	
	MidLen = CurPos;
	
	hFile = fopen(OutFileName, "wb");
	if (hFile == NULL)
		return;
	
	fwrite(MidFile, 0x01, MidLen, hFile);
	
	fclose(hFile);
	free(MidFile);
	
	return;
}
