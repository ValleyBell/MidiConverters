// Tales of Phantasia SPC -> Midi Converter
// ----------------------------------------
// Written by Valley Bell, 2012, 2014, 2016

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <stdtype.h>
#include <stdbool.h>

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


#define INS_MAX_INSTRUMENTS	0x100
#define INS_DATA_BLK_LINES	1
#define INS_LINE_CNT		(INS_MAX_INSTRUMENTS * INS_DATA_BLK_LINES)

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
	UINT8 DefaultChn;
} CHN_DATA_SEQ;

typedef struct channel_data_midi
{
	UINT8 Vol;
	UINT8 Pan;
	UINT8 Expr;
	//UINT16 Pitch;
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


typedef struct _track_info
{
	UINT8 Mode;
	UINT16 StartPos;
	UINT16 LoopPos;
	UINT16 LoopTimes;
	UINT32 TickCnt;
	UINT32 LoopTick;
} TRK_INFO;


void ReadConvData(const char* FileName);
INS_SETUP* GetInsSetupData(UINT8 Ins, UINT8 Note);
void WriteRPN(UINT8* MidFile, UINT32* MidPos, UINT32* Delay, UINT8 Chn,
			  UINT8 RpnMSB, UINT8 RpnLSB, UINT8 RpnData, CHN_DATA_MID* MidChnData);
UINT8 ToP2Mid(void);
static void PreparseSeq(UINT8 TrkCnt, TRK_INFO* TrkInfo, UINT16 BasePtr);
static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf);
static void CheckRunningNotes(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt);
static void WriteMidiDelay(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 EvtCode);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);
static UINT16 ReadLE16(const UINT8* Data);
static double Lin2DB(UINT8 LinVol);
static UINT8 DB2Mid(double DB);


typedef struct running_note
{
	UINT8 MidChn;
	UINT8 Note;	// orignal Note value - used to extend notes
	UINT8 MidiNote;
	UINT16 RemLen;
} RUN_NOTE;


