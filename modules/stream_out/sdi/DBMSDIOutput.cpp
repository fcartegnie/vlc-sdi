#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "DBMSDIOutput.hpp"
#include "SDIOutputStream.hpp"
#include "Ancillary.hpp"
#include "V210.hpp"

#include <DeckLinkAPIDispatch.cpp>
#include "sdiout.hpp"

#include <vlc_es.h>
#include <vlc_picture.h>
#include <vlc_interrupt.h>
#include <vlc_image.h>
#include <arpa/inet.h>

using namespace sdi_sout;

DBMSDIOutput::DBMSDIOutput(sout_stream_t *p_stream) :
    SDIOutput(p_stream)
{
    p_card = NULL;
    p_output = NULL;
    offset = 0;
    es_format_Init(&video.configuredfmt, VIDEO_ES, 0);
    es_format_Init(&audio.configuredfmt, VIDEO_ES, 0);
    video.tenbits = var_InheritBool(p_stream, CFG_PREFIX "tenbits");
    video.nosignal_delay = var_InheritInteger(p_stream, CFG_PREFIX "nosignal-delay");
    video.pic_nosignal = NULL;
    audio.i_rate = var_InheritInteger(p_stream, CFG_PREFIX "audio-rate");;
    ancillary.afd = var_InheritInteger(p_stream, CFG_PREFIX "afd");
    ancillary.ar = var_InheritInteger(p_stream, CFG_PREFIX "ar");
    ancillary.afd_line = var_InheritInteger(p_stream, CFG_PREFIX "afd-line");
    videoStream = NULL;
    audioStream = NULL;
    captionsStream = NULL;
    lasttimestamp = 0;
    b_running = false;
}

DBMSDIOutput::~DBMSDIOutput()
{
    if(video.pic_nosignal)
        picture_Release(video.pic_nosignal);
    es_format_Clean(&video.configuredfmt);
    es_format_Clean(&audio.configuredfmt);
    if(p_output)
    {
        BMDTimeValue out;
        p_output->StopScheduledPlayback(lasttimestamp, &out, timescale);
        p_output->DisableVideoOutput();
        p_output->DisableAudioOutput();
        p_output->Release();
    }
    if(p_card)
        p_card->Release();
}

AbstractStream *DBMSDIOutput::Add(const es_format_t *fmt)
{
    AbstractStream *s = NULL;
    if(fmt->i_cat == VIDEO_ES && !videoStream)
    {
        if(ConfigureVideo(&fmt->video) == VLC_SUCCESS)
            s = videoStream = dynamic_cast<VideoDecodedStream *>(SDIOutput::Add(fmt));
        videoStream->setOutputFormat(&video.configuredfmt);
    }
    else if(fmt->i_cat == AUDIO_ES && audio.i_rate && !audioStream)
    {
        if(ConfigureAudio(&fmt->audio) == VLC_SUCCESS)
            s = audioStream = dynamic_cast<AudioDecodedStream *>(SDIOutput::Add(fmt));
        audioStream->setOutputFormat(&audio.configuredfmt);
    }
    else if(fmt->i_cat == SPU_ES && fmt->i_codec == VLC_CODEC_CEA608)
    {
        s = captionsStream = dynamic_cast<CaptionsStream *>(SDIOutput::Add(fmt));
    }

    if(s)
    {
        if( videoStream && (audioStream || audio.i_rate <= 0) )
            Start();
    }
    else
    {
        msg_Err(p_stream, "rejecting stream %4.4s id %d",
                &fmt->i_codec, fmt->i_profile);
    }

    return s;
}

void DBMSDIOutput::Del(AbstractStream *id)
{
    if(videoStream == id)
        videoStream = NULL;
    else if(audioStream == id)
        audioStream = NULL;
    SDIOutput::Del(id);
}

