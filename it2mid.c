// IT -> MIDI Converter
// --------------------
// Valley Bell, 2015-05/2015-06
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "stdtype.h"

#define INLINE	static __inline


#define USE_NOTE_VEL
//#define IGNORE_NOTE_OFF


#define IT_CHANNELS	0x40
typedef struct _it_header
{
	char fileSig[4];
	char songName[26];
	UINT16 pHilight;
	UINT16 ordCount;
	UINT16 insCount;
	UINT16 smpCount;
	UINT16 patCount;
	UINT16 verCreate;
	UINT16 verCompat;
	UINT16 flags;
	UINT16 special;
	UINT8 globalVol;
	UINT8 mixVol;
	UINT8 initSpeed;
	UINT8 initTempo;
	UINT8 panSep;
	UINT8 midiPbDepth;
	UINT16 msgLength;
	UINT32 msgOffset;	// absolute file offset
	UINT32 reserved;
	UINT8 chnPan[IT_CHANNELS];
	UINT8 chnVol[IT_CHANNELS];
	UINT8* orderList;
	UINT32* insOfs;	// absolute file offsets
	UINT32* smpOfs;	// absolute file offsets
	UINT32* patOfs;	// absolute file offsets
} IT_HEAD;
typedef struct _it_edit_history_date
{
	UINT16 fatDate;
	UINT16 fatTime;
	UINT32 dosTimer;
} IT_EHST_DATE;
typedef struct _it_edit_history
{
	UINT16 numEntries;
	IT_EHST_DATE* Entries;
} IT_EDT_HST;
typedef struct _it_channel_names
{
	char fileSig[4];
	UINT16 chnCount;
	char** chnNames;
} IT_CHN_NAMES;

typedef struct _it_ins_keyboard_table
{
	UINT8 note;
	UINT8 smplID;
} IT_INS_KBD_TBL;
typedef struct _it_instrument_old
{
	char fileSig[4];
	char fileName[12];
	UINT8 reserved10;
	UINT8 flags;
	UINT8 volLpStart;
	UINT8 volLpEnd;
	UINT8 susLpStart;
	UINT8 susLpEnd;
	UINT8 reserved16;
	UINT8 reserved17;
	UINT16 fadeOut;
	UINT8 newNoteAct;
	UINT8 dupNoteChk;
	UINT16 trkVer;
	UINT8 numSmpls;
	UINT8 reserved1F;
	char insName[26];
	UINT8 initFltCut;
	UINT8 initFltRes;
	UINT8 midChn;
	UINT8 midPrg;
	UINT16 midBank;
	IT_INS_KBD_TBL noteMap[120];
	UINT8 volEnv[200];
	//IT_ENV_NODE_OLD envNodes[25];
} IT_INS_OLD;
typedef struct _it_instrument
{
	char fileSig[4];
	char fileName[12];
	UINT8 reserved10;
	UINT8 newNoteAct;
	UINT8 dupNoteChk;
	UINT8 dupNoteAct;
	UINT16 fadeOut;
	UINT8 ptchPanSep;
	UINT8 ptchPanCntr;
	UINT8 globalVol;
	UINT8 defaultPan;
	UINT8 randVol;
	UINT8 randPan;
	UINT16 trkVer;
	UINT8 numSmpls;
	UINT8 reserved1F;
	char insName[26];
	UINT8 initFltCut;
	UINT8 initFltRes;
	UINT8 midChn;
	UINT8 midPrg;
	UINT16 midBank;
	IT_INS_KBD_TBL noteMap[120];
	//IT_ENVELOPE envelopes[3];	// 0 - volume, 1 - pan, 2 - pitch
} IT_INS;
typedef struct _it_instrument_generic
{
	char fileSig[4];
	char fileName[12];
	UINT8 reserved10;
	UINT8 misc11[11];
	UINT16 trkVer;
	UINT8 numSmpls;
	UINT8 reserved1F;
	char insName[26];
	UINT8 initFltCut;
	UINT8 initFltRes;
	UINT8 midChn;
	UINT8 midPrg;
	UINT16 midBank;
	IT_INS_KBD_TBL noteMap[120];
	UINT8 envSpace[250];
} IT_INS_GENERIC;

typedef struct _it_sample
{
	char fileSig[4];
	char fileName[12];
	UINT8 reserved10;
	UINT8 globalVol;
	UINT8 flags;
	UINT8 defaultVol;
	char smplName[26];
	UINT8 convert;
	UINT8 defaultPan;
	UINT32 smplLength;
	UINT32 loopStart;
	UINT32 loopEnd;
	UINT32 c5Speed;
	UINT32 susLpStart;
	UINT32 susLpEnd;
	UINT32 smplOfs;	// absolute file offset
	UINT8 vibSpeed;
	UINT8 vibDepth;
	UINT8 vibRate;
	UINT8 vibType;
} IT_SMPL;

typedef struct _it_row
{
	UINT8 mask;
	UINT8 note;
	UINT8 ins;
	UINT8 volcmd;
	UINT8 fxcmd;
	UINT8 fxval;
} IT_ROW;
typedef struct _it_rows
{
	IT_ROW rowChn[IT_CHANNELS];
} IT_ROWS;
typedef struct _it_pattern
{
	UINT16 length;
	UINT16 rows;
	UINT32 reserved04;
	UINT8* patDataPtr;
	IT_ROWS* patRows;
} IT_PAT;



typedef struct
{
	UINT8 type;
	UINT8 param1;
	UINT8 param2;
} RUNNING_FX;
typedef struct _channel_info
{
	UINT8 ins;
	UINT8 chnvol;
	UINT8 pan;
	UINT8 note;
	UINT8 smpvol;
	
	union
	{
		// Flags:
		//	Bit 0 (01) - Hold Note (for portamento)
		//	Bit 1 (02) - Sample Volume set (if unset, use default sample volume)
		struct
		{
			UINT8 hold : 1;
			UINT8 volset : 1;
		};
		UINT8 all;
	} flags;
	
	UINT8 fx_act[26];	// effect active flags (01 - enabled this frame, 02 - last frame)
	UINT8 fx_mem[26];	// effect parameter memory
	//RUNNING_FX fx_run[2];
	
	UINT8 midiIns;
	UINT8 midiNote;
	UINT8 pbRange;
	UINT8 midModFX;
	UINT8 midModFXLast;
	UINT8 midModParam;
} IT_CHN_INFO;
typedef struct _playback_info
{
	UINT32 tick;
	UINT16 curOrd;
	UINT16 curRow;
	UINT16 startRow;
	UINT8 curSpd;
	IT_CHN_INFO chns[IT_CHANNELS];
} IT_PB_INFO;

