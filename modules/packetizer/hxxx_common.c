/*****************************************************************************
 * hxxx_common.c: AVC/HEVC packetizers shared code
 *****************************************************************************
 * Copyright (C) 2001-2015 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "hxxx_common.h"

#include <vlc_block.h>
#include <vlc_codec.h>
#include "../codec/cc.h"

/****************************************************************************
 * Closed captions handling
 ****************************************************************************/
struct cc_storage_t
{
    uint32_t i_flags;
    mtime_t i_dts;
    mtime_t i_pts;
    cc_data_t current;
    cc_data_t next;
};

cc_storage_t * cc_storage_new( void )
{
    cc_storage_t *p_ccs = malloc( sizeof(*p_ccs) );
    if( likely(p_ccs) )
    {
        p_ccs->i_pts = VLC_TS_INVALID;
        p_ccs->i_dts = VLC_TS_INVALID;
        p_ccs->i_flags = 0;
        cc_Init( &p_ccs->current );
        cc_Init( &p_ccs->next );
    }
    return p_ccs;
}

void cc_storage_delete( cc_storage_t *p_ccs )
{
    cc_Exit( &p_ccs->current );
    cc_Exit( &p_ccs->next );
    free( p_ccs );
}

void cc_storage_reset( cc_storage_t *p_ccs )
{
    cc_Flush( &p_ccs->next );
}

void cc_storage_append( cc_storage_t *p_ccs, bool b_top_field_first,
                        const uint8_t *p_buf, size_t i_buf )
{
    cc_Extract( &p_ccs->next, CC_PAYLOAD_GA94, b_top_field_first, p_buf, i_buf );
}

void cc_storage_commit( cc_storage_t *p_ccs, block_t *p_pic )
{
    p_ccs->i_pts = p_pic->i_pts;
    p_ccs->i_dts = p_pic->i_dts;
    p_ccs->i_flags = p_pic->i_flags;
    p_ccs->current = p_ccs->next;
    cc_Flush( &p_ccs->next );
}

vlc_ancillary_t * cc_storage_copy_last_vanc( cc_storage_t *p_ccs )
{
    if( !p_ccs->next.i_data )
        return NULL;

    vlc_ancillary_t *p_anc = vlc_ancillary_New( ANCILLARY_CLOSED_CAPTIONS );
    if( p_anc )
    {
        p_anc->cc.p_data = malloc( p_ccs->next.i_data );
        if( p_anc->cc.p_data )
        {
            memcpy( p_anc->cc.p_data, p_ccs->next.p_data,
                                      p_ccs->next.i_data );
            p_anc->cc.i_data = p_ccs->next.i_data;
        }
    }
    return p_anc;
}

block_t * cc_storage_get_current( cc_storage_t *p_ccs, bool pb_present[4] )
{
    block_t *p_block;

    for( int i = 0; i < 4; i++ )
        pb_present[i] = p_ccs->current.pb_present[i];

    if( p_ccs->current.i_data <= 0 )
        return NULL;

    p_block = block_Alloc( p_ccs->current.i_data);
    if( p_block )
    {
        memcpy( p_block->p_buffer, p_ccs->current.p_data, p_ccs->current.i_data );
        p_block->i_dts =
        p_block->i_pts = p_ccs->current.b_reorder ? p_ccs->i_pts : p_ccs->i_dts;
        p_block->i_flags = ( p_ccs->current.b_reorder  ? p_ccs->i_flags : BLOCK_FLAG_TYPE_P ) & BLOCK_FLAG_TYPE_MASK;
    }
    cc_Flush( &p_ccs->current );

    return p_block;
}

/****************************************************************************
 * PacketizeXXC1: Takes VCL blocks of data and creates annexe B type NAL stream
 * Will always use 4 byte 0 0 0 1 startcodes
 * Will prepend a SPS and PPS before each keyframe
 ****************************************************************************/
block_t *PacketizeXXC1( decoder_t *p_dec, uint8_t i_nal_length_size,
                        block_t **pp_block, pf_annexb_nal_packetizer pf_nal_parser )
{
    block_t       *p_block;
    block_t       *p_ret = NULL;
    uint8_t       *p;

    if( !pp_block || !*pp_block )
        return NULL;
    if( (*pp_block)->i_flags&(BLOCK_FLAG_CORRUPTED) )
    {
        block_Release( *pp_block );
        return NULL;
    }

    p_block = *pp_block;
    *pp_block = NULL;

    for( p = p_block->p_buffer; p < &p_block->p_buffer[p_block->i_buffer]; )
    {
        bool b_dummy;
        int i_size = 0;
        int i;

        if( &p_block->p_buffer[p_block->i_buffer] - p < i_nal_length_size )
            break;

        for( i = 0; i < i_nal_length_size; i++ )
        {
            i_size = (i_size << 8) | (*p++);
        }

        if( i_size <= 0 ||
            i_size > ( p_block->p_buffer + p_block->i_buffer - p ) )
        {
            msg_Err( p_dec, "Broken frame : size %d is too big", i_size );
            break;
        }

        /* Convert AVC to AnnexB */
        block_t *p_nal;
        /* If data exactly match remaining bytes (1 NAL only or trailing one) */
        if( i_size == p_block->p_buffer + p_block->i_buffer - p )
        {
            p_block->i_buffer = i_size;
            p_block->p_buffer = p;
            p_nal = block_Realloc( p_block, 4, i_size );
            if( p_nal )
                p_block = NULL;
        }
        else
        {
            p_nal = block_Alloc( 4 + i_size );
            if( p_nal )
            {
                p_nal->i_dts = p_block->i_dts;
                p_nal->i_pts = p_block->i_pts;
                /* Copy nalu */
                memcpy( &p_nal->p_buffer[4], p, i_size );
            }
            p += i_size;
        }

        if( !p_nal )
            break;

        /* Add start code */
        p_nal->p_buffer[0] = 0x00;
        p_nal->p_buffer[1] = 0x00;
        p_nal->p_buffer[2] = 0x00;
        p_nal->p_buffer[3] = 0x01;

        /* Parse the NAL */
        block_t *p_pic;
        if( ( p_pic = pf_nal_parser( p_dec, &b_dummy, p_nal ) ) )
        {
            block_ChainAppend( &p_ret, p_pic );
        }

        if( !p_block )
            break;
    }

    if( p_block )
        block_Release( p_block );

    return p_ret;
}
