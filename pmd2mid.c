// P.M.D -> Midi Converter
// -----------------------
// Written by Valley Bell

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <stdtype.h>


#include "midi_funcs.h"


UINT8 PMD2Mid(UINT8 fileVer, UINT16 songLen, UINT8* songData);

static UINT8 WriteFileData(UINT32 DataLen, const UINT8* Data, const char* FileName);
static UINT8 PMDVol2Mid(UINT8 TrkMode, UINT8 Vol, UINT8 PanBoost);
static double OPNVol2DB(UINT8 TL);
static double PSGVol2DB(UINT8 Vol);
static UINT8 DB2Mid(double DB);
static UINT32 Tempo2Mid(UINT8 TempoVal);
static UINT8 PanBits2MidiPan(UINT8 Pan);

static UINT16 ReadBE16(const UINT8* Data);
static UINT32 ReadBE32(const UINT8* Data);
static UINT16 ReadLE16(const UINT8* Data);
static void WriteBE32(UINT8* Buffer, UINT32 Value);
static void WriteBE16(UINT8* Buffer, UINT16 Value);


static UINT32 ROMLen;
static UINT8* ROMData;
static UINT32 MidLen;
static UINT8* MidData;

static UINT16 MIDI_RES = 24;
static UINT16 NUM_LOOPS = 2;

