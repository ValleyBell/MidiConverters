// P.M.D -> Midi Converter
// -----------------------
// Written by Valley Bell

#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

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


#include "midi_funcs.h"


UINT8 PMD2Mid(UINT8 fileVer, UINT16 songLen, UINT8* songData);

static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);
static UINT8 PMDVol2Mid(UINT8 trkMode, UINT8 vol, UINT8 panBoost);
INLINE double OPNVol2DB(UINT8 TL);
INLINE double SSGVol2DB(UINT8 vol);
INLINE double DeltaTVol2DB(UINT8 vol);
INLINE double RhythmVol2DB(UINT8 vol);
INLINE UINT8 DB2Mid(double db);
INLINE UINT32 Tempo2Mid(UINT8 tempoVal);
INLINE UINT8 PanBits2MidiPan(UINT8 pan);

INLINE UINT16 ReadLE16(const UINT8* data);


typedef struct track_info
{
	UINT16 dataOfs;
	UINT32 startTick;
} TRK_INFO;

#define TRKMODE_FM		0
#define TRKMODE_SSG		1
#define TRKMODE_ADPCM	2
#define TRKMODE_RHYTHM	3
#define TRKMODE_RHY_CHN	10	// OPN rhythm channel volume
#define TRKMODE_RHY_MST	11	// OPN rhythm master volume
#define CHNFLAG_HOLD	0x01
#define CHNFLAG_PORTA	0x02
#define CHNFLAG_VOL_ONE	0x10
#define CHNFLAG_STOP	0x80
typedef struct channel_info
{
	UINT8 trkMode;
	UINT8 flags;
	INT8 transp;
	UINT8 volume;
	UINT8 volOnce;
	UINT8 panOn;
	INT16 detune;
	UINT8 earlyOff;
	
	UINT16 mstLoopPos;
	UINT8 mstLoopCnt;
	UINT8 tempo;
	UINT8 temp48;
	
	UINT8 rhythmKeyMask;
	UINT8 rhythmMstVol;
	UINT8 rhythmVol[6];
	UINT8 rhythmPanOn[6];
} CHN_INF;


