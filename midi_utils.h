// MIDI Utility Routines
// ---------------------
// written by Valley Bell
// to be included as header file in addition to midi_funcs.h
//
// Use the following macros to enable certain features:
//  RUNNING_NOTES
//      RUN_NOTE* AddRunningNote(UINT16 runNoteMax, UINT16* runNoteCnt, RUN_NOTE* runNotes,
//                               UINT8 midChn, UINT8 note, UINT8 velOff, UINT32 length);
//          Adds a note event to the "runNotes" list, so that NoteOff events can be inserted
//          automatically by CheckRunningNotes while processing delays.
//          "length" specifies the number of ticks after which the note is turned off.
//          "velOff" specifies the velocity for the Note Off event.
//                   A value of 0x80 results in Note On with velocity 0.
//          Returns a pointer to the inserted struct or NULL if (NoteCnt >= NoteMax).
//      UINT16 CheckRunningNotes(FILE_INF* fInf, UINT32* delay, UINT16* runNoteCnt, RUN_NOTE* runNotes);
//          Checks, if any note expires within the N ticks specified by the "delay" parameter and
//          insert respective Note Off events. (In that case, the value of "delay" will be reduced.)
//          Call this function from the delay handler and before extending notes.
//          Returns the number of expired notes.
//      void FlushRunningNotes(FILE_INF* fInf, UINT32* delay, UINT16* runNoteCnt, RUN_NOTE* runNotes, UINT8 cutNotes);
//          Writes Note Off events for all running notes.
//          "cutNotes" = 0 -> all notes are played fully (even if "delay" is smaller than the longest note)
//          "cutNotes" = 1 -> notes playing after "delay" ticks are cut there
//
//      Note: The typedef struct RUN_NOTE gets declared by this header.
//            If you want to declare it by yourself, define HAS_RUN_NOTE_STRUCT.
//
//  BALANCE_TRACK_TIMES
//      UINT16 BalanceTrackTimes(UINT16 trkCnt, TRK_INF* trkInf, UINT32 minLoopTicks, UINT8 verbose);
//          Adjusts the value of trkInf->loopTimes so that all tracks play for the approximately same time.
//          When a track's loop has less ticks than minLoopTicks, then it is ignored.
//          Returns the number of adjusted tracks.
//
//      Note: Needs a typedef struct TRK_INF to be declared *before including the header* with
//            the following members:
//          UINT32 tickCnt;     // total number of ticks (including 1 loop)
//          UINT32 loopTick;    // tick where the loop begins
//          UINT16 loopTimes;   // 0 - non-looping, 1+ - minimum number of loops
//      More members may be declared in order to store additional information, but the function
//      needs only those three.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "stdtype.h"

#ifdef RUNNING_NOTES

#ifndef HAS_RUN_NOTE_STRUCT
#define HAS_RUN_NOTE_STRUCT
typedef struct running_note
{
	UINT8 midChn;
	UINT8 note;
	UINT8 velOff;	// note off velocity
	UINT32 remLen;
} RUN_NOTE;
#endif

static RUN_NOTE* AddRunningNote(UINT16 runNoteMax, UINT16* runNoteCnt, RUN_NOTE* runNotes,
								UINT8 midChn, UINT8 note, UINT8 velOff, UINT32 length)
{
	RUN_NOTE* rn;
	
	if (*runNoteCnt >= runNoteMax)
		return NULL;
	
	rn = &runNotes[*runNoteCnt];
	rn->midChn = midChn;
	rn->note = note;
	rn->velOff = velOff;
	rn->remLen = length;
	(*runNoteCnt) ++;
	
	return rn;
}

