/*	$OpenBSD: rf_aselect.c,v 1.1 1999/01/11 14:28:59 niklas Exp $	*/
/*	$NetBSD: rf_aselect.c,v 1.1 1998/11/13 04:20:26 oster Exp $	*/
/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Mark Holland, William V. Courtright II
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*****************************************************************************
 *
 * aselect.c -- algorithm selection code
 * 
 *****************************************************************************/
/*
 * :  
 * Log: rf_aselect.c,v 
 * Revision 1.35  1996/07/28 20:31:39  jimz
 * i386netbsd port
 * true/false fixup
 *
 * Revision 1.34  1996/07/27  18:39:39  jimz
 * cleanup sweep
 *
 * Revision 1.33  1996/07/22  19:52:16  jimz
 * switched node params to RF_DagParam_t, a union of
 * a 64-bit int and a void *, for better portability
 * attempted hpux port, but failed partway through for
 * lack of a single C compiler capable of compiling all
 * source files
 *
 * Revision 1.32  1996/06/12  03:29:40  jimz
 * Note: things that call InitHdrNode should check
 * for successful return.
 *
 * Revision 1.31  1996/06/07  21:33:04  jimz
 * begin using consistent types for sector numbers,
 * stripe numbers, row+col numbers, recon unit numbers
 *
 * Revision 1.30  1996/06/05  18:06:02  jimz
 * Major code cleanup. The Great Renaming is now done.
 * Better modularity. Better typing. Fixed a bunch of
 * synchronization bugs. Made a lot of global stuff
 * per-desc or per-array. Removed dead code.
 *
 * Revision 1.29  1996/05/31  22:26:54  jimz
 * fix a lot of mapping problems, memory allocation problems
 * found some weird lock issues, fixed 'em
 * more code cleanup
 *
 * Revision 1.28  1996/05/30  11:29:41  jimz
 * Numerous bug fixes. Stripe lock release code disagreed with the taking code
 * about when stripes should be locked (I made it consistent: no parity, no lock)
 * There was a lot of extra serialization of I/Os which I've removed- a lot of
 * it was to calculate values for the cache code, which is no longer with us.
 * More types, function, macro cleanup. Added code to properly quiesce the array
 * on shutdown. Made a lot of stuff array-specific which was (bogusly) general
 * before. Fixed memory allocation, freeing bugs.
 *
 * Revision 1.27  1996/05/27  18:56:37  jimz
 * more code cleanup
 * better typing
 * compiles in all 3 environments
 *
 * Revision 1.26  1996/05/24  22:17:04  jimz
 * continue code + namespace cleanup
 * typed a bunch of flags
 *
 * Revision 1.25  1996/05/24  04:28:55  jimz
 * release cleanup ckpt
 *
 * Revision 1.24  1996/05/23  21:46:35  jimz
 * checkpoint in code cleanup (release prep)
 * lots of types, function names have been fixed
 *
 * Revision 1.23  1996/05/23  00:33:23  jimz
 * code cleanup: move all debug decls to rf_options.c, all extern
 * debug decls to rf_options.h, all debug vars preceded by rf_
 *
 * Revision 1.22  1996/05/18  19:51:34  jimz
 * major code cleanup- fix syntax, make some types consistent,
 * add prototypes, clean out dead code, et cetera
 *
 * Revision 1.21  1996/05/08  21:01:24  jimz
 * fixed up enum type names that were conflicting with other
 * enums and function names (ie, "panic")
 * future naming trends will be towards RF_ and rf_ for
 * everything raidframe-related
 *
 * Revision 1.20  1996/05/03  19:45:35  wvcii
 * removed includes of old deg creation files
 * updated SelectAlgorithm comments
 *
 * Revision 1.19  1995/12/12  18:10:06  jimz
 * MIN -> RF_MIN, MAX -> RF_MAX, ASSERT -> RF_ASSERT
 * fix 80-column brain damage in comments
 *
 * Revision 1.18  1995/11/30  16:27:48  wvcii
 * added copyright info
 *
 * Revision 1.17  1995/11/19  16:25:55  wvcii
 * SelectAlgorithm now creates an array, returned in desc->dagArray
 * return value is now int (1 = FAIL)
 *
 * Revision 1.16  1995/11/17  15:09:58  wvcii
 * fixed bug in SelectAlgorithm in which multiple graphs per stripe are required
 *
 * Revision 1.15  1995/11/07  17:12:42  wvcii
 * changed SelectAlgorithm as follows:
 *
 * dag creation funcs now create term nodes
 * dag selection funcs no longer return numHdrSucc, numTermAnt
 * there is now one dag hdr for each dag in a request, implying
 * that SelectAlgorithm now returns a linked list of dag hdrs
 *
 */