int main(int argc, char* argv[])
{
	FILE* hFile;
	char* StrPtr;
	
	printf("P.M.D. -> Midi Converter\n------------------------------\n");
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

UINT8 PMD2Mid(UINT8 fileVer, UINT16 songLen, UINT8* songData)
{
	UINT16 trkPtrs[11];
	const UINT16* rhythmPtrs;
	UINT16 fmInsPtr;
	char trkName[0x10];
	
	UINT8 trkCnt;
	UINT8 curTrk;
	UINT16 inPos;
	UINT16 rhyStackPos;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	UINT8 trkEnd;
	UINT8 curCmd;
	UINT8 trkMode;	// 00 - FM, 01 - SSG, 02 - ADPCM, 03 - Rhythm
	INT8 noteMove;
	
	UINT16 mstLoopPos;
	UINT8 mstLoopCnt;
	UINT8 curTempo;
	UINT8 curTpQ;
	
	UINT8 tempByt;
	//UINT16 tempSht;
	INT16 tempSSht;
	UINT16 tempPos;
	UINT32 tempLng;
	UINT8 tempArr[4];
	
	INT8 curTransp;
	UINT8 curNote;
	UINT8 curNoteVol;
	UINT8 curChnVol;
	UINT8 chnPanOn;
	UINT8 lastNote;
	UINT8 holdNote;
	UINT8 portaNote;
	UINT8 tempVol;
	
	if (fileVer >= 0x10)
		return 0x80;	// invalid file version
	
	trkCnt = 11;	// 6xFM + 3xSSG + 1xADPCM + 1xRhythm
	inPos = 0x00;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++, inPos += 0x02)
		trkPtrs[curTrk] = ReadLE16(&songData[inPos]);
	tempPos = ReadLE16(&songData[inPos]);	inPos += 0x02;
	rhythmPtrs = (UINT16*)&songData[tempPos];
	fmInsPtr = ReadLE16(&songData[inPos]);	inPos += 0x02;
	if (trkPtrs[0] != 0x001A)
		return 0x81;	// invalid header size
	
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x0001, trkCnt, MIDI_RES);
	
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		inPos = trkPtrs[curTrk];
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (curTrk < 6)
		{
			sprintf(trkName, "FM %u", 1 + (curTrk - 0));
			trkMode = 0;
			MTS.midChn = 0 + (curTrk - 0);
			noteMove = +12 + 12;
			curChnVol = 0x6C;
		}
		else if (curTrk < 9)
		{
			sprintf(trkName, "SSG %u", 1 + (curTrk - 6));
			trkMode = 1;
			MTS.midChn = 10 + (curTrk - 6);
			noteMove = +24;
			curChnVol = 0x08;
		}
		else if (curTrk < 10)
		{
			sprintf(trkName, "ADPCM");
			trkMode = 2;
			MTS.midChn = 8;
			noteMove = 0;
		}
		else //if (curTrk < 11)
		{
			sprintf(trkName, "Rhythm");
			trkMode = 3;
			MTS.midChn = 9;
			noteMove = 0;
		}
		rhyStackPos = 0x0000;
		mstLoopCnt = 0;
		mstLoopPos = 0x0000;
		
		curTempo = 200;
		curTpQ = 0x112C / (0x100 - curTempo);
		curTransp = 0;
		curNote = 0xFF;
		lastNote = 0xFF;
		curNoteVol = 0x7F;
		chnPanOn = 0x00;
		holdNote = 0;
		portaNote = 0;
		tempVol = 0xFF;
		
		WriteMetaEvent(&midFileInf, &MTS, 0x03, strlen(trkName), trkName);
		if (trkMode == 1)
			WriteEvent(&midFileInf, &MTS, 0xC0, 0x50, 0x00);	// SSG: Square Lead
		else if (trkMode == 3)
			WriteEvent(&midFileInf, &MTS, 0xC0, 0x00, 0x00);	// drum channel
		//tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
		//WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
		
		trkEnd = 0;
		while(! trkEnd)
		{
			curCmd = songData[inPos];	inPos ++;
			if (trkMode == 3)	// special rhythm channel handling
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
			
			if (curCmd < 0x80)	// note or portamento
			{
				if (trkMode == 3)
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
						curNote += curTransp + noteMove;
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
				
				if (lastNote != curNote || ! holdNote)
				{
					if (holdNote)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x40);
					
					if (lastNote != 0xFF)
						WriteEvent(&midFileInf, &MTS, 0x90, lastNote, 0x00);
					if (curNote != 0xFF)
						WriteEvent(&midFileInf, &MTS, 0x90, curNote, curNoteVol);
					lastNote = curNote;
					
					if (holdNote && ! portaNote)
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);
				}
				holdNote = 0;
				
				MTS.curDly += songData[inPos];	inPos ++;
				if (portaNote)
				{
					portaNote = 0;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);	// Portamento Off
				}
				if (tempVol != 0xFF)
				{
					tempVol = 0xFF;
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
				}
			}
			else
			{
				switch(curCmd)
				{
				case 0x80:	// track end
					if (mstLoopPos)
					{
						mstLoopCnt ++;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, mstLoopCnt);
						if (mstLoopCnt < NUM_LOOPS)
						{
							inPos = mstLoopPos;
							break;
						}
					}
					trkEnd = 1;
					break;
				case 0xFF:	// set Instrument
					tempByt = songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xC0, tempByt & 0x7F, 0x00);
					break;
				case 0xFE:	// set QData??
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos]);
					inPos += 0x01;
					break;
				case 0xFD:	// Set Volume
					curChnVol = songData[inPos];	inPos ++;
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xFC:	// set/change Tempo
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos]);
					tempByt = songData[inPos];	inPos ++;
					if (tempByt < 0xFD)
					{
						// set Tempo
						curTempo = tempByt;
						curTpQ = 0x112C / (0x100 - curTempo);
					}
					else
					{
						curCmd = tempByt;
						tempByt = songData[inPos];	inPos ++;
						if (tempByt == 0xFD)
						{
							// set Ticks per Quarter
							curTpQ = tempByt;
							curTempo = 0x100 - (0x112C / curTpQ);
						}
						else if (tempByt == 0xFE)
						{
							// change Tempo
							curTempo += tempByt;
							curTpQ = 0x112C / (0x100 - curTempo);
						}
						else if (tempByt == 0xFF)
						{
							// change Ticks per Quarter
							curTpQ += tempByt;
							curTempo = 0x100 - (0x112C / curTpQ);
						}
					}
					tempLng = Tempo2Mid(curTempo);
					WriteBE32(tempArr, tempLng);
					WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &tempArr[0x01]);
					break;
				case 0xFB:	// Tie
					holdNote = 1;
					break;
				case 0xFA:	// Detune
					tempSSht = (INT16)ReadLE16(&songData[inPos]);	inPos += 0x02;
					tempSSht = 0x2000 - (tempSSht << 5);
					WriteEvent(&midFileInf, &MTS, 0xE0, (tempSSht >> 0) & 0x7F, (tempSSht >> 7) & 0x7F);
					break;
				case 0xF9:	// Loop Start
					tempPos = ReadLE16(&songData[inPos]);
					songData[tempPos + 0x01] = 0;	// initialize loop counter
					inPos += 0x02;
					break;
				case 0xF8:	// Loop End
					tempPos = inPos;
					songData[tempPos + 0x01] ++;
					if (songData[tempPos + 0x01] < songData[tempPos + 0x00])
						inPos = ReadLE16(&songData[tempPos + 0x02]) + 0x02;
					else
						inPos += 0x04;
					break;
				case 0xF7:	// Loop Exit
					tempPos = ReadLE16(&songData[inPos]);
					if (songData[tempPos + 0x01] == songData[tempPos + 0x00] - 1)
						inPos = tempPos + 0x04;	// jump to loop end
					else
						inPos += 0x02;	// just continue
					break;
				case 0xF6:	// Master Loop Start
					mstLoopPos = inPos;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, mstLoopCnt);
					break;
				case 0xF5:	// set Transposition
					curTransp = (INT8)songData[inPos];	inPos ++;
					break;
				case 0xF4:	// Volume Up (3 db)
					switch(trkMode)
					{
					case 0:	// FM
						curChnVol += 0x04;
						if (curChnVol > 0x7F)
							curChnVol = 0x7F;
						break;
					case 1:	// PSG
					case 3:	// Rhythm
						curChnVol += 0x01;
						if (curChnVol > 0x0F)
							curChnVol = 0x0F;
						break;
					case 2:	// ADPCM
						if (curChnVol >= 0xF0)
							curChnVol = 0xFF;
						else
							curChnVol += 0x10;
						break;
					}
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xF3:	// Volume Down (3 db)
					switch(trkMode)
					{
					case 0:	// FM
						tempByt = 0x04;
						break;
					case 1:	// PSG
					case 3:	// Rhythm
						tempByt = 0x01;
						break;
					case 2:	// ADPCM
						tempByt = 0x10;
						break;
					default:
						tempByt = 0x00;
						break;
					}
					if (curChnVol > tempByt)
						curChnVol -= tempByt;
					else
						curChnVol = 0x00;
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xF2:	// set Modulation
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x10, songData[inPos + 0x00]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x11, songData[inPos + 0x01]);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x12, songData[inPos + 0x02] & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x13, songData[inPos + 0x03] & 0x7F);
					inPos += 0x04;
					break;
				case 0xF1:	// set Modulation Mask #1
					tempByt = songData[inPos];	inPos ++;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, tempByt << 3);
					break;
				case 0xF0:	// set PSG Envelope
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					inPos += 0x04;
					break;
				case 0xEC:	// Pan
					tempByt = songData[inPos];	inPos ++;
					tempByt = PanBits2MidiPan(tempByt);
					chnPanOn = (tempByt == 0x40) ? 0x00 : 0x01;
					
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, tempByt);
					
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xE3:	// Volume Up
					tempByt = songData[inPos];	inPos ++;
					switch(trkMode)
					{
					case 0:	// FM
						if (curChnVol > 0x7F)
							curChnVol = 0x7F;
						curChnVol += tempByt;
						break;
					case 1:	// PSG
					case 3:	// Rhythm
						if (curChnVol > 0x0F)
							curChnVol = 0x0F;
						curChnVol += tempByt;
						break;
					default:
						if (curChnVol + tempByt > 0xFF)
							curChnVol = 0xFF;
						else
							curChnVol += tempByt;
						break;
					}
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xE2:	// Volume Down
					tempByt = songData[inPos];	inPos ++;
					if (curChnVol < tempByt)
						curChnVol = 0;
					else
						curChnVol -= tempByt;
					tempByt = PMDVol2Mid(trkMode, curChnVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xDE:	// temporary Volume Up
					tempByt = songData[inPos];	inPos ++;
					tempVol = curChnVol + tempByt;
					tempByt = PMDVol2Mid(trkMode, tempVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xDD:	// temporary Volume Down
					tempByt = songData[inPos];	inPos ++;
					tempVol = curChnVol - tempByt;
					tempByt = PMDVol2Mid(trkMode, tempVol, chnPanOn);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, tempByt);
					break;
				case 0xDA:	// Portamento
					portaNote = 1;
					tempByt = songData[inPos];	inPos ++;
					curNote = tempByt & 0x0F;
					curNote += (tempByt >> 4) * 12;
					curNote += curTransp + noteMove;
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x54, curNote & 0x7F);	// Portamento Control (Note)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x7F);	// Portamento On
					break;
				case 0xCF:	// set FM Slot Mask
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x26, songData[inPos]);
					inPos += 0x01;
					break;
				case 0xCD:	// set PSG Envelope (extended)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, curCmd & 0x7F);
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
					tempPos = ReadLE16(&songData[inPos + 0x00]);
					tempPos = ReadLE16(&songData[inPos + 0x02]);
					tempPos = ReadLE16(&songData[inPos + 0x04]);
					inPos += 0x06;
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
						printf("Unknown C0 event %02X on track %X at %04X\n", tempByt, curTrk, inPos - 0x02);
						inPos ++;
					}
					break;
				default:
					printf("Unknown event %02X on track %X at %04X\n", curCmd, curTrk, inPos - 0x01);
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x6E, curCmd & 0x7F);
					trkEnd = 1;
					break;
				}
			}
		}
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
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