typedef struct midi_track_event
{
	UINT32 tick;
	UINT8 type;
	UINT8 param1;	// note height/controller type
	UINT8 param2;	// note velocity/controller data
	UINT16 dataLen;
	UINT8* data;
} MIDTRK_EVENT;
typedef struct midi_track_events
{
	UINT32 EvtAlloc;
	UINT32 EvtCount;
	MIDTRK_EVENT* Evts;
} MIDTRK_EVTS;

void ReadITFile(void);
static UINT8 ReadITHeader(UINT32 baseOfs, IT_HEAD* itHead, UINT32* bytesRead);
static UINT8 ReadITEditHistory(UINT32 baseOfs, IT_EDT_HST* itEdtHist, UINT32* bytesRead);
static UINT8 ReadITChannelNames(UINT32 baseOfs, IT_CHN_NAMES* itChnNames, UINT32* bytesRead);
static UINT8 ReadITInstrument_Gen(UINT32 baseOfs, IT_INS_GENERIC* itIns);
static UINT8 ReadITInstrument(UINT32 baseOfs, IT_INS* itIns);
static UINT8 ReadITSample(UINT32 baseOfs, IT_SMPL* itSmpl);
static UINT8 ReadITPattern(UINT32 baseOfs, IT_PAT* itPat);
static UINT32 ReadITPatternData(UINT32 baseOfs, IT_ROWS* patRow, IT_ROW* chnRowStates);

static MIDTRK_EVENT* GetNewMidiTrkEvt(UINT16 trkID);
static void AddMidiTrkEvt(UINT16 chnID, UINT32 time, UINT8 evtType, UINT8 evtVal1, UINT8 evtVal2);
static void AddMidiTrkEvt_Data(UINT16 chnID, UINT32 time, UINT8 evtType, UINT8 evtSubType, UINT16 dataLen, void* data);
static void ConvertItRowFX(IT_PB_INFO* itPbInf, UINT8 chn, UINT8 col, char fxcmd, UINT8 fxval);
static void ConvertItRow(IT_PB_INFO* itPbInf, UINT8 chn, IT_ROW* rowState);
static void ConvertFX2Mid(IT_PB_INFO* itPbInf, UINT8 chn);
static void GenerateMidiTracks(void);
void ConvertIT2Mid(void);

INLINE double Lin2DB(UINT8 LinVol);
INLINE UINT8 DB2Mid(double DB);
INLINE UINT32 Tempo2Mid(UINT16 BPM, UINT16 TicksPerBeat);

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2);
static void WriteMidiValue(UINT8* Buffer, UINT32* Pos, UINT32 Value);
static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT32 ReadLE32(const UINT8* Data);
INLINE void WriteBE16(UINT8* Buffer, UINT16 Value);
INLINE void WriteBE32(UINT8* Buffer, UINT32 Value);



UINT32 ItLen;
UINT8* ItData;

UINT32 MidLen;
UINT8* MidData;
UINT16 MIDI_RES;
UINT16 MidiTickMult;	// MidiTick = ItTick * MidiTickMult
UINT16 ItTickspQrt;

UINT16 itUsedChns;
IT_HEAD ItHead;
IT_EDT_HST ItEdtHist;
IT_CHN_NAMES ItChnNames;
IT_SMPL* ItSmpls;
IT_INS* ItIns;
IT_PAT* ItPats;

UINT16 MidiTrkCount;
MIDTRK_EVTS* MidiTrks;

int main(int argc, char* argv[])
{
	FILE* hFile;
	
	if (argc <= 2)
	{
		printf("Usage: ...\n");
		return 0;
	}
	
	MidiTickMult = 0x14;
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
	{
		printf("Error reading file!\n");
		return 1;
	}
	fseek(hFile, 0, SEEK_END);
	ItLen = ftell(hFile);
	
	ItData = (UINT8*)malloc(ItLen);
	fseek(hFile, 0, SEEK_SET);
	fread(ItData, 0x01, ItLen, hFile);
	fclose(hFile);
	
	ReadITFile();
	
	ConvertIT2Mid();
	
	hFile = fopen(argv[2], "wb");
	if (hFile == NULL)
	{
		printf("Error reading file!\n");
		return 1;
	}
	fwrite(MidData, 0x01, MidLen, hFile);
	fclose(hFile);
	
	return 0;
}

INLINE UINT16 ReadLE16(const UINT8* Data)
{
#if 1
	return *(UINT16*)Data;
#else
	return (Data[0x00] << 0) | (Data[0x01] << 8);
#endif
}

INLINE UINT32 ReadLE32(const UINT8* Data)
{
#if 1
	return *(UINT32*)Data;
#else
	return	(Data[0x00] <<  0) | (Data[0x01] <<  8) |
			(Data[0x02] << 16) | (Data[0x03] << 24);
#endif
}

void ReadITFile(void)
{
	UINT8 retVal;
	UINT16 curIdx;
	UINT32 procBytes;
	UINT32 filePos;
	
	filePos = 0x00;
	retVal = ReadITHeader(filePos, &ItHead, &procBytes);
	if (retVal)
		return;
	filePos += procBytes;
	if (ItHead.special & 0x02)
	{
		retVal = ReadITEditHistory(filePos, &ItEdtHist, &procBytes);
		if (! retVal)
			filePos += procBytes;
	}
	memset(&ItChnNames, 0x00, sizeof(IT_CHN_NAMES));
	retVal = ReadITChannelNames(filePos, &ItChnNames, &procBytes);
	if (! retVal)
		filePos += procBytes;
	
	ItIns = (IT_INS*)malloc(ItHead.insCount * sizeof(IT_INS));
	ItSmpls = (IT_SMPL*)malloc(ItHead.smpCount * sizeof(IT_SMPL));
	ItPats = (IT_PAT*)malloc(ItHead.patCount * sizeof(IT_PAT));
	
	for (curIdx = 0; curIdx < ItHead.insCount; curIdx ++)
		ReadITInstrument(ItHead.insOfs[curIdx], &ItIns[curIdx]);
	for (curIdx = 0; curIdx < ItHead.smpCount; curIdx ++)
		ReadITSample(ItHead.smpOfs[curIdx], &ItSmpls[curIdx]);
	itUsedChns = 0x00;
	for (curIdx = 0; curIdx < ItHead.patCount; curIdx ++)
		ReadITPattern(ItHead.patOfs[curIdx], &ItPats[curIdx]);
	
	return;
}