IDeckLinkDisplayMode * DBMSDIOutput::MatchDisplayMode(const video_format_t *fmt, BMDDisplayMode forcedmode)
{
    HRESULT result;
    IDeckLinkDisplayMode *p_selected = NULL;
    IDeckLinkDisplayModeIterator *p_iterator = NULL;

    for(int i=0; i<4 && p_selected==NULL; i++)
    {
        int i_width = (i % 2 == 0) ? fmt->i_width : fmt->i_visible_width;
        int i_height = (i % 2 == 0) ? fmt->i_height : fmt->i_visible_height;
        int i_div = (i > 2) ? 4 : 0;

        result = p_output->GetDisplayModeIterator(&p_iterator);
        if(result == S_OK)
        {
            IDeckLinkDisplayMode *p_mode = NULL;
            while(p_iterator->Next(&p_mode) == S_OK)
            {
                BMDDisplayMode mode_id = p_mode->GetDisplayMode();
                BMDTimeValue frameduration;
                BMDTimeScale timescale;
                const char *psz_mode_name;

                if(p_mode->GetFrameRate(&frameduration, &timescale) == S_OK &&
                        p_mode->GetName(&psz_mode_name) == S_OK)
                {
                    BMDDisplayMode modenl = htonl(mode_id);
                    if(i==0)
                    {
                        BMDFieldDominance field = htonl(p_mode->GetFieldDominance());
                        msg_Dbg(p_stream, "Found mode '%4.4s': %s (%ldx%ld, %.3f fps, %4.4s, scale %ld dur %ld)",
                                (char*)&modenl, psz_mode_name,
                                p_mode->GetWidth(), p_mode->GetHeight(),
                                (char *)&field,
                                double(timescale) / frameduration,
                                timescale, frameduration);
                    }
                }
                else
                {
                    p_mode->Release();
                    continue;
                }

                if(forcedmode != bmdDisplayModeNotSupported && unlikely(!p_selected))
                {
                    BMDDisplayMode modenl = htonl(forcedmode);
                    msg_Dbg(p_stream, "Forced mode '%4.4s'", (char *)&modenl);
                    if(forcedmode == mode_id)
                        p_selected = p_mode;
                    else
                        p_mode->Release();
                    continue;
                }

                if(p_selected == NULL && forcedmode == bmdDisplayModeNotSupported)
                {
                    if(i_width >> i_div == p_mode->GetWidth() >> i_div &&
                       i_height >> i_div == p_mode->GetHeight() >> i_div)
                    {
                        unsigned int num_deck, den_deck;
                        unsigned int num_stream, den_stream;
                        vlc_ureduce(&num_deck, &den_deck, timescale, frameduration, 0);
                        vlc_ureduce(&num_stream, &den_stream,
                                    fmt->i_frame_rate, fmt->i_frame_rate_base, 0);

                        if (num_deck == num_stream && den_deck == den_stream)
                        {
                            msg_Info(p_stream, "Matches incoming stream");
                            p_selected = p_mode;
                            continue;
                        }
                    }
                }

                p_mode->Release();
            }
            p_iterator->Release();
        }
    }
    return p_selected;
}


const char * DBMSDIOutput::ErrorToString(long i_code)
{
    static struct
    {
        long i_return_code;
        const char * const psz_string;
    } const errors_to_string[] = {
        { E_UNEXPECTED,  "Unexpected error" },
        { E_NOTIMPL,     "Not implemented" },
        { E_OUTOFMEMORY, "Out of memory" },
        { E_INVALIDARG,  "Invalid argument" },
        { E_NOINTERFACE, "No interface" },
        { E_POINTER,     "Invalid pointer" },
        { E_HANDLE,      "Invalid handle" },
        { E_ABORT,       "Aborted" },
        { E_FAIL,        "Failed" },
        { E_ACCESSDENIED,"Access denied" }
    };

    for(size_t i=0; i<ARRAY_SIZE(errors_to_string); i++)
    {
        if(errors_to_string[i].i_return_code == i_code)
            return errors_to_string[i].psz_string;
    }
    return NULL;
}

