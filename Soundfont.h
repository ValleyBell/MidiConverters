
typedef char	CHAR;
typedef UINT8	BYTE;
typedef INT16	SHORT;
typedef UINT16	WORD;
typedef UINT32	DWORD;
typedef DWORD	FOURCC;

// --- General Types ---
typedef struct _item_chunk ITEM_CHUNK;
struct _item_chunk
{
	FOURCC ckID;
	DWORD ckSize;
	void* ckData;
	ITEM_CHUNK* next;
};

typedef struct _list_chunk LIST_CHUNK;
struct _list_chunk
{
	FOURCC ckID;
	DWORD ckSize;
	FOURCC ckType;
	ITEM_CHUNK* Items;
	ITEM_CHUNK* LastItem;	// not really necessary, but improves performance
	LIST_CHUNK* next;
};

typedef struct _sf2_data
{
	FOURCC fccRIFF;
	DWORD RiffSize;
	FOURCC RiffType;
	LIST_CHUNK* Lists;
	LIST_CHUNK* LastLst;	// not really necessary, but improves performance
} SF2_DATA;


// --- Item Types ---
/*enum SF2_TYPES
{
	SFT_ANY = 0x00,
	SFT_STRING = 0x01,
	SFT_VER_TAG = 0x10,
	SFT_PRESET_HDR,
	SFT_PRESET_BAG,
	SFT_MOD_LIST,
	SFT_GEN_LIST,
	SFT_INST,
	SFT_INST_BAG,
	SFT_INST_MOD_LIST,
	SFT_INST_GEN_LIST,
	SFT_SAMPLE
};*/

#pragma pack(1)

// -- Info Data Types --
// <iver-rec>
typedef struct
{
	WORD wMajor;
	WORD wMinor;
} sfVersionTag;


// -- Preset Data Types --
// <phdr-rec>
typedef struct
{
	CHAR achPresetName[20];
	WORD wPreset;
	WORD wBank;
	WORD wPresetBagNdx;
	DWORD dwLibrary;
	DWORD dwGenre;
	DWORD dwMorphology;
} sfPresetHeader;

// <pbag-rec>
typedef struct
{
	WORD wGenNdx;
	WORD wModNdx;
} sfPresetBag;

// <pmod-rec>
typedef struct
{
	UINT16 sfModSrcOper;	// sfModulator
	UINT16 sfModDestOper;	// sfGenerator
	SHORT modAmount;
	UINT16 sfModAmtSrcOper;	// sfModulator
	UINT16 sfModTransOper;	// sfTransform
} sfModList;

// <pgen-rec>
typedef struct
{
	BYTE byLo;
	BYTE byHi;
} rangesType;

typedef union
{
	rangesType ranges;
	SHORT shAmount;
	WORD wAmount;
} genAmountType;

typedef enum
{
	startAddrsOffset = 0,		// instrument only
	endAddrsOffset = 1,			// instrument only
	startloopAddrsOffset = 2,	// instrument only
	endloopAddrsOffset = 3,		// instrument only
	startAddrsCoarseOffset = 4,	// instrument only
	modLfoToPitch = 5,
	vibLfoToPitch = 6,
	modEnvToPitch = 7,
	initialFilterFc = 8,
	initialFilterQ = 9,
	modLfoToFilterFc = 10,
	modEnvToFilterFc = 11,
	endAddrsCoarseOffset = 12,	// instrument only
	modLfoToVolume = 13,
	chorusEffectsSend = 15,
	reverbEffectsSend = 16,
	pan = 17,
	delayModLFO = 21,
	freqModLFO = 22,
	delayVibLFO = 23,
	freqVibLFO = 24,
	delayModEnv = 25,
	attackModEnv = 26,
	holdModEnv = 27,
	decayModEnv = 28,
	sustainModEnv = 29,
	releaseModEnv = 30,
	keynumToModEnvHold = 31,
	keynumToModEnvDecay = 32,
	delayVolEnv = 33,
	attackVolEnv = 34,
	holdVolEnv = 35,
	decayVolEnv = 36,
	sustainVolEnv = 37,
	releaseVolEnv = 38,
	keynumToVolEnvHold = 39,
	keynumToVolEnvDecay = 40,
	instrument = 41,			// preset only
	keyRange = 43,
	velRange = 44,
	startloopAddrsCoarseOffset = 45,	// instrument only
	keynum = 46,				// instrument only
	velocity = 47,				// instrument only
	initialAttenuation = 48,
	endloopAddrsCoarseOffset = 50,		// instrument only
	coarseTune = 51,
	fineTune = 52,
	sampleID = 53,
	sampleModes = 54,			// instrument only
	scaleTuning = 56,
	exclusiveClass = 57,		// instrument only
	overridingRootKey = 58,		// instrument only
	endOper = 60
} sfGenerator;

