#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>
#include <stddef.h>	// for NULL
#include <time.h>

#include "stdtype.h"
#include "Soundfont.h"


SF2_DATA* CreateSF2Base(const char* SoundfontName)
{
	SF2_DATA* SF2Data;
	LIST_CHUNK* LstInfo;
	LIST_CHUNK* LstSample;
	LIST_CHUNK* LstPreset;
	ITEM_CHUNK* CurItm;
	sfVersionTag SFVer;
	char TempStr[0x40];
	time_t CurTime;
	struct tm* TimeInfo;
	
	SF2Data = (SF2_DATA*)malloc(sizeof(SF2_DATA));
	if (SF2Data == NULL)
		return NULL;
	
	// generate Header
	SF2Data->fccRIFF = FCC_RIFF;
	SF2Data->RiffSize = 0x00;
	SF2Data->RiffType = FCC_sfbk;
	
	// Now generate the 3 LIST chunks.
	LstInfo = List_MakeChunk(FCC_INFO);
	LstSample = List_MakeChunk(FCC_sdta);
	LstPreset = List_MakeChunk(FCC_pdta);
	
	// Link them correctly and set the pointers in SF2Data.
	LstInfo->next = LstSample;
	LstSample->next = LstPreset;
	SF2Data->Lists = LstInfo;
	SF2Data->LastLst = LstPreset;
	
	
	// make the 'INFO' Items
	
	// Version (ifil)
	SFVer.wMajor = 2;
	SFVer.wMinor = 0;
	CurItm = Item_MakeChunk(FCC_ifil, sizeof(sfVersionTag), &SFVer, 0x01);
	List_AddItem(LstInfo, CurItm);
	
	// Sound Engine (isng)
	CurItm = Item_MakeChunk_String(FCC_isng, "EMU8000", 0x01);
	List_AddItem(LstInfo, CurItm);
	
	// Soundfont Name (INAM)
	CurItm = Item_MakeChunk_String(FCC_INAM, SoundfontName, 0x01);
	List_AddItem(LstInfo, CurItm);
	
	// Creation Date (ICRD)
	time(&CurTime);
	TimeInfo = localtime(&CurTime);
	strftime(TempStr, 0x40, "%#d %b %Y", TimeInfo);
	CurItm = Item_MakeChunk_String(FCC_ICRD, TempStr, 0x01);
	List_AddItem(LstInfo, CurItm);
	
	return SF2Data;
}

void FreeSF2Data(SF2_DATA* SF2Data)
{
	LIST_CHUNK* CurLst;
	LIST_CHUNK* LastLst;
	
	CurLst = SF2Data->Lists;
	while(CurLst != NULL)
	{
		List_FreeListItems(CurLst);
		
		LastLst = CurLst;
		CurLst = CurLst->next;
		free(LastLst);
	}
	free(SF2Data);
	
	return;
}

void CalculateBlockSizes(SF2_DATA* SF2Data)
{
	UINT32 RIFFSize;
	LIST_CHUNK* CurLst;
	
	RIFFSize = 0x04;	// RIFF Type
	
	CurLst = SF2Data->Lists;
	while(CurLst != NULL)
	{
		List_CalculateSize(CurLst);
		RIFFSize += 0x08 + CurLst->ckSize;
		
		CurLst = CurLst->next;
	}
	SF2Data->RiffSize = RIFFSize;
	
	return;
}

UINT8 WriteSF2toFile(SF2_DATA* SF2Data, const char* FileName)
{
	FILE* hFile;
	LIST_CHUNK* CurLst;
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
		return 0xFF;
	
	CalculateBlockSizes(SF2Data);
	
	// write RIFF header
	fwrite(&SF2Data->fccRIFF, 0x04, 0x01, hFile);
	fwrite(&SF2Data->RiffSize, 0x04, 0x01, hFile);
	fwrite(&SF2Data->RiffType, 0x04, 0x01, hFile);
	
	CurLst = SF2Data->Lists;
	while(CurLst != NULL)
	{
		List_WriteToFile(CurLst, hFile);
		
		CurLst = CurLst->next;
	}
	
	fclose(hFile);
	
	return 0x00;
}


// --- List Chunk Handling ---
LIST_CHUNK* List_MakeChunk(const FOURCC fccID)
{
	LIST_CHUNK* ListChk;
	
	ListChk = (LIST_CHUNK*)malloc(sizeof(LIST_CHUNK));
	if (ListChk == NULL)
		return NULL;
	
	ListChk->ckID = FCC_LIST;
	ListChk->ckSize = 0x00;
	ListChk->ckType = fccID;
	ListChk->Items = NULL;
	ListChk->LastItem = ListChk->Items;
	ListChk->next = NULL;
	
	return ListChk;
}

LIST_CHUNK* List_GetChunk(const LIST_CHUNK* FirstChk, const FOURCC fccID)
{
	const LIST_CHUNK* CurChk;
	
	CurChk = FirstChk;
	while(CurChk != NULL)
	{
		if (CurChk->ckType == fccID)
			return (LIST_CHUNK*)CurChk;
		CurChk = CurChk->next;
	}
	
	return NULL;
}