static UINT8 PMDVol2Mid(UINT8 TrkMode, UINT8 Vol, UINT8 PanBoost)
{
	double DBVol;
	
	if (TrkMode == 0)
		DBVol = OPNVol2DB(Vol ^ 0x7F);
	else if (TrkMode == 1 || TrkMode == 3)
		DBVol = PSGVol2DB(Vol ^ 0x0F);
	/*else if (TrkMode == 2)
		DBVol = DeltaTVol2DB(Vol);
	else if (TrkMode == 3)
		DBVol = DeltaTVol2DB(Vol * 4);*/
	else
		return Vol / 2;
	if (PanBoost)
		DBVol -= 3.0;
	return DB2Mid(DBVol);
}

static double OPNVol2DB(UINT8 TL)
{
	return -(TL * 3 / 4.0f);
}

static double PSGVol2DB(UINT8 Vol)
{
	if (Vol < 0x0F)
		return Vol * -3.0;	// AY8910 volume is 3 db per step
	else
		return -999;
}

static UINT8 DB2Mid(double DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

static UINT32 Tempo2Mid(UINT8 TempoVal)
{
	// Note: The tempo value is the value of YM Timer B.
	// higher value = higher tick frequency = higher tempo
	
	// Base Clock = 2 MHz
	// Prescaler: 6 * 12
	// internal Timer Countdown: (100h - value) * 10h
	// Timer Frequency: Clock / (Countdown * Prescaler)
	double TicksPerSec;
	UINT16 TmrVal;
	
	TmrVal = (0x100 - TempoVal) << 4;
	TicksPerSec = 2000000.0 / (6 * 12 * TmrVal);
	return (UINT32)(500000 * MIDI_RES / TicksPerSec + 0.5);
}

static UINT8 PanBits2MidiPan(UINT8 Pan)
{
	switch(Pan & 0x03)
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

static UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}
