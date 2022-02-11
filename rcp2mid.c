// RCP -> Midi Converter
// ---------------------
// Written by Valley Bell
// based on FMP -> Midi Converter
// Thanks a lot to https://github.com/shingo45endo/rcm2smf/
// for being a great reference for all the more exotic commands.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <stdtype.h>

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE	static __inline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif
#endif	// INLINE

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


#include "midi_funcs.h"


typedef struct _file_data
{
	UINT32 len;
	UINT8* data;
} FILE_DATA;

typedef struct _rcp_string
{
	UINT16 maxSize;	// maximum size
	UINT16 length;	// actual length (after trimming spaces)
	const char* data;
} RCP_STR;

typedef struct _user_sysex_data
{
	RCP_STR name;
	UINT16 dataLen;
	const UINT8* data;
} USER_SYX_DATA;

typedef struct _rcp_info
{
	UINT8 fileVer;
	UINT16 trkCnt;
	UINT16 tickRes;
	UINT16 tempoBPM;
	UINT8 beatNum;
	UINT8 beatDen;
	UINT8 keySig;
	UINT8 playBias;
	RCP_STR songTitle;
	UINT16 cmntLineSize;
	RCP_STR comments;
	RCP_STR cm6File;
	RCP_STR gsdFile1;
	RCP_STR gsdFile2;
	USER_SYX_DATA usrSyx[8];
} RCP_INFO;

typedef struct _track_info
{
	UINT32 startOfs;
	UINT32 trkLen;
	UINT32 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
} TRK_INF;

typedef struct _cm6_info
{
	UINT8 deviceType;	// 0 - MT-32, 3 - CM-64
	RCP_STR comment;
	// MT-32 (LA) data
	const UINT8* laSystem;
	const UINT8* laChnVol;
	const UINT8* laPatchTemp;
	const UINT8* laRhythmTemp;
	const UINT8* laTimbreTemp;
	const UINT8* laPatchMem;
	const UINT8* laTimbreMem;
	// CM-32P (PCM) data
	const UINT8* pcmPatchTemp;
	const UINT8* pcmPatchMem;
	const UINT8* pcmSystem;
	const UINT8* pcmChnVol;
} CM6_INFO;

typedef struct _gsd_info
{
	const UINT8* sysParams;
	const UINT8* reverbParams;
	const UINT8* chorusParams;
	const UINT8* partParams;
	const UINT8* drumSetup;
	const UINT8* masterTune;
} GSD_INFO;


#define RUNNING_NOTES
#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


#define MCMD_INI_EXCLUDE	0x00	// exclude initial command
#define MCMD_INI_INCLUDE	0x01	// include initial command
#define MCMD_RET_CMDCOUNT	0x00	// return number of commands
#define MCMD_RET_DATASIZE	0x02	// return number of data bytes

#define SYXOPT_DELAY	0x01


static UINT8 ReadFileData(FILE_DATA* fData, const char* fileName);
static UINT8 WriteFileData(const FILE_DATA* fData, const char* fileName);
static const char* GetFileTitle(const char* filePath);
static const char* GetFileExt(const char* fileName);

static UINT8 GetFileVer(const FILE_DATA* rcpFile);
UINT8 Rcp2Mid(const FILE_DATA* rcpFile, FILE_DATA* midFile);
static UINT8 RcpTrk2MidTrk(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
							UINT32* rcpInPos, TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS);
static UINT8 PreparseRcpTrack(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
							UINT32 startPos, TRK_INF* trkInf);
static UINT16 GetMultiCmdDataSize(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
									UINT32 startPos, UINT8 flags);
static UINT16 ReadMultiCmdData(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
								UINT32* rcpInPos, UINT32 bufSize, UINT8* buffer, UINT8 flags);
static UINT16 GetTrimmedLength(UINT16 dataLen, const char* data, char trimChar, UINT8 leaveLast);
static UINT32 ReadRcpStr(RCP_STR* strInfo, UINT16 maxlen, const UINT8* data);
static UINT32 ReadRcpStr0(RCP_STR* strInfo, UINT16 maxlen, const UINT8* data);
INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale);
static void RcpTimeSig2Mid(UINT8 buffer[4], UINT8 beatNum, UINT8 beatDen);
static void RcpKeySig2Mid(UINT8 buffer[2], UINT8 rcpKeySig);
static UINT8 val2shift(UINT32 value);
static UINT16 ProcessRcpSysEx(UINT16 syxMaxLen, const UINT8* syxData, UINT8* syxBuffer,
								UINT8 param1, UINT8 param2, UINT8 midChn);
static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay);

static void WriteRolandSyxData(FILE_INF* fInf, MID_TRK_STATE* MTS, const UINT8* syxHdr,
	UINT32 address, UINT32 len, const UINT8* data, UINT8 opts);
static void WriteRolandSyxBulk(FILE_INF* fInf, MID_TRK_STATE* MTS, const UINT8* syxHdr,
	UINT32 address, UINT32 len, const UINT8* data, UINT32 bulkSize, UINT8 opts);
static void WriteMetaEventFromStr(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 metaType, const char* text);
static UINT8 Cm62MidTrk(const CM6_INFO* cm6Inf, FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 mode);
static UINT8 ParseCM6File(const FILE_DATA* cm6File, CM6_INFO* cm6Inf);
static UINT8 ParseGSDFile(const FILE_DATA* gsdFile, GSD_INFO* gsdInf);
static void Bytes2NibblesHL(UINT32 bytes, UINT8* nibData, const UINT8* byteData);
static void GsdPartParam2BulkDump(UINT8* bulkData, const UINT8* partData);
static UINT8 Gsd2MidTrk(const GSD_INFO* gsdInf, FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 mode);
UINT8 Control2Mid(const FILE_DATA* ctrlFile, FILE_DATA* midFile, UINT8 fileType, UINT8 outMode);

INLINE UINT32 MulDivCeil(UINT32 val, UINT32 mul, UINT32 div);
INLINE UINT32 MulDivRound(UINT32 val, UINT32 mul, UINT32 div);
INLINE UINT16 ReadLE16(const UINT8* data);
INLINE UINT32 ReadLE32(const UINT8* data);


static const UINT8 MT32_SYX_HDR[4] = {0x41, 0x10, 0x16, 0x12};
static const UINT8 SC55_SYX_HDR[4] = {0x41, 0x10, 0x42, 0x12};
static const UINT8 MT32_PATCH_CHG[0x07] = {0xFF, 0xFF, 0x18, 0x32, 0x0C, 0x00, 0x01};


#define MAX_RUN_NOTES	0x20	// should be more than enough even for the MIDI sequences
static UINT16 RunNoteCnt;
static RUN_NOTE RunNotes[MAX_RUN_NOTES];
static UINT16 midiTickRes = 0;
static UINT32 midiTickCount = 0;
static UINT32 midiTempoTicks = 500000;
static const char* inputFilePath = NULL;

static UINT16 NUM_LOOPS = 2;
static UINT8 NO_LOOP_EXT = 0;
static UINT8 BAR_MARKERS = 0;
static UINT8 WOLFTEAM_LOOP = 0;
static UINT8 KEEP_DUMMY_CH = 0;
static UINT8 INCLUDE_CTRL_DATA = 1;

int main(int argc, char* argv[])
{
	int argbase;
	int result;
	UINT8 retVal;
	UINT8 fileType;
	FILE_DATA inFile;
	FILE_DATA outFile;
	
	printf("RCP -> Midi Converter\n---------------------\n");
	if (argc < 3)
	{
		printf("Usage: rcp2mid.exe [options] input.bin output.mid\n");
		printf("Input file formats:\n");
		printf("    RCP/R36/G36 Recomposer sequence file\n");
		printf("    CM6         Recomposer MT-32/CM-64 control file\n");
		printf("    GSD         Recomposer SC-55 control file\n");
		printf("Output file formats:\n");
		printf("    Sequence files are converted into MIDIs.\n");
		printf("    Control files can be converted to raw SYX or MIDI.\n");
		printf("        The file extension of the output file specifies the format.");
		printf("\n");
		printf("Options:\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extension\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		printf("    -WtLoop     Wolfteam Loop mode (loop from measure 2 on)\n");
		printf("    -KeepDummyCh convert data with MIDI channel set to -1\n");
		printf("                channel -1 is invalid, some RCPs use it for muting\n");
		return 0;
	}
	
	MidiDelayCallback = MidiDelayHandler;
	
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				NUM_LOOPS = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! NUM_LOOPS)
					NUM_LOOPS = 2;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "NoLpExt"))
			NO_LOOP_EXT = 1;
		else if (! stricmp(argv[argbase] + 1, "WtLoop"))
			WOLFTEAM_LOOP = 1;
		else if (! stricmp(argv[argbase] + 1, "KeepDummyCh"))
			KEEP_DUMMY_CH = 1;
		else
			break;
		argbase ++;
	}
	if (argc < argbase + 2)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	inFile.data = NULL;
	outFile.data = NULL;
	inputFilePath = argv[argbase + 0];
	retVal = ReadFileData(&inFile, inputFilePath);
	if (retVal)
		return 1;
	
	result = 9;
	fileType = GetFileVer(&inFile);
	if (fileType < 0x10)
	{
		retVal = Rcp2Mid(&inFile, &outFile);
		if (! retVal)
		{
			WriteFileData(&outFile, argv[argbase + 1]);
			printf("Done.\n");
			result = 0;
		}
	}
	else if (fileType < 0x20)
	{
		const char* inFileExt;
		UINT8 outMode;
		
		inFileExt = GetFileExt(argv[argbase + 1]);
		if (! stricmp(inFileExt, "mid"))
			outMode = 0x01;	// MIDI
		else if (! stricmp(inFileExt, "syx"))
			outMode = 0x00;	// SYX
		else
			outMode = 0xFF;
		
		if (outMode == 0xFF)
		{
			printf("Unknown output format \"%s\"!\n", inFileExt);
			result = 3;
		}
		else
		{
			retVal = Control2Mid(&inFile, &outFile, fileType, outMode);
			if (! retVal)
			{
				WriteFileData(&outFile, argv[argbase + 1]);
				printf("Done.\n");
				result = 0;
			}
		}
	}
	else
	{
		printf("Unknown file type!\n");
		result = 2;
	}
	
	free(inFile.data);
	free(outFile.data);
	
#ifdef _DEBUG
	//getchar();
#endif
	
	return result;
}