static UINT8 ReadITHeader(UINT32 baseOfs, IT_HEAD* itHead, UINT32* bytesRead)
{
	UINT32 curPos;
	UINT16 curIdx;
	
	curPos = baseOfs;
	memcpy(&itHead->fileSig, &ItData[curPos + 0x00], 4);
	if (strncmp(itHead->fileSig, "IMPM", 4))
		return 0xFF;
	memcpy(&itHead->songName, &ItData[curPos + 0x04], 26);
	
	itHead->pHilight = ReadLE16(&ItData[curPos + 0x1E]);
	itHead->ordCount = ReadLE16(&ItData[curPos + 0x20]);
	itHead->insCount = ReadLE16(&ItData[curPos + 0x22]);
	itHead->smpCount = ReadLE16(&ItData[curPos + 0x24]);
	itHead->patCount = ReadLE16(&ItData[curPos + 0x26]);
	itHead->verCreate = ReadLE16(&ItData[curPos + 0x28]);
	itHead->verCompat = ReadLE16(&ItData[curPos + 0x2A]);
	itHead->flags = ReadLE16(&ItData[curPos + 0x2C]);
	itHead->special = ReadLE16(&ItData[curPos + 0x2E]);
	itHead->globalVol = ItData[curPos + 0x30];
	itHead->mixVol = ItData[curPos + 0x31];
	itHead->initSpeed = ItData[curPos + 0x32];
	itHead->initTempo = ItData[curPos + 0x33];
	itHead->panSep = ItData[curPos + 0x34];
	itHead->midiPbDepth = ItData[curPos + 0x35];
	itHead->msgLength = ReadLE16(&ItData[curPos + 0x36]);
	itHead->msgOffset = ReadLE32(&ItData[curPos + 0x38]);
	itHead->reserved = ReadLE32(&ItData[curPos + 0x3C]);
	curPos += 0x40;
	memcpy(itHead->chnPan, &ItData[curPos], IT_CHANNELS);
	curPos += IT_CHANNELS;
	memcpy(itHead->chnVol, &ItData[curPos], IT_CHANNELS);
	curPos += IT_CHANNELS;
	
	itHead->orderList = (UINT8*)malloc(itHead->ordCount * sizeof(UINT8));
	itHead->insOfs = (UINT32*)malloc(itHead->insCount * sizeof(UINT32));
	itHead->smpOfs = (UINT32*)malloc(itHead->smpCount * sizeof(UINT32));
	itHead->patOfs = (UINT32*)malloc(itHead->patCount * sizeof(UINT32));
	
	memcpy(itHead->orderList, &ItData[curPos], itHead->ordCount);
	curPos += itHead->ordCount;
	for (curIdx = 0; curIdx < itHead->insCount; curIdx ++, curPos += 0x04)
		itHead->insOfs[curIdx] = ReadLE32(&ItData[curPos]);
	for (curIdx = 0; curIdx < itHead->smpCount; curIdx ++, curPos += 0x04)
		itHead->smpOfs[curIdx] = ReadLE32(&ItData[curPos]);
	for (curIdx = 0; curIdx < itHead->patCount; curIdx ++, curPos += 0x04)
		itHead->patOfs[curIdx] = ReadLE32(&ItData[curPos]);
	
	if (bytesRead != NULL)
		*bytesRead = curPos - baseOfs;
	return 0x00;
}

static UINT8 ReadITEditHistory(UINT32 baseOfs, IT_EDT_HST* itEdtHist, UINT32* bytesRead)
{
	UINT32 curPos;
	UINT16 curHst;
	
	curPos = baseOfs;
	itEdtHist->numEntries = ReadLE16(&ItData[curPos]);
	curPos += 0x02;
	if (itEdtHist->numEntries == 0)
	{
		itEdtHist->Entries = NULL;
	}
	else
	{
		itEdtHist->Entries = (IT_EHST_DATE*)malloc(itEdtHist->numEntries * sizeof(IT_EHST_DATE));
		for (curHst = 0; curHst < itEdtHist->numEntries; curHst ++, curPos += 0x08)
		{
			itEdtHist->Entries[curHst].fatDate = ReadLE16(&ItData[curPos + 0x00]);
			itEdtHist->Entries[curHst].fatTime = ReadLE16(&ItData[curPos + 0x02]);
			itEdtHist->Entries[curHst].dosTimer = ReadLE16(&ItData[curPos + 0x04]);
		}
	}
	
	if (bytesRead != NULL)
		*bytesRead = curPos - baseOfs;
	return 0x00;
}

static UINT8 ReadITChannelNames(UINT32 baseOfs, IT_CHN_NAMES* itChnNames, UINT32* bytesRead)
{
	UINT32 curPos;
	UINT32 dataSize;
	UINT16 curChn;
	
	curPos = baseOfs;
	memcpy(itChnNames->fileSig, &ItData[curPos + 0x00], 4);
	if (strncmp(itChnNames->fileSig, "CNAM", 4))
		return 0xFF;
	dataSize = ReadLE32(&ItData[curPos + 0x04]);
	curPos += 0x08;
	
	itChnNames->chnCount = dataSize / 20;
	if (itChnNames->chnCount == 0)
	{
		itChnNames->chnNames = NULL;
	}
	else
	{
		itChnNames->chnNames = (char**)malloc(itChnNames->chnCount * sizeof(char*));
		for (curChn = 0; curChn < itChnNames->chnCount; curChn ++, curPos += 20)
			itChnNames->chnNames[curChn] = (char*)&ItData[curPos];
	}
	
	if (bytesRead != NULL)
		*bytesRead = curPos - baseOfs;
	return 0x00;
}