#define CHECK(message) do { \
    if (result != S_OK) \
    { \
    const char *psz_err = ErrorToString(result); \
    if(psz_err)\
    msg_Err(p_stream, message ": %s", psz_err); \
    else \
    msg_Err(p_stream, message ": 0x%X", result); \
    goto error; \
} \
} while(0)

int DBMSDIOutput::Open()
{
    HRESULT result;
    IDeckLinkIterator *decklink_iterator = NULL;

    int i_card_index = var_InheritInteger(p_stream, CFG_PREFIX "card-index");

    if (i_card_index < 0)
    {
        msg_Err(p_stream, "Invalid card index %d", i_card_index);
        goto error;
    }

    decklink_iterator = CreateDeckLinkIteratorInstance();
    if (!decklink_iterator)
    {
        msg_Err(p_stream, "DeckLink drivers not found.");
        goto error;
    }

    for(int i = 0; i <= i_card_index; ++i)
    {
        if (p_card)
        {
            p_card->Release();
            p_card = NULL;
        }
        result = decklink_iterator->Next(&p_card);
        CHECK("Card not found");
    }

    const char *psz_model_name;
    result = p_card->GetModelName(&psz_model_name);
    CHECK("Unknown model name");

    msg_Dbg(p_stream, "Opened DeckLink PCI card %s", psz_model_name);

    result = p_card->QueryInterface(IID_IDeckLinkOutput, (void**)&p_output);
    CHECK("No outputs");

    decklink_iterator->Release();

    return VLC_SUCCESS;

error:
    if (p_output)
    {
        p_output->Release();
        p_output = NULL;
    }
    if (p_card)
    {
        p_card->Release();
        p_output = NULL;
    }
    if (decklink_iterator)
        decklink_iterator->Release();

    return VLC_EGENERIC;
}

int DBMSDIOutput::ConfigureAudio(const audio_format_t *)
{
    HRESULT result;

    if(FORCE_INPUT)
        return VLC_SUCCESS;

    if(!p_output)
        return VLC_EGENERIC;

    if(!video.configuredfmt.i_codec && b_running)
        return VLC_EGENERIC;

    if (audio.i_rate > 0)
    {
        result = p_output->EnableAudioOutput(
                    audio.i_rate,
                    bmdAudioSampleType16bitInteger,
                    2,
                    bmdAudioOutputStreamTimestamped);
        CHECK("Could not start audio output");
    }
    return VLC_SUCCESS;

error:
    return VLC_EGENERIC;
}

static BMDVideoConnection getVConn(const char *psz)
{
    BMDVideoConnection conn = bmdVideoConnectionSDI;

    if(!psz)
        return conn;

    if (!strcmp(psz, "sdi"))
        conn = bmdVideoConnectionSDI;
    else if (!strcmp(psz, "hdmi"))
        conn = bmdVideoConnectionHDMI;
    else if (!strcmp(psz, "opticalsdi"))
        conn = bmdVideoConnectionOpticalSDI;
    else if (!strcmp(psz, "component"))
        conn = bmdVideoConnectionComponent;
    else if (!strcmp(psz, "composite"))
        conn = bmdVideoConnectionComposite;
    else if (!strcmp(psz, "svideo"))
        conn = bmdVideoConnectionSVideo;

    return conn;
}