static UINT8 ReadFileData(FILE_DATA* fData, const char* fileName)
{
	FILE* hFile;
	
	hFile = fopen(fileName, "rb");
	if (hFile == NULL)
	{
		printf("Error reading %s!\n", fileName);
		return 0xFF;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	fData->len = ftell(hFile);
	if (fData->len > 0x100000)	// 1 MB
		fData->len = 0x100000;
	
	fseek(hFile, 0x00, SEEK_SET);
	fData->data = (UINT8*)malloc(fData->len);
	fread(fData->data, 0x01, fData->len, hFile);
	
	fclose(hFile);
	
	return 0x00;
}

static UINT8 WriteFileData(const FILE_DATA* fData, const char* fileName)
{
	FILE* hFile;
	
	hFile = fopen(fileName, "wb");
	if (hFile == NULL)
	{
		printf("Error writing %s!\n", fileName);
		return 0xFF;
	}
	
	fwrite(fData->data, 0x01, fData->len, hFile);
	fclose(hFile);
	
	return 0x00;
}

static const char* GetFileTitle(const char* filePath)
{
	const char* dirSep;
	const char* dirSepW;
	
	dirSep = strrchr(filePath, '/');
	dirSepW = strrchr(filePath, '\\');
	if (dirSep == NULL)
		dirSep = dirSepW;
	else if (dirSepW != NULL && dirSepW > dirSep)
		dirSep = dirSepW;
	return (dirSep != NULL) ? (dirSep + 1) : filePath;
}

static const char* GetFileExt(const char* fileName)
{
	const char* ext = strrchr(GetFileTitle(fileName), '.');
	return (ext != NULL) ? (ext + 1) : "";
}


static UINT8 GetFileVer(const FILE_DATA* rcpFile)
{
	const char* rcpHdr = (const char*)rcpFile->data;
	
	if (rcpFile->len < 0x20)
		return 0xFE;	// incomplete header
	
	if (! strcmp(rcpHdr, "RCM-PC98V2.0(C)COME ON MUSIC\r\n"))
		return 2;
	else if (! strcmp(rcpHdr, "COME ON MUSIC RECOMPOSER RCP3.0"))
		return 3;
	
	if (! strcmp(rcpHdr, "COME ON MUSIC"))
	{
		if (! memcmp(&rcpHdr[0x0E], "\0\0R ", 0x04))
			return 0x10;	// CM6 file
		else if (! strcmp(&rcpHdr[0x0E], "GS CONTROL 1.0"))
			return 0x11;	// GSD file
	}
	
	return 0xFF;	// unknown file
}

UINT8 Rcp2Mid(const FILE_DATA* rcpFile, FILE_DATA* midFile)
{
	const UINT8* rcpData = rcpFile->data;
	UINT8 tempArr[0x20];
	RCP_INFO rcpInf;
	TRK_INF* trkInf;
	TRK_INF* tempTInf;
	UINT16 curTrk;
	UINT32 inPos;
	UINT32 tempLng;
	UINT8 retVal;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	UINT8 ctrlTrkCnt;
	UINT32 initDelay;
	
	FILE_DATA cm6FData;
	CM6_INFO cm6Inf;
	FILE_DATA gsd1FData;
	GSD_INFO gsd1Inf;
	FILE_DATA gsd2FData;
	GSD_INFO gsd2Inf;
	
	rcpInf.fileVer = GetFileVer(rcpFile);
	if (rcpInf.fileVer >= 0x10)
		return 0x10;
	printf("RCP file version %u.\n", rcpInf.fileVer);
	
	midFInf.alloc = 0x20000;	// 128 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	inPos = 0x00;
	if (rcpInf.fileVer == 2)
	{
		ReadRcpStr(&rcpInf.songTitle, 0x40, &rcpData[inPos + 0x020]);
		rcpInf.cmntLineSize = 28;
		ReadRcpStr(&rcpInf.comments, 0x150, &rcpData[inPos + 0x060]);
		
		rcpInf.tickRes = rcpData[inPos + 0x1C0];
		rcpInf.tempoBPM = rcpData[inPos + 0x1C1];
		rcpInf.beatNum = rcpData[inPos + 0x1C2];
		rcpInf.beatDen = rcpData[inPos + 0x1C3];
		rcpInf.keySig = rcpData[inPos + 0x1C4];
		rcpInf.playBias = rcpData[inPos + 0x1C5];
		
		// names of additional files
		ReadRcpStr0(&rcpInf.cm6File, 0x10, &rcpData[inPos + 0x1C6]);
		ReadRcpStr0(&rcpInf.gsdFile1, 0x10, &rcpData[inPos + 0x1D6]);
		rcpInf.gsdFile2.maxSize = rcpInf.gsdFile2.length = 0;
		rcpInf.gsdFile2.data = NULL;
		
		rcpInf.trkCnt = rcpData[inPos + 0x1E6];
		rcpInf.tickRes |= (rcpData[inPos + 0x1E7] << 8);
		
		// somewhere here it also stores the TONENAME.TB file path
		inPos += 0x206;
		
		inPos += 0x20 * 0x10;	// skip rhythm definitions
	}
	else //if (rcpInf.fileVer == 3)
	{
		ReadRcpStr(&rcpInf.songTitle, 0x80, &rcpData[inPos + 0x020]);
		rcpInf.cmntLineSize = 30;
		ReadRcpStr(&rcpInf.comments, 0x168, &rcpData[inPos + 0x0A0]);
		
		rcpInf.trkCnt = ReadLE16(&rcpData[inPos + 0x0208]);
		rcpInf.tickRes = ReadLE16(&rcpData[inPos + 0x020A]);
		rcpInf.tempoBPM = ReadLE16(&rcpData[inPos + 0x020C]);
		rcpInf.beatNum = rcpData[inPos + 0x020E];
		rcpInf.beatDen = rcpData[inPos + 0x020F];
		rcpInf.keySig = rcpData[inPos + 0x0210];
		rcpInf.playBias = rcpData[inPos + 0x0211];
		
		// names of additional files
		ReadRcpStr0(&rcpInf.gsdFile1, 0x10, &rcpData[inPos + 0x298]);
		ReadRcpStr0(&rcpInf.gsdFile2, 0x10, &rcpData[inPos + 0x2A8]);
		ReadRcpStr0(&rcpInf.cm6File, 0x10, &rcpData[inPos + 0x2B8]);
		
		inPos += 0x318;
		
		inPos += 0x80 * 0x10;	// skip rhythm definitions
	}
	
	if (rcpInf.trkCnt == 0)
		rcpInf.trkCnt = 18;	// early RCP files have the value set to 0 and assume always 18 tracks
	
	trkInf = (TRK_INF*)calloc(rcpInf.trkCnt, sizeof(TRK_INF));
	{
		UINT32 pos = inPos;
		pos += 0x30 * 8;	// skip User SysEx data
		for (curTrk = 0; curTrk < rcpInf.trkCnt; curTrk ++)
		{
			tempTInf = &trkInf[curTrk];
			retVal = PreparseRcpTrack(rcpFile->len, rcpFile->data, &rcpInf, pos, tempTInf);
			if (retVal)
				break;
			tempTInf->loopTimes = tempTInf->loopOfs ? NUM_LOOPS : 0;
			pos += tempTInf->trkLen;
		}
	}
	
	if (! NO_LOOP_EXT)
		BalanceTrackTimes(rcpInf.trkCnt, trkInf, rcpInf.tickRes / 4, 0xFF);
	
	ctrlTrkCnt = 0;
	initDelay = 0;
	cm6FData.data = NULL;
	if (INCLUDE_CTRL_DATA)
	{
		const char* fileTitle = GetFileTitle(inputFilePath);
		size_t baseLen = fileTitle - inputFilePath;
		size_t maxPathLen = baseLen + 0x20;
		char* ctrlFilePath;
		
		ctrlFilePath = (char*)malloc(maxPathLen);
		strncpy(ctrlFilePath, inputFilePath, baseLen);
		
		if (rcpInf.cm6File.length > 0)
		{
			memcpy(&ctrlFilePath[baseLen], rcpInf.cm6File.data, rcpInf.cm6File.length);
			ctrlFilePath[baseLen + rcpInf.cm6File.length] = '\0';
			
			retVal = ReadFileData(&cm6FData, ctrlFilePath);
			if (! retVal)
			{
				retVal = ParseCM6File(&cm6FData, &cm6Inf);
				if (retVal)
				{
					printf("CM6 Control file: %.*s - Invalid file type\n",
						rcpInf.cm6File.length, rcpInf.cm6File.data);
				}
				else
				{
					printf("CM6 Control File: %.*s, %s mode\n",
						rcpInf.cm6File.length, rcpInf.cm6File.data, cm6Inf.deviceType ? "CM-64" : "MT-32");
					ctrlTrkCnt ++;
				}
			}
		}
		if (rcpInf.gsdFile1.length > 0)
		{
			memcpy(&ctrlFilePath[baseLen], rcpInf.gsdFile1.data, rcpInf.gsdFile1.length);
			ctrlFilePath[baseLen + rcpInf.gsdFile1.length] = '\0';
			
			retVal = ReadFileData(&gsd1FData, ctrlFilePath);
			if (! retVal)
			{
				retVal = ParseGSDFile(&gsd1FData, &gsd1Inf);
				if (retVal)
				{
					printf("GSD Control file: %.*s - Invalid file type\n",
						rcpInf.gsdFile1.length, rcpInf.gsdFile1.data);
				}
				else
				{
					printf("GSD Control file: %.*s\n", rcpInf.gsdFile1.length, rcpInf.gsdFile1.data);
					ctrlTrkCnt ++;
				}
			}
		}
		if (rcpInf.gsdFile2.length > 0)
		{
			memcpy(&ctrlFilePath[baseLen], rcpInf.gsdFile2.data, rcpInf.gsdFile2.length);
			ctrlFilePath[baseLen + rcpInf.gsdFile2.length] = '\0';
			
			retVal = ReadFileData(&gsd2FData, ctrlFilePath);
			if (! retVal)
			{
				retVal = ParseGSDFile(&gsd2FData, &gsd2Inf);
				if (retVal)
				{
					printf("GSD Control file (Port B): %.*s - Invalid file type\n",
						rcpInf.gsdFile1.length, rcpInf.gsdFile1.data);
				}
				else
				{
					printf("GSD Control file (Port B): %.*s\n", rcpInf.gsdFile2.length, rcpInf.gsdFile2.data);
					ctrlTrkCnt ++;
				}
			}
		}
	}
	
	WriteMidiHeader(&midFInf, 0x0001, 1 + ctrlTrkCnt + rcpInf.trkCnt, rcpInf.tickRes);
	midiTickRes = rcpInf.tickRes;
	
	WriteMidiTrackStart(&midFInf, &MTS);
	midiTickCount = 0;
	
	// song title
	if (rcpInf.songTitle.length > 0)
		WriteMetaEvent(&midFInf, &MTS, 0x03, rcpInf.songTitle.length, rcpInf.songTitle.data);
	
	// comments
	if (rcpInf.comments.length > 0)
	{
		// The comments section consists of 12 lines with 28 or 30 characters each.
		// Lines are padded with spaces, so we have to split them manually.
		UINT16 lineStart;
		
		for (lineStart = 0; lineStart < rcpInf.comments.length; lineStart += rcpInf.cmntLineSize)
		{
			const char* lineData = &rcpInf.comments.data[lineStart];
			// Note: Even though comments.length might tell otherwise, I can assume that the buffer
			//       contains cmntLineSize bytes on the last line. (works fine with all RCP files)
			UINT16 lineLen = GetTrimmedLength(rcpInf.cmntLineSize, lineData, ' ', 0);
			if (lineLen == 0)
				lineLen = 1;	// some sequencers remove empty events, so keep at least 1 space
			WriteMetaEvent(&midFInf, &MTS, 0x01, lineLen, lineData);
		}
	}
	
	// tempo
	tempLng = Tempo2Mid(rcpInf.tempoBPM, 0x40);
	midiTempoTicks = tempLng;	// save in global variable for use with CM6/GSD initialization block
	WriteBE32(tempArr, tempLng);
	WriteMetaEvent(&midFInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
	
	if (rcpInf.beatNum > 0)	// time signature being 0/0 happened in AB_AFT32.RCP
	{
		// time signature
		RcpTimeSig2Mid(tempArr, rcpInf.beatNum, rcpInf.beatDen);
		WriteMetaEvent(&midFInf, &MTS, 0x58, 0x04, tempArr);
	}
	
	// key signature
	RcpKeySig2Mid(tempArr, rcpInf.keySig);
	WriteMetaEvent(&midFInf, &MTS, 0x59, 0x02, tempArr);
	
	if (rcpInf.playBias)
		printf("Warning: PlayBIAS == %u!\n", rcpInf.playBias);
	
	WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFInf, &MTS);
	
	if (ctrlTrkCnt > 0)
	{
		if (rcpInf.cm6File.length > 0)
		{
			WriteMidiTrackStart(&midFInf, &MTS);
			midiTickCount = 0;
			WriteMetaEvent(&midFInf, &MTS, 0x03, rcpInf.cm6File.length, rcpInf.cm6File.data);
			
			WriteRolandSyxData(&midFInf, &MTS, MT32_SYX_HDR, 0x7F0000, 0x00, NULL, 0x00);	// MT-32 Reset
			// (N ms / 1000 ms) / (tempoTicks / 1 000 000)
			MTS.curDly += MulDivRound(400, midiTickRes * 1000, midiTempoTicks);	// add delay of ~400 ms
			
			Cm62MidTrk(&cm6Inf, &midFInf, &MTS, 0x11);
			initDelay += midiTickCount;
			
			WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
			WriteMidiTrackEnd(&midFInf, &MTS);
		}
		if (rcpInf.gsdFile1.length > 0)
		{
			WriteMidiTrackStart(&midFInf, &MTS);
			midiTickCount = 0;
			WriteMetaEvent(&midFInf, &MTS, 0x03, rcpInf.gsdFile1.length, rcpInf.gsdFile1.data);
			
			if (rcpInf.gsdFile2.length > 0)
			{
				tempArr[0x00] = 0x00;	// Port A
				WriteMetaEvent(&midFInf, &MTS, 0x21, 0x01, tempArr);
			}
			
			Gsd2MidTrk(&gsd1Inf, &midFInf, &MTS, 0x11);
			initDelay += midiTickCount;
			
			WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
			WriteMidiTrackEnd(&midFInf, &MTS);
		}
		if (rcpInf.gsdFile2.length > 0)
		{
			WriteMidiTrackStart(&midFInf, &MTS);
			midiTickCount = 0;
			WriteMetaEvent(&midFInf, &MTS, 0x03, rcpInf.gsdFile2.length, rcpInf.gsdFile2.data);
			
			tempArr[0x00] = 0x01;	// Port B
			WriteMetaEvent(&midFInf, &MTS, 0x21, 0x01, tempArr);
			
			Gsd2MidTrk(&gsd2Inf, &midFInf, &MTS, 0x11);
			initDelay += midiTickCount;
			
			WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
			WriteMidiTrackEnd(&midFInf, &MTS);
		}
	}
	
	// user SysEx data
	for (curTrk = 0; curTrk < 8; curTrk ++)
	{
		USER_SYX_DATA* usrSyx = &rcpInf.usrSyx[curTrk];
		
		inPos += ReadRcpStr(&usrSyx->name, 0x18, &rcpData[inPos]);
		
		usrSyx->data = &rcpData[inPos];
		usrSyx->dataLen = GetTrimmedLength(0x18, (const char*)usrSyx->data, (char)0xF7, 0x01);
		inPos += 0x18;
	}
	
	if (initDelay > 0)
	{
		UINT32 barTicks;
		
		if (rcpInf.beatNum == 0 || rcpInf.beatDen == 0)
			barTicks = 4 * midiTickRes;	// assume 4/4 time signature
		else
			barTicks = rcpInf.beatNum * 4 * midiTickRes / rcpInf.beatDen;
		// round initDelay up to a full bar
		initDelay = (initDelay + barTicks - 1) / barTicks * barTicks;
	}
	
	retVal = 0x00;
	for (curTrk = 0; curTrk < rcpInf.trkCnt; curTrk ++)
	{
		WriteMidiTrackStart(&midFInf, &MTS);
		midiTickCount = 0;
		
		MTS.curDly = initDelay;
		retVal = RcpTrk2MidTrk(rcpFile->len, rcpFile->data, &rcpInf, &inPos, &trkInf[curTrk], &midFInf, &MTS);
		
		WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFInf, &MTS);
		
		if (retVal)
		{
			if (retVal == 0x01)
			{
				printf("Early EOF when trying to read track %u!\n", 1 + curTrk);
				retVal = 0x00;	// assume that early EOF is not an error (trkCnt may be wrong)
			}
			break;
		}
	}
	
	midFile->data = midFInf.data;
	midFile->len = midFInf.pos;
	
	midFInf.pos = 0x00;
	WriteMidiHeader(&midFInf, 0x0001, 1 + ctrlTrkCnt + curTrk, rcpInf.tickRes);
	
	free(trkInf);
	return retVal;
}