static UINT8 ReadITInstrument_Gen(UINT32 baseOfs, IT_INS_GENERIC* itIns)
{
	UINT8 curNote;
	UINT32 curPos;
	
	memcpy(itIns->fileSig, &ItData[baseOfs + 0x00], 4);
	if (strncmp(itIns->fileSig, "IMPI", 4))
		return 0xFF;
	memcpy(itIns->fileName, &ItData[baseOfs + 0x04], 12);
	itIns->reserved10 = ItData[baseOfs + 0x10];
	
	itIns->trkVer = ReadLE16(&ItData[baseOfs + 0x1C]);
	itIns->numSmpls = ItData[baseOfs + 0x1E];
	itIns->reserved1F = ItData[baseOfs + 0x1F];
	memcpy(itIns->insName, &ItData[baseOfs + 0x20], 26);
	itIns->initFltCut = ItData[baseOfs + 0x3A];
	itIns->initFltRes = ItData[baseOfs + 0x3B];
	itIns->midChn = ItData[baseOfs + 0x3C];
	itIns->midPrg = ItData[baseOfs + 0x3D];
	itIns->midBank = ReadLE16(&ItData[baseOfs + 0x3E]);
	
	curPos = baseOfs + 0x40;
	for (curNote = 0; curNote < 120; curNote ++, curPos += 0x02)
	{
		itIns->noteMap[curNote].note = ItData[curPos + 0x00];
		itIns->noteMap[curNote].smplID = ItData[curPos + 0x01];
	}
	
	return 0x00;
}

static UINT8 ReadITInstrument(UINT32 baseOfs, IT_INS* itIns)
{
	UINT8 retVal;
	UINT8 curEnv;
	UINT8 curNode;
	UINT32 curPos;
	
	retVal = ReadITInstrument_Gen(baseOfs, (IT_INS_GENERIC*)itIns);
	if (retVal)
		return retVal;
	
	itIns->newNoteAct = ItData[baseOfs + 0x11];
	itIns->dupNoteChk = ItData[baseOfs + 0x12];
	itIns->dupNoteAct = ItData[baseOfs + 0x13];
	itIns->fadeOut = ReadLE16(&ItData[baseOfs + 0x14]);
	itIns->ptchPanSep = ItData[baseOfs + 0x16];
	itIns->ptchPanCntr = ItData[baseOfs + 0x17];
	itIns->globalVol = ItData[baseOfs + 0x18];
	itIns->defaultPan = ItData[baseOfs + 0x19];
	itIns->randVol = ItData[baseOfs + 0x1A];
	itIns->randPan = ItData[baseOfs + 0x1B];
	
	curPos = baseOfs + 0x40 + 120 * 0x02;
	for (curEnv = 0; curEnv < 3; curEnv ++)
	{
		curPos += 0x06;
		for (curNode = 0; curNode < 25; curNode ++, curPos += 0x03)
		{
		}
	}
	return 0x00;
}

static UINT8 ReadITSample(UINT32 baseOfs, IT_SMPL* itSmpl)
{
	memcpy(itSmpl->fileSig, &ItData[baseOfs + 0x00], 4);
	if (strncmp(itSmpl->fileSig, "IMPS", 4))
		return 0xFF;
	memcpy(itSmpl->fileName, &ItData[baseOfs + 0x04], 12);
	itSmpl->reserved10 = ItData[baseOfs + 0x10];
	itSmpl->globalVol = ItData[baseOfs + 0x11];
	itSmpl->flags = ItData[baseOfs + 0x12];
	itSmpl->defaultVol = ItData[baseOfs + 0x13];
	memcpy(itSmpl->smplName, &ItData[baseOfs + 0x14], 26);
	itSmpl->convert = ItData[baseOfs + 0x2E];
	itSmpl->defaultPan = ItData[baseOfs + 0x2F];
	itSmpl->smplLength = ReadLE32(&ItData[baseOfs + 0x30]);
	itSmpl->loopStart = ReadLE32(&ItData[baseOfs + 0x34]);
	itSmpl->loopEnd = ReadLE32(&ItData[baseOfs + 0x38]);
	itSmpl->c5Speed = ReadLE32(&ItData[baseOfs + 0x3C]);
	itSmpl->susLpStart = ReadLE32(&ItData[baseOfs + 0x40]);
	itSmpl->susLpEnd = ReadLE32(&ItData[baseOfs + 0x44]);
	itSmpl->smplOfs = ReadLE32(&ItData[baseOfs + 0x48]);
	itSmpl->vibSpeed = ItData[baseOfs + 0x4C];
	itSmpl->vibDepth = ItData[baseOfs + 0x4D];
	itSmpl->vibRate = ItData[baseOfs + 0x4E];
	itSmpl->vibType = ItData[baseOfs + 0x4F];
	
	return 0x00;
}

static UINT8 ReadITPattern(UINT32 baseOfs, IT_PAT* itPat)
{
	UINT16 curRow;
	UINT32 curPos;
	IT_ROW rowStates[IT_CHANNELS];
	
	itPat->length = ReadLE16(&ItData[baseOfs + 0x00]);
	itPat->rows = ReadLE16(&ItData[baseOfs + 0x02]);
	itPat->reserved04 = ReadLE32(&ItData[baseOfs + 0x04]);
	curPos = baseOfs + 0x08;
	itPat->patDataPtr = &ItData[curPos];
	itPat->patRows = (IT_ROWS*)malloc(itPat->rows * sizeof(IT_ROWS));
	
	memset(rowStates, 0x00, IT_CHANNELS * sizeof(IT_ROW));
	for (curRow = 0; curRow < itPat->rows; curRow ++)
		curPos += ReadITPatternData(curPos, &itPat->patRows[curRow], rowStates);
	
	return 0x00;
}