int DBMSDIOutput::ConfigureVideo(const video_format_t *vfmt)
{
    HRESULT result;
    BMDDisplayMode wanted_mode_id = bmdDisplayModeNotSupported;
    IDeckLinkConfiguration *p_config = NULL;
    IDeckLinkAttributes *p_attributes = NULL;
    IDeckLinkDisplayMode *p_display_mode = NULL;
    char *psz_string = NULL;

    if(FORCE_INPUT)
        return VLC_SUCCESS;

    if(!p_output)
        return VLC_EGENERIC;

    if(!video.configuredfmt.i_codec && b_running)
        return VLC_EGENERIC;

    /* Now configure card */
    if(!p_output)
        return VLC_EGENERIC;

    result = p_card->QueryInterface(IID_IDeckLinkConfiguration, (void**)&p_config);
    CHECK("Could not get config interface");

    psz_string = var_InheritString(p_stream, CFG_PREFIX "mode");
    if(psz_string)
    {
        size_t len = strlen(psz_string);
        if (len > 4)
        {
            free(psz_string);
            msg_Err(p_stream, "Invalid mode %s", psz_string);
            goto error;
        }
        memset(&wanted_mode_id, ' ', 4);
        strncpy((char*)&wanted_mode_id, psz_string, 4);
        wanted_mode_id = ntohl(wanted_mode_id);
        free(psz_string);
    }

    /* Read attributes */
    result = p_card->QueryInterface(IID_IDeckLinkAttributes, (void**)&p_attributes);
    CHECK("Could not get IDeckLinkAttributes");

    int64_t vconn;
    result = p_attributes->GetInt(BMDDeckLinkVideoOutputConnections, &vconn); /* reads mask */
    CHECK("Could not get BMDDeckLinkVideoOutputConnections");

    psz_string = var_InheritString(p_stream, CFG_PREFIX "video-connection");
    vconn = getVConn(psz_string);
    free(psz_string);
    if (vconn == 0)
    {
        msg_Err(p_stream, "Invalid video connection specified");
        goto error;
    }

    result = p_config->SetInt(bmdDeckLinkConfigVideoOutputConnection, vconn);
    CHECK("Could not set video output connection");

    p_display_mode = MatchDisplayMode(vfmt, wanted_mode_id);
    if(p_display_mode == NULL)
    {
        msg_Err(p_stream, "Could not negociate a compatible display mode");
        goto error;
    }
    else
    {
        BMDDisplayMode mode_id = p_display_mode->GetDisplayMode();
        BMDDisplayMode modenl = htonl(mode_id);
        msg_Dbg(p_stream, "Selected mode '%4.4s'", (char *) &modenl);

        BMDVideoOutputFlags flags = bmdVideoOutputVANC;
        if (mode_id == bmdModeNTSC ||
                mode_id == bmdModeNTSC2398 ||
                mode_id == bmdModePAL)
        {
            flags = bmdVideoOutputVITC;
        }

        BMDDisplayModeSupport support;
        IDeckLinkDisplayMode *resultMode;

        result = p_output->DoesSupportVideoMode(mode_id,
                                                video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV,
                                                flags, &support, &resultMode);
        CHECK("Does not support video mode");
        if (support == bmdDisplayModeNotSupported)
        {
            msg_Err(p_stream, "Video mode not supported");
            goto error;
        }

        if (p_display_mode->GetWidth() <= 0 || p_display_mode->GetWidth() & 1)
        {
            msg_Err(p_stream, "Unknown video mode specified.");
            goto error;
        }

        result = p_display_mode->GetFrameRate(&frameduration,
                                              &timescale);
        CHECK("Could not read frame rate");

        result = p_output->EnableVideoOutput(mode_id, flags);
        CHECK("Could not enable video output");

        video_format_t *fmt = &video.configuredfmt.video;
        video_format_Copy(fmt, vfmt);
        fmt->i_width = fmt->i_visible_width = p_display_mode->GetWidth();
        fmt->i_height = fmt->i_visible_height = p_display_mode->GetHeight();
        fmt->i_x_offset = 0;
        fmt->i_y_offset = 0;
        fmt->i_sar_num = 0;
        fmt->i_sar_den = 0;
        fmt->i_chroma = !video.tenbits ? VLC_CODEC_UYVY : VLC_CODEC_I422_10L; /* we will convert to v210 */
        fmt->i_frame_rate = (unsigned) frameduration;
        fmt->i_frame_rate_base = (unsigned) timescale;

        char *psz_file = var_InheritString(p_stream, CFG_PREFIX "nosignal-image");
        if(psz_file)
        {
            video.pic_nosignal = CreateNoSignalPicture(psz_file, fmt);
            if (!video.pic_nosignal)
                msg_Err(p_stream, "Could not create no signal picture");
            free(psz_file);
        }
    }

    p_display_mode->Release();
    p_attributes->Release();
    p_config->Release();

    return VLC_SUCCESS;

error:
    if (p_display_mode)
        p_display_mode->Release();
    if(p_attributes)
        p_attributes->Release();
    if (p_config)
        p_config->Release();
    return VLC_EGENERIC;
}

