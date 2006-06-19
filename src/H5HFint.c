/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the files COPYING and Copyright.html.  COPYING can be found at the root   *
 * of the source code distribution tree; Copyright.html can be found at the  *
 * root level of an installed copy of the electronic HDF5 document set and   *
 * is linked from the top-level documents page.  It can also be found at     *
 * http://hdf.ncsa.uiuc.edu/HDF5/doc/Copyright.html.  If you do not have     *
 * access to either file, you may request a copy from hdfhelp@ncsa.uiuc.edu. *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:		H5HFint.c
 *			Feb 24 2006
 *			Quincey Koziol <koziol@ncsa.uiuc.edu>
 *
 * Purpose:		"Internal" routines for fractal heaps.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#define H5HF_PACKAGE		/*suppress error about including H5HFpkg  */

/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5HFpkg.h"		/* Fractal heaps			*/
#include "H5MFprivate.h"	/* File memory management		*/
#include "H5MMprivate.h"	/* Memory management			*/
#include "H5Vprivate.h"		/* Vectors and arrays 			*/

/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/


/*********************/
/* Package Variables */
/*********************/


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/



/*-------------------------------------------------------------------------
 * Function:	H5HF_man_locate_block
 *
 * Purpose:	Locate a block in a managed heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		May  8 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_man_locate_block(H5HF_hdr_t *hdr, hid_t dxpl_id, hsize_t obj_off,
    hbool_t locate_indirect,
    H5HF_indirect_t **ret_iblock, unsigned *ret_entry,
    H5AC_protect_t rw)
{
    haddr_t iblock_addr;            /* Indirect block's address */
    H5HF_indirect_t *iblock;        /* Pointer to indirect block */
    unsigned bot_row, top_row;      /* Bottom & top acceptable rows */
    unsigned row, col;              /* Row & column for object's block */
    size_t entry;                   /* Entry of block */
    herr_t ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_man_locate_block)
#ifdef QAK
HDfprintf(stderr, "%s: obj_off = %Hu\n", FUNC, obj_off);
#endif /* QAK */

    /*
     * Check arguments.
     */
    HDassert(hdr);
    HDassert(hdr->man_dtable.curr_root_rows);   /* Only works for heaps with indirect root block */

    /* Set up target bottom & top rows */
    if(locate_indirect) {
        bot_row = hdr->man_dtable.max_direct_rows;
        top_row = hdr->man_dtable.max_direct_rows + (H5V_log2_of2(hdr->man_dtable.cparam.width) + 1);
    } /* end if */
    else {
        bot_row = 0;
        top_row = hdr->man_dtable.max_direct_rows;
    } /* end else */
#ifdef QAK
HDfprintf(stderr, "%s: bot_row = %u, top_row = %u\n", FUNC, bot_row, top_row);
#endif /* QAK */

    /* Look up row & column for object */
    if(H5HF_dtable_lookup(&hdr->man_dtable, obj_off, &row, &col) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTCOMPUTE, FAIL, "can't compute row & column of object")
#ifdef QAK
HDfprintf(stderr, "%s: row = %u, col = %u\n", FUNC, row, col);
#endif /* QAK */

    /* Set initial indirect block info */
    iblock_addr = hdr->man_dtable.table_addr;