static UINT8 RcpTrk2MidTrk(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
							UINT32* rcpInPos, TRK_INF* trkInf, FILE_INF* fInf, MID_TRK_STATE* MTS)
{
	UINT32 inPos;
	UINT32 trkBasePos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT16 repMeasure;
	UINT8 trkID;
	UINT8 rhythmMode;
	UINT8 midiDev;
	UINT8 midChn;
	UINT8 transp;
	INT32 startTick;
	UINT8 trkMute;
	UINT8 tempArr[0x40];
	RCP_STR trkName;
	UINT16 measPosAlloc;
	UINT16 measPosCount;
	UINT32* measurePos;
	UINT16 curBar;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT8 cmdP1;
	UINT8 cmdP2;
	UINT16 cmdP0Delay;
	UINT16 cmdDurat;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT16 loopCnt[8];
	UINT8 gsParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT8 xgParams[6];	// 0 device ID, 1 model ID, 2 address high, 3 address low
	UINT32 txtBufSize;
	UINT8* txtBuffer;
	
	inPos = *rcpInPos;
	if (inPos >= rcpLen)
		return 0x01;
	
	trkBasePos = inPos;
	if (rcpInf->fileVer == 2)
	{
		trkLen = ReadLE16(&rcpData[inPos]);
		// Bits 0/1 are used as 16/17, allowing for up to 256 KB per track.
		// This is used by some ItoR.x conversions.
		trkLen = (trkLen & ~0x03) | ((trkLen & 0x03) << 16);
		inPos += 0x02;
	}
	else if (rcpInf->fileVer == 3)
	{
		trkLen = ReadLE32(&rcpData[inPos]);
		inPos += 0x04;
	}
	trkEndPos = trkBasePos + trkLen;
	if (trkEndPos > rcpLen)
		trkEndPos = rcpLen;
	if (inPos + 0x2A > rcpLen)
		return 0x01;	// not enough bytes to read the header
	
	trkID = rcpData[inPos + 0x00];		// track ID
	rhythmMode = rcpData[inPos + 0x01];	// rhythm mode
	midChn = rcpData[inPos + 0x02];		// MIDI channel
	if (midChn & 0x80)
	{
		// When the KeepDummyCh option is off, prevent events from being
		// written to the MIDI by setting midiDev to 0xFF.
		midiDev = KEEP_DUMMY_CH ? 0x00 : 0xFF;
		midChn = 0x00;
	}
	else
	{
		midiDev = midChn >> 4;
		midChn &= 0x0F;
	}
	transp = rcpData[inPos + 0x03];				// transposition
	startTick = (INT8)rcpData[inPos + 0x04];	// start tick
	trkMute = rcpData[inPos + 0x05];			// mute
	ReadRcpStr(&trkName, 0x24, &rcpData[inPos + 0x06]);	// track name
	inPos += 0x2A;
	
	parentPos = MTS->curDly;
	MTS->curDly = 0;	// enforce tick 0 for track main events
	if (trkName.length > 0)
		WriteMetaEvent(fInf, MTS, 0x03, trkName.length, trkName.data);
	if (trkMute && ! KEEP_DUMMY_CH)
	{
		// just ignore muted tracks
		*rcpInPos = trkBasePos + trkLen;
		return 0x00;
	}
	if (midiDev != 0xFF)
	{
		WriteMetaEvent(fInf, MTS, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
		WriteMetaEvent(fInf, MTS, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
	}
	if (rhythmMode > 0x80)
		printf("Warning Track %u: Rhythm Mode %u!\n", trkID, rhythmMode);
	if (transp > 0x80)
	{
		// known values are: 0x00..0x3F (+0 .. +63), 0x40..0x7F (-64 .. -1), 0x80 (drums)
		printf("Warning Track %u: Key 0x%02X!\n", trkID, transp);
		transp = 0x00;
	}
	MTS->curDly = parentPos;
	
	measPosAlloc = 0x100;
	measPosCount = 0x00;
	measurePos = (UINT32*)malloc(measPosAlloc * sizeof(UINT32));
	txtBufSize = 0x00;
	txtBuffer = NULL;
	
	memset(gsParams, 0x00, 6);
	memset(xgParams, 0x00, 6);
	trkEnd = 0;
	parentPos = 0x00;
	repMeasure = 0xFFFF;
	RunNoteCnt = 0;
	MTS->midChn = midChn;
	loopIdx = 0x00;
	curBar = 0;
	
	// add "startTick" offset to initial delay
	if (startTick >= 0 || -startTick <= (INT32)MTS->curDly)
	{
		MTS->curDly += startTick;
		startTick = 0;
	}
	else
	{
		startTick += MTS->curDly;
		MTS->curDly = 0;
	}
	
	measurePos[measPosCount] = inPos;
	measPosCount ++;
	while(inPos < trkEndPos && ! trkEnd)
	{
		UINT32 prevPos = inPos;
		
		if (rcpInf->fileVer == 2)
		{
			cmdType = rcpData[inPos + 0x00];
			cmdP0Delay = rcpData[inPos + 0x01];
			cmdP1 = rcpData[inPos + 0x02];
			cmdDurat = cmdP1;
			cmdP2 = rcpData[inPos + 0x03];
			inPos += 0x04;
		}
		else if (rcpInf->fileVer == 3)
		{
			cmdType = rcpData[inPos + 0x00];
			cmdP2 = rcpData[inPos + 0x01];
			cmdP0Delay = ReadLE16(&rcpData[inPos + 0x02]);
			cmdP1 = rcpData[inPos + 0x04];
			cmdDurat = ReadLE16(&rcpData[inPos + 0x04]);
			inPos += 0x06;
		}
		
		if (cmdType < 0x80)
		{
			UINT8 curNote;
			UINT8 curRN;
			
			CheckRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes);
			
			curNote = (cmdType + transp) & 0x7F;
			for (curRN = 0; curRN < RunNoteCnt; curRN ++)
			{
				if (RunNotes[curRN].note == curNote)
				{
					// note already playing - set new length
					RunNotes[curRN].remLen = MTS->curDly + cmdDurat;
					cmdDurat = 0;	// prevent adding note below
					break;
				}
			}
			
			// duration == 0 -> no note
			if (cmdDurat > 0 && midiDev != 0xFF)
			{
				WriteEvent(fInf, MTS, 0x90, curNote, cmdP2);
				AddRunningNote(MAX_RUN_NOTES, &RunNoteCnt, RunNotes, MTS->midChn, curNote, 0x80, cmdDurat);
			}
		}
		else switch(cmdType)
		{
		case 0x90: case 0x91: case 0x92: case 0x93:	// send User SysEx (defined via header)
		case 0x94: case 0x95: case 0x96: case 0x97:
			if (midiDev == 0xFF)
				break;
			{
				const USER_SYX_DATA* usrSyx = &rcpInf->usrSyx[cmdType & 0x07];
				UINT16 syxLen = ProcessRcpSysEx(usrSyx->dataLen, usrSyx->data, tempArr, cmdP1, cmdP2, midChn);
				// append F7 byte (may be missing with UserSysEx of length 0x18)
				if (syxLen > 0 && tempArr[syxLen - 1] != 0xF7)
				{
					tempArr[syxLen] = 0xF7;
					syxLen ++;
				}
				if (usrSyx->name.length > 0 && 0)
					WriteMetaEvent(fInf, MTS, 0x01, usrSyx->name.length, usrSyx->name.data);
				if (syxLen > 1)
					WriteLongEvent(fInf, MTS, 0xF0, syxLen, tempArr);
				else
					printf("Warning Track %u: Using empty User SysEx command %u at 0x%04X\n", trkID, cmdType & 0x07, prevPos);
			}
			break;
		case 0x98:	// send SysEx
			{
				UINT16 syxLen;
				
				// at first, determine the size of the required buffer
				syxLen = GetMultiCmdDataSize(rcpLen, rcpData, rcpInf, inPos, MCMD_INI_EXCLUDE | MCMD_RET_DATASIZE);
				if (txtBufSize < (UINT32)syxLen)
				{
					txtBufSize = (syxLen + 0x0F) & ~0x0F;	// round up to 0x10
					txtBuffer = (UINT8*)realloc(txtBuffer, txtBufSize);
				}
				// then read input data
				syxLen = ReadMultiCmdData(rcpLen, rcpData, rcpInf, &inPos, txtBufSize, txtBuffer, MCMD_INI_EXCLUDE);
				if (midiDev == 0xFF)
					break;
				syxLen = ProcessRcpSysEx(syxLen, txtBuffer, txtBuffer, cmdP1, cmdP2, midChn);
				WriteLongEvent(fInf, MTS, 0xF0, syxLen, txtBuffer);
			}
			break;
		//case 0x99:	// execute external command
		case 0xC0:	// DX7 Function
		case 0xC1:	// DX Parameter
		case 0xC2:	// DX RERF
		case 0xC3:	// TX Function
		case 0xC5:	// FB-01 P Parameter
		case 0xC7:	// TX81Z V VCED
		case 0xC8:	// TX81Z A ACED
		case 0xC9:	// TX81Z P PCED
		case 0xCC:	// DX7-2 R Remote SW
		case 0xCD:	// DX7-2 A ACED
		case 0xCE:	// DX7-2 P PCED
		case 0xCF:	// TX802 P PCED
			if (midiDev == 0xFF)
				break;
			{
				static const UINT8 DX_PARAM[0x10] = {
					0x08, 0x00, 0x04, 0x11, 0xFF, 0x15, 0xFF, 0x12,
					0x13, 0x10, 0xFF, 0xFF, 0x1B, 0x18, 0x19, 0x1A,
				};
				tempArr[0] = 0x43;	// YAMAHA ID
				tempArr[1] = 0x10 | MTS->midChn;
				tempArr[2] = DX_PARAM[cmdType & 0x0F];
				tempArr[3] = cmdP1;
				tempArr[4] = cmdP2;
				tempArr[5] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 6, tempArr);
			}
			break;
		case 0xC6:	// FB-01 S System
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x75;
			tempArr[2] = MTS->midChn;
			tempArr[3] = 0x10;
			tempArr[4] = cmdP1;
			tempArr[5] = cmdP2;
			tempArr[6] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 7, tempArr);
			break;
		case 0xCA:	// TX81Z S System
		case 0xCB:	// TX81Z E EFFECT
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x10 | MTS->midChn;
			tempArr[2] = 0x10;
			tempArr[3] = 0x7B + (cmdType - 0xCA);	// command CA -> param = 7B, command CB -> param = 7C
			tempArr[4] = cmdP1;
			tempArr[5] = cmdP2;
			tempArr[6] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 7, tempArr);
			break;
		case 0xD0:	// YAMAHA Base Address
			xgParams[2] = cmdP1;
			xgParams[3] = cmdP2;
			break;
		case 0xD1:	// YAMAHA Device Data
			xgParams[0] = cmdP1;
			xgParams[1] = cmdP2;
			break;
		case 0xD2:	// YAMAHA Address / Parameter
			xgParams[4] = cmdP1;
			xgParams[5] = cmdP2;
			if (midiDev == 0xFF)
				break;
			
			tempArr[0] = 0x43;	// YAMAHA ID
			memcpy(&tempArr[1], &xgParams[0], 6);
			tempArr[7] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 8, tempArr);
			break;
		case 0xD3:	// YAMAHA XG Address / Parameter
			xgParams[4] = cmdP1;
			xgParams[5] = cmdP2;
			if (midiDev == 0xFF)
				break;
			
			tempArr[0] = 0x43;	// YAMAHA ID
			tempArr[1] = 0x10;	// Parameter Change
			tempArr[2] = 0x4C;	// XG
			memcpy(&tempArr[3], &xgParams[2], 4);
			tempArr[7] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 8, tempArr);
			break;
		case 0xDC:	// MKS-7
			if (midiDev == 0xFF)
				break;
			tempArr[0] = 0x41;	// Roland ID
			tempArr[1] = 0x32;
			tempArr[2] = MTS->midChn;
			tempArr[3] = cmdP1;
			tempArr[4] = cmdP2;
			tempArr[5] = 0xF7;
			WriteLongEvent(fInf, MTS, 0xF0, 6, tempArr);
			break;
		case 0xDD:	// Roland Base Address
			gsParams[2] = cmdP1;
			gsParams[3] = cmdP2;
			break;
		case 0xDE:	// Roland Parameter
			gsParams[4] = cmdP1;
			gsParams[5] = cmdP2;
			if (midiDev == 0xFF)
				break;
			
			{
				UINT8 chkSum;
				UINT8 curParam;
				
				tempArr[0] = 0x41;	// Roland ID
				tempArr[1] = gsParams[0];
				tempArr[2] = gsParams[1];
				tempArr[3] = 0x12;
				chkSum = 0x00;	// initialize checksum
				for (curParam = 0; curParam < 4; curParam ++)
				{
					tempArr[4 + curParam] = gsParams[2 + curParam];
					chkSum += gsParams[2 + curParam];	// add to checksum
				}
				tempArr[8] = (0x100 - chkSum) & 0x7F;
				tempArr[9] = 0xF7;
				WriteLongEvent(fInf, MTS, 0xF0, 10, tempArr);
			}
			break;
		case 0xDF:	// Roland Device
			gsParams[0] = cmdP1;
			gsParams[1] = cmdP2;
			break;
		case 0xE1:	// set XG instrument
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xB0, 0x20, cmdP2);
			WriteEvent(fInf, MTS, 0xC0, cmdP1, 0x00);
			break;
		case 0xE2:	// set GS instrument
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xB0, 0x00, cmdP2);
			WriteEvent(fInf, MTS, 0xC0, cmdP1, 0x00);
			break;
		case 0xE5:	// "Key Scan"
			printf("Warning Track %u: Key Scan command found at 0x%04X\n", trkID, prevPos);
			break;
		case 0xE6:	// MIDI channel
			//printf("Warning Track %u: Set MIDI Channel command found at 0x%04X\n", trkID, prevPos);
			cmdP1 --;	// It's same as in the track header, except 1 added.
			if (cmdP1 & 0x80)
			{
				// When the KeepDummyCh option is off, ignore the event.
				// Else set midiDev to 0xFF to prevent events from being written.
				if (! KEEP_DUMMY_CH)
				{
					midiDev = 0xFF;
					midChn = 0x00;
				}
			}
			else
			{
				midiDev = cmdP1 >> 4;	// port ID
				midChn = cmdP1 & 0x0F;	// channel ID
				WriteMetaEvent(fInf, MTS, 0x21, 1, &midiDev);	// Meta Event: MIDI Port Prefix
				WriteMetaEvent(fInf, MTS, 0x20, 1, &midChn);	// Meta Event: MIDI Channel Prefix
			}
			MTS->midChn = midChn;
			break;
		case 0xE7:	// Tempo Modifier
			{
				UINT32 tempoVal;
				
				if (cmdP2)
					printf("Warning Track %u: Interpolated Tempo Change at 0x%04X!\n", trkID, prevPos);
				tempoVal = Tempo2Mid(rcpInf->tempoBPM, cmdP1);
				WriteBE32(tempArr, tempoVal);
				WriteMetaEvent(fInf, MTS, 0x51, 0x03, &tempArr[0x01]);
			}
			break;
		case 0xEA:	// Channel Aftertouch
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xD0, cmdP1, 0x00);
			break;
		case 0xEB:	// Control Change
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xB0, cmdP1, cmdP2);
			break;
		case 0xEC:	// Instrument
			if (midiDev == 0xFF)
				break;
			if (cmdP1 < 0x80)
			{
				WriteEvent(fInf, MTS, 0xC0, cmdP1, 0x00);
			}
			else if (cmdP1 < 0xC0 && (midChn >= 1 && midChn < 9))
			{
				// set MT-32 instrument from user bank
				// used by RCP files from Granada X68000
				UINT8 partMemOfs = (midChn - 1) << 4;
				
				memcpy(tempArr, MT32_PATCH_CHG, 0x07);
				tempArr[0x00] = (cmdP1 >> 6) & 0x03;
				tempArr[0x01] = (cmdP1 >> 0) & 0x3F;
				WriteRolandSyxData(fInf, MTS, MT32_SYX_HDR, 0x030000 | partMemOfs, 0x07, tempArr, 0x00);
			}
			break;
		case 0xED:	// Note Aftertouch
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xA0, cmdP1, cmdP2);
			break;
		case 0xEE:	// Pitch Bend
			if (midiDev == 0xFF)
				break;
			WriteEvent(fInf, MTS, 0xE0, cmdP1, cmdP2);
			break;
		case 0xF5:	// Key Signature Change
			RcpKeySig2Mid(tempArr, (UINT8)cmdP0Delay);
			WriteMetaEvent(fInf, MTS, 0x59, 0x02, tempArr);
			cmdP0Delay = 0;
			break;
		case 0xF6:	// comment
			{
				UINT16 txtLen;
				
				// at first, determine the size of the required buffer
				txtLen = GetMultiCmdDataSize(rcpLen, rcpData, rcpInf, inPos, MCMD_INI_INCLUDE | MCMD_RET_DATASIZE);
				if (txtBufSize < txtLen)
				{
					txtBufSize = (txtLen + 0x0F) & ~0x0F;	// round up to 0x10
					txtBuffer = (UINT8*)realloc(txtBuffer, txtBufSize);
				}
				// then read input data
				txtLen = ReadMultiCmdData(rcpLen, rcpData, rcpInf, &inPos, txtBufSize, txtBuffer, MCMD_INI_INCLUDE);
				txtLen = GetTrimmedLength(txtLen, (char*)txtBuffer, ' ', 0);
				WriteMetaEvent(fInf, MTS, 0x01, txtLen, txtBuffer);
			}
			cmdP0Delay = 0;
			break;
		case 0xF7:	// continuation of previous command
			printf("Warning Track %u: Unexpected continuation command at 0x%04X!\n", trkID, prevPos);
			break;
		case 0xF8:	// Loop End
			if (loopIdx == 0)
			{
				printf("Warning Track %u: Loop End without Loop Start at 0x%04X!\n", trkID, prevPos);
				if (BAR_MARKERS)
				{
					UINT32 txtLen = sprintf((char*)tempArr, "Bad Loop End");
					WriteMetaEvent(fInf, MTS, 0x07, txtLen, tempArr);
				}
			}
			else
			{
				UINT8 takeLoop;
				
				takeLoop = 0;
				loopIdx --;
				loopCnt[loopIdx] ++;
				// loops == 0 -> infinite, but some songs also use very high values (like 0xFF) for that
				if (cmdP0Delay == 0 || cmdP0Delay >= 0x7F)
				{
					// infinite loop
					if (loopCnt[loopIdx] < 0x80 && midiDev != 0xFF)
						WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)loopCnt[loopIdx]);
					
					if (loopCnt[loopIdx] < trkInf->loopTimes)
						takeLoop = 1;
				}
				else
				{
					if (loopCnt[loopIdx] < cmdP0Delay)
						takeLoop = 1;
				}
				if (BAR_MARKERS)
				{
					UINT32 txtLen = sprintf((char*)tempArr, "Loop %u End (%u/%u)",
						1 + loopIdx, loopCnt[loopIdx], cmdP0Delay);
					WriteMetaEvent(fInf, MTS, 0x07, txtLen, tempArr);
				}
				if (takeLoop)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			cmdP0Delay = 0;
			break;
		case 0xF9:	// Loop Start
			if (BAR_MARKERS)
			{
				UINT32 txtLen = sprintf((char*)tempArr, "Loop %u Start", 1 + loopIdx);
				WriteMetaEvent(fInf, MTS, 0x07, txtLen, tempArr);
			}
			
			if (loopIdx >= 8)
			{
				printf("Error Track %u: Trying to do more than 8 nested loops at 0x%04X!\n", trkID, prevPos);
			}
			else
			{
				if (inPos == trkInf->loopOfs && midiDev != 0xFF)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, 0);
				
				loopPPos[loopIdx] = parentPos;	// required by YS-2･018.RCP
				loopPos[loopIdx] = inPos;
				loopCnt[loopIdx] = 0;
				//if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
				//	loopIdx --;	// ignore loop command (required by YS-2･018.RCP)
				loopIdx ++;
			}
			cmdP0Delay = 0;
			break;
		case 0xFC:	// repeat previous measure
			// Behaviour of the FC command:
			//	- already in "repeating measure" mode: return to parent measure (same as FD)
			//	- else: follow chain of FC commands
			//	        i.e. "FC -> FC -> FC -> non-FC command" is a valid sequence that is followed to the end.
			if (parentPos)
			{
				printf("Warning Track %u: Leaving recursive Repeat Measure at 0x%04X!\n", trkID, prevPos);
				inPos = parentPos;
				parentPos = 0x00;
				repMeasure = 0xFFFF;
			}
			else
			{
				UINT16 measureID;
				UINT32 repeatPos;
				UINT32 cachedPos;
				
				if (rcpInf->fileVer == 2)
					inPos -= 0x04;
				else if (rcpInf->fileVer == 3)
					inPos -= 0x06;
				do
				{
					if (rcpInf->fileVer == 2)
					{
						cmdP0Delay = rcpData[inPos + 0x01];
						cmdP1 = rcpData[inPos + 0x02];
						cmdP2 = rcpData[inPos + 0x03];
						measureID = (cmdP0Delay << 0) | ((cmdP1 & 0x03) << 8);
						repeatPos = ((cmdP1 & ~0x03) << 0) | (cmdP2 << 8);
						inPos += 0x04;
					}
					else if (rcpInf->fileVer == 3)
					{
						cmdP0Delay = ReadLE16(&rcpData[inPos + 0x02]);
						cmdDurat = ReadLE16(&rcpData[inPos + 0x04]);
						measureID = cmdP0Delay;
						// I have no idea why the first command has ID 0x30.
						repeatPos = 0x002E + (cmdDurat - 0x0030) * 0x06;	// calculate offset from command ID
						inPos += 0x06;
					}
					
					if (BAR_MARKERS)
					{
						UINT32 txtLen = sprintf((char*)tempArr, "Repeat Bar %u", 1 + measureID);
						WriteMetaEvent(fInf, MTS, 0x07, txtLen, tempArr);
					}
					
					if (measureID >= measPosCount)
					{
						printf("Warning Track %u: Trying to repeat invalid bar %u (have %u bars) at 0x%04X!\n",
							trkID, measureID, curBar + 1, prevPos);
						break;
					}
					
					cachedPos = measurePos[measureID] - trkBasePos;
					if (cachedPos != repeatPos)
						printf("Warning Track %u: Repeat Measure %u: offset mismatch (file: 0x%04X != expected 0x%04X) at 0x%04X!\n",
							trkID, measureID, repeatPos, cachedPos, prevPos);
					if (trkBasePos + repeatPos == prevPos)
						break;	// prevent recursion (just for safety)
					
					if (! parentPos)	// necessary for following FC command chain
						parentPos = inPos;
					repMeasure = measureID;
					// YS3-25.RCP relies on using the actual offset. (*Some* of its measure numbers are off by 1.)
					inPos = trkBasePos + repeatPos;
					prevPos = inPos;
				} while(rcpData[inPos] == 0xFC);
			}
			cmdP0Delay = 0;
			break;
		case 0xFD:	// measure end
			if (measPosCount >= 0x8000)	// prevent infinite loops (and seg. fault due to overflow in measPosAlloc)
			{
				trkEnd = 1;
				break;
			}
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
				repMeasure = 0xFFFF;
			}
			if (measPosCount >= measPosAlloc)
			{
				measPosAlloc *= 2;
				measurePos = (UINT32*)realloc(measurePos, measPosAlloc * sizeof(UINT32));
			}
			measurePos[measPosCount] = inPos;
			measPosCount ++;
			curBar ++;
			cmdP0Delay = 0;
			
			if (BAR_MARKERS)
			{
				UINT32 txtLen = sprintf((char*)tempArr, "Bar %u", 1 + curBar);
				WriteMetaEvent(fInf, MTS, 0x07, txtLen, tempArr);
			}
			if (WOLFTEAM_LOOP && measPosCount == 2)
			{
				loopIdx = 0;
				if (midiDev != 0xFF)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, 0);
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopCnt[loopIdx] = 0;
				loopIdx ++;
			}
			break;
		case 0xFE:	// track end
			trkEnd = 1;
			cmdP0Delay = 0;
			if (WOLFTEAM_LOOP)
			{
				loopIdx = 0;
				loopCnt[loopIdx] ++;
				if (loopCnt[loopIdx] < 0x80 && midiDev != 0xFF)
					WriteEvent(fInf, MTS, 0xB0, 0x6F, (UINT8)loopCnt[loopIdx]);
				if (loopCnt[loopIdx] < NUM_LOOPS)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
					trkEnd = 0;
				}
			}
			break;
		default:
			printf("Warning Track %u: Unhandled RCP command 0x%02X at position 0x%04X!\n", trkID, cmdType, prevPos);
			break;
		}	// end if (cmdType >= 0x80) / switch(cmdType)
		MTS->curDly += cmdP0Delay;
		
		// remove ticks from curDly from all events until startTicks reaches 0
		if (startTick < 0 && MTS->curDly > 0)
		{
			startTick += MTS->curDly;
			if (startTick >= 0)
			{
				MTS->curDly = startTick;
				startTick = 0;
			}
			else
			{
				MTS->curDly = 0;
			}
		}
	}	// end while(! trkEnd)
	free(txtBuffer);
	free(measurePos);
	if (midiDev == 0xFF)
		MTS->curDly = 0;
	FlushRunningNotes(fInf, &MTS->curDly, &RunNoteCnt, RunNotes, 0);
	
	*rcpInPos = trkBasePos + trkLen;
	return 0x00;
}