int DBMSDIOutput::Start()
{
    HRESULT result;
    if(FORCE_INPUT)
        return VLC_SUCCESS;
    if(b_running)
        return VLC_EGENERIC;
    result = p_output->StartScheduledPlayback(
                     SEC_FROM_VLC_TICK(vlc_tick_now() * timescale), timescale, 1.0);
    CHECK("Could not start playback");
    b_running = true;
    return VLC_SUCCESS;

error:
    return VLC_EGENERIC;
}

int DBMSDIOutput::Process()
{
    if(!p_output || !b_running)
        return VLC_EGENERIC;
    if(videoStream)
    {
        picture_t *p;
        while((p = videoStream->Dequeue()))
        {
            block_t *p_cc = captionsStream ? captionsStream->Dequeue() : NULL;
            ProcessVideo(p, p_cc);
        }
    }
    if(audioStream)
    {
        block_t *p;
        while((p = audioStream->Dequeue()))
            ProcessAudio(p);
    }
    return VLC_SUCCESS;
}

int DBMSDIOutput::ProcessAudio(block_t *p_block)
{
    if (!p_output)
    {
        block_Release(p_block);
        return VLC_EGENERIC;
    }

    p_block->i_pts -= offset;

    uint32_t sampleFrameCount = p_block->i_nb_samples;
    uint32_t written;
    HRESULT result = p_output->ScheduleAudioSamples(
            p_block->p_buffer, p_block->i_nb_samples, p_block->i_pts, CLOCK_FREQ, &written);

    if (result != S_OK)
        msg_Err(p_stream, "Failed to schedule audio sample: 0x%X", result);
    else
    {
        lasttimestamp = __MAX(p_block->i_pts, lasttimestamp);
        if (sampleFrameCount != written)
            msg_Err(p_stream, "Written only %d samples out of %d", written, sampleFrameCount);
    }

    block_Release(p_block);

    return result != S_OK ? VLC_EGENERIC : VLC_SUCCESS;
}

int DBMSDIOutput::ProcessVideo(picture_t *picture, block_t *p_cc)
{
    mtime_t now = vlc_tick_now();

    if (!picture)
        return VLC_EGENERIC;

    if(picture->date - now >  5000)
        vlc_msleep_i11e(picture->date - now);

    if (video.pic_nosignal &&
        now - picture->date > vlc_tick_from_sec(video.nosignal_delay))
    {
        msg_Dbg(p_stream, "no signal");

        picture_Hold(video.pic_nosignal);
        video.pic_nosignal->date = now;
        doProcessVideo(picture, NULL);
    }

    return doProcessVideo(picture, p_cc);
}