#include "rf_archs.h"
#include "rf_types.h"
#include "rf_raid.h"
#include "rf_dag.h"
#include "rf_dagutils.h"
#include "rf_dagfuncs.h"
#include "rf_general.h"
#include "rf_desc.h"
#include "rf_map.h"

#if (defined(__NetBSD__) || defined(__OpenBSD__)) && defined(_KERNEL)
/* the function below is not used... so don't define it! */
#else
static void TransferDagMemory(RF_DagHeader_t *, RF_DagHeader_t *);
#endif

static int InitHdrNode(RF_DagHeader_t **, RF_Raid_t *, int);
static void UpdateNodeHdrPtr(RF_DagHeader_t *, RF_DagNode_t *);
int rf_SelectAlgorithm(RF_RaidAccessDesc_t *, RF_RaidAccessFlags_t );


/******************************************************************************
 *
 * Create and Initialiaze a dag header and termination node
 *
 *****************************************************************************/
static int InitHdrNode(hdr, raidPtr, memChunkEnable)
  RF_DagHeader_t  **hdr;
  RF_Raid_t        *raidPtr;
  int               memChunkEnable;
{
  /* create and initialize dag hdr */
  *hdr = rf_AllocDAGHeader();
  rf_MakeAllocList((*hdr)->allocList);
  if ((*hdr)->allocList == NULL) {
    rf_FreeDAGHeader(*hdr);
    return(ENOMEM);
  }
  (*hdr)->status = rf_enable;
  (*hdr)->numSuccedents = 0;
  (*hdr)->raidPtr = raidPtr;
  (*hdr)->next = NULL;
  return(0);
}

/******************************************************************************
 *
 * Transfer allocation list and mem chunks from one dag to another
 *
 *****************************************************************************/
#if (defined(__NetBSD__) || defined(__OpenBSD__)) && defined(_KERNEL)
/* the function below is not used... so don't define it! */
#else
static void TransferDagMemory(daga, dagb)
  RF_DagHeader_t  *daga;
  RF_DagHeader_t  *dagb;
{
  RF_AccessStripeMapHeader_t *end;
  RF_AllocListElem_t *p;
  int i, memChunksXfrd = 0, xtraChunksXfrd = 0;

  /* transfer allocList from dagb to daga */
  for (p = dagb->allocList; p ; p = p->next)
    {
      for (i = 0; i < p->numPointers; i++)
	{
	  rf_AddToAllocList(daga->allocList, p->pointers[i], p->sizes[i]);
	  p->pointers[i] = NULL;
	  p->sizes[i] = 0;
	}
      p->numPointers = 0;
    }

  /* transfer chunks from dagb to daga */
  while ((memChunksXfrd + xtraChunksXfrd < dagb->chunkIndex + dagb->xtraChunkIndex) && (daga->chunkIndex < RF_MAXCHUNKS))
    {
      /* stuff chunks into daga's memChunk array */
      if (memChunksXfrd < dagb->chunkIndex)
	{
	  daga->memChunk[daga->chunkIndex++] = dagb->memChunk[memChunksXfrd];
	  dagb->memChunk[memChunksXfrd++] = NULL;
	}
      else
	{
	  daga->memChunk[daga->xtraChunkIndex++] = dagb->xtraMemChunk[xtraChunksXfrd];
	  dagb->xtraMemChunk[xtraChunksXfrd++] = NULL;
	}
    }
  /* use escape hatch to hold excess chunks */
  while (memChunksXfrd + xtraChunksXfrd < dagb->chunkIndex + dagb->xtraChunkIndex) {
    if (memChunksXfrd < dagb->chunkIndex)
      {
	daga->xtraMemChunk[daga->xtraChunkIndex++] = dagb->memChunk[memChunksXfrd];
	dagb->memChunk[memChunksXfrd++] = NULL;
      }
    else
      {
	daga->xtraMemChunk[daga->xtraChunkIndex++] = dagb->xtraMemChunk[xtraChunksXfrd];
	dagb->xtraMemChunk[xtraChunksXfrd++] = NULL;
      }
  }
  RF_ASSERT((memChunksXfrd == dagb->chunkIndex) && (xtraChunksXfrd == dagb->xtraChunkIndex));
  RF_ASSERT(daga->chunkIndex <= RF_MAXCHUNKS);
  RF_ASSERT(daga->xtraChunkIndex <= daga->xtraChunkCnt);
  dagb->chunkIndex = 0;
  dagb->xtraChunkIndex = 0;

  /* transfer asmList from dagb to daga */
  if (dagb->asmList)
    {
      if (daga->asmList)
	{
	  end = daga->asmList;
	  while (end->next)
	    end = end->next;
	  end->next = dagb->asmList;
	}
      else
	daga->asmList = dagb->asmList;
      dagb->asmList = NULL;
    }
}
#endif /* __NetBSD__ || __OpenBSD__ */