UINT32 SpcLen;
UINT8* SpcData;
UINT32 MidLen;
UINT8* MidData;
UINT8 RunNoteCnt;
RUN_NOTE RunNotes[0x100];
INS_SETUP InsSetup[INS_LINE_CNT];
bool FixInsSet;
bool FixVolume;
bool WriteDbgCtrls;
bool LoopExt;
UINT8 DefLoopCur;
UINT8 DrvVer;

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	char TempArr[0x08];
	int RetVal;
	
	printf("ToP SPC -> Midi Converter\n-------------------------\n");
	if (argc < 4)
	{
		printf("Usage: top2mid.exe Options Song.spc Song.mid [InsMap.ini]\n");
		printf("Options: (options can be combined)\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    i   fix Instruments (needs InsMap.ini)\n");
		printf("    v   fix Volume (convert linear SNES to logarithmic MIDI)\n");
		printf("    d   write Debug Controllers (for unknown events)\n");
		printf("    x   no loop extention\n");
		printf("Supported games: Tales Of Phantasia SFC and Star Ocean.\n");
		return 0;
	}
	
	FixInsSet = false;
	FixVolume = false;
	WriteDbgCtrls = false;
	LoopExt = true;
	DefLoopCur = 2;
	
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			FixInsSet = false;
			FixVolume = false;
			break;
		case 'I':
			FixInsSet = true;
			break;
		case 'V':
			FixVolume = true;
			break;
		case 'D':
			WriteDbgCtrls = true;
			break;
		case 'X':
			LoopExt = false;
			break;
		}
		StrPtr ++;
	}
	
	if (FixInsSet)
	{
		if (argc <= 4)
		{
			printf("Insufficient arguments!\n");
			return 9;
		}
		
		ReadConvData(argv[4]);
	}
	
	hFile = fopen(argv[2], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fread(TempArr, 0x01, 0x08, hFile);
	if (strncmp(TempArr, "SNES-SPC", 0x08))
	{
		fclose(hFile);
		printf("Not an SPC file!\n");
		return 2;
	}
	
	fseek(hFile, 0x100, SEEK_SET);	// jump to SPC RAM dump
	
	SpcLen = 0x10000;	// 64 KB
	SpcData = (UINT8*)malloc(SpcLen);
	fread(SpcData, 0x01, SpcLen, hFile);
	
	fclose(hFile);
	
	RetVal = ToP2Mid();
	if (RetVal)
		return 3;
	
	hFile = fopen(argv[3], "wb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fwrite(MidData, 0x01, MidLen, hFile);
	
	fclose(hFile);
	printf("Done.\n");
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

void ReadConvData(const char* FileName)
{
	FILE* hFile;
	char TempStr[0x100];
	char* TempPnt;
	char* EndPnt;
	UINT16 InsNum;
	UINT8 InsLine;
	INS_SETUP* TempIns;
	
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
		//InsLine &= 0x0F;
		
		//TempIns = &InsSetup[(InsNum << 4) | InsLine];
		TempIns = &InsSetup[InsNum];
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

INS_SETUP* GetInsSetupData(UINT8 Ins, UINT8 Note)
{
	INS_SETUP* TempIS;
	
	TempIS = &InsSetup[Ins];
	if (TempIS->MidiIns == 0xFF)
	{
		printf("Warning: Unmapped instrument %02X!\n", Ins);
		TempIS->MidiIns = Ins & 0x7F;
	}
	return TempIS;
}

void WriteRPN(UINT8* MidFile, UINT32* MidPos, UINT32* Delay, UINT8 Chn,
			  UINT8 RpnMSB, UINT8 RpnLSB, UINT8 RpnData, CHN_DATA_MID* MidChnData)
{
	UINT8 RPNCtrl;
	
	if (RpnMSB & 0x80)
		RPNCtrl = 0x62;	// NRPN
	else
		RPNCtrl = 0x64;	// RPN
	
	// sadly, this doesn't work, because I process every track separately
//	if (MidChnData->RPN_MSB != RpnMSB || MidChnData->RPN_LSB != RpnLSB)
	{
		WriteEvent(MidFile, MidPos, Delay, 0xB0 | Chn, RPNCtrl | 0x01, RpnMSB & 0x7F);
		WriteEvent(MidFile, MidPos, Delay, 0xB0 | Chn, RPNCtrl | 0x00, RpnLSB & 0x7F);
		MidChnData->RPN_MSB = RpnMSB;
		MidChnData->RPN_LSB = RpnLSB;
	}
	
	if (! (RpnData & 0x80))
		WriteEvent(MidFile, MidPos, Delay, 0xB0 | Chn, 0x06, RpnData);
	
	return;
}


#define TRK_COUNT	0x0F
UINT8 ToP2Mid(void)
{
	static const UINT8 DRIVER_SIG[0x14] =
	{	0x20, 0xE8, 0x00, 0xC4, 0xF4, 0xC4, 0xF5, 0xC4,
		0xF6, 0xC4, 0xF7, 0xC4, 0x83, 0x8F, 0x30, 0xF1,
		0xCD, 0xFF, 0xBD, 0x3F};
	static const UINT8 DRIVER_SIG_OLD[0x14] =
	{	0x20, 0x8F, 0x20, 0xF4, 0xE8, 0x00, 0xC4, 0xF5,
		0xC4, 0xF6, 0xC4, 0xF7, 0xC4, 0xE7, 0x8F, 0xA0, 
		0xE6, 0x8F, 0xF0, 0xF1};
	static const UINT8 OLDDRV_CMDLUT[0x20] =
	{	0x90, 0x97, 0x99, 0xB2, 0x9B, 0x9C, 0x9D, 0x9E,
		0xE8, 0xE9, 0xEA, 0xEB, 0x96, 0xED, 0xEE, 0xEF,
		0xF0, 0xF1, 0xF2, 0xF3, 0xA2, 0xF5, 0xF6, 0xF7,
		0xF0, 0xF0, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};
	UINT16 BasePtr;
	TRK_INFO TrkInfo[TRK_COUNT];
	TRK_INFO* TempTInf;
	UINT8 CurTrk;
	UINT16 SegBase;
	UINT16 SegIdx;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	UINT8 CmdBase;
	
	UINT8 LoopIdx;
	UINT16 LoopCur[0x10];	// current loop counter (16-bit due to loop extention)
	UINT16 LoopPos[0x10];	// loop offset
	UINT16 LoopSeg[0x10];	// loop segment
	CHN_DATA_SEQ ChnSeq;
	CHN_DATA_MID ChnMid;
	CHN_DATA_DRM ChnDrm[0x80];
	INS_SETUP* TempIS;
	CHN_DATA_DRM* TempChD;
	UINT8 TempArr[0x04];
	UINT32 TempLng;
	INT16 TempSSht;
	UINT8 TempByt;
	UINT8 CurNote;
	UINT8 LastChn;
	UINT32 CurDly;
	UINT8 NoteVol;
	UINT8 ChnVol;
	//INT8 NoteMove;
	//UINT8 DrmNote;
	//UINT8 PBRange;
	UINT8 CurDrmPtch;
	
	UINT8 MsgMask;
	UINT8 InitTempo;
	
	DrvVer = 0;
	if (! memcmp(&SpcData[0x0840], DRIVER_SIG, 0x14))
		DrvVer = 2;
	else if (! memcmp(&SpcData[0x0700], DRIVER_SIG_OLD, 0x14))
		DrvVer = 1;
	if (! DrvVer)
	{
		printf("This SPC uses a wrong sound driver!\n");
		return 0xFF;
	}
	if (DrvVer == 2)
	{
		BasePtr = ReadLE16(&SpcData[0x0854]);
		CmdBase = 0x90;
	}
	else //if (DrvVer == 1)
	{
		BasePtr = ReadLE16(&SpcData[0x0844]);
		CmdBase = 0xE0;
	}
	
	MidLen = 0x40000;	// 256 KB should be enough (so-37 has 66.4 KB)
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBE32(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBE16(&MidData[DstPos + 0x00], 0x0001);			// Format 1
	WriteBE16(&MidData[DstPos + 0x02], 1 + TRK_COUNT);	// Tracks: 1 (master) + 15
	WriteBE16(&MidData[DstPos + 0x04], 0x0018);			// Ticks per Quarter: 48
	DstPos += 0x06;
	
	InPos = BasePtr + 0x0020;
	
	WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
	DstPos += 0x08;
	TrkBase = DstPos;
	CurDly = 0x00;
	
	InPos += 0x02;	// skip file size
	
	InitTempo = SpcData[InPos];
	TempLng = 60000000 / (InitTempo * 2);	// base guessed
	WriteBE32(TempArr, TempLng);
	WriteMetaEvent_Data(MidData, &DstPos, &CurDly, 0x51, 0x03, &TempArr[0x01]);
	InPos ++;
	
	if (FixInsSet)
		WriteEvent(MidData, &DstPos, &CurDly, 0xC9, 0x00, 0x00);	// make sure that the drum settings are reset
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	
	for (CurTrk = 0x00; CurTrk < TRK_COUNT; CurTrk ++, InPos += 0x03)
	{
		TempTInf = &TrkInfo[CurTrk];
		TempTInf->Mode = SpcData[InPos + 0x00];
		TempTInf->StartPos = ReadLE16(&SpcData[InPos + 0x01]);
		TempTInf->LoopPos = 0x0000;
		TempTInf->LoopTimes = DefLoopCur;
		TempTInf->TickCnt = 0;
		TempTInf->LoopTick = 0;
	}
	PreparseSeq(TRK_COUNT, TrkInfo, BasePtr);
	if (LoopExt)
		GuessLoopTimes(TRK_COUNT, TrkInfo);
	
	for (CurTrk = 0x00; CurTrk < TRK_COUNT; CurTrk ++)
	{
		TempTInf = &TrkInfo[CurTrk];
		SegBase = BasePtr + TempTInf->StartPos;
		
		WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0x00;
		
		if (DrvVer == 2)
			TrkEnd = ! (TempTInf->Mode >> 7);
		else
			TrkEnd = ! (TempTInf->Mode >> 0);
		LoopIdx = 0x00;
		MidChn = CurTrk + (CurTrk + 6) / 15;
		LastChn = MidChn;
		NoteVol = 0x7F;
		ChnVol = 0x64;
		//DrmNote = 0x00;
		//NoteMove = 0x00;
		RunNoteCnt = 0x00;
		MsgMask = 0x00;
		SegIdx = 0x00;
		InPos = 0x0000;
		//PBRange = 0x00;
		
		ChnSeq.Ins = 0xFF;
		ChnSeq.Move = 0;
		ChnSeq.DrmNote = 0xFF;
		ChnSeq.DefaultChn = MidChn;
		CurDrmPtch = 0xFF;
		//ChnMid.Pitch = 0x4000;
		ChnMid.PBRange = 0x00;
		ChnMid.RPN_MSB = 0x7F;	// RPN Null
		ChnMid.RPN_LSB = 0x7F;
		for (TempByt = 0x00; TempByt < 0x80; TempByt ++)
		{
			TempChD = &ChnDrm[TempByt];
			TempChD->Pitch = 0x40;
			TempChD->Volume = 0x7F;
			TempChD->Pan = 0x40;
		}
		
		while(! TrkEnd)
		{
			if (InPos == 0x0000)
			{
				//if (SegIdx == 0xFFFF)
				//	break;
				InPos = ReadLE16(&SpcData[SegBase + SegIdx * 0x02]);
				if (InPos == 0xFFFF)
					break;
				InPos += BasePtr;
				SegIdx ++;
			}
			
			CurCmd = SpcData[InPos];
			if (CurCmd < CmdBase)
			{
				if (ChnSeq.DrmNote == 0xFF)
				{
					// normal Melody instrument note
					CurNote = CurCmd + ChnSeq.Move;
					TempSSht = (INT16)CurCmd + ChnSeq.Move;
					if (TempSSht < 0x00)
						CurNote = 0x00;
					else if (TempSSht > 0x7F)
						CurNote = 0x7F;
					else
						CurNote = (UINT8)TempSSht;
				}
				else
				{
					TempIS = GetInsSetupData(ChnSeq.Ins, CurCmd);
					if (TempIS->DrumBase != 0xFF)
					{
						// single Drum instrument with varying pitch
						// -> convert to NRPN: Drum Pitch + Drum Note
						TempChD = &ChnDrm[ChnSeq.DrmNote];
						// The "default" pitch is 0x40. 00..3F is lower, 41..7F is higher.
						TempSSht = (INT16)CurCmd - TempIS->DrumBase + 0x40;
						if (TempSSht < 0x00)
							TempByt = 0x00;
						else if (TempSSht > 0x7F)
							TempByt = 0x7F;
						else
							TempByt = (UINT8)TempSSht;
						
						if (TempChD->Pitch != TempByt)
						{
							// NRPN: Drum Note Pitch
							WriteRPN(MidData, &DstPos, &CurDly, MidChn, 0x98, ChnSeq.DrmNote, TempByt, &ChnMid);
							TempChD->Pitch = TempByt;
						}
						
						//CurNote = ChnSeq.DrmNote;
						CurNote = TempIS->NoteMove;
					}
					else
					{
						// instrument with multiple drums
						// This would need a lookup table.
						CurNote = TempIS->NoteMove;
					}
					CurNote |= 0x80;
				}
				
				if (DrvVer == 2)
				{
					if (FixVolume)
						NoteVol = DB2Mid(Lin2DB(SpcData[InPos + 0x03]));
					else
						NoteVol = SpcData[InPos + 0x03] >> 1;
				}
				else
				{
					if (FixVolume)
						NoteVol = DB2Mid(Lin2DB(SpcData[InPos + 0x03] << 1));
					else
						NoteVol = SpcData[InPos + 0x03];
				}
				
				WriteEvent(MidData, &DstPos, &CurDly, 0x00, 0x00, 0x00);
				
				for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
				{
					if (RunNotes[TempByt].Note == CurCmd)
					{
						RunNotes[TempByt].RemLen = (UINT16)CurDly + SpcData[InPos + 0x02];
						break;
					}
				}
				if (TempByt >= RunNoteCnt)
				{
					WriteEvent(MidData, &DstPos, &CurDly,
								0x90 | MidChn, CurNote & 0x7F, NoteVol);
					if (RunNoteCnt < 0x80)
					{
						RunNotes[RunNoteCnt].MidChn = MidChn;
						RunNotes[RunNoteCnt].Note = CurCmd;
						RunNotes[RunNoteCnt].MidiNote = CurNote;
						RunNotes[RunNoteCnt].RemLen = SpcData[InPos + 0x02];
						RunNoteCnt ++;
					}
				}
				
				CurDly += SpcData[InPos + 0x01];
				InPos += 0x04;
			}
			else
			{
				if (DrvVer == 1)
					CurCmd = OLDDRV_CMDLUT[CurCmd & 0x1F];
				switch(CurCmd)
				{
				case 0x90:	// Delay
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0x92:	// Loop Start
					if (InPos == TempTInf->LoopPos)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, 0x00);
					else if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x70, LoopIdx);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, 0x00);
					}
					InPos += 0x01;
					
					LoopSeg[LoopIdx] = SegIdx;
					LoopPos[LoopIdx] = InPos;
					LoopCur[LoopIdx] = 0x00;
					LoopIdx ++;
					break;
				case 0x93:	// Loop End
					TempByt = SpcData[InPos + 0x01];	// loop count
					InPos += 0x02;
					
					if (! LoopIdx)
					{
						// missing Loop Start (see ToP 219)
						printf("Trk %u: Warning: Loop End without Loop Start at 0x%04X\n", CurTrk, InPos - 0x02);
						//LoopSeg[LoopIdx] = 0x01;	// start from beginning of the 2nd segment
						//LoopPos[LoopIdx] = 0x0000;
						//LoopCur[LoopIdx] = 0x00;
						//LoopIdx ++;
						if (! TempByt)
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | MidChn, 0x6F, 0x01);
						else if (WriteDbgCtrls)
						{
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | MidChn, 0x70, LoopIdx);
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | MidChn, 0x26, 0x7F);
						}
						break;
					}
					LoopIdx --;
					LoopCur[LoopIdx] ++;
					if (! TempByt && LoopCur[LoopIdx] <= 0x7F)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, (UINT8)LoopCur[LoopIdx]);
					else if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x70, LoopIdx);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, LoopCur[LoopIdx] & 0x7F);
					}
					/*if (! TempByt && LoopCur[LoopIdx] >= 0x02)
					{
						SegIdx = 0xFFFF;
						break;
					}*/
					if (LoopCur[LoopIdx] < TempByt ||
						(! TempByt && LoopCur[LoopIdx] < TempTInf->LoopTimes))
					{
						SegIdx = LoopSeg[LoopIdx];
						InPos = LoopPos[LoopIdx];
						LoopIdx ++;
					}
					
					break;
				case 0x94:	// Pitch Bend
					TempByt = (DrvVer == 2) ? 12 : 2;
					if (ChnMid.PBRange != TempByt)
					{
						WriteRPN(MidData, &DstPos, &CurDly, MidChn, 0x00, 0x00, TempByt, &ChnMid);
						ChnMid.PBRange = TempByt;
					}
					
					WriteEvent(MidData, &DstPos, &CurDly,
								0xE0 | MidChn, 0x00, SpcData[InPos + 0x02]);
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x95:	// Set Tempo (relative to initial tempo)
					WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x51, 0x00);
					TempByt = SpcData[InPos + 0x02];
					//TempLng = 60000000 / (InitTempo * 2 * TempByt / 0x40);	// guessed
					TempLng = 0x40 * 30000000 / (InitTempo * TempByt);
					WriteBE32(&MidData[DstPos - 0x01], TempLng);
					MidData[DstPos - 0x01] = 0x03;
					DstPos += 0x03;
					
					if (WriteDbgCtrls)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x96:	// Set Instrument
					TempByt = SpcData[InPos + 0x01];
					ChnSeq.Ins = TempByt;
					if (FixInsSet)
					{
						TempIS = GetInsSetupData(ChnSeq.Ins, 0xFF);
						TempByt = TempIS->MidiIns & 0x7F;
						
						// MidiIns 80..FF -> Drum Note
						if ((TempIS->MidiIns & 0x80) && TempIS->DrumBase != 0xFF)
						{
							// Drum Instrument: one drum with varying pitch
							ChnSeq.DrmNote = (UINT8)TempIS->NoteMove;
							
#if 0
							if (CurDrmPtch == 0xFF)
							{
								WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x00, 0x7F);
								WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x20, 0x00);
								WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, 0x00, 0x00);
								
								WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x07, 0x7F);
								CurDrmPtch = 0x00;
							}
