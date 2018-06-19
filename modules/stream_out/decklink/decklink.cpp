/*****************************************************************************
 * decklink.cpp: decklink sout module for vlc
 *****************************************************************************
 * Copyright © 2014-2016 VideoLAN
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#include "decklink.hpp"

#include <vlc_common.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>
#include <vlc_block.h>
#include <vlc_codec.h>
#include <vlc_es.h>
#include <vlc_sout.h>
#include <vlc_filter.h>

#include <cassert>

struct sout_stream_sys_t
{
    sout_stream_t     *p_out;

    decklink_sys_t    *p_decklink_sys;
};


struct sout_stream_id_sys_t
{
    void *id;

    /* Decoder */
    decoder_t       *p_decoder;
    es_format_t      decoder_lastfmtout;

    union
    {
         struct
         {
             filter_chain_t  *p_f_chain; /**< Video filters */
             filter_chain_t  *p_uf_chain; /**< User-specified video filters */
             video_format_t  fmt_input_video;
             video_format_t  video_dec_out; /* only rw from pf_vout_format_update() */
         };
         struct
         {
             struct aout_filters    *p_af_chain; /**< Audio filters */
             audio_format_t  fmt_audio;
             audio_format_t  audio_dec_out; /* only rw from pf_aout_format_update() */
         };

    };
};

struct decoder_owner
{
    decoder_t dec;
    sout_stream_t *p_stream;
    sout_stream_id_sys_t *id;
    bool b_error;
};

static inline struct decoder_owner *dec_get_owner( decoder_t *p_dec )
{
    return container_of( p_dec, struct decoder_owner, dec );
}

#define SOUT_CFG_PREFIX "sout-decklink-"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

static const char *const ppsz_sout_options[] = {
    "ip", "port",  "http-port", "mux", "mime", "video", NULL
};

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()

    set_shortname(N_("Decklink"))
    set_description(N_("Decklink stream output"))
    set_capability("sout stream", 0)
    add_shortcut("sout-decklink")
    set_category(CAT_SOUT)
    set_subcategory(SUBCAT_SOUT_STREAM)
    set_callbacks(Open, Close)

vlc_module_end ()

/*****************************************************************************
 * Sout callbacks
 *****************************************************************************/

static void decoder_queue_video( decoder_t *p_dec, picture_t *p_pic );

static int video_update_format_decoder( decoder_t *p_dec )
{
    p_dec->fmt_out.video.i_chroma = p_dec->fmt_out.i_codec;
    return 0;
}

static picture_t *video_new_buffer_decoder( decoder_t *p_dec )
{
    return picture_NewFromFormat( &p_dec->fmt_out.video );
}

static picture_t *transcode_video_filter_buffer_new( filter_t *p_filter )
{
    p_filter->fmt_out.video.i_chroma = p_filter->fmt_out.i_codec;
    return picture_NewFromFormat( &p_filter->fmt_out.video );
}

static const struct filter_video_callbacks transcode_filter_video_cbs =
{
    .buffer_new = transcode_video_filter_buffer_new,
};

static filter_chain_t * transcode_video_filter_create( sout_stream_t *p_stream,
                                                       const es_format_t *p_srcfmt )
{
    filter_chain_t *p_chain;
    filter_owner_t owner;
    memset( &owner, 0, sizeof(owner) );
    owner.video = &transcode_filter_video_cbs;

    es_format_t targetfmt;
    es_format_InitFromVideo( &targetfmt, &p_srcfmt->video );
    targetfmt.video.i_chroma = targetfmt.i_codec = VLC_CODEC_I422_10L;

    p_chain = filter_chain_NewVideo( p_stream, false, &owner );
    if( !p_chain )
        return NULL;

    filter_chain_Reset( p_chain, p_srcfmt, &targetfmt );

    if( p_srcfmt->video.i_chroma != targetfmt.video.i_chroma )
    {
        if( filter_chain_AppendFilter( p_chain, NULL, NULL,
                                       p_srcfmt, &targetfmt ) == NULL )
        {
            msg_Err(p_stream, "FAILED 0");
            es_format_Clean( &targetfmt );
            filter_chain_Delete( p_chain );
            return NULL;
        }
    }

    const es_format_t *p_fmt_out = filter_chain_GetFmtOut( p_chain );
    if( !es_format_IsSimilar( &targetfmt, p_fmt_out ) )
    {
        msg_Err(p_stream, "FAILED 1");
        es_format_Clean( &targetfmt );
        filter_chain_Delete( p_chain );
        return NULL;
    }

    return p_chain;
}