static UINT32 ReadITPatternData(UINT32 baseOfs, IT_ROWS* patRow, IT_ROW* chnRowStates)
{
	UINT32 curPos;
	IT_ROW* rowState;
	UINT8 chnMask;
	UINT8 chnID;
	IT_ROW* tempChn;
	
	memset(patRow->rowChn, 0x00, IT_CHANNELS * sizeof(IT_ROW));
	curPos = baseOfs;
	chnMask = ItData[curPos];	curPos ++;
	while(chnMask)
	{
		chnID = (chnMask - 1) & 0x3F;
		tempChn = &patRow->rowChn[chnID];
		rowState = &chnRowStates[chnID];
		if (chnMask & 0x80)
		{
			rowState->mask = ItData[curPos];
			curPos ++;
		}
		
		if (rowState->mask & 0x01)
		{
			if (chnID >= itUsedChns)
				itUsedChns = chnID + 1;
			rowState->note = ItData[curPos];
			curPos ++;
		}
		if (rowState->mask & 0x02)
		{
			rowState->ins = ItData[curPos];
			curPos ++;
		}
		if (rowState->mask & 0x04)
		{
			rowState->volcmd = ItData[curPos];
			curPos ++;
		}
		if (rowState->mask & 0x08)
		{
			rowState->fxcmd = ItData[curPos];	curPos ++;
			rowState->fxval = ItData[curPos];	curPos ++;
		}
		
		tempChn->mask = rowState->mask;
		if (rowState->mask & 0x11)
			tempChn->note = rowState->note;
		if (rowState->mask & 0x22)
			tempChn->ins = rowState->ins;
		if (rowState->mask & 0x44)
			tempChn->volcmd = rowState->volcmd;
		if (rowState->mask & 0x88)
		{
			tempChn->fxcmd = rowState->fxcmd;
			tempChn->fxval = rowState->fxval;
		}
		
		chnMask = ItData[curPos];	curPos ++;
	}
	return curPos - baseOfs;
}




static MIDTRK_EVENT* GetNewMidiTrkEvt(UINT16 trkID)
{
	MIDTRK_EVTS* midTrk;
	MIDTRK_EVENT* midEvt;
	
	if (trkID >= MidiTrkCount)
		return NULL;
	
	midTrk = &MidiTrks[trkID];
	if (midTrk->EvtCount >= midTrk->EvtAlloc)
	{
		midTrk->EvtAlloc += 0x10000;	// 64K more elements
		midTrk->Evts = (MIDTRK_EVENT*)realloc(midTrk->Evts, midTrk->EvtAlloc * sizeof(MIDTRK_EVENT));
	}
	midEvt = &midTrk->Evts[midTrk->EvtCount];
	memset(midEvt, 0x00, sizeof(MIDTRK_EVENT));
	midTrk->EvtCount ++;
	
	return midEvt;
}

static void AddMidiTrkEvt(UINT16 chnID, UINT32 time, UINT8 evtType, UINT8 evtVal1, UINT8 evtVal2)
{
	MIDTRK_EVENT* midEvt;
	
	midEvt = GetNewMidiTrkEvt(1 + chnID);
	if (midEvt == NULL)
		return;
	
	midEvt->tick = time;
	if (evtType < 0xF0)
		midEvt->type = (evtType & 0xF0) | (chnID & 0x0F);
	else
		midEvt->type = evtType;
	midEvt->param1 = evtVal1;
	midEvt->param2 = evtVal2;
	
	return;
}

static void AddMidiTrkEvt_Data(UINT16 chnID, UINT32 time, UINT8 evtType, UINT8 evtSubType, UINT16 dataLen, void* data)
{
	MIDTRK_EVENT* midEvt;
	
	midEvt = GetNewMidiTrkEvt(1 + chnID);
	if (midEvt == NULL)
		return;
	
	midEvt->tick = time;
	midEvt->type = evtType;
	midEvt->param1 = evtSubType;
	midEvt->dataLen = dataLen;
	midEvt->data = (UINT8*)malloc(dataLen);
	memcpy(midEvt->data, data, dataLen);
	
	return;
}

static void ConvertItRowFX(IT_PB_INFO* itPbInf, UINT8 chn, UINT8 col, char fxcmd, UINT8 fxval)
{
	IT_CHN_INFO* chnInf;
	UINT8 fxMemID;
	UINT8 fxActID;
	UINT8 tempByt;
	UINT8 evtParam;
	UINT8 fxHigh;
	UINT8 fxLow;
	
	chnInf = &itPbInf->chns[chn];
	fxActID = fxcmd - 'A';
	fxMemID = fxActID;
	fxHigh = (fxval & 0xF0) >> 4;
	fxLow = (fxval & 0x0F) >> 4;
	
	// do effect memory
	if (fxcmd == 'F')
		fxMemID = 'E' - 'A';
	if (fxActID < 26)
	{
		if ((fxcmd >= 'D' && fxcmd <= 'L') ||
			(fxcmd >= 'N' && fxcmd <= 'R') ||
			fxcmd == 'U' || fxcmd == 'W' || fxcmd == 'Y')
		{
			if (! fxval)
				fxval = chnInf->fx_mem[fxMemID];
		}
		chnInf->fx_act[fxActID] |= 0x01;
		if (chnInf->fx_mem[fxMemID] != fxval)
			chnInf->fx_act[fxActID] |= 0x80;
		chnInf->fx_mem[fxMemID] = fxval;
		if (fxMemID != fxActID)
			chnInf->fx_mem[fxActID] = fxval;
	}
	
	switch(fxcmd)
	{
	case 'A':	// Set Speed
		if (fxval)
			itPbInf->curSpd = fxval;
		break;
	case 'B':	// Jump to Order
		// prevent backwards jumps
		if (fxval >= itPbInf->curOrd)
		{
			itPbInf->curOrd = fxval - 1;
			itPbInf->curRow = 0xFFFE;
		}
		break;
	case 'C':	// Break to Row
		itPbInf->startRow = fxval;
		itPbInf->curRow = 0xFFFE;
		break;
	case 'G':	// Portamento
		chnInf->flags.hold = 1;
		break;
	case 'H':	// Vibrato
		break;
	case 'J':	// Arpeggio
		break;
	case 'M':	// Set Channel Volume
		if (fxval != chnInf->chnvol)
		{
			chnInf->chnvol = fxval;
			evtParam = DB2Mid(Lin2DB(chnInf->chnvol));
			AddMidiTrkEvt(chn, 0, 0xB0, 0x07, evtParam);
		}
		break;
	case 'X':	// set Pan
		tempByt = (fxval + 2) / 4;
		if (chnInf->pan != tempByt)
		{
			chnInf->pan = tempByt;
			evtParam = fxval / 2;
			AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x0A, evtParam);
		}
		break;
	}
	
	
	return;
}