#else
							if (MidChn != 0x09)
							{
								MidChn = 0x09;
								// copy Volume/Pan to Drum Channel
								WriteRPN(MidData, &DstPos, &CurDly, 0xB0 | MidChn,
									0x9A, ChnSeq.DrmNote, ChnMid.Vol, &ChnMid);	// Drum Volume
								WriteRPN(MidData, &DstPos, &CurDly, 0xB0 | MidChn,
									0x9C, ChnSeq.DrmNote, ChnMid.Pan, &ChnMid);	// Drum Panorama
							}
#endif
						}
						else
						{
							if ((TempIS->MidiIns & 0x80) && TempIS->DrumBase == 0xFF)
							{
								// generic Drum Channel
								MidChn = 0x09;
								ChnSeq.Move = 0;
								ChnSeq.DrmNote = (UINT8)TempIS->NoteMove;
							}
							else
							{
								// normal Instrument
								if (MidChn == 0x09)
								{
									// copy Volume/Pan back to Normal Channel
									MidChn = ChnSeq.DefaultChn;
									WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x07, ChnMid.Vol);	// Volume
									WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, ChnMid.Pan);	// Panorama
								}
								MidChn = ChnSeq.DefaultChn;
								ChnSeq.Move = TempIS->NoteMove;
								ChnSeq.DrmNote = 0xFF;
							}
						}
					}
					
					if (ChnSeq.DrmNote == 0xFF)
						WriteEvent(MidData, &DstPos, &CurDly, 0xC0 | MidChn, TempByt, 0x00);
					InPos += 0x02;
					break;
				case 0x97:	// another Volume setting?
				case 0x98:	// Set Volume
					if (DrvVer == 2)
					{
						if (FixVolume)
							TempByt = DB2Mid(Lin2DB(SpcData[InPos + 0x02]));
						else
							TempByt = SpcData[InPos + 0x02] >> 1;
					}
					else
					{
						if (FixVolume)
							TempByt = DB2Mid(Lin2DB(SpcData[InPos + 0x02] << 1));
						else
							TempByt = SpcData[InPos + 0x02];
					}
					if (CurCmd == 0x97)
					{
						ChnMid.Vol = TempByt;
						if (ChnSeq.DrmNote == 0xFF)
							WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x07, ChnMid.Vol);
						else
							WriteRPN(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x9A, ChnSeq.DrmNote, ChnMid.Vol, &ChnMid);
					}
					else //if (CurCmd == 0x98)
					{
						ChnMid.Expr = TempByt;
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0B, ChnMid.Expr);
					}
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x99:	// Set Pan
					ChnMid.Pan = 0x80 - SpcData[InPos + 0x02];
					if (ChnMid.Pan >= 0x80)
						ChnMid.Pan = 0x7F;
					if (ChnSeq.DrmNote == 0xFF)
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, ChnMid.Pan);
					else
						WriteRPN(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x9C, ChnSeq.DrmNote, ChnMid.Pan, &ChnMid);
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x09:	// Set Pan
					ChnMid.Pan = 0x80 - SpcData[InPos + 0x01];
					if (ChnMid.Pan >= 0x80)
						ChnMid.Pan = 0x7F;
					if (ChnSeq.DrmNote == 0xFF)
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0A, ChnMid.Pan);
					else
						WriteRPN(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x9C, ChnSeq.DrmNote, ChnMid.Pan, &ChnMid);
					InPos += 0x02;
					break;
				case 0x9B:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					}
					InPos += 0x02;
					break;
				case 0x9C:	// Modulation
					// Parameters: Tick Delay, Depth, Time
					//ModDelay = SpcData[InPos + 0x01]
					//ModVal = SpcData[InPos + 0x02] * 4
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x03]);
					}
					InPos += 0x04;
					break;
				case 0xA2:	// Detune
					if (WriteDbgCtrls)
					{
						/*WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);*/
					}
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x65, 0x00);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x64, 0x01);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x06, SpcData[InPos + 0x01]);
					WriteEvent(MidData, &DstPos, &CurDly,
								0xB0 | MidChn, 0x26, 0x11);
					InPos += 0x02;
					break;
				case 0xA3:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					}
					InPos += 0x02;
					break;
				case 0xAA:	// set Reverb/Chorus
					//WriteEvent(MidData, &DstPos, &CurDly,
					//			0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					if (0 || WriteDbgCtrls)
					{
						// These are global settings, so I write them to all channels.
						for (TempByt = 0x00; TempByt < 0x10; TempByt ++)
						{
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | TempByt, 0x5B, SpcData[InPos + 0x01]);
							WriteEvent(MidData, &DstPos, &CurDly,
										0xB0 | TempByt, 0x5D, SpcData[InPos + 0x02]);
						}
					}
					InPos += 0x03;
					break;
				case 0xAD:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					}
					InPos += 0x02;
					break;
				case 0xAE:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					}
					InPos += 0x02;
					break;
				case 0xAF:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x02]);
					}
					InPos += 0x03;
					break;
				case 0xB2:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x26, SpcData[InPos + 0x01]);
					}
					InPos += 0x02;
					break;
				case 0xC8:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					}
					InPos += 0x01;
					break;
				case 0xF0:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					}
					InPos += 0x01;
					break;
				case 0xFD:	// Segment Return
					//InPos += 0x01;
					InPos = 0x0000;
					break;
				case 0xFE:
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6D, CurCmd & 0x7F);
					}
					InPos += 0x01;
					break;
				case 0xFF:
					TrkEnd = true;
					InPos += 0x01;
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", SpcData[InPos + 0x00], CurTrk, InPos);
					if (WriteDbgCtrls)
					{
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6E, CurCmd & 0x7F);
					}
					InPos += 0x01;
					TrkEnd = true;
					break;
				}
			}
		}
		for (TempByt = 0x00; TempByt < RunNoteCnt; TempByt ++)
		{
			if (RunNotes[TempByt].RemLen > CurDly)
				CurDly = RunNotes[TempByt].RemLen;
		}
		WriteEvent(MidData, &DstPos, &CurDly, 0x7F, 0x00, 0x00);	// flush all notes
		
		WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
		
		WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
}