static const UINT8 OPNA_RHYTHM_NOTES[6] = {0x23, 0x26, 0x33, 0x2A, 0x2D, 0x27};


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 24;
static UINT16 NUM_LOOPS = 2;
static UINT8 VOL_MODE = 0;	// 0  - Controller: Main Volume, 1 - Note Velocity
static double VOL_BOOST = 0.0;

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	
	printf("P.M.D. -> Midi Converter\n------------------------\n");
	if (argc < 3)
	{
		printf("Usage: pmd2mid.exe Options input.bin output.mid\n");
		printf("Options: [none current]\n");
		return 0;
	}
	
	StrPtr = argv[1];
	while(*StrPtr != '\0')
	{
		switch(toupper(*StrPtr))
		{
		case 'R':
			break;
		case 'V':
			VOL_MODE = 1;
			break;
		}
		StrPtr ++;
	}
	
	hFile = fopen(argv[2], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	ROMLen = ftell(hFile);
	if (ROMLen > 0xFFFF)	// 64 KB
		ROMLen = 0xFFFF;
	
	fseek(hFile, 0x00, SEEK_SET);
	ROMData = (UINT8*)malloc(ROMLen);
	fread(ROMData, 0x01, ROMLen, hFile);
	
	fclose(hFile);
	
	PMD2Mid(ROMData[0], ROMLen - 1, &ROMData[1]);
	WriteFileData(MidLen, MidData, argv[3]);
	free(MidData);	MidData = NULL;
	
	printf("Done.\n");
	
	free(ROMData);	ROMData = NULL;
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

/* Internal Track IDs:
	0-5: FM 1-6
	6-8: SSG 1-3
	9: ADPCM
	10: Rhythm
	12-14: FM 3 EX 1-3
*/
UINT8 PMD2Mid(UINT8 fileVer, UINT16 songLen, UINT8* songData)
{
	TRK_INFO trkInf[11];
	TRK_INFO extFM3Inf[3];
	UINT8 trkIDTbl[0x20];
	const UINT16* rhythmPtrs;
	UINT16 fmInsPtr;
	char trkName[0x10];
	
	TRK_INFO* curTInf;
	UINT8 trkCnt;
	UINT8 trkID;
	UINT8 curTrk;
	UINT16 inPos;
	UINT16 rhyStackPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 curCmd;
	INT8 noteMove;
	
	UINT8 tempByt;
	//UINT16 tempSht;
	INT16 tempSSht;
	UINT16 tempPos;
	UINT32 tempLng;
	UINT8 tempArr[4];
	UINT8 tempVol;
	
	CHN_INF chnInf;
	UINT8 curNote;
	UINT8 curNoteVol;
	UINT8 curDly;
	UINT8 lastNote;
	UINT8 didInitCmds;
	
	if (fileVer >= 0x10)
		return 0x80;	// invalid file version
	
	memset(trkIDTbl, 0xFF, 0x20);
	trkCnt = 11;	// 6xFM + 3xSSG + 1xADPCM + 1xRhythm
	inPos = 0x00;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
	{
		trkInf[curTrk].dataOfs = ReadLE16(&songData[inPos]);
		trkInf[curTrk].startTick = 0;
		trkIDTbl[curTrk] = curTrk;
	}
	tempPos = ReadLE16(&songData[inPos]);	inPos += 0x02;
	rhythmPtrs = (UINT16*)&songData[tempPos];
	fmInsPtr = ReadLE16(&songData[inPos]);	inPos += 0x02;
	if (trkInf[0].dataOfs != 0x001A)
		return 0x81;	// invalid header size
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	// init those once, since parameters are shared between tracks
	chnInf.rhythmMstVol = 0x3C;
	for (curNote = 0; curNote < 6; curNote ++)
	{
		chnInf.rhythmVol[curNote] = 0x0F;
		chnInf.rhythmPanOn[curNote] = 0x00;
	}
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		trkID = trkIDTbl[curTrk];
		if (trkID == 0xFF)
			break;
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		curTInf = &trkInf[trkID];
		if (fileVer == 0x02 && trkID < 11)
		{
			// PMD IBM patch
			sprintf(trkName, "FM %u", 1 + (trkID - 0));
			MTS.midChn = 0 + (trkID - 0);
			chnInf.trkMode = TRKMODE_FM;
			noteMove = +12;
			chnInf.volume = 0x6C;
		}
		else if (trkID < 6 || (trkID >= 12 && trkID < 15))
		{
			if (trkID >= 12 && trkID < 15)
			{
				sprintf(trkName, "FM 3 EX %u", 1 + (trkID - 12));
				MTS.midChn = 6 + (trkID - 12);
				curTInf = &extFM3Inf[trkID - 12];
			}
			else
			{
				sprintf(trkName, "FM %u", 1 + (trkID - 0));
				MTS.midChn = 0 + (trkID - 0);
			}
			chnInf.trkMode = TRKMODE_FM;
			noteMove = +12;
			chnInf.volume = 0x6C;
		}
		else if (trkID < 9)
		{
			sprintf(trkName, "SSG %u", 1 + (trkID - 6));
			chnInf.trkMode = TRKMODE_SSG;
			MTS.midChn = 10 + (trkID - 6);
			noteMove = +24;
			chnInf.volume = 0x08;
		}
		else if (trkID < 10)
		{
			sprintf(trkName, "ADPCM");
			chnInf.trkMode = TRKMODE_ADPCM;
			MTS.midChn = 8;
			noteMove = 0;
		}
		else //if (trkID < 11)
		{
			sprintf(trkName, "Rhythm");
			chnInf.trkMode = TRKMODE_RHYTHM;
			MTS.midChn = 9;
			noteMove = 0;
		}
		inPos = curTInf->dataOfs;
		MTS.curDly = curTInf->startTick;
		rhyStackPos = 0x0000;
		chnInf.mstLoopCnt = 0;
		chnInf.mstLoopPos = 0x0000;
		
		chnInf.tempo = 200;
		chnInf.temp48 = 0x112C / (0x100 - chnInf.tempo);
		chnInf.transp = 0;
		curNote = 0xFF;
		lastNote = 0xFF;
		curNoteVol = 0x7F;
		chnInf.panOn = 0x00;
		chnInf.flags = 0x00;
		chnInf.detune = 0;
		didInitCmds = 0x00;
		chnInf.volOnce = 0xFF;
		chnInf.earlyOff = 0;
		chnInf.rhythmKeyMask = 0x00;
		if (inPos == 0x0000 || songData[inPos] == 0x80)
			chnInf.flags |= CHNFLAG_STOP;
		
		WriteMetaEvent(&midFileInf, &MTS, 0x03, strlen(trkName), trkName);
		if (! (chnInf.flags & CHNFLAG_STOP))
		{
			if (chnInf.trkMode == TRKMODE_SSG)
				WriteEvent(&midFileInf, &MTS, 0xC0, 0x50, 0x00);	// SSG: Square Lead
			else if (chnInf.trkMode == TRKMODE_RHYTHM)
				WriteEvent(&midFileInf, &MTS, 0xC0, 0x00, 0x00);	// drum channel
			//tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
			//WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
			if (VOL_MODE == 0x01)
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, 0x7F);
			WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, 0x40);	// center panning
		}
		
		while(! (chnInf.flags & CHNFLAG_STOP))
		{
			curCmd = songData[inPos];	inPos ++;
			if (chnInf.trkMode == TRKMODE_RHYTHM)	// special rhythm channel handling
			{
				if (! rhyStackPos)
				{
					if (curCmd < 0x80)
					{
						// execute rhythm subroutine
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x3F, curCmd);
						rhyStackPos = inPos;
						inPos = rhythmPtrs[curCmd];
						continue;
					}
					// else fall through and do command handling
				}
				else
				{
					// inside rhythm subroutine
					if (curCmd == 0xFF)
					{
						// Rhythm Subroutine Return
						inPos = rhyStackPos;
						rhyStackPos = 0x0000;
						continue;
					}
					else if (curCmd < 0x80)
					{
						curNote = 0xFF;	// rest
					}
					else if (curCmd < 0xC0)
					{
						tempSSht = ((curCmd << 8) | (songData[inPos] << 0)) & 0x3FFF;
						inPos ++;
						curCmd &= 0x7F;
						curNote = tempSSht & 0x7F;
					}
					else
					{
						// do usual command handling
					}
				}
			}
			
			if (curCmd < 0x80)	// note
			{
				if (chnInf.trkMode == TRKMODE_RHYTHM)
				{
					if (curNote == 0x00)
						curNote = 0x2A;
					else if (curNote == 0x01)
						curNote = 0x24;
					else if (curNote == 0x02)
						curNote = 0x28;
					else if (curNote == 0x1A)
						curNote = 0x26;
				}
				else
				{
					curNote = curCmd & 0x0F;
					if (curNote < 0x0C)
					{
						curNote += (curCmd >> 4) * 12;
						curNote += chnInf.transp + noteMove;
					}
					else if (curNote == 0x0F)
					{
						curNote = 0xFF;
					}
					else
					{
						if (curNote == 0xFF)
							printf("Warning: Invalid Note %02X!\n", curCmd);
					}
				}
				
				if (lastNote != curNote || ! (chnInf.flags & CHNFLAG_HOLD))
				{
					if (chnInf.flags & CHNFLAG_HOLD)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x40);
					
					if (lastNote != 0xFF)
						WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
					if (curNote != 0xFF)
						WriteEvent(&midFileInf, &MTS, 0x90, curNote, curNoteVol);
					lastNote = curNote;
					
					if ((chnInf.flags & CHNFLAG_HOLD) && ! (chnInf.flags & CHNFLAG_PORTA))
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);
				}
				chnInf.flags &= ~CHNFLAG_HOLD;
				
				curDly = songData[inPos];	inPos ++;
				if (chnInf.earlyOff && songData[inPos] != 0xFB && songData[inPos] != 0xC1)
				{
					// The PMD driver instantly cuts of the note when earlyOff >= noteLen. (used by RUSTY/MUSS.M)
					tempByt = (chnInf.earlyOff < curDly) ? chnInf.earlyOff : (curDly - 1);
					MTS.curDly += (curDly - tempByt);
					if (lastNote != 0xFF)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
						lastNote = 0xFF;
					}
					MTS.curDly += tempByt;
				}
				else
				{
					MTS.curDly += curDly;
				}
				if (chnInf.flags & CHNFLAG_PORTA)
				{
					chnInf.flags &= ~CHNFLAG_PORTA;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);	// Portamento Off
				}
				if (chnInf.flags & CHNFLAG_VOL_ONE)
				{
					chnInf.flags &= ~CHNFLAG_VOL_ONE;
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
				}
			}
			else
			{
				switch(curCmd)
				{
				case 0x80:	// track end
					if (chnInf.mstLoopPos)
					{
						chnInf.mstLoopCnt ++;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, chnInf.mstLoopCnt);
						if (chnInf.mstLoopCnt < NUM_LOOPS)
						{
							inPos = chnInf.mstLoopPos;
							break;
						}
					}
					chnInf.flags |= CHNFLAG_STOP;
					break;
				case 0xFF:	// set Instrument
					tempByt = songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt & 0x7F, 0x00);
					break;
				case 0xFE:	// set Early Key Off
					chnInf.earlyOff = songData[inPos];
					if (chnInf.earlyOff)
						printf("Track %u at %04X: Early Key Off = %02X\n", trkID, inPos - 0x01, chnInf.earlyOff);
					if (0)
					{
						if (! (didInitCmds & 0x04))
						{
							didInitCmds |= 0x04;
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x29, 0x01);	// SMPS Note Timeout: Reverse
						}
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x09, chnInf.earlyOff);
						chnInf.earlyOff = 0;
					}
					inPos += 0x01;
					break;
				case 0xFD:	// set Volume
					chnInf.volume = songData[inPos];	inPos ++;
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xFC:	// set/change Tempo
					tempByt = songData[inPos];	inPos ++;
					if (tempByt < 0xFD)
					{
						// set Tempo
						chnInf.tempo = tempByt;
						chnInf.temp48 = 0x112C / (0x100 - chnInf.tempo);
					}
					else
					{
						printf("Special Tempo Event track %u at %04X\n", trkID, inPos - 0x02);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, tempByt);
						curCmd = tempByt;
						tempByt = songData[inPos];	inPos ++;
						if (tempByt == 0xFD)
						{
							// set Ticks per Quarter
							chnInf.temp48 = tempByt;
							chnInf.tempo = 0x100 - (0x112C / chnInf.temp48);
						}
						else if (tempByt == 0xFE)
						{
							// change Tempo
							chnInf.tempo += tempByt;
							chnInf.temp48 = 0x112C / (0x100 - chnInf.tempo);
						}
						else if (tempByt == 0xFF)
						{
							// change Ticks per Quarter
							chnInf.temp48 += tempByt;
							chnInf.tempo = 0x100 - (0x112C / chnInf.temp48);
						}
					}
					tempLng = Tempo2Mid(chnInf.tempo);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					break;
				case 0xFB:	// Tie
					chnInf.flags |= CHNFLAG_HOLD;
					break;
				case 0xFA:	// Detune
					chnInf.detune = (INT16)ReadLE16(&songData[inPos]);	inPos += 0x02;
					if (chnInf.trkMode == TRKMODE_SSG)
						tempSSht = 0x2000 + (chnInf.detune << 5);
					else
						tempSSht = 0x2000 + (chnInf.detune << 7);
					WriteEvent(&midFileInf, &MTS, 0xE0, (tempSSht >> 0) & 0x7F, (tempSSht >> 7) & 0x7F);
					break;
				case 0xF9:	// Loop Start
					tempPos = ReadLE16(&songData[inPos]);
					songData[tempPos + 0x01] = 0;	// initialize loop counter
					if (songData[tempPos + 0x00] == 0x00)	// check for alternate Master Loop
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, songData[tempPos + 0x01]);
					inPos += 0x02;
					break;
				case 0xF8:	// Loop End
					tempPos = inPos;
					songData[tempPos + 0x01] ++;
					if (songData[tempPos + 0x00] == 0x00)	// check for alternate Master Loop
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, songData[tempPos + 0x01]);
						if (songData[tempPos + 0x01] < NUM_LOOPS)
							inPos = ReadLE16(&songData[tempPos + 0x02]) + 0x02;
						else
							inPos += 0x04;
					}
					else
					{
						if (songData[tempPos + 0x01] < songData[tempPos + 0x00])
							inPos = ReadLE16(&songData[tempPos + 0x02]) + 0x02;
						else
							inPos += 0x04;
					}
					break;
				case 0xF7:	// Loop Exit
					tempPos = ReadLE16(&songData[inPos]);
					if (songData[tempPos + 0x01] == songData[tempPos + 0x00] - 1)
						inPos = tempPos + 0x04;	// jump to loop end
					else
						inPos += 0x02;	// just continue
					break;
				case 0xF6:	// Master Loop Start
					chnInf.mstLoopPos = inPos;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, chnInf.mstLoopCnt);
					break;
				case 0xF5:	// set Transposition
					chnInf.transp = (INT8)songData[inPos];	inPos ++;
					break;
				case 0xF4:	// Volume Up (3 db)
					switch(chnInf.trkMode)
					{
					case TRKMODE_FM:
						chnInf.volume += 0x04;
						if (chnInf.volume > 0x7F)
							chnInf.volume = 0x7F;
						break;
					case TRKMODE_SSG:
					case TRKMODE_RHYTHM:
						chnInf.volume += 0x01;
						if (chnInf.volume > 0x0F)
							chnInf.volume = 0x0F;
						break;
					case TRKMODE_ADPCM:
						if (chnInf.volume >= 0xF0)
							chnInf.volume = 0xFF;
						else
							chnInf.volume += 0x10;
						break;
					}
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xF3:	// Volume Down (3 db)
					switch(chnInf.trkMode)
					{
					case TRKMODE_FM:
						tempByt = 0x04;
						break;
					case TRKMODE_SSG:
					case TRKMODE_RHYTHM:
						tempByt = 0x01;
						break;
					case TRKMODE_ADPCM:
						tempByt = 0x10;
						break;
					default:
						tempByt = 0x00;
						break;
					}
					if (chnInf.volume > tempByt)
						chnInf.volume -= tempByt;
					else
						chnInf.volume = 0x00;
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xF2:	// set Modulation
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x10, songData[inPos + 0x00]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x11, songData[inPos + 0x01]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x12, songData[inPos + 0x02] & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x13, songData[inPos + 0x03] & 0x7F);
					tempByt = songData[inPos + 0x02];
					if (tempByt < 0x10)
						tempByt <<= 3;
					else
						tempByt = 0x7F;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, tempByt);
					inPos += 0x04;
					break;
				case 0xF1:	// set Modulation Mask #1
					tempByt = songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x21, tempByt);
					break;
				case 0xF0:	// set PSG Envelope
					WriteEvent(&midFileInf, &MTS, 0xC0, 0x50, 0x00);
					inPos += 0x04;
					break;
				case 0xEE:	// set PSG Noise Frequency
					tempByt = songData[inPos];	inPos ++;
					tempByt += noteMove;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x54, tempByt);	// Portamento Control (Note)
					break;
				case 0xED:	// set PSG Noise Mask
					tempByt = songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x03, tempByt);
					break;
				case 0xEC:	// Pan
					tempByt = songData[inPos];	inPos ++;
					tempByt = PanBits2MidiPan(tempByt);
					chnInf.panOn = (tempByt == 0x40) ? 0x00 : 0x01;
					
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xEB:	// OPNA Rhythm Key On
					tempByt = songData[inPos];	inPos ++;
					if (tempByt & 0x80)
					{
						// key off
						UINT8 midChnBak = MTS.midChn;
						MTS.midChn = 0x09;
						for (curNote = 0; curNote < 6; curNote ++)
						{
							if (tempByt & (1 << curNote) & chnInf.rhythmKeyMask)
							{
								chnInf.rhythmKeyMask &= ~(1 << curNote);
								WriteEvent(&midFileInf, &MTS, 0x90, OPNA_RHYTHM_NOTES[curNote], 0x00);
							}
						}
						MTS.midChn = midChnBak;
					}
					else
					{
						// key on
						UINT8 midChnBak = MTS.midChn;
						MTS.midChn = 0x09;
						for (curNote = 0; curNote < 6; curNote ++)
						{
							if (tempByt & (1 << curNote))
							{
								if (chnInf.rhythmKeyMask & (1 << curNote))
									WriteEvent(&midFileInf, &MTS, 0x90, OPNA_RHYTHM_NOTES[curNote], 0x00);
								else
									chnInf.rhythmKeyMask |= (1 << curNote);
								tempVol = PMDVol2Mid(TRKMODE_RHY_CHN, chnInf.rhythmVol[curNote], chnInf.rhythmPanOn[curNote]);
								WriteEvent(&midFileInf, &MTS, 0x90, OPNA_RHYTHM_NOTES[curNote], tempVol);
							}
						}
						MTS.midChn = midChnBak;
					}
					break;
				case 0xEA:	// set OPNA Rhythm Volume
					tempByt = songData[inPos];	inPos ++;
					curNote = (tempByt >> 5) - 1;
					chnInf.rhythmVol[curNote] = tempByt & 0x1F;
					break;
				case 0xE9:	// set OPNA Rhythm Panning
					tempByt = songData[inPos];	inPos ++;
					curNote = (tempByt >> 5) - 1;
					tempByt = PanBits2MidiPan(tempByt & 0x03);
					chnInf.rhythmPanOn[curNote] = (tempByt == 0x40) ? 0x00 : 0x01;
					{
						UINT8 midChnBak = MTS.midChn;
						MTS.midChn = 0x09;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x63, 0x1C);	// NRPN MSB: 1C - Drum Pan
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x62, OPNA_RHYTHM_NOTES[curNote]);	// NRPN LSB: drum note
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x06, tempByt);	// Data MSB
						MTS.midChn = midChnBak;
					}
					break;
				case 0xE8:	// set OPNA Rhythm Master Volume
					chnInf.rhythmMstVol = songData[inPos];	inPos ++;
					{
						UINT8 midChnBak = MTS.midChn;
						MTS.midChn = 0x09;
						tempVol = PMDVol2Mid(TRKMODE_RHY_MST, chnInf.rhythmMstVol, 0x00);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempVol);	// Data MSB
						MTS.midChn = midChnBak;
					}
					break;
				case 0xE7:	// add to Transposition
					chnInf.transp += (INT8)songData[inPos];	inPos ++;
					break;
				case 0xE6:	// add to OPNA Rhythm Master Volume
					chnInf.rhythmMstVol += songData[inPos];	inPos ++;
					if (chnInf.rhythmMstVol >= 0x80)
						chnInf.rhythmMstVol = 0x00;
					else if (chnInf.rhythmMstVol > 0x3F)
						chnInf.rhythmMstVol = 0x3F;
					{
						UINT8 midChnBak = MTS.midChn;
						MTS.midChn = 0x09;
						tempVol = PMDVol2Mid(TRKMODE_RHY_MST, chnInf.rhythmMstVol, 0x00);
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempVol);	// Data MSB
						MTS.midChn = midChnBak;
					}
					break;
				case 0xE5:	// add to OPNA Rhythm Volume
					tempByt = songData[inPos];	inPos ++;
					curNote = (tempByt >> 5) - 1;
					chnInf.rhythmVol[curNote] += songData[inPos];	inPos ++;
					if (chnInf.rhythmVol[curNote] & 0x80)
						chnInf.rhythmVol[curNote] = 0x00;
					else if (chnInf.rhythmVol[curNote] > 0x1F)
						chnInf.rhythmVol[curNote] = 0x1F;
					break;
				case 0xE3:	// Volume Up
					tempByt = songData[inPos];	inPos ++;
					switch(chnInf.trkMode)
					{
					case TRKMODE_FM:
						if (chnInf.volume > 0x7F)
							chnInf.volume = 0x7F;
						chnInf.volume += tempByt;
						break;
					case TRKMODE_SSG:
					case TRKMODE_RHYTHM:
						if (chnInf.volume > 0x0F)
							chnInf.volume = 0x0F;
						chnInf.volume += tempByt;
						break;
					default:
						if (chnInf.volume + tempByt > 0xFF)
							chnInf.volume = 0xFF;
						else
							chnInf.volume += tempByt;
						break;
					}
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xE2:	// Volume Down
					tempByt = songData[inPos];	inPos ++;
					if (chnInf.volume < tempByt)
						chnInf.volume = 0;
					else
						chnInf.volume -= tempByt;
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volume, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xDE:	// temporary Volume Up
					tempByt = songData[inPos];	inPos ++;
					chnInf.flags |= CHNFLAG_VOL_ONE;
					chnInf.volOnce = chnInf.volume + tempByt;
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volOnce, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xDD:	// temporary Volume Down
					tempByt = songData[inPos];	inPos ++;
					chnInf.flags |= CHNFLAG_VOL_ONE;
					chnInf.volOnce = chnInf.volume - tempByt;
					tempByt = PMDVol2Mid(chnInf.trkMode, chnInf.volOnce, chnInf.panOn);
					if (VOL_MODE == 0x00)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					else
						curNoteVol = tempByt;
					break;
				case 0xDA:	// Portamento
					chnInf.flags |= CHNFLAG_PORTA;
					tempByt = songData[inPos];	inPos ++;
					curNote = tempByt & 0x0F;
					curNote += (tempByt >> 4) * 12;
					curNote += chnInf.transp + noteMove;
					if (! (didInitCmds & 0x02))
					{
						didInitCmds |= 0x02;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x05, 64);	// Portamento Time
					}
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x54, curNote & 0x7F);	// Portamento Control (Note)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x7F);	// Portamento On
					break;
				case 0xD6:	// ??
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos+0]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos+1]);
					inPos += 0x02;
					break;
				case 0xD5:	// add to Detune
					chnInf.detune += (INT16)ReadLE16(&songData[inPos]);	inPos += 0x02;
					if (chnInf.trkMode == TRKMODE_SSG)
						tempSSht = 0x2000 + (chnInf.detune << 5);
					else
						tempSSht = 0x2000 + (chnInf.detune << 7);
					WriteEvent(&midFileInf, &MTS, 0xE0, (tempSSht >> 0) & 0x7F, (tempSSht >> 7) & 0x7F);
					break;
				case 0xCF:	// set FM Slot Mask
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos]);
					inPos += 0x01;
					break;
				case 0xCD:	// set PSG Envelope (extended)
					WriteEvent(&midFileInf, &MTS, 0xC0, 0x50, 0x00);
					inPos += 0x05;
					break;
				case 0xCB:	// set Modulation Waveform
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos]);
					inPos += 0x01;
					break;
				case 0xCC:	// set PSG Modulation "Extend Mode"?
				case 0xCA:	// set PSG Modulation "Extend Mode"?
				case 0xC9:	// set PSG Modulation "Extend Mode"?
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos]);
					inPos += 0x01;
					break;
				case 0xC6:	// FM3 Extended Part Init
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x7F, 0x00);	// write Poly Mode On
					
					memmove(&trkIDTbl[6+3], &trkIDTbl[6+0], 0x20 - (6+3));
					trkCnt += 3;
					for (tempByt = 0; tempByt < 3; tempByt ++, inPos += 0x02)
					{
						extFM3Inf[tempByt].dataOfs = ReadLE16(&songData[inPos]);
						extFM3Inf[tempByt].startTick = 0;
						trkIDTbl[6 + tempByt] = 12 + tempByt;
					}
					break;
				case 0xC1:	// Early Key Off Ignore
					// used during note handling, nothing to do here
					break;
				case 0xC0:	// MML Part Mask
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos] & 0x7F);
					tempByt = songData[inPos];	inPos ++;
					if (tempByt < 2)
					{
						// ...
					}
					else
					{
						// special commands
						printf("Unknown C0 event %02X on track %u at %04X\n", tempByt, trkID, inPos - 0x02);
						inPos ++;
					}
					break;
				default:
					printf("Unknown event %02X on track %u at %04X\n", curCmd, trkID, inPos - 0x01);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					chnInf.flags |= CHNFLAG_STOP;
					break;
				}
			}
		}
		if (lastNote != 0xFF)
			WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
		if (chnInf.rhythmKeyMask)
		{
			MTS.midChn = 0x09;
			for (curNote = 0; curNote < 6; curNote ++)
			{
				if (chnInf.rhythmKeyMask & (1 << curNote))
					WriteEvent(&midFileInf, &MTS, 0x90, OPNA_RHYTHM_NOTES[curNote], 0x00);
			}
			chnInf.rhythmKeyMask = 0x00;
		}
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	WriteBE16(&midFileInf.data[0x08 + 0x02], curTrk);	// write final track count
	
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}