typedef struct
{
	UINT16 sfGenOper;	// sfGenerator
	genAmountType genAmount;
} sfGenList;

// <inst-rec>
typedef struct
{
	CHAR achInstName[20];
	WORD wInstBagNdx;
} sfInst;

// <ibag-rec>
typedef struct
{
	WORD wInstGenNdx;
	WORD wInstModNdx;
} sfInstBag;

// <imod-rec>
typedef struct
{
	UINT16 sfModSrcOper;	// sfModulator
	UINT16 sfModDestOper;	// sfGenerator
	SHORT modAmount;
	UINT16 sfModAmtSrcOper;	// sfModulator
	UINT16 sfModTransOper;	// sfTransform
} sfInstModList;

// <igen-rec>
typedef struct
{
	UINT16 sfGenOper;	// sfGenerator
	genAmountType genAmount;
} sfInstGenList;

// <shdr-rec>
typedef enum
{
	monoSample = 1,
	rightSample = 2,
	leftSample = 4,
	linkedSample = 8,
	RomMonoSample = 0x8001,
	RomRightSample = 0x8002,
	RomLeftSample = 0x8004,
	RomLinkedSample = 0x8008
} SFSampleLink;

typedef struct
{
	CHAR achSampleName[20];
	DWORD dwStart;
	DWORD dwEnd;
	DWORD dwStartloop;
	DWORD dwEndloop;
	DWORD dwSampleRate;
	BYTE byOriginalKey;
	CHAR chCorrection;
	WORD wSampleLink;
	UINT16 sfSampleType;	// SFSampleLink
} sfSample;

#pragma pack()

/*
Format: (mostly copy-paste from SF2 Specification 2.1 and 2.4)

RIFF 'sfbk'
{
	LIST 'INFO'		// Supplemental Information
	{
		<ifil-ck>	// Refers to the version of the Sound Font RIFF file
		<isng-ck>	// Refers to the target Sound Engine
		<INAM-ck>	// Refers to the Sound Font Bank Name
		[<irom-ck>]	// Refers to the Sound ROM Name
		[<iver-ck>]	// Refers to the Sound ROM Version
		[<ICRD-ck>]	// Refers to the Date of Creation of the Bank
		[<IENG-ck>]	// Sound Designers and Engineers for the Bank
		[<IPRD-ck>]	// Product for which the Bank was intended
		[<ICOP-ck>]	// Contains any Copyright message
		[<ICMT-ck>]	// Contains any Comments on the Bank
		[<ISFT-ck>]	// The SoundFont tools used to create and alter the bank
	}
	
	LIST 'SDTA'		// The Sample Binary Data
	{
		[<smpl-ck.]	// The Digital Audio Samples
	}
	
	LIST 'PDTA'		// The Preset, Instrument, and Sample Header data
	{
		<phdr-ck>	// The Preset Headers
		<pbag-ck>	// The Preset Index list
		<pmod-ck>	// The Preset Modulator list
		<pgen-ck>	// The Preset Generator list
		<inst-ck>	// The Instrument Names and Indices
		<ibag-ck>	// The Instrument Index list
		<imod-ck>	// The Instrument Modulator list
		<igen-ck>	// The Instrument Generator list
		<shdr-ck>	// The Sample Headers
	}
}
*/