static void PreparseSeq(UINT8 TrkCnt, TRK_INFO* TrkInfo, UINT16 BasePtr)
{
	UINT8 CurTrk;
	UINT16 SegBase;
	UINT16 SegIdx;
	UINT16 InPos;
	UINT32 CurTick;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopIdx;
	UINT8 LoopCur[0x10];
	UINT16 LoopPos[0x10];
	UINT16 LoopSeg[0x10];
	UINT8 LoopCount;
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		SegBase = BasePtr + TrkInfo[CurTrk].StartPos;
		
		TrkEnd = ! (TrkInfo[CurTrk].Mode >> 7);
		LoopIdx = 0x00;
		SegIdx = 0x00;
		InPos = 0x0000;
		CurTick = 0;
		
		while(! TrkEnd)
		{
			if (InPos == 0x0000)
			{
				InPos = ReadLE16(&SpcData[SegBase + SegIdx * 0x02]);
				if (InPos == 0xFFFF)
					break;
				InPos += BasePtr;
				SegIdx ++;
			}
			
			CurCmd = SpcData[InPos];
			if (CurCmd < 0x90)
			{
				CurTick += SpcData[InPos + 0x01];
				InPos += 0x04;
			}
			else
			{
				switch(CurCmd)
				{
				case 0x90:	// Delay
					CurTick += SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0x92:	// Loop Start
					if (! LoopIdx)
					{
						TrkInfo[CurTrk].LoopPos = InPos;
						TrkInfo[CurTrk].LoopTick = CurTick;
					}
					InPos += 0x01;
					
					LoopSeg[LoopIdx] = SegIdx;
					LoopPos[LoopIdx] = InPos;
					LoopCur[LoopIdx] = 0x00;
					LoopIdx ++;
					break;
				case 0x93:	// Loop End
					LoopCount = SpcData[InPos + 0x01];
					InPos += 0x02;
					
					if (! LoopIdx)
						break;	// missing Loop Start
					LoopIdx --;
					LoopCur[LoopIdx] ++;
					
					if (LoopCount)
						TrkInfo[CurTrk].LoopPos = 0x0000;	// not an infinite loop - reset
					// Note: Infinite loops (LoopCount == 0) exit immediately
					if (LoopCur[LoopIdx] < LoopCount)
					{
						SegIdx = LoopSeg[LoopIdx];
						InPos = LoopPos[LoopIdx];
						LoopIdx ++;
					}
					break;
				case 0x94:	// Pitch Bend
					CurTick += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x95:	// Set Tempo (relative to initial tempo)
					CurTick += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x96:	// Set Instrument
					InPos += 0x02;
					break;
				case 0x97:	// another Volume setting?
					CurTick += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x98:	// Set Volume
					CurTick += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x99:	// Set Pan
					CurTick += SpcData[InPos + 0x01];
					InPos += 0x03;
					break;
				case 0x9B:
					InPos += 0x02;
					break;
				case 0x9C:
					InPos += 0x04;
					break;
				case 0xA2:	// Detune
					InPos += 0x02;
					break;
				case 0xA3:
					InPos += 0x02;
					break;
				case 0xAA:	// set Reverb/Chorus
					InPos += 0x03;
					break;
				case 0xAD:
					InPos += 0x02;
					break;
				case 0xAE:
					InPos += 0x02;
					break;
				case 0xAF:
					InPos += 0x03;
					break;
				case 0xB2:
					InPos += 0x02;
					break;
				case 0xC8:
					InPos += 0x01;
					break;
				case 0xF0:
					InPos += 0x01;
					break;
				case 0xFD:	// Segment Return
					//InPos += 0x01;
					InPos = 0x0000;
					break;
				case 0xFE:
					InPos += 0x01;
					break;
				case 0xFF:
					TrkEnd = true;
					InPos += 0x01;
					break;
				default:
					InPos += 0x01;
					TrkEnd = true;
					break;
				}
			}
		}
		TrkInfo[CurTrk].TickCnt = CurTick;
	}
	
	return;
}