static UINT8 PreparseRcpTrack(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
							UINT32 startPos, TRK_INF* trkInf)
{
	UINT32 inPos;
	UINT32 trkBasePos;
	UINT32 trkEndPos;
	UINT32 trkLen;
	UINT32 parentPos;
	UINT16 measPosAlloc;
	UINT16 measPosCount;
	UINT32* measurePos;
	UINT8 trkEnd;
	UINT8 cmdType;
	UINT8 cmdP1;
	UINT8 cmdP2;
	UINT16 cmdP0Delay;
	UINT16 cmdDurat;
	UINT8 loopIdx;
	UINT32 loopPPos[8];
	UINT32 loopPos[8];
	UINT32 loopTick[8];
	UINT16 loopCnt[8];
	
	inPos = startPos;
	if (inPos >= rcpLen)
		return 0x01;
	
	trkBasePos = inPos;
	if (rcpInf->fileVer == 2)
	{
		trkLen = ReadLE16(&rcpData[inPos]);
		inPos += 0x02;
	}
	else if (rcpInf->fileVer == 3)
	{
		trkLen = ReadLE32(&rcpData[inPos]);
		inPos += 0x04;
	}
	trkEndPos = trkBasePos + trkLen;
	if (trkEndPos > rcpLen)
		trkEndPos = rcpLen;
	if (inPos + 0x2A > rcpLen)
		return 0x01;	// not enough bytes to read the header
	
	inPos += 0x2A;
	
	trkInf->startOfs = trkBasePos;
	trkInf->trkLen = trkLen;
	trkInf->loopOfs = 0x00;
	trkInf->tickCnt = 0;
	trkInf->loopTick = 0;
	
	measPosAlloc = 0x100;
	measPosCount = 0x00;
	measurePos = (UINT32*)malloc(measPosAlloc * sizeof(UINT32));
	
	trkEnd = 0;
	parentPos = 0x00;
	loopIdx = 0x00;
	
	measurePos[measPosCount] = inPos;
	measPosCount ++;
	while(inPos < trkEndPos && ! trkEnd)
	{
		if (rcpInf->fileVer == 2)
		{
			cmdType = rcpData[inPos + 0x00];
			cmdP0Delay = rcpData[inPos + 0x01];
			cmdP1 = rcpData[inPos + 0x02];
			cmdDurat = cmdP1;
			cmdP2 = rcpData[inPos + 0x03];
			inPos += 0x04;
		}
		else if (rcpInf->fileVer == 3)
		{
			cmdType = rcpData[inPos + 0x00];
			cmdP2 = rcpData[inPos + 0x01];
			cmdP0Delay = ReadLE16(&rcpData[inPos + 0x02]);
			cmdP1 = rcpData[inPos + 0x04];
			cmdDurat = ReadLE16(&rcpData[inPos + 0x04]);
			inPos += 0x06;
		}
		
		switch(cmdType)
		{
		case 0xF8:	// Loop End
			if (loopIdx > 0)
			{
				loopIdx --;
				loopCnt[loopIdx] ++;
				if (cmdP0Delay == 0)
				{
					trkInf->loopOfs = loopPos[loopIdx];
					trkInf->loopTick = loopTick[loopIdx];
					trkEnd = 1;
				}
				else if (loopCnt[loopIdx] < cmdP0Delay)
				{
					parentPos = loopPPos[loopIdx];
					inPos = loopPos[loopIdx];
					loopIdx ++;
				}
			}
			cmdP0Delay = 0;
			break;
		case 0xF9:	// Loop Start
			if (loopIdx < 8)
			{
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopCnt[loopIdx] = 0;
				//if (loopIdx > 0 && loopPos[loopIdx] == loopPos[loopIdx - 1])
				//	loopIdx --;	// ignore loop command
				loopIdx ++;
			}
			cmdP0Delay = 0;
			break;
		case 0xFC:	// repeat previous measure
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
			}
			else
			{
				if (rcpInf->fileVer == 2)
					inPos -= 0x04;
				else if (rcpInf->fileVer == 3)
					inPos -= 0x06;
				do
				{
					UINT32 prevPos = inPos;
					UINT32 repeatPos;
					UINT16 measureID;
					
					if (rcpInf->fileVer == 2)
					{
						cmdP0Delay = rcpData[inPos + 0x01];
						cmdP1 = rcpData[inPos + 0x02];
						cmdP2 = rcpData[inPos + 0x03];
						measureID = (cmdP0Delay << 0) | ((cmdP1 & 0x03) << 8);
						repeatPos = ((cmdP1 & ~0x03) << 0) | (cmdP2 << 8);
						inPos += 0x04;
					}
					else if (rcpInf->fileVer == 3)
					{
						measureID = ReadLE16(&rcpData[inPos + 0x02]);
						repeatPos = 0x002E + (ReadLE16(&rcpData[inPos + 0x04]) - 0x0030) * 0x06;
						inPos += 0x06;
					}
					if (measureID >= measPosCount)
						break;
					if (trkBasePos + repeatPos == prevPos)
						break;	// prevent recursion
					
					if (! parentPos)	// necessary for following FC command chain
						parentPos = inPos;
					inPos = trkBasePos + repeatPos;
					prevPos = inPos;
				} while(rcpData[inPos] == 0xFC);
			}
			cmdP0Delay = 0;
			break;
		case 0xFD:	// measure end
			if (measPosCount >= 0x8000)
			{
				trkEnd = 1;
				break;
			}
			if (parentPos)
			{
				inPos = parentPos;
				parentPos = 0x00;
			}
			if (measPosCount >= measPosAlloc)
			{
				measPosAlloc *= 2;
				measurePos = (UINT32*)realloc(measurePos, measPosAlloc * sizeof(UINT32));
			}
			measurePos[measPosCount] = inPos;
			measPosCount ++;
			cmdP0Delay = 0;
			if (WOLFTEAM_LOOP && measPosCount == 2)
			{
				loopIdx = 0;
				loopPPos[loopIdx] = parentPos;
				loopPos[loopIdx] = inPos;
				loopTick[loopIdx] = trkInf->tickCnt;
				loopCnt[loopIdx] = 0;
				loopIdx ++;
			}
			break;
		case 0xFE:	// track end
			trkEnd = 1;
			cmdP0Delay = 0;
			if (WOLFTEAM_LOOP)
			{
				loopIdx = 0;
				trkInf->loopOfs = loopPos[loopIdx];
				trkInf->loopTick = loopTick[loopIdx];
			}
			break;
		default:
			if (cmdType >= 0xF0)
				cmdP0Delay = 0;
		}	// end switch(cmdType)
		
		trkInf->tickCnt += cmdP0Delay;
	}	// end while(! trkEnd)
	free(measurePos);
	
	return 0x00;
}