// macro from mmsystem.h
#define MAKEFOURCC(ch0, ch1, ch2, ch3)						\
		((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |	\
		((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))

// RIFF header FCCs
#define FCC_RIFF		MAKEFOURCC('R', 'I', 'F', 'F')
#define FCC_sfbk		MAKEFOURCC('s', 'f', 'b', 'k')
#define FCC_LIST		MAKEFOURCC('L', 'I', 'S', 'T')

// LIST chunk FCCs
#define FCC_INFO		MAKEFOURCC('I', 'N', 'F', 'O')
#define FCC_sdta		MAKEFOURCC('s', 'd', 't', 'a')
#define FCC_pdta		MAKEFOURCC('p', 'd', 't', 'a')

// 'INFO' chunk FCCs
#define FCC_ifil		MAKEFOURCC('i', 'f', 'i', 'l')
#define FCC_isng		MAKEFOURCC('i', 's', 'n', 'g')
#define FCC_INAM		MAKEFOURCC('I', 'N', 'A', 'M')
#define FCC_irom		MAKEFOURCC('i', 'r', 'o', 'm')
#define FCC_iver		MAKEFOURCC('i', 'v', 'e', 'r')
#define FCC_ICRD		MAKEFOURCC('I', 'C', 'R', 'D')
#define FCC_IENG		MAKEFOURCC('I', 'E', 'N', 'G')
#define FCC_IPRD		MAKEFOURCC('i', 'P', 'R', 'D')
#define FCC_ICOP		MAKEFOURCC('I', 'C', 'O', 'P')
#define FCC_ICMT		MAKEFOURCC('I', 'C', 'M', 'T')
#define FCC_ISFT		MAKEFOURCC('I', 'S', 'F', 'T')

// 'sdta' chunk FCCs
#define FCC_smpl		MAKEFOURCC('s', 'm', 'p', 'l')
#define FCC_sm24		MAKEFOURCC('s', 'm', '2', '4')

// 'pdta' chunk FCCs
#define FCC_phdr		MAKEFOURCC('p', 'h', 'd', 'r')
#define FCC_pbag		MAKEFOURCC('p', 'b', 'a', 'g')
#define FCC_pmod		MAKEFOURCC('p', 'm', 'o', 'd')
#define FCC_pgen		MAKEFOURCC('p', 'g', 'e', 'n')
#define FCC_inst		MAKEFOURCC('i', 'n', 's', 't')
#define FCC_ibag		MAKEFOURCC('i', 'b', 'a', 'g')
#define FCC_imod		MAKEFOURCC('i', 'm', 'o', 'd')
#define FCC_igen		MAKEFOURCC('i', 'g', 'e', 'n')
#define FCC_shdr		MAKEFOURCC('s', 'h', 'd', 'r')


SF2_DATA* CreateSF2Base(const char* SoundfontName);
void FreeSF2Data(SF2_DATA* SF2Data);
void CalculateBlockSizes(SF2_DATA* SF2Data);
UINT8 WriteSF2toFile(SF2_DATA* SF2Data, const char* FileName);

// --- List Chunk Handling ---
LIST_CHUNK* List_MakeChunk(const FOURCC fccID);
LIST_CHUNK* List_GetChunk(const LIST_CHUNK* FirstChk, const FOURCC fccID);
void List_AddItem(LIST_CHUNK* Chunk, ITEM_CHUNK* Item);
void List_FreeListItems(LIST_CHUNK* Chunk);
void List_CalculateSize(LIST_CHUNK* Chunk);
void List_WriteToFile(LIST_CHUNK* Chunk, FILE* hFile);

// --- Item Chunk Handling ---
ITEM_CHUNK* Item_MakeChunk(const FOURCC fccID, DWORD DataSize, void* Data, UINT8 CopyData);
ITEM_CHUNK* Item_MakeChunk_String(const FOURCC fccID, const char* Data, UINT8 CopyData);
ITEM_CHUNK* Item_GetChunk(const ITEM_CHUNK* FirstChk, const FOURCC fccID);
void Item_FreeItemData(ITEM_CHUNK* Chunk);
void Item_WriteToFile(ITEM_CHUNK* Chunk, FILE* hFile);