static void decoder_queue_video( decoder_t *p_dec, picture_t *p_pic )
{
    struct decoder_owner *p_owner = dec_get_owner( p_dec );
    sout_stream_id_sys_t *id = p_owner->id;
    sout_stream_sys_t *p_sys = (sout_stream_sys_t *) p_owner->p_stream->p_sys;

    if( !es_format_IsSimilar( &id->p_decoder->fmt_out, &id->decoder_lastfmtout ) )
    {

        msg_Err( p_owner->p_stream, "decoder output format now %4.4s", (char*)&id->p_decoder->fmt_out.i_codec );

        id->p_f_chain = transcode_video_filter_create( p_owner->p_stream, &id->p_decoder->fmt_out );
        if( !id->p_f_chain )
        {
            picture_Release( p_pic );
            return;
        }
        es_format_Clean( &id->decoder_lastfmtout );
        es_format_Copy( &id->decoder_lastfmtout, &id->p_decoder->fmt_out );

        if( p_sys->p_decklink_sys == NULL )
        {
            p_sys->p_decklink_sys = OpenVideo(VLC_OBJECT(p_owner->p_stream),
                                              & filter_chain_GetFmtOut( id->p_f_chain )->video );
            if( p_sys->p_decklink_sys == NULL )
            {
                picture_Release( p_pic );
                return;
            }
        }
    }

    p_pic = filter_chain_VideoFilter( id->p_f_chain, p_pic );
    if( p_pic )
        DisplayVideo(VLC_OBJECT(p_owner->p_stream), p_sys->p_decklink_sys, p_pic, NULL );

}

static void *Add(sout_stream_t *p_stream, const es_format_t *p_fmt)
{
    sout_stream_sys_t *p_sys = (sout_stream_sys_t *) p_stream->p_sys;
    sout_stream_id_sys_t *id;

    if( p_fmt->i_cat != VIDEO_ES  )
        return NULL;

    id =( sout_stream_id_sys_t * ) calloc( 1, sizeof( sout_stream_id_sys_t ) );
    if( !id )
        return NULL;

    /* Create decoder object */
    struct decoder_owner * p_owner = (struct decoder_owner *)
                                     vlc_object_create( p_stream, sizeof( *p_owner ) );
    if( !p_owner )
        return NULL;
    p_owner->p_stream = p_stream;
    p_owner->b_error = false;
    p_owner->id = id;

    id->p_decoder = &p_owner->dec;
    id->p_decoder->p_module = NULL;
    es_format_Init( &id->p_decoder->fmt_out, p_fmt->i_cat, 0 );
    es_format_Copy( &id->p_decoder->fmt_in, p_fmt );
    id->p_decoder->b_frame_drop_allowed = false;

//    es_format_Init( &id->p_decoder->fmt_in, 0, 0 );
//    es_format_Copy( &id->p_decoder->fmt_in, p_fmt );
//    id->p_decoder->pf_decode_video = NULL;
//    id->p_decoder->pf_get_cc = NULL;
//    id->p_decoder->pf_vout_format_update = video_update_format_decoder;
//    id->p_decoder->pf_vout_buffer_new = video_new_buffer_decoder;

    //es_format_InitFromVideo( &id->p_decoder->fmt_out, &p_fmt->video );
    //id->p_decoder->fmt_out.video.i_chroma = id->p_decoder->fmt_out.i_codec = 0;//VLC_CODEC_I422_10L;

    static struct decoder_owner_callbacks dec_cbs;
    memset( &dec_cbs, 0, sizeof(dec_cbs) );
    dec_cbs.video.format_update = video_update_format_decoder;
    dec_cbs.video.buffer_new = video_new_buffer_decoder;
    dec_cbs.video.queue = decoder_queue_video;

    id->p_decoder->cbs = &dec_cbs;

    id->p_decoder->pf_decode = NULL;
    id->p_decoder->pf_get_cc = NULL;

    id->p_decoder->p_module = module_need_var( id->p_decoder, "video decoder", "codec" );

    if( !id->p_decoder->p_module )
    {
        msg_Err( p_stream, "cannot find video decoder" );
        //vlc_object_release( id->p_decoder );
        return NULL;
    }

    es_format_Copy( &id->decoder_lastfmtout, &id->p_decoder->fmt_out );

    msg_Err( p_stream, "chroma %4.4s", (char*)&id->p_decoder->fmt_out.i_codec );

    //id->p_f_chain = transcode_video_filter_create( p_stream, &id->p_decoder->fmt_out );

    //msg_Err( p_stream, "chain %x", id->p_f_chain );

    return id;
}


