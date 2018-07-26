#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "SDIOutputStream.hpp"

#include "sdiout.hpp"

#include <vlc_modules.h>
#include <vlc_codec.h>
#include <vlc_meta.h>
#include <vlc_block.h>

using namespace sdi_sout;

AbstractStream::AbstractStream(vlc_object_t *p_obj)
{
    p_stream = p_obj;
}

AbstractStream::~AbstractStream()
{

}

struct decoder_owner
{
    decoder_t dec;
    AbstractDecodedStream *id;
    bool b_error;
    es_format_t last_fmt_update;
    es_format_t decoder_out;
};

AbstractDecodedStream::AbstractDecodedStream(vlc_object_t *p_obj)
    : AbstractStream(p_obj)
{
    p_decoder = NULL;
    es_format_Init(&requestedoutput, 0, 0);
}

AbstractDecodedStream::~AbstractDecodedStream()
{
    es_format_Clean(&requestedoutput);

    if(!p_decoder)
        return;

    struct decoder_owner *p_owner;
    p_owner = container_of(p_decoder, struct decoder_owner, dec);
    if(p_decoder->p_module)
        module_unneed(p_decoder, p_decoder->p_module);
    es_format_Clean(&p_owner->dec.fmt_in);
    es_format_Clean(&p_owner->dec.fmt_out);
    es_format_Clean(&p_owner->decoder_out);
    es_format_Clean(&p_owner->last_fmt_update);
    if(p_decoder->p_description)
        vlc_meta_Delete(p_decoder->p_description);
    vlc_object_release(p_decoder);
}

bool AbstractDecodedStream::init(const es_format_t *p_fmt)
{
    const char *category;
    if(p_fmt->i_cat == VIDEO_ES)
        category = "video decoder";
    else if(p_fmt->i_cat == AUDIO_ES)
        category = "audio decoder";
    else
        return false;

    /* Create decoder object */
    struct decoder_owner * p_owner =
            reinterpret_cast<struct decoder_owner *>(
                vlc_object_create(p_stream, sizeof(*p_owner)));
    if(!p_owner)
        return false;

    es_format_Init(&p_owner->decoder_out, p_fmt->i_cat, 0);
    es_format_Init(&p_owner->last_fmt_update, p_fmt->i_cat, 0);
    p_owner->b_error = false;
    p_owner->id = this;

    p_decoder = &p_owner->dec;
    p_decoder->p_module = NULL;
    es_format_Init(&p_decoder->fmt_out, p_fmt->i_cat, 0);
    es_format_Copy(&p_decoder->fmt_in, p_fmt);
    p_decoder->b_frame_drop_allowed = false;

    setCallbacks();

    p_decoder->pf_decode = NULL;
    p_decoder->pf_get_cc = NULL;

    p_decoder->p_module = module_need_var(p_decoder, category, "codec");
    if(!p_decoder->p_module)
    {
        msg_Err(p_stream, "cannot find %s for %4.4s", category, (char *)&p_fmt->i_codec);
        es_format_Clean(&p_decoder->fmt_in);
        es_format_Clean(&p_decoder->fmt_out);
        es_format_Clean(&p_owner->decoder_out);
        es_format_Clean(&p_owner->last_fmt_update);
        vlc_object_release(p_decoder);
        p_decoder = NULL;
        return false;
    }

    return true;
}

int AbstractDecodedStream::Send(block_t *p_block)
{
    assert(p_decoder || !p_block);
    if(!p_block)
        return VLC_EGENERIC;
    struct decoder_owner *p_owner =
            container_of(p_decoder, struct decoder_owner, dec);

     if(!p_owner->b_error)
    {
        int ret = p_decoder->pf_decode(p_decoder, p_block);
        switch(ret)
        {
            case VLCDEC_SUCCESS:
                break;
            case VLCDEC_ECRITICAL:
                p_owner->b_error = true;
                break;
            case VLCDEC_RELOAD:
                p_owner->b_error = true;
                block_Release(p_block);
                break;
            default:
                vlc_assert_unreachable();
        }
    }

    return p_owner->b_error ? VLC_EGENERIC : VLC_SUCCESS;
}

void AbstractDecodedStream::Flush()
{

}

void AbstractDecodedStream::setOutputFormat(const es_format_t *p_fmt)
{
    es_format_Clean(&requestedoutput);
    es_format_Copy(&requestedoutput, p_fmt);
}