static UINT16 GetMultiCmdDataSize(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
									UINT32 startPos, UINT8 flags)
{
	UINT32 inPos;
	UINT16 cmdCount;
	
	cmdCount = (flags & MCMD_INI_INCLUDE) ? 1 : 0;
	if (rcpInf->fileVer == 2)
	{
		for (inPos = startPos; inPos < rcpLen && rcpData[inPos] == 0xF7; inPos += 0x04)
			cmdCount ++;
		if (flags & MCMD_RET_DATASIZE)
			cmdCount *= 2;	// 2 data bytes per command
	}
	else if (rcpInf->fileVer == 3)
	{
		for (inPos = startPos; inPos < rcpLen && rcpData[inPos] == 0xF7; inPos += 0x06)
			cmdCount ++;
		if (flags & MCMD_RET_DATASIZE)
			cmdCount *= 5;	// 5 data bytes per command
	}
	return cmdCount;
}

static UINT16 ReadMultiCmdData(UINT32 rcpLen, const UINT8* rcpData, const RCP_INFO* rcpInf,
								UINT32* rcpInPos, UINT32 bufSize, UINT8* buffer, UINT8 flags)
{
	UINT32 inPos;
	UINT32 bufPos;
	
	bufPos = 0x00;
	inPos = *rcpInPos;
	if (rcpInf->fileVer == 2)
	{
		if (flags & MCMD_INI_INCLUDE)
		{
			if (bufPos + 0x02 > bufSize)
				return 0x00;
			buffer[bufPos + 0x00] = rcpData[inPos - 0x02];
			buffer[bufPos + 0x01] = rcpData[inPos - 0x01];
			bufPos += 0x02;
		}
		for (; inPos < rcpLen && rcpData[inPos] == 0xF7; inPos += 0x04)
		{
			if (bufPos + 0x02 > bufSize)
				break;
			buffer[bufPos + 0x00] = rcpData[inPos + 0x02];
			buffer[bufPos + 0x01] = rcpData[inPos + 0x03];
			bufPos += 0x02;
		}
	}
	else if (rcpInf->fileVer == 3)
	{
		if (flags & MCMD_INI_INCLUDE)
		{
			if (bufPos + 0x05 > bufSize)
				return 0x00;
			memcpy(&buffer[bufPos], &rcpData[inPos - 0x05], 0x05);
			bufPos += 0x05;
		}
		for (; inPos < rcpLen && rcpData[inPos] == 0xF7; inPos += 0x06)
		{
			if (bufPos + 0x05 > bufSize)
				break;
			memcpy(&buffer[bufPos], &rcpData[inPos + 0x01], 0x05);
			bufPos += 0x05;
		}
	}
	
	*rcpInPos = inPos;
	return (UINT16)bufPos;
}