static UINT8 GetInsSmplVol(UINT8 insID, UINT8 note)
{
	IT_INS* itIns;
	IT_SMPL* itSmpl;
	UINT8 smplID;
	
	if (note >= 120)
		return 0x40;
	
	if (! ItHead.insCount)
	{
		smplID = insID;
	}
	else
	{
		insID --;
		if (insID >= ItHead.insCount)
			return 0x40;
		itIns = &ItIns[insID];
		smplID = itIns->noteMap[note].smplID;
	}
	
	smplID --;
	if (smplID >= ItHead.smpCount)
		return 0x40;
	itSmpl = &ItSmpls[smplID];
	return itSmpl->defaultVol;
}

static void ConvertItRow(IT_PB_INFO* itPbInf, UINT8 chn, IT_ROW* rowState)
{
	IT_CHN_INFO* chnInf;
	UINT8 tempByt;
	UINT8 evtParam;
	UINT8 newNote;
	
	chnInf = &itPbInf->chns[chn];
	chnInf->flags.all = 0x00;
	for (tempByt = 0; tempByt < 26; tempByt ++)
		chnInf->fx_act[tempByt] = (chnInf->fx_act[tempByt] & 0x01) << 1;
	if (rowState->mask & 0x22)
	{
		if (rowState->ins && chnInf->ins != rowState->ins)
		{
			chnInf->ins = rowState->ins;
			tempByt = chnInf->ins - 1;
			if (chnInf->midiIns != tempByt)
			{
				chnInf->midiIns = tempByt;
				AddMidiTrkEvt(chn, itPbInf->tick, 0xC0, chnInf->midiIns, 0x00);
			}
		}
	}
	if (rowState->mask & 0x44)
	{
		if (rowState->volcmd <= 64)
		{
			tempByt = rowState->volcmd - 0x00;	// Volume
			if (chnInf->smpvol != tempByt)
			{
				chnInf->smpvol = tempByt;
#ifndef USE_NOTE_VEL
				evtParam = DB2Mid(Lin2DB(chnInf->smpvol));
				AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x0B, evtParam);
#endif
			}
			chnInf->flags.volset = 1;	// set "Sample Volume" flag
		}
		else if (rowState->volcmd < 75)
		{
			tempByt = rowState->volcmd - 65;	// Fine volume up
			ConvertItRowFX(itPbInf, chn, 1, 'D', (tempByt << 4) | 0x0F);
		}
		else if (rowState->volcmd < 85)
		{
			tempByt = rowState->volcmd - 75;	// Fine volume down
			ConvertItRowFX(itPbInf, chn, 1, 'D', 0xF0 | (tempByt << 0));
		}
		else if (rowState->volcmd < 95)
		{
			tempByt = rowState->volcmd - 85;	// Volume slide up
			ConvertItRowFX(itPbInf, chn, 1, 'D', (tempByt << 4) | 0x00);
		}
		else if (rowState->volcmd < 105)
		{
			tempByt = rowState->volcmd - 95;	// Volume slide down
			ConvertItRowFX(itPbInf, chn, 1, 'D', 0x00 | (tempByt << 0));
		}
		else if (rowState->volcmd < 115)
		{
			tempByt = rowState->volcmd - 105;	// Pitch Slide down
			ConvertItRowFX(itPbInf, chn, 1, 'E', tempByt * 4);
		}
		else if (rowState->volcmd < 125)
		{
			tempByt = rowState->volcmd - 115;	// Pitch Slide up
			ConvertItRowFX(itPbInf, chn, 1, 'F', tempByt * 4);
		}
		else if (rowState->volcmd < 0x80)
		{
		}
		else if (rowState->volcmd <= 0xC0)
		{
			tempByt = rowState->volcmd - 0x80;	// Panning
			if (chnInf->pan != tempByt)
			{
				chnInf->pan = tempByt;
				evtParam = (tempByt >= 0x40) ? 0x7F : (tempByt * 2);
				AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x0A, evtParam);
			}
		}
		else if (rowState->volcmd < 203)
		{
			static const UINT8 portTable[10] =
			{	0x00, 0x01, 0x04, 0x08, 0x10, 0x20, 0x40, 0x60, 0x80, 0xFF};
			
			tempByt = rowState->volcmd - 193;	// Portamento
			ConvertItRowFX(itPbInf, chn, 1, 'G', portTable[tempByt]);
		}
		else if (rowState->volcmd < 213)
		{
			tempByt = rowState->volcmd - 203;	// Vibrato
			ConvertItRowFX(itPbInf, chn, 1, 'H', 0x00 | (tempByt << 0));
		}
	}
	if (rowState->mask & 0x88)
	{
		ConvertItRowFX(itPbInf, chn, 0, (char)(rowState->fxcmd - 1 + 'A'), rowState->fxval);
	}
	
	if (rowState->mask & 0x11)
		chnInf->note = rowState->note;	// for FX delay detection
	ConvertFX2Mid(itPbInf, chn);
	
	if (rowState->mask & 0x11)
	{
		chnInf->note = rowState->note;
		newNote = rowState->note;
		if (rowState->note < 0xFE)
		{
			if (! chnInf->flags.volset)	// if sample volume was NOT overridden
			{
				tempByt = GetInsSmplVol(chnInf->ins, chnInf->note);
				if (chnInf->smpvol != tempByt)
				{
					chnInf->smpvol = tempByt;
#ifndef USE_NOTE_VEL
					evtParam = DB2Mid(Lin2DB(chnInf->smpvol));
					AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x0B, evtParam);
#endif
				}
			}
			
			newNote += 0;	// apply instrument transpose
		}
		else	// Note Off/Cut
		{
			chnInf->flags.hold = 0;
#ifdef IGNORE_NOTE_OFF
			if (rowState->note == 0xFF)
				return;	// ignore Note Off (for poorly-done envelopes)
#endif
		}
		
		if (chnInf->flags.hold)
		{
			if (chnInf->midiNote != newNote)
				chnInf->flags.hold = 0;
		}
		if (! chnInf->flags.hold)
		{
			if (chnInf->midiNote < 0xFE)
				AddMidiTrkEvt(chn, itPbInf->tick, 0x90, chnInf->midiNote, 0x00);
			
			chnInf->midiNote = newNote;
			if (chnInf->midiNote < 0xFE)
			{
#ifdef USE_NOTE_VEL
				evtParam = DB2Mid(Lin2DB(chnInf->smpvol));
				AddMidiTrkEvt(chn, itPbInf->tick, 0x90, chnInf->midiNote, evtParam);
#else
				AddMidiTrkEvt(chn, itPbInf->tick, 0x90, chnInf->midiNote, 0x7F);
#endif
			}
		}
	}
	
	return;
}