static void GuessLoopTimes(UINT8 TrkCnt, TRK_INFO* TrkInf)
{
	UINT8 CurTrk;
	TRK_INFO* TempTInf;
	UINT32 TrkLen;
	UINT32 TrkLoopLen;
	UINT32 MaxTrkLen;
	
	MaxTrkLen = 0x00;
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		if (TempTInf->LoopPos)
			TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		else
			TrkLoopLen = 0x00;
		
		TrkLen = TempTInf->TickCnt + TrkLoopLen * (TempTInf->LoopTimes - 1);
		if (MaxTrkLen < TrkLen)
			MaxTrkLen = TrkLen;
	}
	
	for (CurTrk = 0x00; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		if (TempTInf->LoopPos)
			TrkLoopLen = TempTInf->TickCnt - TempTInf->LoopTick;
		else
			TrkLoopLen = 0;
		if (TrkLoopLen < 6)	// ignore loops < 1/16
		{
			if (TrkLoopLen > 0)
				printf("Trk %u: ignoring micro-loop (%u ticks)\n", 1 + CurTrk, TrkLoopLen);
			continue;
		}
		
		TrkLen = TempTInf->TickCnt + TrkLoopLen * (TempTInf->LoopTimes - 1);
		if (TrkLen * 5 / 4 < MaxTrkLen)
		{
			// TrkLen = desired length of the loop
			TrkLen = MaxTrkLen - TempTInf->LoopTick;
			
			TempTInf->LoopTimes = (TrkLen + TrkLoopLen / 3) / TrkLoopLen;
			printf("Trk %u: Extended loop to %u times\n", 1 + CurTrk, TempTInf->LoopTimes);
		}
	}
	
	return;
}