/*****************************************************************************************
 *
 * Ensure that all node->dagHdr fields in a dag are consistent
 *
 * IMPORTANT: This routine recursively searches all succedents of the node.  If a
 * succedent is encountered whose dagHdr ptr does not require adjusting, that node's
 * succedents WILL NOT BE EXAMINED.
 *
 ****************************************************************************************/
static void UpdateNodeHdrPtr(hdr, node)
  RF_DagHeader_t  *hdr;
  RF_DagNode_t    *node;
{
  int i;
  RF_ASSERT(hdr != NULL && node != NULL);
  for (i = 0; i < node->numSuccedents; i++)
    if (node->succedents[i]->dagHdr != hdr)
      UpdateNodeHdrPtr(hdr, node->succedents[i]);
  node->dagHdr = hdr;
}

/******************************************************************************
 *
 * Create a DAG to do a read or write operation.
 *
 * create an array of dagLists, one list per parity stripe.
 * return the lists in the array desc->dagArray.
 *
 * Normally, each list contains one dag for the entire stripe.  In some
 * tricky cases, we break this into multiple dags, either one per stripe
 * unit or one per block (sector).  When this occurs, these dags are returned
 * as a linked list (dagList) which is executed sequentially (to preserve
 * atomic parity updates in the stripe).
 * 
 * dags which operate on independent parity goups (stripes) are returned in
 * independent dagLists (distinct elements in desc->dagArray) and may be
 * executed concurrently.
 *
 * Finally, if the SelectionFunc fails to create a dag for a block, we punt
 * and return 1.
 *
 * The above process is performed in two phases:
 *   1) create an array(s) of creation functions (eg stripeFuncs)
 *   2) create dags and concatenate/merge to form the final dag.
 *
 * Because dag's are basic blocks (single entry, single exit, unconditional
 * control flow, we can add the following optimizations (future work):
 *   first-pass optimizer to allow max concurrency (need all data dependencies)
 *   second-pass optimizer to eliminate common subexpressions (need true
 *                         data dependencies)
 *   third-pass optimizer to eliminate dead code (need true data dependencies)
 *****************************************************************************/

#define MAXNSTRIPES 50

