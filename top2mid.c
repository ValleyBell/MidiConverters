// Tales of Phantasia SPC -> Midi Converter
// ----------------------------------------
// Written by Valley Bell, 2012

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef unsigned char	bool;
typedef signed char		INT8;
typedef unsigned char	UINT8;
typedef unsigned short	UINT16;
typedef signed short	INT16;
typedef unsigned long	UINT32;

#define false	0x00
#define true	0x01

//#define CONVERT_VOL


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


void ReadConvData(const char* FileName);
INS_SETUP* GetInsSetupData(UINT8 Ins, UINT8 Note);
void WriteRPN(UINT8* MidFile, UINT32* MidPos, UINT32* Delay, UINT8 Chn,
			  UINT8 RpnMSB, UINT8 RpnLSB, UINT8 RpnData, CHN_DATA_MID* MidChnData);
UINT8 ToP2Mid(void);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);
static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
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

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	char TempArr[0x08];
	int RetVal;
	
	/*UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 TempLng;*/
	
	printf("ToP SPC -> Midi Converter\n-------------------------\n");
	if (argc < 4)
	{
		printf("Usage: top2mid.exe Options Song.spc Song.mid [InsMap.ini]\n");
		printf("Options: (options can be combined)\n");
		printf("    r   Raw conversion (other options are ignored)\n");
		printf("    i   fix Instruments (needs InsMap.ini)\n");
		printf("    v   fix Volume (convert linear SNES to logarithmic MIDI)\n");
		printf("    d   write Debug Controllers (for unknown events)\n");
		printf("Supported games: Tales Of Phantasia SFC and Star Ocean.\n");
		return 0;
	}
	
	FixInsSet = false;
	FixVolume = false;
	WriteDbgCtrls = false;
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
	UINT8 RPNMsk;
	UINT8 RPNCtrl;
	
	RPNMsk = RpnMSB & 0x80;
	if (! RPNMsk)
		RPNCtrl = 0x64;	// RPN
	else
		RPNCtrl = 0x62;	// NRPN
	RpnMSB &= 0x7F;
	
	// sadly, this doesn't work, because I process every track separately
//	if (MidChnData->RPN_MSB != (RPNMsk | RpnMSB) ||
//		MidChnData->RPN_LSB != (RPNMsk | RpnLSB))
	{
		WriteEvent(MidFile, MidPos, Delay, 0xB0 | Chn, RPNCtrl | 0x01, RpnMSB);
		WriteEvent(MidFile, MidPos, Delay, 0xB0 | Chn, RPNCtrl | 0x00, RpnLSB);
		MidChnData->RPN_MSB = RPNMsk | RpnMSB;
		MidChnData->RPN_LSB = RPNMsk | RpnLSB;
	}
	
	if (! (RpnData & 0x80))
		WriteEvent(MidFile, MidPos, Delay, 0xB0 | Chn, 0x06, RpnData);
	
	return;
}