static void Del(sout_stream_t *p_stream, void *id_)
{
    sout_stream_sys_t *p_sys = (sout_stream_sys_t *) p_stream->p_sys;
    sout_stream_id_sys_t *id = (sout_stream_id_sys_t *) id_;

    if( id->p_decoder )
    {
        module_unneed( id->p_decoder, id->p_decoder->p_module );
        vlc_object_release( id->p_decoder );
    }

    if( id->p_f_chain )
        filter_chain_Delete( id->p_f_chain );

    es_format_Clean( &id->decoder_lastfmtout );

    free( id );
}

static int Send(sout_stream_t *p_stream, void *id_, block_t *p_block)
{
    sout_stream_sys_t *p_sys = (sout_stream_sys_t *) p_stream->p_sys;
    sout_stream_id_sys_t *id = (sout_stream_id_sys_t *) id_;
    struct decoder_owner *p_owner = dec_get_owner( id->p_decoder );

    //msg_Err(p_stream, "DEC %ld %ld", p_buffer->i_dts, p_buffer->i_flags);
    if( !p_owner->b_error )
    {
        int ret = id->p_decoder->pf_decode( id->p_decoder, p_block );
        switch( ret )
        {
            case VLCDEC_SUCCESS:
                break;
            case VLCDEC_ECRITICAL:
                p_owner->b_error = true;
                break;
            case VLCDEC_RELOAD:
                p_owner->b_error = true;
                block_Release( p_block );
                break;
            default:
                vlc_assert_unreachable();
        }
    }

    return p_owner->b_error ? VLC_EGENERIC : VLC_SUCCESS;
}

static void Flush( sout_stream_t *p_stream, void *id_ )
{
    sout_stream_sys_t *p_sys = (sout_stream_sys_t *) p_stream->p_sys;
    sout_stream_id_sys_t *id = (sout_stream_id_sys_t *) id_;

    //sout_StreamFlush( p_sys->p_out, id );
}

static int Control(sout_stream_t *p_stream, int i_query, va_list args)
{
    sout_stream_sys_t *p_sys = (sout_stream_sys_t *) p_stream->p_sys;
return VLC_EGENERIC;
    if ( !p_sys->p_out->pf_control )
        return VLC_EGENERIC;

    return p_sys->p_out->pf_control( p_sys->p_out, i_query, args );
}

/*****************************************************************************
 * Open: connect to the Chromecast and initialize the sout
 *****************************************************************************/
static int Open(vlc_object_t *p_this)
{
    sout_stream_t *p_stream = reinterpret_cast<sout_stream_t*>(p_this);
    sout_stream_sys_t *p_sys = new sout_stream_sys_t;

    p_sys->p_decklink_sys = NULL;
    p_sys->p_out = NULL;
//    config_ChainParse(p_stream, SOUT_CFG_PREFIX, ppsz_sout_options, p_stream->p_cfg);

    /*p_sys = new(std::nothrow) sout_stream_sys_t( p_intf, b_has_video, i_local_server_port,
                                                 psz_mux, psz_var_mime );
    if (unlikely(p_sys == NULL))
        goto error;*/

    // Set the sout callbacks.
    p_stream->pf_add     = Add;
    p_stream->pf_del     = Del;
    p_stream->pf_send    = Send;
    p_stream->pf_flush   = Flush;
    p_stream->pf_control = Control;

    p_stream->p_sys = p_sys;
    return VLC_SUCCESS;

error:
    delete p_sys;
    return VLC_EGENERIC;
}

/*****************************************************************************
 * Close: destroy interface
 *****************************************************************************/
static void Close(vlc_object_t *p_this)
{
    sout_stream_t *p_stream = reinterpret_cast<sout_stream_t*>(p_this);

    delete p_stream->p_sys;
}