int rf_SelectAlgorithm(desc, flags)
  RF_RaidAccessDesc_t   *desc;
  RF_RaidAccessFlags_t   flags;
{
  RF_AccessStripeMapHeader_t *asm_h = desc->asmap;
  RF_IoType_t type     = desc->type;
  RF_Raid_t *raidPtr = desc->raidPtr;
  void *bp      = desc->bp;

  RF_AccessStripeMap_t *asmap = asm_h->stripeMap;
  RF_AccessStripeMap_t *asm_p;
  RF_DagHeader_t *dag_h = NULL, *tempdag_h, *lastdag_h;
  int i, j, k;
  RF_VoidFuncPtr *stripeFuncs, normalStripeFuncs[MAXNSTRIPES];
  RF_AccessStripeMap_t *asm_up, *asm_bp;
  RF_AccessStripeMapHeader_t ***asmh_u, *endASMList;
  RF_AccessStripeMapHeader_t ***asmh_b;
  RF_VoidFuncPtr **stripeUnitFuncs, uFunc;
  RF_VoidFuncPtr **blockFuncs, bFunc;
  int numStripesBailed = 0, cantCreateDAGs = RF_FALSE;
  int numStripeUnitsBailed = 0;
  int stripeNum, numUnitDags = 0, stripeUnitNum, numBlockDags = 0;
  RF_StripeNum_t numStripeUnits;
  RF_SectorNum_t numBlocks;
  RF_RaidAddr_t address;
  int length;
  RF_PhysDiskAddr_t *physPtr;
  caddr_t buffer;

  lastdag_h = NULL;
  asmh_u = asmh_b = NULL;
  stripeUnitFuncs = NULL;
  blockFuncs = NULL;

  /* get an array of dag-function creation pointers, try to avoid calling malloc */
  if (asm_h->numStripes <= MAXNSTRIPES) stripeFuncs = normalStripeFuncs;
  else RF_Calloc(stripeFuncs, asm_h->numStripes, sizeof(RF_VoidFuncPtr), (RF_VoidFuncPtr *));

  /* walk through the asm list once collecting information */
  /* attempt to find a single creation function for each stripe */
  desc->numStripes = 0;
  for (i=0,asm_p = asmap; asm_p; asm_p=asm_p->next,i++) {
    desc->numStripes++;
    (raidPtr->Layout.map->SelectionFunc)(raidPtr, type, asm_p, &stripeFuncs[i]);
    /* check to see if we found a creation func for this stripe */
    if (stripeFuncs[i] == (RF_VoidFuncPtr) NULL)
      {
	/* could not find creation function for entire stripe
	   so, let's see if we can find one for each stripe unit in the stripe */
	
	if (numStripesBailed == 0)
	  {
	    /* one stripe map header for each stripe we bail on */
	    RF_Malloc(asmh_u, sizeof(RF_AccessStripeMapHeader_t **) * asm_h->numStripes, (RF_AccessStripeMapHeader_t ***));
	    /* create an array of ptrs to arrays of stripeFuncs */
	    RF_Calloc(stripeUnitFuncs, asm_h->numStripes, sizeof(RF_VoidFuncPtr), (RF_VoidFuncPtr **));
	  }
	
	/* create an array of creation funcs (called stripeFuncs) for this stripe */
	numStripeUnits = asm_p->numStripeUnitsAccessed;
	RF_Calloc(stripeUnitFuncs[numStripesBailed], numStripeUnits, sizeof(RF_VoidFuncPtr), (RF_VoidFuncPtr *));
	RF_Malloc(asmh_u[numStripesBailed], numStripeUnits * sizeof(RF_AccessStripeMapHeader_t *), (RF_AccessStripeMapHeader_t **));
	
	/* lookup array of stripeUnitFuncs for this stripe */
	for (j=0, physPtr = asm_p->physInfo; physPtr; physPtr = physPtr->next, j++)
	  {
	    /* remap for series of single stripe-unit accesses */
	    address = physPtr->raidAddress;
	    length  = physPtr->numSector;
	    buffer  = physPtr->bufPtr;

	    asmh_u[numStripesBailed][j] = rf_MapAccess(raidPtr, address, length, buffer, RF_DONT_REMAP);
	    asm_up = asmh_u[numStripesBailed][j]->stripeMap;
	    
	    /* get the creation func for this stripe unit */
	    (raidPtr->Layout.map-> SelectionFunc)(raidPtr, type, asm_up, &(stripeUnitFuncs[numStripesBailed][j]));
	    
	    /* check to see if we found a creation func for this stripe unit */
	    if (stripeUnitFuncs[numStripesBailed][j] == (RF_VoidFuncPtr) NULL)
	      {
		/* could not find creation function for stripe unit so,
		   let's see if we can find one for each block in the stripe unit */
		if (numStripeUnitsBailed == 0)
		  {
		    /* one stripe map header for each stripe unit we bail on */
		    RF_Malloc(asmh_b, sizeof(RF_AccessStripeMapHeader_t **) * asm_h->numStripes * raidPtr->Layout.numDataCol, (RF_AccessStripeMapHeader_t ***));
		    /* create an array of ptrs to arrays of blockFuncs */
		    RF_Calloc(blockFuncs, asm_h->numStripes * raidPtr->Layout.numDataCol, sizeof(RF_VoidFuncPtr), (RF_VoidFuncPtr **));
		  }

		/* create an array of creation funcs (called blockFuncs) for this stripe unit */
		numBlocks = physPtr->numSector;
		numBlockDags += numBlocks;
		RF_Calloc(blockFuncs[numStripeUnitsBailed], numBlocks, sizeof(RF_VoidFuncPtr), (RF_VoidFuncPtr *));
		RF_Malloc(asmh_b[numStripeUnitsBailed], numBlocks * sizeof(RF_AccessStripeMapHeader_t *), (RF_AccessStripeMapHeader_t **));
	
		/* lookup array of blockFuncs for this stripe unit */
		for (k=0; k < numBlocks; k++)
		  {
		    /* remap for series of single stripe-unit accesses */
		    address = physPtr->raidAddress + k;
		    length  = 1;
		    buffer  = physPtr->bufPtr + (k * (1<<raidPtr->logBytesPerSector));
		    
		    asmh_b[numStripeUnitsBailed][k] = rf_MapAccess(raidPtr, address, length, buffer, RF_DONT_REMAP);
		    asm_bp = asmh_b[numStripeUnitsBailed][k]->stripeMap;
		    
		    /* get the creation func for this stripe unit */
		    (raidPtr->Layout.map-> SelectionFunc)(raidPtr, type, asm_bp, &(blockFuncs[numStripeUnitsBailed][k]));
		    
		    /* check to see if we found a creation func for this stripe unit */
		    if (blockFuncs[numStripeUnitsBailed][k] == NULL)
		      cantCreateDAGs = RF_TRUE;
		  }
		numStripeUnitsBailed++;
	      }
	    else
	      {
		numUnitDags++;
	      }
	  }
	RF_ASSERT(j == numStripeUnits);
	numStripesBailed++;
      }
  }

  if (cantCreateDAGs)
    {
      /* free memory and punt */
      if (asm_h->numStripes > MAXNSTRIPES)
	RF_Free(stripeFuncs, asm_h->numStripes * sizeof(RF_VoidFuncPtr));
      if (numStripesBailed > 0)
	{
	  stripeNum = 0;
	  for (i = 0, asm_p = asmap; asm_p; asm_p = asm_p->next, i++)
	    if (stripeFuncs[i] == NULL)
	      {
		numStripeUnits = asm_p->numStripeUnitsAccessed;
		for (j = 0; j < numStripeUnits; j++)
		    rf_FreeAccessStripeMap(asmh_u[stripeNum][j]);
		RF_Free(asmh_u[stripeNum], numStripeUnits * sizeof(RF_AccessStripeMapHeader_t *));
		RF_Free(stripeUnitFuncs[stripeNum], numStripeUnits * sizeof(RF_VoidFuncPtr));
		stripeNum++;
	      }
	  RF_ASSERT(stripeNum == numStripesBailed);
	  RF_Free(stripeUnitFuncs, asm_h->numStripes * sizeof(RF_VoidFuncPtr));
	  RF_Free(asmh_u, asm_h->numStripes * sizeof(RF_AccessStripeMapHeader_t **));
	}
      return(1);
    }
  else
    {
      /* begin dag creation */
      stripeNum = 0;
      stripeUnitNum = 0;

      /* create an array of dagLists and fill them in */
      RF_CallocAndAdd(desc->dagArray, desc->numStripes, sizeof(RF_DagList_t), (RF_DagList_t *), desc->cleanupList);

      for (i=0, asm_p = asmap; asm_p; asm_p=asm_p->next,i++) {
	/* grab dag header for this stripe */
	dag_h = NULL;
	desc->dagArray[i].desc = desc;

	if (stripeFuncs[i] == (RF_VoidFuncPtr) NULL)
	  {
	    /* use bailout functions for this stripe */
	    for (j = 0, physPtr = asm_p->physInfo; physPtr; physPtr=physPtr->next, j++)
	      {
		uFunc = stripeUnitFuncs[stripeNum][j];
		if (uFunc == (RF_VoidFuncPtr) NULL)
		  {
		    /* use bailout functions for this stripe unit */
		    for (k = 0; k < physPtr->numSector; k++)
		      {
			/* create a dag for this block */
			InitHdrNode(&tempdag_h, raidPtr, rf_useMemChunks);
			desc->dagArray[i].numDags++;
			if (dag_h == NULL) {
			  dag_h = tempdag_h;
			}
			else {
			  lastdag_h->next = tempdag_h;
			}
			lastdag_h = tempdag_h;
			
			bFunc = blockFuncs[stripeUnitNum][k];
			RF_ASSERT(bFunc);
			asm_bp = asmh_b[stripeUnitNum][k]->stripeMap;
			(*bFunc)(raidPtr, asm_bp, tempdag_h, bp, flags, tempdag_h->allocList);
		      }
		    stripeUnitNum++;
		  }
		else
		  {
		    /* create a dag for this unit */
		    InitHdrNode(&tempdag_h, raidPtr, rf_useMemChunks);
		    desc->dagArray[i].numDags++;
		    if (dag_h == NULL) {
		      dag_h = tempdag_h;
		    }
		    else {
		      lastdag_h->next = tempdag_h;
		    }
		    lastdag_h = tempdag_h;
	  
		    asm_up = asmh_u[stripeNum][j]->stripeMap;
		    (*uFunc)(raidPtr, asm_up, tempdag_h, bp, flags, tempdag_h->allocList);
		  }
	      }
	    RF_ASSERT(j == asm_p->numStripeUnitsAccessed);
	    /* merge linked bailout dag to existing dag collection */
	    stripeNum++;
	  }
	else {
	  /* Create a dag for this parity stripe */
	  InitHdrNode(&tempdag_h, raidPtr, rf_useMemChunks);
	  desc->dagArray[i].numDags++;
	  if (dag_h == NULL) {
	    dag_h = tempdag_h;
	  }
	  else {
	    lastdag_h->next = tempdag_h;
	  }
	  lastdag_h = tempdag_h;
	  
	  (stripeFuncs[i])(raidPtr, asm_p, tempdag_h, bp, flags, tempdag_h->allocList);
	}
	desc->dagArray[i].dags = dag_h;
      }
      RF_ASSERT(i == desc->numStripes);

      /* free memory */
      if (asm_h->numStripes > MAXNSTRIPES)
	RF_Free(stripeFuncs, asm_h->numStripes * sizeof(RF_VoidFuncPtr));
      if ((numStripesBailed > 0) || (numStripeUnitsBailed > 0))
	{
	  stripeNum = 0;
	  stripeUnitNum = 0;
	  if (dag_h->asmList)
	    {
	      endASMList = dag_h->asmList;
	      while (endASMList->next)
		endASMList = endASMList->next;
	    }
	  else
	    endASMList = NULL;
	  /* walk through io, stripe by stripe */
	  for (i = 0, asm_p = asmap; asm_p; asm_p = asm_p->next, i++)
	    if (stripeFuncs[i] == NULL)
	      {
		numStripeUnits = asm_p->numStripeUnitsAccessed;
		/* walk through stripe, stripe unit by stripe unit */
		for (j = 0, physPtr = asm_p->physInfo; physPtr; physPtr = physPtr->next, j++)
		  {
		    if (stripeUnitFuncs[stripeNum][j] == NULL)
		      {
			numBlocks = physPtr->numSector;
			/* walk through stripe unit, block by block */
			for (k = 0; k < numBlocks; k++)
			  if (dag_h->asmList == NULL)
			    {
			      dag_h->asmList = asmh_b[stripeUnitNum][k];
			      endASMList = dag_h->asmList;
			    }
			  else
			    {
			      endASMList->next = asmh_b[stripeUnitNum][k];
			      endASMList = endASMList->next;
			    }
			RF_Free(asmh_b[stripeUnitNum], numBlocks * sizeof(RF_AccessStripeMapHeader_t *));
			RF_Free(blockFuncs[stripeUnitNum], numBlocks * sizeof(RF_VoidFuncPtr));
			stripeUnitNum++;
		      }
		    if (dag_h->asmList == NULL)
		      {
			dag_h->asmList = asmh_u[stripeNum][j];
			endASMList = dag_h->asmList;
		      }
		    else
		      {
			endASMList->next = asmh_u[stripeNum][j];
			endASMList = endASMList->next;
		      }
		  }
		RF_Free(asmh_u[stripeNum], numStripeUnits * sizeof(RF_AccessStripeMapHeader_t *));
		RF_Free(stripeUnitFuncs[stripeNum], numStripeUnits * sizeof(RF_VoidFuncPtr));
		stripeNum++;
	      }
	  RF_ASSERT(stripeNum == numStripesBailed);
	  RF_Free(stripeUnitFuncs, asm_h->numStripes * sizeof(RF_VoidFuncPtr));
	  RF_Free(asmh_u, asm_h->numStripes * sizeof(RF_AccessStripeMapHeader_t **));
	  if (numStripeUnitsBailed > 0)
	    {
	      RF_ASSERT(stripeUnitNum == numStripeUnitsBailed);
	      RF_Free(blockFuncs, raidPtr->Layout.numDataCol * asm_h->numStripes * sizeof(RF_VoidFuncPtr));
	      RF_Free(asmh_b, raidPtr->Layout.numDataCol * asm_h->numStripes * sizeof(RF_AccessStripeMapHeader_t **));
	    }
	}
      return(0);
    }
}