VideoDecodedStream::VideoDecodedStream(vlc_object_t *p_obj)
    :AbstractDecodedStream(p_obj)
{
    p_filters_chain = NULL;
}

VideoDecodedStream::~VideoDecodedStream()
{
    if(p_filters_chain)
        filter_chain_Delete(p_filters_chain);
}

void VideoDecodedStream::setCallbacks()
{
    static struct decoder_owner_callbacks dec_cbs;
    memset(&dec_cbs, 0, sizeof(dec_cbs));
    dec_cbs.video.format_update = VideoDecCallback_update_format;
    dec_cbs.video.buffer_new = VideoDecCallback_new_buffer;
    dec_cbs.video.queue = VideoDecCallback_queue;

    p_decoder->cbs = &dec_cbs;
}

void VideoDecodedStream::VideoDecCallback_queue(decoder_t *p_dec, picture_t *p_pic)
{
    struct decoder_owner *p_owner;
    p_owner = container_of(p_dec, struct decoder_owner, dec);
    static_cast<VideoDecodedStream *>(p_owner->id)->Output(p_pic);
}

int VideoDecodedStream::VideoDecCallback_update_format(decoder_t *p_dec)
{
    struct decoder_owner *p_owner;
    p_owner = container_of(p_dec, struct decoder_owner, dec);

    /* fixup */
    p_dec->fmt_out.video.i_chroma = p_dec->fmt_out.i_codec;

    es_format_Clean(&p_owner->last_fmt_update);
    es_format_Copy(&p_owner->last_fmt_update, &p_dec->fmt_out);

    return VLC_SUCCESS;
}

picture_t *VideoDecodedStream::VideoDecCallback_new_buffer(decoder_t *p_dec)
{
    return picture_NewFromFormat(&p_dec->fmt_out.video);
}


static picture_t *transcode_video_filter_buffer_new(filter_t *p_filter)
{
    p_filter->fmt_out.video.i_chroma = p_filter->fmt_out.i_codec;
    return picture_NewFromFormat(&p_filter->fmt_out.video);
}

static const struct filter_video_callbacks transcode_filter_video_cbs =
{
    .buffer_new = transcode_video_filter_buffer_new,
};

filter_chain_t * VideoDecodedStream::VideoFilterCreate(const es_format_t *p_srcfmt)
{
    filter_chain_t *p_chain;
    filter_owner_t owner;
    memset(&owner, 0, sizeof(owner));
    owner.video = &transcode_filter_video_cbs;

    es_format_t targetfmt;
    es_format_InitFromVideo(&targetfmt, &p_srcfmt->video);
    targetfmt.video.i_chroma = targetfmt.i_codec = VLC_CODEC_I422_10L;

    p_chain = filter_chain_NewVideo(p_stream, false, &owner);
    if(!p_chain)
        return NULL;
    filter_chain_Reset(p_chain, p_srcfmt, &targetfmt);

    if(p_srcfmt->video.i_chroma != targetfmt.video.i_chroma)
    {
        if(filter_chain_AppendConverter(p_chain, p_srcfmt, &targetfmt) != VLC_SUCCESS)
        {
            msg_Err(p_stream, "FAILED 0 %4.4s %4.4s", &p_srcfmt->i_codec, &targetfmt.i_codec);
            es_format_Clean(&targetfmt);
            filter_chain_Delete(p_chain);
            return NULL;
        }
    }

    const es_format_t *p_fmt_out = filter_chain_GetFmtOut(p_chain);
    if(!es_format_IsSimilar(&targetfmt, p_fmt_out))
    {
        msg_Err(p_stream, "FAILED 1");
        es_format_Clean(&targetfmt);
        filter_chain_Delete(p_chain);
        return NULL;
    }

    return p_chain;
}

void VideoDecodedStream::FlushQueued()
{
    picture_t *p;
    while(p = Dequeue())
        picture_Release(p);
}

void VideoDecodedStream::Output(picture_t *p_pic)
{
    struct decoder_owner *p_owner;
    p_owner = container_of(p_decoder, struct decoder_owner, dec);

    if(!es_format_IsSimilar(&p_owner->last_fmt_update, &p_owner->decoder_out))
    {

        msg_Err(p_stream, "decoder output format now %4.4s",
                (char*)&p_owner->last_fmt_update.i_codec);

        if(p_filters_chain)
            filter_chain_Delete(p_filters_chain);
        p_filters_chain = VideoFilterCreate(&p_owner->last_fmt_update);
        if(!p_filters_chain)
        {
            picture_Release(p_pic);
            return;
        }

        es_format_Clean(&p_owner->decoder_out);
        es_format_Copy(&p_owner->decoder_out, &p_owner->last_fmt_update);
    }

    if(p_filters_chain)
        p_pic = filter_chain_VideoFilter(p_filters_chain, p_pic);

    if(p_pic)
        Enqueue(p_pic);
}