#ifdef QAK
HDfprintf(stderr, "%s: iblock_addr = %a\n", FUNC, iblock_addr);
#endif /* QAK */

    /* Lock root indirect block */
    if(NULL == (iblock = H5HF_man_iblock_protect(hdr, dxpl_id, iblock_addr, hdr->man_dtable.curr_root_rows, NULL, 0, rw)))
        HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to protect fractal heap indirect block")

    /* Check for indirect block row */
    while(row < bot_row || row >= top_row) {
        haddr_t new_iblock_addr;       /* New indirect block's address */
        H5HF_indirect_t *new_iblock;   /* Pointer to new indirect block */
        unsigned nrows;                /* Number of rows in new indirect block */

        /* Compute # of rows in child indirect block */
        nrows = (H5V_log2_gen(hdr->man_dtable.row_block_size[row]) - hdr->man_dtable.first_row_bits) + 1;

        /* Compute indirect block's entry */
        entry = (row * hdr->man_dtable.cparam.width) + col;
#ifdef QAK
HDfprintf(stderr, "%s: entry = %Zu\n", FUNC, entry);
#endif /* QAK */

        /* Locate child indirect block */
        new_iblock_addr = iblock->ents[entry].addr;

        /* Lock new indirect block */
        if(NULL == (new_iblock = H5HF_man_iblock_protect(hdr, dxpl_id, new_iblock_addr, nrows, iblock, entry, rw)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to protect fractal heap indirect block")

        /* Release the current indirect block */
        if(H5AC_unprotect(hdr->f, dxpl_id, H5AC_FHEAP_IBLOCK, iblock_addr, iblock, H5AC__NO_FLAGS_SET) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap indirect block")

        /* Switch variables to use new indirect block */
        iblock = new_iblock;
        iblock_addr = new_iblock_addr;
#ifdef QAK
HDfprintf(stderr, "%s: iblock_addr = %a\n", FUNC, iblock_addr);
HDfprintf(stderr, "%s: iblock->block_off = %Hu\n", FUNC, iblock->block_off);
#endif /* QAK */

        /* Look up row & column in new indirect block for object */
        if(H5HF_dtable_lookup(&hdr->man_dtable, (obj_off - iblock->block_off), &row, &col) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTCOMPUTE, FAIL, "can't compute row & column of object")

#ifdef QAK
HDfprintf(stderr, "%s: row = %u, col = %u\n", FUNC, row, col);
#endif /* QAK */
    } /* end while */

    /* Set return parameters */
    *ret_entry = (row * hdr->man_dtable.cparam.width) + col;
    *ret_iblock = iblock;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_man_locate_block() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_man_find
 *
 * Purpose:	Find space for an object in a managed obj. heap
 *
 * Return:	Non-negative on success (with direct block info
 *              filled in), negative on failure
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Mar 13 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_man_find(H5HF_hdr_t *hdr, hid_t dxpl_id, size_t request,
    H5HF_free_section_t **sec_node/*out*/)
{
    htri_t node_found;                  /* Whether an existing free list node was found */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_man_find)
#ifdef QAK
HDfprintf(stderr, "%s: request = %Zu\n", FUNC, request);
#endif /* QAK */

    /*
     * Check arguments.
     */
    HDassert(hdr);
    HDassert(request > 0);
    HDassert(sec_node);

    /* Look for free space */
    if((node_found = H5HF_space_find(hdr, dxpl_id, (hsize_t)request, sec_node)) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, FAIL, "can't locate free space in fractal heap")

    /* If we didn't find a node, go create a direct block big enough to hold the requested block */
    if(!node_found)
        /* Allocate direct block big enough to hold requested size */
        if(H5HF_man_dblock_new(hdr, dxpl_id, request, sec_node) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTCREATE, FAIL, "can't create fractal heap direct block")

    HDassert(*sec_node);
#ifdef QAK
HDfprintf(stderr, "%s: (*sec_node)->sect_info.addr = %a\n", FUNC, (*sec_node)->sect_info.addr);
HDfprintf(stderr, "%s: (*sec_node)->sect_info.size = %Hu\n", FUNC, (*sec_node)->sect_info.size);
HDfprintf(stderr, "%s: (*sec_node)->sect_info.type = %u\n", FUNC, (*sec_node)->sect_info.type);
#endif /* QAK */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_man_find() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_man_insert
 *
 * Purpose:	Insert an object in a managed direct block
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Mar 13 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_man_insert(H5HF_hdr_t *hdr, hid_t dxpl_id, H5HF_free_section_t *sec_node,
    size_t obj_size, const void *obj, void *id)
{
    H5HF_direct_t *dblock = NULL;       /* Pointer to direct block to modify */
    haddr_t dblock_addr;                /* Direct block address */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_man_insert)
#ifdef QAK
HDfprintf(stderr, "%s: obj_size = %Zu\n", FUNC, obj_size);
#endif /* QAK */

    /*
     * Check arguments.
     */
    HDassert(hdr);
    HDassert(obj_size > 0);
    HDassert(obj);
    HDassert(id);

    /* Check for indirect section */
    if(sec_node->sect_info.type == H5HF_FSPACE_SECT_INDIRECT) {
#ifdef QAK
HDfprintf(stderr, "%s: sec_node->sect_info.addr = %a\n", FUNC, sec_node->sect_info.addr);
HDfprintf(stderr, "%s: sec_node->sect_info.size = %Zu\n", FUNC, sec_node->sect_info.size);
HDfprintf(stderr, "%s: sec_node->u.indirect.iblock = %p\n", FUNC, sec_node->u.indirect.iblock);
if(sec_node->sect_info.state == H5FS_SECT_LIVE && sec_node->u.indirect.iblock)
    HDfprintf(stderr, "%s: sec_node->u.indirect.iblock->addr = %a\n", FUNC, sec_node->u.indirect.iblock->addr);
HDfprintf(stderr, "%s: sec_node->u.indirect.row = %u\n", FUNC, sec_node->u.indirect.row);
HDfprintf(stderr, "%s: sec_node->u.indirect.col = %u\n", FUNC, sec_node->u.indirect.col);
HDfprintf(stderr, "%s: sec_node->u.indirect.num_entries = %u\n", FUNC, sec_node->u.indirect.num_entries);
HDfprintf(stderr, "%s: sec_node->u.indirect.indir_row = %u\n", FUNC, sec_node->u.indirect.indir_row);
HDfprintf(stderr, "%s: sec_node->u.indirect.indir_nrows = %u\n", FUNC, sec_node->u.indirect.indir_nrows);
#endif /* QAK */

        /* Allocate 'single' selection out of 'indirect' selection */
        if(H5HF_man_iblock_alloc_indirect(hdr, dxpl_id, &sec_node) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, FAIL, "can't break up indirect free section")
    } /* end if */
    /* Check for range section */
    else if(sec_node->sect_info.type == H5HF_FSPACE_SECT_RANGE) {
#ifdef QAK
HDfprintf(stderr, "%s: sec_node->sect_info.addr = %a\n", FUNC, sec_node->sect_info.addr);
HDfprintf(stderr, "%s: sec_node->sect_info.size = %Zu\n", FUNC, sec_node->sect_info.size);
HDfprintf(stderr, "%s: sec_node->u.range.iblock = %p\n", FUNC, sec_node->u.range.iblock);
if(sec_node->sect_info.state == H5FS_SECT_LIVE && sec_node->u.range.iblock)
    HDfprintf(stderr, "%s: sec_node->u.range.iblock->addr = %a\n", FUNC, sec_node->u.range.iblock->addr);
HDfprintf(stderr, "%s: sec_node->u.range.row = %u\n", FUNC, sec_node->u.range.row);
HDfprintf(stderr, "%s: sec_node->u.range.col = %u\n", FUNC, sec_node->u.range.col);
HDfprintf(stderr, "%s: sec_node->u.range.num_entries = %u\n", FUNC, sec_node->u.range.num_entries);
#endif /* QAK */
        /* Allocate 'single' selection out of 'range' selection */
        if(H5HF_man_iblock_alloc_range(hdr, dxpl_id, &sec_node) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTALLOC, FAIL, "can't break up range free section")
    } /* end if */
    HDassert(sec_node->sect_info.type == H5HF_FSPACE_SECT_SINGLE);

    /* Check for serialized 'single' section */
    if(sec_node->sect_info.state == H5FS_SECT_SERIALIZED) {
        if(H5HF_sect_single_revive(hdr, dxpl_id, sec_node) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, FAIL, "can't revive single free section")
    } /* end if */
    HDassert(sec_node->sect_info.state == H5FS_SECT_LIVE);

    /* Lock direct block */
#ifdef QAK
HDfprintf(stderr, "%s: sec_node->sect_info.addr = %a\n", FUNC, sec_node->sect_info.addr);
HDfprintf(stderr, "%s: sec_node->sect_info.size = %Hu\n", FUNC, sec_node->sect_info.size);
HDfprintf(stderr, "%s: sec_node->u.single.parent = %p\n", FUNC, sec_node->u.single.parent);
if(sec_node->u.single.parent)
    HDfprintf(stderr, "%s: sec_node->u.single.parent->addr = %a\n", FUNC, sec_node->u.single.parent->addr);
HDfprintf(stderr, "%s: sec_node->u.single.par_entry = %u\n", FUNC, sec_node->u.single.par_entry);
HDfprintf(stderr, "%s: sec_node->u.single.dblock_addr = %a\n", FUNC, sec_node->u.single.dblock_addr);
HDfprintf(stderr, "%s: sec_node->u.single.dblock_size = %Zu\n", FUNC, sec_node->u.single.dblock_size);
#endif /* QAK */
    dblock_addr = sec_node->u.single.dblock_addr;
    if(NULL == (dblock = H5HF_man_dblock_protect(hdr, dxpl_id, dblock_addr, sec_node->u.single.dblock_size, sec_node->u.single.parent, sec_node->u.single.par_entry, H5AC_WRITE)))
        HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to load fractal heap direct block")

    /* Insert object into block */

    /* Check for address mapping type */
    if(hdr->addrmap == H5HF_ABSOLUTE) {
        uint8_t *p;                     /* Temporary pointer to obj info in block */
        size_t blk_off;                 /* Offset of object within block */

        /* Set the offset of the object within the block */
        blk_off = sec_node->sect_info.addr - dblock->block_off;

        /* Sanity checks */
#ifdef QAK
HDfprintf(stderr, "%s: hdr->total_man_free = %Hu\n", FUNC, hdr->total_man_free);
HDfprintf(stderr, "%s: dblock->block_off = %Hu\n", FUNC, dblock->block_off);
#endif /* QAK */
        HDassert(sec_node->sect_info.size >= obj_size);

#ifdef QAK
HDfprintf(stderr, "%s: sec_node->sect_info.size = %Hu\n", FUNC, sec_node->sect_info.size);
#endif /* QAK */
        /* Reduce (& possibly re-add) single section */
        if(H5HF_sect_single_reduce(hdr, dxpl_id, sec_node, obj_size) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTSHRINK, FAIL, "can't reduce single section node")

#ifdef QAK
HDfprintf(stderr, "%s: blk_off = %Zu\n", FUNC, blk_off);
#endif /* QAK */
        /* Reduce space available in heap */
        if(H5HF_hdr_adj_free(hdr, -(ssize_t)obj_size) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTDEC, FAIL, "can't adjust free space for heap")

        /* Encode the object in the block */

        /* Point to location for object */
        p = dblock->blk + blk_off;

        /* Copy the object's data into the heap */
        HDmemcpy(p, obj, obj_size);
        p += obj_size;

        /* Sanity check */
        HDassert((size_t)(p - (dblock->blk + blk_off)) == obj_size);

        /* Set the heap ID for the new object (heap offset & obj length) */
#ifdef QAK
HDfprintf(stderr, "%s: dblock->block_off = %Hu\n", FUNC, dblock->block_off);
#endif /* QAK */
        H5HF_ID_ENCODE(id, hdr, (dblock->block_off + blk_off), obj_size);
    } /* end if */
    else {
HGOTO_ERROR(H5E_HEAP, H5E_UNSUPPORTED, FAIL, "inserting within mapped managed blocks not supported yet")
    } /* end else */

    /* Update statistics about heap */
    hdr->nobjs++;

    /* Mark heap header as modified */
    if(H5HF_hdr_dirty(hdr) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTDIRTY, FAIL, "can't mark heap header as dirty")

done:
    /* Release the direct block (marked as dirty) */
    if(dblock && H5AC_unprotect(hdr->f, dxpl_id, H5AC_FHEAP_DBLOCK, dblock_addr, dblock, H5AC__DIRTIED_FLAG) < 0)
        HDONE_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap direct block")

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_man_insert() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_man_read
 *
 * Purpose:	Read an object from a managed heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		Mar 17 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_man_read(H5HF_hdr_t *hdr, hid_t dxpl_id, hsize_t obj_off, size_t obj_len, void *obj)
{
    H5HF_direct_t *dblock;              /* Pointer to direct block to query */
    size_t blk_off;                     /* Offset of object in block */
    uint8_t *p;                         /* Temporary pointer to obj info in block */
    haddr_t dblock_addr;                /* Direct block address */
    size_t dblock_size;                 /* Direct block size */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_man_read)
#ifdef QAK
HDfprintf(stderr, "%s: obj_off = %Hu, obj_len = %Zu\n", FUNC, obj_off, obj_len);
#endif /* QAK */

    /*
     * Check arguments.
     */
    HDassert(hdr);
    HDassert(obj_off > 0);
    HDassert(obj_len > 0);
    HDassert(obj);

    /* Check for root direct block */
    if(hdr->man_dtable.curr_root_rows == 0) {
        /* Set direct block info */
        dblock_addr = hdr->man_dtable.table_addr;
        dblock_size = hdr->man_dtable.cparam.start_block_size;

        /* Lock direct block */
        if(NULL == (dblock = H5HF_man_dblock_protect(hdr, dxpl_id, dblock_addr, dblock_size, NULL, 0, H5AC_READ)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to protect fractal heap direct block")
    } /* end if */
    else {
        H5HF_indirect_t *iblock;        /* Pointer to indirect block */
        unsigned entry;                 /* Entry of block */

        /* Look up indirect block containing direct block */
        if(H5HF_man_locate_block(hdr, dxpl_id, obj_off, FALSE, &iblock, &entry, H5AC_READ) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTCOMPUTE, FAIL, "can't compute row & column of section")
#ifdef QAK
HDfprintf(stderr, "%s: entry address = %a\n", FUNC, iblock->ents[entry].addr);
#endif /* QAK */

        /* Set direct block info */
        dblock_addr =  iblock->ents[entry].addr;
        dblock_size =  hdr->man_dtable.row_block_size[entry / hdr->man_dtable.cparam.width];

        /* Lock direct block */
        if(NULL == (dblock = H5HF_man_dblock_protect(hdr, dxpl_id, dblock_addr, dblock_size, iblock, entry, H5AC_READ)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to protect fractal heap direct block")

        /* Unlock indirect block */
        if(H5AC_unprotect(hdr->f, dxpl_id, H5AC_FHEAP_IBLOCK, iblock->addr, iblock, H5AC__NO_FLAGS_SET) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap indirect block")
        iblock = NULL;
    } /* end else */
#ifdef QAK
HDfprintf(stderr, "%s: dblock_addr = %a, dblock_size = %Zu\n", FUNC, dblock_addr, dblock_size);
#endif /* QAK */

    /* Compute offset of object within block */
    HDassert((obj_off - dblock->block_off) < (hsize_t)dblock_size);
    blk_off = (size_t)(obj_off - dblock->block_off);

    /* Point to location for object */
    p = dblock->blk + blk_off;

    /* Copy the object's data into the heap */
    HDmemcpy(obj, p, obj_len);

    /* Unlock direct block */
    if(H5AC_unprotect(hdr->f, dxpl_id, H5AC_FHEAP_DBLOCK, dblock_addr, dblock, H5AC__NO_FLAGS_SET) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap direct block")
    dblock = NULL;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_man_read() */


/*-------------------------------------------------------------------------
 * Function:	H5HF_man_remove
 *
 * Purpose:	Remove an object from a managed heap
 *
 * Return:	SUCCEED/FAIL
 *
 * Programmer:	Quincey Koziol
 *		koziol@ncsa.uiuc.edu
 *		May 15 2006
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5HF_man_remove(H5HF_hdr_t *hdr, hid_t dxpl_id, hsize_t obj_off, size_t obj_len)
{
    H5HF_free_section_t *sec_node;      /* Pointer to free space section for block */
    H5HF_direct_t *dblock;              /* Pointer to direct block to query */
    haddr_t dblock_addr;                /* Direct block address */
    size_t dblock_size;                 /* Direct block size */
    size_t blk_off;                     /* Offset of object in block */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_NOAPI_NOINIT(H5HF_man_remove)
#ifdef QAK
HDfprintf(stderr, "%s: obj_off = %Hu, obj_len = %Zu\n", FUNC, obj_off, obj_len);
#endif /* QAK */

    /*
     * Check arguments.
     */
    HDassert(hdr);
    HDassert(obj_off > 0);
    HDassert(obj_len > 0);

    /* Check for bad offset or length */
    if(obj_off > hdr->man_size)
        HGOTO_ERROR(H5E_HEAP, H5E_BADRANGE, FAIL, "fractal heap object offset too large")
    if(obj_len > hdr->man_dtable.cparam.max_direct_size)
        HGOTO_ERROR(H5E_HEAP, H5E_BADRANGE, FAIL, "fractal heap object size too large for direct block")
    if(obj_len > hdr->standalone_size)
        HGOTO_ERROR(H5E_HEAP, H5E_BADRANGE, FAIL, "fractal heap object should be standalone")

    /* Check for root direct block */
    if(hdr->man_dtable.curr_root_rows == 0) {
#ifdef QAK
HDfprintf(stderr, "%s: direct root block\n", FUNC);
#endif /* QAK */
        /* Set direct block info */
        dblock_addr = hdr->man_dtable.table_addr;
        dblock_size = hdr->man_dtable.cparam.start_block_size;

        /* Lock direct block */
        if(NULL == (dblock = H5HF_man_dblock_protect(hdr, dxpl_id, dblock_addr, dblock_size, NULL, 0, H5AC_WRITE)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to protect fractal heap direct block")
    } /* end if */
    else {
        H5HF_indirect_t *iblock;        /* Pointer to indirect block */
        unsigned entry;                 /* Entry of block */

#ifdef QAK
HDfprintf(stderr, "%s: indirect root block\n", FUNC);
#endif /* QAK */
        /* Look up indirect block containing direct block */
        if(H5HF_man_locate_block(hdr, dxpl_id, obj_off, FALSE, &iblock, &entry, H5AC_WRITE) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTCOMPUTE, FAIL, "can't compute row & column of section")
#ifdef QAK
HDfprintf(stderr, "%s: entry address = %a\n", FUNC, iblock->ents[entry].addr);
#endif /* QAK */

        /* Set direct block info */
        dblock_addr =  iblock->ents[entry].addr;
        dblock_size =  hdr->man_dtable.row_block_size[entry / hdr->man_dtable.cparam.width];

        /* Lock direct block */
        if(NULL == (dblock = H5HF_man_dblock_protect(hdr, dxpl_id, dblock_addr, dblock_size, iblock, entry, H5AC_WRITE)))
            HGOTO_ERROR(H5E_HEAP, H5E_CANTPROTECT, FAIL, "unable to protect fractal heap direct block")

        /* Unlock indirect block */
        if(H5AC_unprotect(hdr->f, dxpl_id, H5AC_FHEAP_IBLOCK, iblock->addr, iblock, H5AC__NO_FLAGS_SET) < 0)
            HGOTO_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap indirect block")
        iblock = NULL;
    } /* end else */
#ifdef QAK
HDfprintf(stderr, "%s: dblock_addr = %a, dblock_size = %Zu\n", FUNC, dblock_addr, dblock_size);
#endif /* QAK */

    /* Compute offset of object within block */
    HDassert((obj_off - dblock->block_off) < (hsize_t)dblock_size);
    blk_off = (size_t)(obj_off - dblock->block_off);
#ifdef QAK
HDfprintf(stderr, "%s: blk_off = %Zu\n", FUNC, blk_off);
#endif /* QAK */

    /* Create free space section node */
    if(NULL == (sec_node = H5HF_sect_single_new(obj_off, obj_len,
            dblock->parent, dblock->par_entry, dblock_addr, dblock_size)))
        HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, FAIL, "can't create section for direct block's free space")

    /* Unlock direct block */
    if(H5AC_unprotect(hdr->f, dxpl_id, H5AC_FHEAP_DBLOCK, dblock_addr, dblock, H5AC__NO_FLAGS_SET) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTUNPROTECT, FAIL, "unable to release fractal heap direct block")
    dblock = NULL;

    /* Update statistics about heap */
    hdr->nobjs--;

    /* Reduce space available in heap */
    if(H5HF_hdr_adj_free(hdr, (ssize_t)obj_len) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTDEC, FAIL, "can't adjust free space for heap")

    /* Return free space to the heap's list of space */
    if(H5HF_space_return(hdr, dxpl_id, sec_node) < 0)
        HGOTO_ERROR(H5E_HEAP, H5E_CANTINIT, FAIL, "can't add direct block free space to global list")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5HF_man_remove() */