static UINT16 CheckRunningNotes(FILE_INF* fInf, UINT32* delay, UINT16* runNoteCnt, RUN_NOTE* runNotes)
{
	UINT16 curNote;
	UINT32 tempDly;
	RUN_NOTE* tempNote;
	UINT16 expiredNotes;
	
	expiredNotes = 0;
	while(*runNoteCnt > 0)
	{
		// 1. Check if we're going beyond a note's timeout.
		tempDly = *delay + 1;
		for (curNote = 0; curNote < *runNoteCnt; curNote ++)
		{
			tempNote = &runNotes[curNote];
			if (tempNote->remLen < tempDly)
				tempDly = tempNote->remLen;
		}
		if (tempDly > *delay)
			break;	// not beyond the timeout - do the event
		
		// 2. advance all notes by X ticks
		for (curNote = 0; curNote < *runNoteCnt; curNote ++)
			runNotes[curNote].remLen -= (UINT16)tempDly;
		(*delay) -= tempDly;
		
		// 3. send NoteOff for expired notes
		for (curNote = 0; curNote < *runNoteCnt; curNote ++)
		{
			tempNote = &runNotes[curNote];
			if (tempNote->remLen > 0)
				continue;
			
			// turn note off, if going beyond the Timeout
			WriteMidiValue(fInf, tempDly);
			tempDly = 0;
			
			File_CheckRealloc(fInf, 0x03);
			if (tempNote->velOff < 0x80)
			{
				fInf->data[fInf->pos + 0x00] = 0x80 | tempNote->midChn;
				fInf->data[fInf->pos + 0x01] = tempNote->note;
				fInf->data[fInf->pos + 0x02] = tempNote->velOff;
			}
			else
			{
				fInf->data[fInf->pos + 0x00] = 0x90 | tempNote->midChn;
				fInf->data[fInf->pos + 0x01] = tempNote->note;
				fInf->data[fInf->pos + 0x02] = 0x00;
			}
			fInf->pos += 0x03;
			
			(*runNoteCnt) --;
			memmove(tempNote, &runNotes[curNote + 1], (*runNoteCnt - curNote) * sizeof(RUN_NOTE));
			curNote --;
			expiredNotes ++;
		}
	}
	
	return expiredNotes;
}

static void FlushRunningNotes(FILE_INF* fInf, UINT32* delay, UINT16* runNoteCnt, RUN_NOTE* runNotes, UINT8 cutNotes)
{
	UINT16 curNote;
	
	for (curNote = 0; curNote < *runNoteCnt; curNote ++)
	{
		if (runNotes[curNote].remLen > *delay)
		{
			if (cutNotes)
				runNotes[curNote].remLen = *delay;	// cut all notes at "delay"
			else
				*delay = runNotes[curNote].remLen;	// set "delay" to longest note
		}
	}
	CheckRunningNotes(fInf, delay, runNoteCnt, runNotes);
	
	return;
}
#endif

#ifdef BALANCE_TRACK_TIMES
static UINT16 BalanceTrackTimes(UINT16 trkCnt, TRK_INF* trkInf, UINT32 minLoopTicks, UINT8 verbose)
{
	UINT16 curTrk;
	TRK_INF* tInf;
	UINT32 maxTicks;
	UINT32 trkTicks;
	UINT32 loopTicks;
	UINT16 adjustCnt;
	
	maxTicks = 0;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tInf = &trkInf[curTrk];
		if (tInf->loopTimes == 0)
		{
			trkTicks = tInf->tickCnt;
		}
		else
		{
			loopTicks = tInf->tickCnt - tInf->loopTick;
			trkTicks = tInf->tickCnt + loopTicks * (tInf->loopTimes - 1);
		}
		if (maxTicks < trkTicks)
			maxTicks = trkTicks;
	}
	
	adjustCnt = 0;
	for (curTrk = 0; curTrk < trkCnt; curTrk ++)
	{
		tInf = &trkInf[curTrk];
		loopTicks = tInf->loopTimes ? (tInf->tickCnt - tInf->loopTick) : 0;
		if (loopTicks < minLoopTicks)
		{
			if (loopTicks > 0 && (verbose & 0x02))
				printf("Trk %u: ignoring micro-loop (%u ticks)\n", curTrk, loopTicks);
			continue;	// ignore tracks with very short loops
		}
		
		// heuristic: The track needs additional loops, if the longest track is
		//            longer than the current track + 1/4 loop.
		trkTicks = tInf->tickCnt + loopTicks * (tInf->loopTimes - 1);
		if (trkTicks + loopTicks / 4 < maxTicks)
		{
			trkTicks = maxTicks - tInf->loopTick;	// desired length of the loop
			tInf->loopTimes = (UINT16)((trkTicks + loopTicks / 3) / loopTicks);
			adjustCnt ++;
			if (verbose & 0x01)
				printf("Trk %u: Extended loop to %u times\n", curTrk, tInf->loopTimes);
		}
	}
	
	return adjustCnt;
}
#endif