void List_AddItem(LIST_CHUNK* Chunk, ITEM_CHUNK* Item)
{
	if (Chunk->Items == NULL)
	{
		Chunk->Items = Item;
		Chunk->LastItem = Item;
	}
	else
	{
		if (Chunk->LastItem == NULL)	// this should really not happen
		{
			ITEM_CHUNK* CurChk;
			
			CurChk = Chunk->Items;
			while(CurChk->next != NULL)
				CurChk = CurChk->next;
			Chunk->LastItem = CurChk;
		}
		
		Chunk->LastItem->next = Item;
		Chunk->LastItem = Item;
	}
	
	return;
}

void List_FreeListItems(LIST_CHUNK* Chunk)
{
	ITEM_CHUNK* CurItm;
	ITEM_CHUNK* LastItm;
	
	CurItm = Chunk->Items;
	while(CurItm != NULL)
	{
		Item_FreeItemData(CurItm);
		
		LastItm = CurItm;
		CurItm = CurItm->next;
		free(LastItm);
	}
	Chunk->Items = NULL;
	Chunk->LastItem = NULL;
	
	return;
}

void List_CalculateSize(LIST_CHUNK* Chunk)
{
	ITEM_CHUNK* CurItm;
	UINT32 LstSize;
	
	CurItm = Chunk->Items;
	LstSize = 0x04;	// List Type
	while(CurItm != NULL)
	{
		LstSize += 0x08 + CurItm->ckSize;
		CurItm = CurItm->next;
	}
	Chunk->ckSize = LstSize;
	
	return;
}

void List_WriteToFile(LIST_CHUNK* Chunk, FILE* hFile)
{
	ITEM_CHUNK* CurItm;
	
	fwrite(&Chunk->ckID, 0x04, 0x01, hFile);
	fwrite(&Chunk->ckSize, 0x04, 0x01, hFile);
	fwrite(&Chunk->ckType, 0x04, 0x01, hFile);
	
	CurItm = Chunk->Items;
	while(CurItm != NULL)
	{
		Item_WriteToFile(CurItm, hFile);
		CurItm = CurItm->next;
	}
	
	return;
}

// --- Item Chunk Handling ---
ITEM_CHUNK* Item_MakeChunk(const FOURCC fccID, DWORD DataSize, void* Data, UINT8 CopyData)
{
	ITEM_CHUNK* ItemChk;
	
	ItemChk = (ITEM_CHUNK*)malloc(sizeof(ITEM_CHUNK));
	if (ItemChk == NULL)
		return NULL;
	
	ItemChk->ckID = fccID;
	ItemChk->ckSize = DataSize;
	if (! CopyData)
	{
		// the Data was already allocated with malloc
		ItemChk->ckData = Data;
	}
	else
	{
		// We need to make copies of static structures (which are sometimes nicer to work with).
		ItemChk->ckData = malloc(DataSize);
		memcpy(ItemChk->ckData, Data, DataSize);
	}
	ItemChk->next = NULL;
	
	return ItemChk;
}

ITEM_CHUNK* Item_MakeChunk_String(const FOURCC fccID, const char* Data, UINT8 CopyData)
{
	ITEM_CHUNK* ItemChk;
	UINT32 StrSize;
	
	ItemChk = (ITEM_CHUNK*)malloc(sizeof(ITEM_CHUNK));
	if (ItemChk == NULL)
		return NULL;
	
	StrSize = strlen(Data) + 1;
	if (StrSize & 1)
		StrSize ++;	// pad to even size
	
	ItemChk->ckID = fccID;
	ItemChk->ckSize = StrSize;
	if (! CopyData)
	{
		ItemChk->ckData = (void*)Data;
	}
	else
	{
		ItemChk->ckData = malloc(StrSize);
		strncpy(ItemChk->ckData, Data, StrSize);	// strncpy includes padding :)
	}
	ItemChk->next = NULL;
	
	return ItemChk;
}

ITEM_CHUNK* Item_GetChunk(const ITEM_CHUNK* FirstChk, const FOURCC fccID)
{
	const ITEM_CHUNK* CurChk;
	
	CurChk = FirstChk;
	while(CurChk != NULL)
	{
		if (CurChk->ckID == fccID)
			return (ITEM_CHUNK*)CurChk;
		CurChk = CurChk->next;
	}
	
	return NULL;
}

void Item_FreeItemData(ITEM_CHUNK* Chunk)
{
	free(Chunk->ckData);
	Chunk->ckData = NULL;
	Chunk->ckSize = 0x00;
	
	return;
}

void Item_WriteToFile(ITEM_CHUNK* Chunk, FILE* hFile)
{
	fwrite(&Chunk->ckID, 0x04, 0x01, hFile);
	fwrite(&Chunk->ckSize, 0x04, 0x01, hFile);
	fwrite(Chunk->ckData, 0x01, Chunk->ckSize, hFile);
	
	return;
}