static void ConvertFX2Mid(IT_PB_INFO* itPbInf, UINT8 chn)
{
	static const char FX_LIST[] = {'E', 'F', 'G', 'H', 'J'};
	IT_CHN_INFO* chnInf;
	UINT8 curFX;
	UINT8 fxID;
	UINT8 fxType;
	UINT8 fxParams;
	UINT8 evtParam;
	
	chnInf = &itPbInf->chns[chn];
	
	fxType = 0x00;
	for (curFX = 0; curFX < 5; curFX ++)
	{
		fxID = FX_LIST[curFX] - 'A';
		if (chnInf->fx_act[fxID] & 0x01)
		{
			fxType = FX_LIST[curFX];
			fxParams = chnInf->fx_mem[fxID];
		}
	}
	if (! fxType)
	{
		if (! chnInf->midModFX)
			return;
		if (chnInf->note >= 0xFE)
			return;	// ignore 'no effect' if no note is playing
	}
	
	if (fxType != chnInf->midModFX || fxParams != chnInf->midModParam)
	{
		if (fxType == 'J' || chnInf->midModFX == 'J')
		{
			if (fxType == 'J')
				AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x21, fxParams);
			else
				AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x21, 0x00);
		}
		else if (fxType)
		{
			if (chnInf->midModFXLast != fxType || fxParams != chnInf->midModParam)
			{
				if (fxType == 'E' ||	// Pitch Slide Down
					fxType == 'F')		// Pitch Slide Up
				{
					evtParam = fxParams * 3 / 2;
					if (evtParam > 0x3F)
						evtParam = 0x3F;
					if (fxType == 'E')
						evtParam *= -1;
					evtParam &= 0x7F;
					AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x12, evtParam);	// step size
					AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x13, 0x7F);	// duration
				}
				else if (fxType == 'H')	// Vibrato
				{
					evtParam = (fxParams & 0x0F) >> 0;
					AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x12, evtParam);
					evtParam = (fxParams & 0xF0) >> 4;
					AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x13, evtParam);
				}
			}
			chnInf->midModFXLast = fxType;
			
			if (fxType == 'H')
				evtParam = (fxParams & 0x0F) * 0x08;
			else
				evtParam = 0x7F;
			if (evtParam < 0x08)
				evtParam = 0x08;
			AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x01, evtParam);
		}
		else
		{
			AddMidiTrkEvt(chn, itPbInf->tick, 0xB0, 0x01, 0x00);
		}
	}
	chnInf->midModFX = fxType;
	if (fxType)
		chnInf->midModParam = fxParams;
	
	return;
}

static void GenerateMidiTracks(void)
{
	UINT16 curPat;
	UINT16 curRow;
	UINT8 curChn;
	UINT8 evtParam;
	IT_PB_INFO itPbInf;
	IT_CHN_INFO* chnInf;
	UINT16 tempLen;
	UINT32 midTempoVal;
	UINT8 tempArr[4];
	
	memset(&itPbInf, 0x00, sizeof(IT_PB_INFO));
	
	// write Song Title
	tempLen = (UINT16)strlen(ItHead.songName);
	if (tempLen > 26)
		tempLen = 26;
	if (tempLen)
		AddMidiTrkEvt_Data(-1, 0, 0xFF, 0x03, tempLen, ItHead.songName);
	
	// write default Tempo
	midTempoVal = Tempo2Mid(ItHead.initTempo, ItTickspQrt);
	WriteBE32(tempArr, midTempoVal);
	AddMidiTrkEvt_Data(-1, 0, 0xFF, 0x51, 0x03, &tempArr[1]);
	
	for (curChn = 0; curChn < IT_CHANNELS; curChn ++)
	{
		chnInf = &itPbInf.chns[curChn];
		chnInf->ins = 0x00;
		chnInf->note = 0xFF;
		chnInf->midiIns = 0xFF;
		chnInf->midiNote = 0xFF;
		chnInf->pbRange = 0x00;
		chnInf->midModFX = 0x00;
		chnInf->midModFXLast = 0x00;
		chnInf->midModParam = 0x00;
		chnInf->chnvol = ItHead.chnVol[curChn];
		chnInf->smpvol = 0x40;
		chnInf->pan = ItHead.chnPan[curChn] & 0x7F;
		
		if (curChn < ItChnNames.chnCount)
		{
			tempLen = (UINT16)strlen(ItChnNames.chnNames[curChn]);
			if (tempLen > 20)
				tempLen = 20;
			AddMidiTrkEvt_Data(curChn, 0, 0xFF, 0x03, tempLen, ItChnNames.chnNames[curChn]);
		}
		evtParam = DB2Mid(Lin2DB(chnInf->chnvol));
		AddMidiTrkEvt(curChn, 0, 0xB0, 0x07, evtParam);
		evtParam = (chnInf->pan >= 0x40) ? 0x7F : (chnInf->pan * 2);
		AddMidiTrkEvt(curChn, 0, 0xB0, 0x0A, evtParam);
	}
	itPbInf.curSpd = ItHead.initSpeed;
	itPbInf.startRow = 0;
	itPbInf.tick = 0;
	for (itPbInf.curOrd = 0; itPbInf.curOrd < ItHead.ordCount; itPbInf.curOrd ++)
	{
		curPat = ItHead.orderList[itPbInf.curOrd];
		if (curPat == 0xFE)			// marker (skip)
			continue;
		else if (curPat == 0xFF)	// song end
			break;
		
		itPbInf.curRow = itPbInf.startRow;
		itPbInf.startRow = 0;
		for (; itPbInf.curRow < ItPats[curPat].rows; itPbInf.curRow ++)
		{
			curRow = itPbInf.curRow;	// make a backup, since itPbInf.curRow can be changed
			for (curChn = 0; curChn < IT_CHANNELS; curChn ++)
				ConvertItRow(&itPbInf, curChn, &ItPats[curPat].patRows[curRow].rowChn[curChn]);
			
			itPbInf.tick += itPbInf.curSpd * MidiTickMult;
		}
	}
	
	for (curChn = 0; curChn < IT_CHANNELS; curChn ++)
	{
		chnInf = &itPbInf.chns[curChn];
		if (chnInf->note < 0xFE)
		{
			AddMidiTrkEvt(curChn, itPbInf.tick, 0x90, chnInf->note, 0x00);
			chnInf->note = 0xFF;
		}
	}
	AddMidiTrkEvt(curChn, itPbInf.tick, 0x00, 0x00, 0x00);	// add dummy event to ensure track length
	
	return;
}

