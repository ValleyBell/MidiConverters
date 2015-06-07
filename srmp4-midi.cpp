// Main File
#include <iostream>
#include <string>
#include "MidiLib.h"


void LoadBinData(const char* FileName);


MidiFile CMidi;

int main(int argc, char* argv[])
{
	std::cout << "SRMP4 Midi Converter\n";
	std::cout << "--------------------\n";
	if (argc < 3)
	{
		std::cout << "Usage: srmp4-midi input.bin output.mid\n";
#ifdef _DEBUG
		getchar();
#endif
		return 0;
	}
	
	UINT8 RetVal;
	
	std::cout << "Opening ...\n";
	LoadBinData(argv[1]);
	RetVal = 0;
	if (RetVal)
	{
		std::cout << "Error opening file!\n";
		std::cout << "Errorcode: " << RetVal;
		return RetVal;
	}
	
	std::cout << "Saving ...\n";
	RetVal = CMidi.SaveFile(argv[2]);
	if (RetVal)
	{
		std::cout << "Error saving file!\n";
		std::cout << "Errorcode: " << RetVal;
		return RetVal;
	}
	
	std::cout << "Cleaning ...\n";
	CMidi.ClearAll();
	std::cout << "Done.\n";
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}

static __inline UINT16 freadLE16(std::ifstream& infile)
{
	UINT8 InData[0x02];
	
	infile.read((char*)InData, 0x02);
	return (InData[0x00] << 0) | (InData[0x01] << 8);
}

static __inline UINT16 freadBE16(std::ifstream& infile)
{
	UINT8 InData[0x02];
	
	infile.read((char*)InData, 0x02);
	return (InData[0x00] << 8) | (InData[0x01] << 0);
}

static __inline UINT32 freadBE32(std::ifstream& infile)
{
	UINT8 InData[0x04];
	
	infile.read((char*)InData, 0x04);
	return  (InData[0x00] << 24) | (InData[0x01] << 16) |
			(InData[0x02] <<  8) | (InData[0x03] <<  0);
}

static __inline void WriteBE24(UINT8* buffer, UINT32 value)
{
	buffer[0x00] = (value & 0xFF0000) >> 16;
	buffer[0x01] = (value & 0x00FF00) >>  8;
	buffer[0x02] = (value & 0x0000FF) >>  0;
	
	return;
}

void LoadBinData(const char* FileName)
{
	std::ifstream infile;
	char tempFCC[4];
	MidiTrack* midTrk;
	size_t trkBasePos;
	size_t trkLength;
	UINT32 curTick;
	UINT8 curCmd;
	UINT8 paramBuf[3];
	bool trkEnd;
	UINT16 tempSht;
	UINT32 tempLng;
	char tempStr[0x20];
	
	infile.open(FileName, std::ios::in | std::ios::binary);
	if (! infile.is_open())
		return;
	
	infile.read(tempFCC, 0x04);
	if (strncmp(tempFCC, "SIFF", 4))
		return;
	freadBE32(infile);	// skip SIFF chunk length
	
	CMidi.ClearAll();
	infile.read(tempFCC, 0x04);
	if (strncmp(tempFCC, "seqs", 4))
		return;
	freadBE32(infile);	// skip seqs chunk length
	
	tempSht = freadLE16(infile);	// read 0x0001
	tempSht = freadLE16(infile);	// read 0x0016
	tempSht = freadLE16(infile);	// read 0x0000
	CMidi.SetMidiFormat(1);
	CMidi.SetMidiResolution(24);
	
	infile.read(tempFCC, 0x04);
	while(! strncmp(tempFCC, "trck", 4))
	{
		trkLength = freadBE32(infile);
		trkBasePos = infile.tellg();
		
		midTrk = CMidi.NewTrack_Append();
		curTick = 0;
		trkEnd = false;
		while(! trkEnd)
		{
			infile.read((char*)&curCmd, 1);
			switch(curCmd & 0xF0)
			{
			case 0x80:
				infile.read((char*)paramBuf, 1);
				midTrk->InsertEventT(curTick, curCmd, paramBuf[0], 0x40);
				break;
			case 0x90:
			case 0xA0:
			case 0xB0:
				infile.read((char*)paramBuf, 2);
				midTrk->InsertEventT(curTick, curCmd, paramBuf[0], paramBuf[1]);
				break;
			case 0xC0:
			case 0xD0:
				infile.read((char*)paramBuf, 1);
				midTrk->InsertEventT(curTick, curCmd, paramBuf[0], 0x00);
				break;
			case 0xE0:
				infile.read((char*)paramBuf, 1);
				midTrk->InsertEventT(curTick, curCmd, 0x00, paramBuf[0] - 0x40);
				break;
			case 0xF0:
				switch(curCmd)
				{
				case 0xF0:	// Delay
					infile.read((char*)paramBuf, 1);
					curTick += paramBuf[0];
					break;
				case 0xF1:	// Track End
					midTrk->InsertEventT(curTick, 0xFF, 0x2F, 0x00);
					trkEnd = true;
					break;
				case 0xF2:	// Tempo
					tempSht = freadLE16(infile);
					sprintf(tempStr, "Tempo Value: 0x%03X\n", tempSht);
					midTrk->InsertMetaEventT(curTick, 0x06, strlen(tempStr), tempStr);
					
					tempLng = tempSht * 0x800;
					WriteBE24(paramBuf, tempLng);
					midTrk->InsertMetaEventT(curTick, 0x51, 0x03, paramBuf);
					break;
				}
				break;
			}
		}
		
		infile.seekg(trkBasePos + trkLength, std::ios::beg);
		infile.read(tempFCC, 0x04);
	}
	if (strncmp(tempFCC, "tend", 4))
		std::cout << "Sequence not terminated by tend!\n";
	infile.close();
	
	return;
}