static void CheckRunningNotes(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt)
{
	UINT8 CurNote;
	UINT32 TempDly;
	RUN_NOTE* TempNote;
	
	while(RunNoteCnt)
	{
		// 1. Check if we're going beyond a note's timeout.
		TempDly = *Delay + 1;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
		{
			TempNote = &RunNotes[CurNote];
			if (TempNote->RemLen < TempDly)
				TempDly = TempNote->RemLen;
		}
		if (Evt != 0x7F)
		{
			if (TempDly >= *Delay)
				break;	// not beyond the timeout - do the event
		}
		else
		{
			// 7F is the 'flush all' command
			if (TempDly > *Delay)
				break;
		}
		
		// 2. advance all notes by X ticks
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
			RunNotes[CurNote].RemLen -= (UINT16)TempDly;
		(*Delay) -= TempDly;
		
		// 3. send NoteOff for expired notes
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
		{
			TempNote = &RunNotes[CurNote];
			if (! TempNote->RemLen)	// turn note off, it going beyond the Timeout
			{
				WriteMidiValue(Buffer, Pos, TempDly);
				TempDly = 0;
				
				Buffer[*Pos + 0x00] = 0x90 | TempNote->MidChn;
				Buffer[*Pos + 0x01] = TempNote->MidiNote & 0x7F;
				Buffer[*Pos + 0x02] = 0x00;
				*Pos += 0x03;
				
				if (TempNote->MidiNote == (0x80 | 0x2E))
				{
					WriteMidiValue(Buffer, Pos, 0);
					Buffer[*Pos + 0x00] = 0x2C;
					Buffer[*Pos + 0x01] = 0x01;
					*Pos += 0x02;
					WriteMidiValue(Buffer, Pos, 0);
					Buffer[*Pos + 0x00] = 0x2C;
					Buffer[*Pos + 0x01] = 0x00;
					*Pos += 0x02;
				}
				
				RunNoteCnt --;
				if (RunNoteCnt)
					*TempNote = RunNotes[RunNoteCnt];
				CurNote --;
			}
		}
	}
	
	return;
}