void ConvertIT2Mid(void)
{
	UINT32 MidTrkBase;
	UINT32 MidPos;
	UINT16 CurChn;
	UINT32 CurDly;
	UINT32 lastTick;
	UINT32 curEvt;
	MIDTRK_EVENT* midEvt;
	
	/*for (CurChn = 0; CurChn < 64; CurChn ++)
	{
		if (ItHead.chnPan[CurChn] & 0x80)
			break;
	}
	MidiTrkCount = 1 + CurChn;*/
	MidiTrkCount = 1 + itUsedChns;
	MidiTrks = (MIDTRK_EVTS*)malloc(MidiTrkCount * sizeof(MIDTRK_EVTS));
	memset(MidiTrks, 0x00, MidiTrkCount * sizeof(MIDTRK_EVTS));
	
	ItTickspQrt = ItHead.initSpeed * (ItHead.pHilight & 0x00FF);
	if (! ItTickspQrt)
		ItTickspQrt *= ItHead.initSpeed * 4;
	if (ItTickspQrt < 16)
		ItTickspQrt *= 2;
	MIDI_RES = ItTickspQrt * MidiTickMult;
	
	GenerateMidiTracks();
	
	MidLen = 0x20000;	// 128 KB
	MidData = (UINT8*)malloc(MidLen);
	
	MidPos = 0x00;
	WriteBE32(&MidData[MidPos], 0x4D546864);	MidPos += 0x04;	// 'MThd' Signature
	WriteBE32(&MidData[MidPos], 0x00000006);	MidPos += 0x04;	// Header Size
	WriteBE16(&MidData[MidPos], 0x0001);		MidPos += 0x02;	// Format: 1
	WriteBE16(&MidData[MidPos], MidiTrkCount);	MidPos += 0x02;	// Tracks
	WriteBE16(&MidData[MidPos], MIDI_RES);		MidPos += 0x02;	// Ticks per Quarter
	
	for (CurChn = 0; CurChn < MidiTrkCount; CurChn ++)
	{
		WriteBE32(&MidData[MidPos], 0x4D54726B);	// write 'MTrk'
		MidPos += 0x08;
		MidTrkBase = MidPos;
		
		lastTick = 0;
		CurDly = 0;
		for (curEvt = 0; curEvt < MidiTrks[CurChn].EvtCount; curEvt ++)
		{
			midEvt = &MidiTrks[CurChn].Evts[curEvt];
			CurDly += midEvt->tick - lastTick;
			if (midEvt->data == NULL)
				WriteEvent(MidData, &MidPos, &CurDly, midEvt->type, midEvt->param1, midEvt->param2);
			else
				WriteMetaEvent_Data(MidData, &MidPos, &CurDly, midEvt->param1, midEvt->dataLen, midEvt->data);
			lastTick = midEvt->tick;
		}
		
		WriteEvent(MidData, &MidPos, &CurDly, 0xFF, 0x2F, 0x00);
		WriteBE32(&MidData[MidTrkBase - 0x04], MidPos - MidTrkBase);	// write Track Length
	}
	MidLen = MidPos;
	
	return;
}



INLINE double Lin2DB(UINT8 LinVol)
{
	//return log(LinVol / 64.0) / log(2.0) * 6.0;
	return log(LinVol / 64.0) * 8.65617024533378;
}

INLINE UINT8 DB2Mid(double DB)
{
	if (DB > 0.0)
		DB = 0.0;
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

INLINE UINT32 Tempo2Mid(UINT16 BPM, UINT16 TicksPerBeat)
{
	double TicksPerSec;
	
	TicksPerSec = BPM * 24 * MidiTickMult / 120.0;
	return (UINT32)(500000 * MIDI_RES / TicksPerSec + 0.5);
}

static void WriteEvent(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 Evt, UINT8 Val1, UINT8 Val2)
{
	if (! (Evt & 0x80))
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

static void WriteMetaEvent_Data(UINT8* Buffer, UINT32* Pos, UINT32* Delay, UINT8 MetaType, UINT32 DataLen, const UINT8* Data)
{
	WriteMidiValue(Buffer, Pos, *Delay);
	*Delay = 0x00;
	
	MidData[*Pos + 0x00] = 0xFF;
	MidData[*Pos + 0x01] = MetaType;
	*Pos += 0x02;
	WriteMidiValue(Buffer, Pos, DataLen);
	memcpy(MidData + *Pos, Data, DataLen);
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

INLINE void WriteBE16(UINT8* Buffer, UINT16 Value)
{
	Buffer[0x00] = (Value & 0xFF00) >> 8;
	Buffer[0x01] = (Value & 0x00FF) >> 0;
	
	return;
}

INLINE void WriteBE32(UINT8* Buffer, UINT32 Value)
{
	Buffer[0x00] = (Value & 0xFF000000) >> 24;
	Buffer[0x01] = (Value & 0x00FF0000) >> 16;
	Buffer[0x02] = (Value & 0x0000FF00) >>  8;
	Buffer[0x03] = (Value & 0x000000FF) >>  0;
	
	return;
}