static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName)
{
	FILE* hFile;
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", FileName);
		return 0xFF;
	}
	
	fwrite(Data, 0x01, DataLen, hFile);
	fclose(hFile);
	
	return 0;
}

static UINT8 PMDVol2Mid(UINT8 trkMode, UINT8 vol, UINT8 panBoost)
{
	double dbVol;
	
	if (trkMode == TRKMODE_FM)
		dbVol = OPNVol2DB(~vol & 0x7F);
	else if (trkMode == TRKMODE_SSG)
		dbVol = SSGVol2DB(~vol & 0x0F);
	else if (trkMode == TRKMODE_ADPCM)
		dbVol = DeltaTVol2DB(vol);
	else if (trkMode == TRKMODE_RHY_CHN)
		dbVol = OPNVol2DB(~vol & 0x1F);
	else if (trkMode == TRKMODE_RHY_MST)
		dbVol = OPNVol2DB(~vol & 0x3F);
	else if (trkMode == TRKMODE_RHYTHM)
		dbVol = RhythmVol2DB(vol);
	else
		return vol / 2;
	if (panBoost && ! (trkMode == TRKMODE_SSG || trkMode == TRKMODE_RHYTHM))	// no panning for SSG channels
		dbVol -= 3.0;
	dbVol += VOL_BOOST;
	return DB2Mid(dbVol);
}