UINT8 ToP2Mid(void)
{
	const UINT8 DRIVER_SIG[0x14] =
	{	0x20, 0xE8, 0x00, 0xC4, 0xF4, 0xC4, 0xF5, 0xC4,
		0xF6, 0xC4, 0xF7, 0xC4, 0x83, 0x8F, 0x30, 0xF1,
		0xCD, 0xFF, 0xBD, 0x3F};
	UINT16 BasePtr;
	UINT16 ChnPtrList[0x10];
	UINT8 ChnModeList[0x10];
	UINT8 CurTrk;
	UINT16 SegBase;
	UINT16 SegIdx;
	UINT16 InPos;
	UINT32 DstPos;
	UINT32 TrkBase;
	UINT8 MidChn;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 LoopIdx;
	UINT8 LoopCount[0x10];
	UINT16 LoopPos[0x10];
	UINT16 LoopSeg[0x10];
	CHN_DATA_SEQ ChnSeq;
	CHN_DATA_MID ChnMid;
	CHN_DATA_DRM ChnDrm[0x80];
	INS_SETUP* TempIS;
	CHN_DATA_DRM* TempChD;
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
	
	if (memcmp(&SpcData[0x0840], DRIVER_SIG, 0x14))
	{
		printf("This SPC uses a wrong sound driver!\n");
		return 0xFF;
	}
	memcpy(&BasePtr, &SpcData[0x0854], 0x02);
	
	MidLen = 0x20000;	// 128 KB should be enough (so-37 has 66.4 KB)
	MidData = (UINT8*)malloc(MidLen);
	
	DstPos = 0x00;
	WriteBE32(&MidData[DstPos + 0x00], 0x4D546864);	// write 'MThd'
	WriteBE32(&MidData[DstPos + 0x04], 0x00000006);
	DstPos += 0x08;
	
	WriteBE16(&MidData[DstPos + 0x00], 0x0001);		// Format 1
	WriteBE16(&MidData[DstPos + 0x02], 0x0010);		// Tracks: 16
	WriteBE16(&MidData[DstPos + 0x04], 0x0018);		// Ticks per Quarter: 48
	DstPos += 0x06;
	
	InPos = BasePtr + 0x0020;
	
	WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
	DstPos += 0x08;
	TrkBase = DstPos;
	CurDly = 0x00;
	
	InPos += 0x02;	// skip file size
	
	InitTempo = SpcData[InPos];
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x51, 0x00);
	TempLng = 60000000 / (InitTempo * 2);	// base guessed
	WriteBE32(&MidData[DstPos - 0x01], TempLng);
	MidData[DstPos - 0x01] = 0x03;
	DstPos += 0x03;
	InPos ++;
	
	WriteEvent(MidData, &DstPos, &CurDly, 0xC9, 0x00, 0x00);	// make sure that the drum settings are reset
	WriteEvent(MidData, &DstPos, &CurDly, 0xFF, 0x2F, 0x00);
	WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	
	for (CurTrk = 0x00; CurTrk < 0x0F; CurTrk ++)
	{
		ChnModeList[CurTrk] = SpcData[InPos];
		InPos ++;
		memcpy(&ChnPtrList[CurTrk], &SpcData[InPos], 0x02);
		InPos += 0x02;
	}
	
	for (CurTrk = 0x00; CurTrk < 0x0F; CurTrk ++)
	{
		SegBase = BasePtr + ChnPtrList[CurTrk];
		
		WriteBE32(&MidData[DstPos + 0x00], 0x4D54726B);	// write 'MTrk'
		DstPos += 0x08;
		
		TrkBase = DstPos;
		CurDly = 0x00;
		
		TrkEnd = ! (ChnModeList[CurTrk] >> 7);
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
				memcpy(&InPos, &SpcData[SegBase + SegIdx * 0x02], 0x02);
				if (InPos == 0xFFFF)
					break;
				InPos += BasePtr;
				SegIdx ++;
			}
			
			CurCmd = SpcData[InPos];
			if (CurCmd < 0x90)
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
				
				if (FixVolume)
					NoteVol = DB2Mid(Lin2DB(SpcData[InPos + 0x03]));
				else
					NoteVol = SpcData[InPos + 0x03] >> 1;
				
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
				switch(CurCmd)
				{
				case 0x90:	// Delay
					CurDly += SpcData[InPos + 0x01];
					InPos += 0x02;
					break;
				case 0x92:	// Loop Start
					if (! LoopIdx)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, 0x00);
					InPos += 0x01;
					
					LoopSeg[LoopIdx] = SegIdx;
					LoopPos[LoopIdx] = InPos;
					LoopCount[LoopIdx] = 0x00;
					LoopIdx ++;
					break;
				case 0x93:	// Loop End
					TempByt = SpcData[InPos + 0x01];
					InPos += 0x02;
					
					LoopIdx --;
					LoopCount[LoopIdx] ++;
					if (! TempByt)
						WriteEvent(MidData, &DstPos, &CurDly,
									0xB0 | MidChn, 0x6F, LoopCount[LoopIdx]);
					/*if (! TempByt && LoopCount[LoopIdx] >= 0x02)
					{
						SegIdx = 0xFFFF;
						break;
					}*/
					if (LoopCount[LoopIdx] < TempByt ||
						(! TempByt && LoopCount[LoopIdx] < 0x02))
					{
						SegIdx = LoopSeg[LoopIdx];
						InPos = LoopPos[LoopIdx];
						LoopIdx ++;
					}
					
					break;
				case 0x94:	// Pitch Bend
					TempByt = (CurCmd == 0x94) ? 12 : 2;
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
					if (FixVolume)
						TempByt = DB2Mid(Lin2DB(SpcData[InPos + 0x02]));
					else
						TempByt = SpcData[InPos + 0x02] >> 1;
					ChnMid.Vol = TempByt;
					if (ChnSeq.DrmNote == 0xFF)
						WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x07, ChnMid.Vol);
					else
						WriteRPN(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x9A, ChnSeq.DrmNote, ChnMid.Vol, &ChnMid);
					InPos += 0x03;
					break;
				case 0x98:	// Set Volume
					if (FixVolume)
						TempByt = DB2Mid(Lin2DB(SpcData[InPos + 0x02]));
					else
						TempByt = SpcData[InPos + 0x02] >> 1;
					ChnMid.Expr = TempByt;
					WriteEvent(MidData, &DstPos, &CurDly, 0xB0 | MidChn, 0x0B, ChnMid.Expr);
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
				case 0x9C:
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
		
		WriteEvent(MidData, &DstPos, &CurDly,
					0xFF, 0x2F, 0x00);
		
		WriteBE32(&MidData[TrkBase - 0x04], DstPos - TrkBase);	// write Track Length
	}
	MidLen = DstPos;
	
	return 0x00;
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

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	UINT8 CurNote;
	UINT32 TempDly;
	RUN_NOTE* TempNote;
	bool MoreNotes;
	
	do
	{
		MoreNotes = false;
		TempNote = RunNotes;
		if (! Evt)
			TempDly = *Delay + 1;
		else
			TempDly = *Delay;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++, TempNote ++)
		{
			if (TempNote->RemLen < TempDly)
				TempDly = TempNote->RemLen;
		}
		if (! Evt && TempDly >= *Delay + 1)
			break;
		
		TempNote = RunNotes;
		for (CurNote = 0x00; CurNote < RunNoteCnt; CurNote ++, TempNote ++)
		{
			TempNote->RemLen -= (UINT16)TempDly;
			if (! TempNote->RemLen)
			{
				if (! MoreNotes)
					WriteMidiValue(Buffer, Pos, TempDly);
				else
					WriteMidiValue(Buffer, Pos, 0);
				MidData[*Pos + 0x00] = 0x90 | TempNote->MidChn;
				MidData[*Pos + 0x01] = TempNote->MidiNote & 0x7F;
				MidData[*Pos + 0x02] = 0x00;
				*Pos += 0x03;
				
				if (TempNote->MidiNote == (0x80 | 0x2E))
				{
					WriteMidiValue(Buffer, Pos, 0);
					MidData[*Pos + 0x00] = 0x2C;
					MidData[*Pos + 0x01] = 0x01;
					*Pos += 0x02;
					WriteMidiValue(Buffer, Pos, 0);
					MidData[*Pos + 0x00] = 0x2C;
					MidData[*Pos + 0x01] = 0x00;
					*Pos += 0x02;
				}
				
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
	if (! Evt)
		return;
	
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0x00;
	
	switch(Evt & 0xF0)
	{
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
	case 0xE0:
		MidData[*Pos + 0x00] = Evt;
		MidData[*Pos + 0x01] = Val1;
		MidData[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	case 0xC0:
	case 0xD0:
		MidData[*Pos + 0x00] = Evt;
		MidData[*Pos + 0x01] = Val1;
		*Pos += 0x02;
		break;
	case 0xF0:	// for Meta Event: Track End
		MidData[*Pos + 0x00] = Evt;
		MidData[*Pos + 0x01] = Val1;
		MidData[*Pos + 0x02] = Val2;
		*Pos += 0x03;
		break;
	default:
		break;
	}
	
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

static double Lin2DB(UINT8 LinVol)
{
	//return log(LinVol / 255.0) / log(2.0) * 6.0;
	return log(LinVol / 255.0) * 8.65617024533378;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}