static UINT16 GetTrimmedLength(UINT16 dataLen, const char* data, char trimChar, UINT8 leaveLast)
{
	UINT16 trimLen;
	
	for (trimLen = dataLen; trimLen > 0; trimLen --)
	{
		if (data[trimLen - 1] != trimChar)
			break;
	}
	if (leaveLast && trimLen < dataLen)
		trimLen ++;
	return trimLen;
}

static UINT32 ReadRcpStr(RCP_STR* strInfo, UINT16 maxlen, const UINT8* data)
{
	strInfo->data = (const char*)data;
	strInfo->maxSize = maxlen;
	strInfo->length = GetTrimmedLength(strInfo->maxSize, strInfo->data, ' ', 0);
	
	return maxlen;
}

static UINT32 ReadRcpStr0(RCP_STR* strInfo, UINT16 maxlen, const UINT8* data)
{
	const char* strEnd;
	
	strInfo->data = (const char*)data;
	strInfo->maxSize = maxlen;
	// special trimming used for file names
	strEnd = (const char*)memchr(strInfo->data, '\0', strInfo->maxSize);
	strInfo->length = (strEnd != NULL) ? (strEnd - strInfo->data) : strInfo->maxSize;	// stop at first '\0'
	strInfo->length = GetTrimmedLength(strInfo->length, strInfo->data, ' ', 0);	// then trim spaces off
	
	return maxlen;
}