static void WriteMidiDelay(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 EvtCode)
{
	UINT8 CurNote;
	
	CheckRunningNotes(Buffer, Pos, Delay, EvtCode);
	if (! (EvtCode & 0x80))
		return;
	
	WriteMidiValue(Buffer, Pos, *Delay);
	if (*Delay)
	{
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++)
			RunNotes[CurNote].RemLen -= (UINT16)*Delay;
		*Delay = 0x00;
	}
	
	return;
}

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	WriteMidiDelay(Buffer, Pos, Delay, Evt);
	
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		Buffer[*Pos + 0x00] = Evt;
		Buffer[*Pos + 0x01] = Val1;
		Buffer[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		Buffer[*Pos + 0x00] = Evt;
		Buffer[*Pos + 0x01] = Val1;
		*Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		Buffer[*Pos + 0x00] = Evt;
		Buffer[*Pos + 0x01] = Val1;
		Buffer[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	default:
		break;
	}
	
	return;
}

static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiDelay(Buffer, Pos, Delay, 0xFF);
	
	Buffer[*Pos + 0x00] = 0xFF;
	Buffer[*Pos + 0x01] = MetaType;
	*Pos += 0x02;
	WriteMidiValue(Buffer, Pos, DataLen);
	memcpy(Buffer + *Pos, Data, DataLen);
	*Pos += DataLen;
	
	return;
}

static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value)
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
	
	ValData = &Buffer[*Pos];
	CurPos = ValSize;
	TempLng = Value;
	do
	{
		CurPos --;
		ValData[CurPos] = 0x80 | (TempLng & 0x7F);
		TempLng >>= 7;
	} while(TempLng);
	ValData[ValSize - 1] &= 0x7F;
	
	*Pos += ValSize;
	
	return;
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

static UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

static double Lin2DB(UINT8 LinVol)
{
	//return log(LinVol / 255.0) / log(2.0) * 6.0;
	return log(LinVol / 255.0) * 8.65617024533378;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}