int DBMSDIOutput::doProcessVideo(picture_t *picture, block_t *p_cc)
{
    HRESULT result;
    int w, h, stride, length;
    mtime_t now;
    w = video.configuredfmt.video.i_visible_width;
    h = video.configuredfmt.video.i_visible_height;

if(p_cc) block_Release(p_cc);

    IDeckLinkMutableVideoFrame *pDLVideoFrame;
    result = p_output->CreateVideoFrame(w, h, w*3,
                                        video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV,
                                        bmdFrameFlagDefault, &pDLVideoFrame);
    if (result != S_OK) {
        msg_Err(p_stream, "Failed to create video frame: 0x%X", result);
        pDLVideoFrame = NULL;
        goto end;
    }

    void *frame_bytes;
    pDLVideoFrame->GetBytes((void**)&frame_bytes);
    stride = pDLVideoFrame->GetRowBytes();

    if (video.tenbits) {
        IDeckLinkVideoFrameAncillary *vanc;
        void *buf;

        result = p_output->CreateAncillaryData(
                    video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV, &vanc);
        if (result != S_OK) {
            msg_Err(p_stream, "Failed to create vanc: %d", result);
            goto end;
        }

        result = vanc->GetBufferForVerticalBlankingLine(ancillary.afd_line, &buf);
        if (result != S_OK) {
            msg_Err(p_stream, "Failed to get VBI line %u: %d", ancillary.afd_line, result);
            goto end;
        }

        sdi::AFD afd(ancillary.afd, ancillary.ar);
        afd.FillBuffer(reinterpret_cast<uint8_t*>(buf), stride);

        sdi::V210::Convert(picture, stride, frame_bytes);

        result = pDLVideoFrame->SetAncillaryData(vanc);
        vanc->Release();
        if (result != S_OK) {
            msg_Err(p_stream, "Failed to set vanc: %d", result);
            goto end;
        }
    }
    else for(int y = 0; y < h; ++y) {
        uint8_t *dst = (uint8_t *)frame_bytes + stride * y;
        const uint8_t *src = (const uint8_t *)picture->p[0].p_pixels +
                picture->p[0].i_pitch * y;
        memcpy(dst, src, w * 2 /* bpp */);
    }


    // compute frame duration in CLOCK_FREQ units
    length = (frameduration * CLOCK_FREQ) / timescale;

    picture->date -= offset;
    result = p_output->ScheduleVideoFrame(pDLVideoFrame,
                                          picture->date, length, CLOCK_FREQ);
    if (result != S_OK) {
        msg_Err(p_stream, "Dropped Video frame %" PRId64 ": 0x%x",
                picture->date, result);
        goto end;
    }
    lasttimestamp = __MAX(picture->date, lasttimestamp);

    now = vlc_tick_now() - offset;

    BMDTimeValue decklink_now;
    double speed;
    p_output->GetScheduledStreamTime (CLOCK_FREQ, &decklink_now, &speed);

    if ((now - decklink_now) > 400000) {
        /* XXX: workaround card clock drift */
        offset += 50000;
        msg_Err(p_stream, "Delaying: offset now %" PRId64, offset);
    }

    picture_Release(picture);
    if (pDLVideoFrame)
        pDLVideoFrame->Release();

    return VLC_SUCCESS;

end:
    picture_Release(picture);
    if (pDLVideoFrame)
        pDLVideoFrame->Release();
    return VLC_EGENERIC;
}

picture_t * DBMSDIOutput::CreateNoSignalPicture(const char *psz_file, const video_format_t *fmt)
{
    picture_t *p_pic = NULL;
    image_handler_t *img = image_HandlerCreate(p_stream);
    if (img)
    {
        video_format_t in, dummy;
        video_format_Init(&dummy, 0);
        video_format_Init(&in, 0);
        video_format_Setup(&in, 0,
                           fmt->i_width, fmt->i_height,
                           fmt->i_width, fmt->i_height, 1, 1);

        picture_t *png = image_ReadUrl(img, psz_file, &dummy, &in);
        if (png)
        {
            video_format_Clean(&dummy);
            video_format_Copy(&dummy, fmt);
            p_pic = image_Convert(img, png, &in, &dummy);
            if(!video_format_IsSimilar(&dummy, fmt))
            {
                picture_Release(p_pic);
                p_pic = NULL;
            }
            picture_Release(png);
        }
        image_HandlerDelete(img);
        video_format_Clean(&in);
        video_format_Clean(&dummy);
    }
    return p_pic;
}