INLINE UINT32 Tempo2Mid(UINT16 bpm, UINT8 scale)
{
	// formula: (60 000 000.0 / bpm) * (scale / 64.0)
	UINT32 div = bpm * scale;
	// I like rounding, but doing so make most MIDI programs display e.g. "144.99 BPM".
	return 60000000U * 64U / div;
	//return (60000000U * 64U + div / 2) / div;
}

static void RcpTimeSig2Mid(UINT8 buffer[4], UINT8 beatNum, UINT8 beatDen)
{
	UINT8 den_base2;
	
	den_base2 = val2shift(beatDen);
	buffer[0] = beatNum;			// numerator
	buffer[1] = den_base2;			// log2(denominator)
	buffer[2] = 96 >> den_base2;	// metronome pulse
	buffer[3] = 8;					// 32nd notes per 1/4 note
	
	return;
}

static void RcpKeySig2Mid(UINT8 buffer[2], UINT8 rcpKeySig)
{
	INT8 key;
	
	if (rcpKeySig & 0x08)
		key = -(rcpKeySig & 0x07);	// flats
	else
		key = rcpKeySig & 0x07;		// sharps
	
	buffer[0] = (UINT8)key;					// main key (number of sharps/flats)
	buffer[1] = (rcpKeySig & 0x10) >> 4;	// major (0) / minor (1)
	
	return;
}

static UINT8 val2shift(UINT32 value)
{
	UINT8 shift;
	
	shift = 0;
	value >>= 1;
	while(value)
	{
		shift ++;
		value >>= 1;
	}
	return shift;
}

static UINT16 ProcessRcpSysEx(UINT16 syxMaxLen, const UINT8* syxData, UINT8* syxBuffer,
								UINT8 param1, UINT8 param2, UINT8 midChn)
{
	UINT16 inPos;
	UINT16 outPos;
	UINT8 chkSum;
	
	chkSum = 0x00;
	outPos = 0x00;
	for (inPos = 0x00; inPos < syxMaxLen; inPos ++)
	{
		UINT8 data = syxData[inPos];
		
		if (data & 0x80)
		{
			switch(data)
			{
			case 0x80:	// put data value (cmdP1)
				data = param1;
				break;
			case 0x81:	// put data value (cmdP2)
				data = param2;
				break;
			case 0x82:	// put data value (midChn)
				data = midChn;
				break;
			case 0x83:	// initialize Roland Checksum
				chkSum = 0x00;
				break;
			case 0x84:	// put Roland Checksum
				data = (0x100 - chkSum) & 0x7F;
				break;
			case 0xF7:	// SysEx end
				syxBuffer[outPos] = data;
				outPos ++;
				return outPos;
			default:
				printf("Unknown SysEx command 0x%02X found in SysEx data!\n", data);
				break;
			}
		}
		
		if (! (data & 0x80))
		{
			syxBuffer[outPos] = data;
			outPos ++;
			chkSum += data;
		}
	}
	
	return outPos;
}

static UINT8 MidiDelayHandler(FILE_INF* fInf, UINT32* delay)
{
	midiTickCount += *delay;
	
	CheckRunningNotes(fInf, delay, &RunNoteCnt, RunNotes);
	if (*delay)
	{
		UINT8 curNote;
		
		for (curNote = 0; curNote < RunNoteCnt; curNote ++)
			RunNotes[curNote].remLen -= *delay;
	}
	
	return 0x00;
}


static void WriteRolandSyxData(FILE_INF* fInf, MID_TRK_STATE* MTS, const UINT8* syxHdr,
	UINT32 address, UINT32 len, const UINT8* data, UINT8 opts)
{
	UINT32 curPos;
	UINT8 chkSum;
	UINT32 dataLen = 0x09 + len;
	
	if (MTS == NULL)
	{
		fInf->data[fInf->pos] = 0xF0;
		fInf->pos += 0x01;
	}
	else
	{
		WriteMidiDelay(fInf, &MTS->curDly);
		
		File_CheckRealloc(fInf, 0x01 + 0x04 + dataLen);	// worst case: 4 bytes of data length
		fInf->data[fInf->pos] = 0xF0;
		fInf->pos += 0x01;
		WriteMidiValue(fInf, dataLen);
	}
	
	fInf->data[fInf->pos + 0x00] = syxHdr[0];
	fInf->data[fInf->pos + 0x01] = syxHdr[1];
	fInf->data[fInf->pos + 0x02] = syxHdr[2];
	fInf->data[fInf->pos + 0x03] = syxHdr[3];
	fInf->data[fInf->pos + 0x04] = (address >> 16) & 0x7F;
	fInf->data[fInf->pos + 0x05] = (address >>  8) & 0x7F;
	fInf->data[fInf->pos + 0x06] = (address >>  0) & 0x7F;
	memcpy(&fInf->data[fInf->pos + 0x07], data, len);
	
	chkSum = 0x00;
	for (curPos = 0x04; curPos < 0x07 + len; curPos ++)
		chkSum += fInf->data[fInf->pos + curPos];
	
	fInf->data[fInf->pos + len + 0x07] = (-chkSum) & 0x7F;
	fInf->data[fInf->pos + len + 0x08] = 0xF7;
	fInf->pos += dataLen;
	
	if (MTS != NULL && (opts & SYXOPT_DELAY))
	{
		// ticks/second = midiTickRes * 1 000 000 / midiTempoTicks
		// tick_delay = ceil(ticks/second * dataLength / 3125)
		MTS->curDly += MulDivCeil(dataLen + 1, midiTickRes * 320, midiTempoTicks);	// (dataLen+1) for counting the initial F0 command
	}
	
	return;
}

static void WriteRolandSyxBulk(FILE_INF* fInf, MID_TRK_STATE* MTS, const UINT8* syxHdr,
	UINT32 address, UINT32 len, const UINT8* data, UINT32 bulkSize, UINT8 opts)
{
	UINT32 curPos;
	UINT32 wrtBytes;
	UINT32 curAddr;
	UINT32 syxAddr;
	
	curAddr = ((address & 0x00007F) >> 0) |
				((address & 0x007F00) >> 1) |
				((address & 0x7F0000) >> 2);
	for (curPos = 0x00; curPos < len; )
	{
		wrtBytes = len - curPos;
		if (wrtBytes > bulkSize)
			wrtBytes = bulkSize;
		syxAddr = ((curAddr & 0x00007F) << 0) |
					((curAddr & 0x003F80) << 1) |
					((curAddr & 0x1FC000) << 2);
		WriteRolandSyxData(fInf, MTS, syxHdr, syxAddr, wrtBytes, &data[curPos], opts);
		curPos += wrtBytes;
		curAddr += wrtBytes;
	}
	
	return;
}

static void WriteMetaEventFromStr(FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 metaType, const char* text)
{
	WriteMetaEvent(fInf, MTS, metaType, strlen(text), text);
	return;
}

static UINT8 ParseCM6File(const FILE_DATA* cm6File, CM6_INFO* cm6Inf)
{
	UINT8 fileVer;
	
	fileVer = GetFileVer(cm6File);
	if (fileVer != 0x10)
		return 0xFF;
	if (cm6File->len < 0x5849)
		return 0xF8;	// file too small
	
	cm6Inf->deviceType = cm6File->data[0x001A];
	ReadRcpStr(&cm6Inf->comment, 0x40, &cm6File->data[0x0040]);
	cm6Inf->laSystem = &cm6File->data[0x0080];
	cm6Inf->laChnVol = &cm6File->data[0x0097];
	cm6Inf->laPatchTemp = &cm6File->data[0x00A0];
	cm6Inf->laRhythmTemp = &cm6File->data[0x0130];
	cm6Inf->laTimbreTemp = &cm6File->data[0x0284];
	cm6Inf->laPatchMem = &cm6File->data[0x0A34];
	cm6Inf->laTimbreMem = &cm6File->data[0x0E34];
	cm6Inf->pcmPatchTemp = &cm6File->data[0x4E34];
	cm6Inf->pcmPatchMem = &cm6File->data[0x4EB2];
	cm6Inf->pcmSystem = &cm6File->data[0x5832];
	cm6Inf->pcmChnVol = &cm6File->data[0x5843];
	
	return 0x00;
}

static UINT8 Cm62MidTrk(const CM6_INFO* cm6Inf, FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 mode)
{
	if (mode & 0x01)	// MIDI mode: convert file comment
		WriteMetaEvent(fInf, MTS, 0x01, cm6Inf->comment.length, cm6Inf->comment.data);
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "MT-32 System");
	WriteRolandSyxData(fInf, MTS, MT32_SYX_HDR, 0x100000, 0x17, cm6Inf->laSystem, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "MT-32 Patch Temporary");
	WriteRolandSyxData(fInf, MTS, MT32_SYX_HDR, 0x030000, 0x90, cm6Inf->laPatchTemp, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "MT-32 Rhythm Setup");
	WriteRolandSyxBulk(fInf, MTS, MT32_SYX_HDR, 0x030110, 0x154, cm6Inf->laRhythmTemp, 0x100, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "MT-32 Timbre Temporary");
	WriteRolandSyxBulk(fInf, MTS, MT32_SYX_HDR, 0x040000, 0x7B0, cm6Inf->laTimbreTemp, 0x100, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "MT-32 Patch Memory");
	WriteRolandSyxBulk(fInf, MTS, MT32_SYX_HDR, 0x050000, 0x400, cm6Inf->laPatchMem, 0x100, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "MT-32 Timbre Memory");
	WriteRolandSyxBulk(fInf, MTS, MT32_SYX_HDR, 0x080000, 0x4000, cm6Inf->laTimbreMem, 0x100, SYXOPT_DELAY);
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "CM-32P Patch Temporary");
	WriteRolandSyxData(fInf, MTS, MT32_SYX_HDR, 0x500000, 0x7E, cm6Inf->pcmPatchTemp, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "CM-32P Patch Memory");
	WriteRolandSyxBulk(fInf, MTS, MT32_SYX_HDR, 0x510000, 0x980, cm6Inf->pcmPatchMem, 0x100, SYXOPT_DELAY);
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "CM-32P System");
	WriteRolandSyxData(fInf, MTS, MT32_SYX_HDR, 0x520000, 0x11, cm6Inf->pcmSystem, SYXOPT_DELAY);
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "Setup Finished.");
	
	return 0x00;
}

static UINT8 ParseGSDFile(const FILE_DATA* gsdFile, GSD_INFO* gsdInf)
{
	UINT8 fileVer;
	
	fileVer = GetFileVer(gsdFile);
	if (fileVer != 0x11)
		return 0xFF;
	if (gsdFile->len < 0xA71)
		return 0xF8;	// file too small
	
	gsdInf->sysParams = &gsdFile->data[0x0020];
	gsdInf->reverbParams = &gsdFile->data[0x0027];
	gsdInf->chorusParams = &gsdFile->data[0x002E];
	gsdInf->partParams = &gsdFile->data[0x0036];
	gsdInf->drumSetup = &gsdFile->data[0x07D6];
	gsdInf->masterTune = &gsdFile->data[0x0A6E];
	
	return 0x00;
}