AudioDecodedStream::AudioDecodedStream(vlc_object_t *p_stream)
    : AbstractDecodedStream(p_stream)
{
    p_filters = NULL;
}

AudioDecodedStream::~AudioDecodedStream()
{
    if(p_filters)
        aout_FiltersDelete(p_stream, p_filters);
}

void AudioDecodedStream::AudioDecCallback_queue(decoder_t *p_dec, block_t *p_block)
{
    struct decoder_owner *p_owner;
    p_owner = container_of(p_dec, struct decoder_owner, dec);
    static_cast<AudioDecodedStream *>(p_owner->id)->Output(p_block);
}

void AudioDecodedStream::FlushQueued()
{
    block_t *p;
    while((p = Dequeue()))
        block_Release(p);
}

void AudioDecodedStream::Output(block_t *p_block)
{
    struct decoder_owner *p_owner;
    p_owner = container_of(p_decoder, struct decoder_owner, dec);

    if(!es_format_IsSimilar(&p_owner->last_fmt_update, &p_owner->decoder_out))
    {
        msg_Err(p_stream, "decoder output format now %4.4s",
                (char*)&p_owner->last_fmt_update.i_codec);

        if(p_filters)
            aout_FiltersDelete(p_stream, p_filters);
        p_filters = AudioFiltersCreate(&p_owner->last_fmt_update);
        if(!p_filters)
        {
            msg_Err(p_stream, "filter issue");
            block_Release(p_block);
            return;
        }

        es_format_Clean(&p_owner->decoder_out);
        es_format_Copy(&p_owner->decoder_out, &p_owner->last_fmt_update);
    }

    /* Run filter chain */
    if(p_filters)
        p_block = aout_FiltersPlay(p_filters, p_block, 1.f);

    if(p_block && !p_block->i_nb_samples)
    {
        p_block->i_nb_samples = p_block->i_buffer / (2 * 2);
    }

    if(p_block)
      Enqueue(p_block);
}

aout_filters_t * AudioDecodedStream::AudioFiltersCreate(const es_format_t *afmt)
{
    es_format_t targetfmt;
    es_format_Init(&targetfmt, AUDIO_ES, VLC_CODEC_S16N);
    targetfmt.audio.i_format = targetfmt.i_codec;
    targetfmt.audio.i_channels = 2;
    targetfmt.audio.i_physical_channels = AOUT_CHANS_STEREO;
    targetfmt.audio.i_rate = 48000;
    targetfmt.audio.i_bitspersample = 16;
    targetfmt.audio.i_blockalign = targetfmt.audio.i_channels * targetfmt.audio.i_bitspersample /8 ;
    targetfmt.audio.i_frame_length = FRAME_SIZE;

    return aout_FiltersNew(p_stream, &afmt->audio, &targetfmt.audio, NULL, NULL);
}

int AudioDecodedStream::AudioDecCallback_update_format(decoder_t *p_dec)
{
    struct decoder_owner *p_owner;
    p_owner = container_of(p_dec, struct decoder_owner, dec);

    if( !AOUT_FMT_LINEAR(&p_dec->fmt_out.audio) )
        return VLC_EGENERIC;

    /* fixup */
    p_dec->fmt_out.audio.i_format = p_dec->fmt_out.i_codec;
    aout_FormatPrepare(&p_dec->fmt_out.audio);

    es_format_Clean(&p_owner->last_fmt_update);
    es_format_Copy(&p_owner->last_fmt_update, &p_dec->fmt_out);

    p_owner->last_fmt_update.audio.i_format = p_owner->last_fmt_update.i_codec;

    return VLC_SUCCESS;
}

void AudioDecodedStream::setCallbacks()
{
    static struct decoder_owner_callbacks dec_cbs;
    memset(&dec_cbs, 0, sizeof(dec_cbs));
    dec_cbs.audio.format_update = AudioDecCallback_update_format;
    dec_cbs.audio.queue = AudioDecCallback_queue;
    p_decoder->cbs = &dec_cbs;
}