INLINE double OPNVol2DB(UINT8 TL)
{
	return -(TL * 3 / 4.0f);
}

INLINE double SSGVol2DB(UINT8 vol)
{
	if (vol < 0x0F)
		return vol * -3.0;	// AY8910 volume is 3 db per step
	else
		return -999;
}

INLINE double DeltaTVol2DB(UINT8 vol)
{
	return log(vol / 255.0) * (6.0 / M_LN2) + 6.0;	// boost its volume
}

INLINE double RhythmVol2DB(UINT8 vol)
{
	if (vol > 0x0F)
		vol = 0x0F;
	// PPS volume is linear
	// Values 0..2 are silent due to the SSG's low precision, but let's ignore that.
	return log(vol / 15.0) * (6.0 / M_LN2) - 6.0;	// lower the volume by 50%
}

INLINE UINT8 DB2Mid(double db)
{
	if (db > 0.0)
		db = 0.0;
	return (UINT8)(pow(10.0, db / 40.0) * 0x7F + 0.5);
}

INLINE UINT32 Tempo2Mid(UINT8 tempoVal)
{
	// Note: The tempo value is the value of YM Timer B.
	// higher value = higher tick frequency = higher tempo
	
	// Base Clock = 2 MHz
	// Prescaler: 6 * 12
	// internal Timer Countdown: (100h - value) * 10h
	// Timer Frequency: Clock / (Countdown * Prescaler)
	double ticksPerSec;
	UINT16 tmrVal;
	
	tmrVal = (0x100 - tempoVal) << 4;
	ticksPerSec = 2000000.0 / (6 * 12 * tmrVal);
	return (UINT32)(500000 * MIDI_RES / ticksPerSec + 0.5);
}

INLINE UINT8 PanBits2MidiPan(UINT8 pan)
{
	switch(pan & 0x03)
	{
	case 0x01:	// Right Channel
		return 0x7F;
	case 0x02:	// Left Channel
		return 0x00;
	case 0x03:	// Center
		return 0x40;
	case 0x00:	// no sound
	default:
		return 0x3F;
	}
}

INLINE UINT16 ReadLE16(const UINT8* data)
{
	return (data[0x01] << 8) | (data[0x00] << 0);
}