// nibbilize, high-low order
static void Bytes2NibblesHL(UINT32 bytes, UINT8* nibData, const UINT8* byteData)
{
	UINT32 curPos;
	
	for (curPos = 0x00; curPos < bytes; curPos ++)
	{
		nibData[curPos * 2 + 0] = (byteData[curPos] >> 4) & 0x0F;
		nibData[curPos * 2 + 1] = (byteData[curPos] >> 0) & 0x0F;
	}
	
	return;
}

static void GsdPartParam2BulkDump(UINT8* bulkData, const UINT8* partData)
{
	UINT8 partMem[0x70];
	UINT8 curPos;
	UINT8 curCtrl;
	
	partMem[0x00] = partData[0x00];	// Bank MSB
	partMem[0x01] = partData[0x01];	// tone number
	
	// Rx. Pitch Bend/Ch. Pressure/Program Change/Control Change/Poly Pressure/Note Message/RPN/NRPN
	partMem[0x02] = 0x00;
	for (curPos = 0; curPos < 8; curPos ++)
		partMem[0x02] |= (partData[0x03 + curPos] & 0x01) << (7 - curPos);
	// Rx. Modulation/Volume/Panpot/Expression/Hold 1 (Sustain)/Portamento/SostenutoSoft Pedal
	partMem[0x03] = 0x00;
	for (curPos = 0; curPos < 8; curPos ++)
		partMem[0x03] |= (partData[0x0B + curPos] & 0x01) << (7 - curPos);
	partMem[0x04] = partData[0x02];	// Rx. Channel
	
	partMem[0x05] = (partData[0x13] & 0x01) << 7;	// Mono/Poly Mode
	partMem[0x05] |= ((partData[0x15] & 0x03) << 5) | (partData[0x15] ? 0x10 : 0x00);	// Rhythm Part Mode
	partMem[0x05] |= (partData[0x14] & 0x03) << 0;	// Assign Mode
	
	partMem[0x06] = partData[0x16];	// Pitch Key Shift
	partMem[0x07] = ((partData[0x17] & 0x0F) << 4) | ((partData[0x18] & 0x0F) << 0);	// Pitch Offset Fine
	partMem[0x08] = partData[0x19];	// Part Level
	partMem[0x09] = partData[0x1C];	// Part Panpot
	partMem[0x0A] = partData[0x1B];	// Velocity Sense Offset
	partMem[0x0B] = partData[0x1A];	// Velocity Sense Depth
	partMem[0x0C] = partData[0x1D];	// Key Range Low
	partMem[0x0D] = partData[0x1E];	// Key Range High
	
	// Chorus Send Depth/Reverb Send Depth/Tone Modify 1-8
	for (curPos = 0x00; curPos < 0x0A; curPos ++)
		partMem[0x0E + curPos] = partData[0x21 + curPos];
	partMem[0x18] = 0x00;
	partMem[0x19] = 0x00;
	// Scale Tuning C to B
	for (curPos = 0x00; curPos < 0x0C; curPos ++)
		partMem[0x1A + curPos] = partData[0x2B + curPos];
	partMem[0x26] = partData[0x1F];	// CC1 Controller Number
	partMem[0x27] = partData[0x20];	// CC2 Controller Number
	
	// Destination Controllers
	for (curCtrl = 0; curCtrl < 6; curCtrl ++)
	{
		UINT8 srcPos = 0x37 + curCtrl * 0x0B;
		UINT8 dstPos = 0x28 + curCtrl * 0x0C;
		for (curPos = 0x00; curPos < 0x03; curPos ++)
			partMem[dstPos + 0x00 + curPos] = partData[srcPos + 0x00 + curPos];
		partMem[dstPos + 0x03] = (curCtrl == 2 || curCtrl == 3) ? 0x40 : 0x00;	// verified with Recomposer 3.0 PC-98
		for (curPos = 0x00; curPos < 0x08; curPos ++)
			partMem[dstPos + 0x04 + curPos] = partData[srcPos + 0x03 + curPos];
	}
	
	Bytes2NibblesHL(0x70, bulkData, partMem);
	return;
}

static UINT8 Gsd2MidTrk(const GSD_INFO* gsdInf, FILE_INF* fInf, MID_TRK_STATE* MTS, UINT8 mode)
{
	static const UINT8 PART2CHN[0x10] =
		{0x09, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
	static const UINT8 CHN2PART[0x10] =
		{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
	UINT8 voiceRes[0x10];
	UINT8 bulkBuffer[0x100];
	UINT8 curChn;
	
	// The order follows how Recomposer 3.0 sends the data.
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "SC-55 Common Settings");
	// Recomposer 3.0 sends Master Volume (40 00 04), Key-Shift (40 00 06) and Pan (via GM SysEx) separately,
	// but doing a bulk-dump works just fine on SC-55/88.
	WriteRolandSyxData(fInf, MTS, SC55_SYX_HDR, 0x400000, 0x07, gsdInf->sysParams, SYXOPT_DELAY);
	
	for (curChn = 0x00; curChn < 0x10; curChn ++)
		voiceRes[curChn] = gsdInf->partParams[PART2CHN[curChn] * 0x7A + 0x79];
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "SC-55 Voice Reserve");
	WriteRolandSyxData(fInf, MTS, SC55_SYX_HDR, 0x400110, 0x10, voiceRes, SYXOPT_DELAY);
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "SC-55 Reverb Settings");
	WriteRolandSyxData(fInf, MTS, SC55_SYX_HDR, 0x400130, 0x07, gsdInf->reverbParams, SYXOPT_DELAY);
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "SC-55 Chorus Settings");
	WriteRolandSyxData(fInf, MTS, SC55_SYX_HDR, 0x400138, 0x08, gsdInf->chorusParams, SYXOPT_DELAY);
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "SC-55 Part Settings");
	for (curChn = 0x00; curChn < 0x10; curChn ++)
	{
		UINT32 addrOfs = 0x90 + CHN2PART[curChn] * 0xE0;
		UINT32 syxAddr = ((addrOfs & 0x007F) << 0) | ((addrOfs & 0x3F80) << 1);
		GsdPartParam2BulkDump(bulkBuffer, &gsdInf->partParams[curChn * 0x7A]);
		WriteRolandSyxBulk(fInf, MTS, SC55_SYX_HDR, 0x480000 | syxAddr, 0xE0, bulkBuffer, 0x80, SYXOPT_DELAY);
	}
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "SC-55 Drum Setup");
	for (curChn = 0; curChn < 2; curChn ++)	// 2 drum maps
	{
		// drum level, pan, reverb, chorus
		static const UINT8 DRMPAR_ADDR[4] = {0x02, 0x06, 0x08, 0x0A};
		const UINT8* drmPtr = &gsdInf->drumSetup[curChn * 0x014C];
		UINT8 curNote;
		UINT8 curParam;
		UINT8 paramBuf[0x80];
		
		for (curParam = 0; curParam < 4; curParam ++)
		{
			UINT32 syxAddr = (curChn << 12) | (DRMPAR_ADDR[curParam] << 8);
			
			memset(paramBuf, 0x00, 0x80);
			for (curNote = 0x00; curNote < 82; curNote ++)
				paramBuf[27 + curNote] = drmPtr[curNote * 4 + curParam];
			Bytes2NibblesHL(0x80, bulkBuffer, paramBuf);
			WriteRolandSyxBulk(fInf, MTS, SC55_SYX_HDR, 0x490000 | syxAddr, 0x100, bulkBuffer, 0x80, SYXOPT_DELAY);
		}
	}
	
	// Recomposer 3.0 doesn't seem to send SysEx for the additional Master Tuning settings.
	//gsdInf->masterTune;
	
	if (mode & 0x10)
		WriteMetaEventFromStr(fInf, MTS, 0x01, "Setup Finished.");
	
	return 0x00;
}

UINT8 Control2Mid(const FILE_DATA* ctrlFile, FILE_DATA* midFile, UINT8 fileType, UINT8 outMode)
{
	UINT8 retVal;
	FILE_INF midFInf;
	MID_TRK_STATE MTS;
	CM6_INFO cm6Inf;
	GSD_INFO gsdInf;
	
	if (fileType == 0x10)
	{
		retVal = ParseCM6File(ctrlFile, &cm6Inf);
		if (retVal)
			return retVal;
		printf("CM6 Control File, %s mode\n", cm6Inf.deviceType ? "CM-64" : "MT-32");
	}
	else if (fileType == 0x11)
	{
		retVal = ParseGSDFile(ctrlFile, &gsdInf);
		if (retVal)
			return retVal;
		printf("GSD Control File\n");
	}
	else
	{
		return 0xFF;
	}
	
	midFInf.alloc = 0x10000;	// 64 KB should be enough
	midFInf.data = (UINT8*)malloc(midFInf.alloc);
	midFInf.pos = 0x00;
	
	if (outMode & 0x01)	// MIDI mode
	{
		midiTickRes = 48;
		WriteMidiHeader(&midFInf, 0x0001, 1, midiTickRes);
		
		WriteMidiTrackStart(&midFInf, &MTS);
		midiTickCount = 0;
		
		if (fileType == 0x10)
			retVal = Cm62MidTrk(&cm6Inf, &midFInf, &MTS, outMode);
		else
			retVal = Gsd2MidTrk(&gsdInf, &midFInf, &MTS, outMode);
		
		WriteEvent(&midFInf, &MTS, 0xFF, 0x2F, 0x00);
		WriteMidiTrackEnd(&midFInf, &MTS);
	}
	else
	{
		if (fileType == 0x10)
			retVal = Cm62MidTrk(&cm6Inf, &midFInf, NULL, outMode);
		else
			retVal = Gsd2MidTrk(&gsdInf, &midFInf, NULL, outMode);
	}
	
	midFile->data = midFInf.data;
	midFile->len = midFInf.pos;
	
	return retVal;
}


INLINE UINT32 MulDivCeil(UINT32 val, UINT32 mul, UINT32 div)
{
	return (UINT32)( ((UINT64)val * mul + div - 1) / div );
}

INLINE UINT32 MulDivRound(UINT32 val, UINT32 mul, UINT32 div)
{
	return (UINT32)( ((UINT64)val * mul + div / 2) / div );
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}

INLINE UINT32 ReadLE32(const UINT8* data)
{
	return	(data[0x03] << 24) | (data[0x02] << 16) |
			(data[0x01] <<  8) | (data[0x00] <<  0);
}
