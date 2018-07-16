#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "DBMSDIOutput.hpp"

#include <DeckLinkAPIDispatch.cpp>
#include "decklink.hpp"

#include <vlc_es.h>
#include <vlc_picture.h>
#include <vlc_interrupt.h>
#include <arpa/inet.h>

using namespace sdi_sout;

DBMSDIOutput::DBMSDIOutput(sout_stream_t *p_stream) :
    SDIOutput(p_stream)
{
    p_card = NULL;
    p_output = NULL;
    offset = 0;
    video_format_Init(&video.currentfmt, 0);
    video.tenbits = var_InheritBool(p_stream, VIDEO_CFG_PREFIX "tenbits");
    video.nosignal_delay = var_InheritInteger(p_stream, VIDEO_CFG_PREFIX "nosignal-delay");
    video.afd = var_InheritInteger(p_stream, VIDEO_CFG_PREFIX "afd");
    video.ar = var_InheritInteger(p_stream, VIDEO_CFG_PREFIX "ar");
    video.pic_nosignal = NULL;
    videoStream = NULL;
    audioStream = NULL;
    b_running = false;
}

DBMSDIOutput::~DBMSDIOutput()
{
    if(video.pic_nosignal)
        picture_Release(video.pic_nosignal);
    video_format_Clean(&video.currentfmt);
    if(p_output)
    {
        p_output->StopScheduledPlayback(0, NULL, 0);
        p_output->DisableVideoOutput();
        p_output->DisableAudioOutput();
        p_output->Release();
    }
    if(p_card)
        p_card->Release();
}

AbstractStream *DBMSDIOutput::Add(const es_format_t *fmt)
{
    if(fmt->i_cat == VIDEO_ES && !videoStream)
    {
        if(ConfigureVideo(&fmt->video) == VLC_SUCCESS)
            videoStream = SDIOutput::Add(fmt);
        if( videoStream )
            Start();
        return videoStream;
    }
    else if(fmt->i_cat == AUDIO_ES && !videoStream)
    {
        if(ConfigureAudio(&fmt->audio) == VLC_SUCCESS)
            audioStream = SDIOutput::Add(fmt);
        return audioStream;
    }
    else return NULL;
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

    if(!p_output || b_running)
        return VLC_EGENERIC;

    if (/*decklink_sys->i_channels > 0 &&*/ audio.i_rate > 0)
    {
        result = p_output->EnableAudioOutput(
                    audio.i_rate,
                    bmdAudioSampleType16bitInteger,
                    /*decklink_sys->i_channels*/ 2,
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

    if(!p_output || b_running)
        return VLC_EGENERIC;

    /* Now configure card */

    result = p_card->QueryInterface(IID_IDeckLinkConfiguration, (void**)&p_config);
    CHECK("Could not get config interface");

    psz_string = var_InheritString(p_stream, VIDEO_CFG_PREFIX "mode");
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

    psz_string = var_InheritString(p_stream, VIDEO_CFG_PREFIX "video-connection");
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

        video_format_t *fmt = &video.currentfmt;
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

static inline void put_le32(uint8_t **p, uint32_t d)
{
    SetDWLE(*p, d);
    (*p) += 4;
}

static inline int clip(int a)
{
    if      (a < 4) return 4;
    else if (a > 1019) return 1019;
    else               return a;
}

static void v210_convert(void *frame_bytes, picture_t *pic, int dst_stride)
{
    int width = pic->format.i_width;
    int height = pic->format.i_height;
    int line_padding = dst_stride - ((width * 8 + 11) / 12) * 4;
    int h, w;
    uint8_t *data = (uint8_t*)frame_bytes;

    const uint16_t *y = (const uint16_t*)pic->p[0].p_pixels;
    const uint16_t *u = (const uint16_t*)pic->p[1].p_pixels;
    const uint16_t *v = (const uint16_t*)pic->p[2].p_pixels;

#define WRITE_PIXELS(a, b, c)           \
    do {                                \
        val =   clip(*a++);             \
        val |= (clip(*b++) << 10) |     \
               (clip(*c++) << 20);      \
        put_le32(&data, val);           \
    } while (0)

    for (h = 0; h < height; h++) {
        uint32_t val = 0;
        for (w = 0; w < width - 5; w += 6) {
            WRITE_PIXELS(u, y, v);
            WRITE_PIXELS(y, u, y);
            WRITE_PIXELS(v, y, u);
            WRITE_PIXELS(y, v, y);
        }
        if (w < width - 1) {
            WRITE_PIXELS(u, y, v);

            val = clip(*y++);
            if (w == width - 2)
                put_le32(&data, val);
#undef WRITE_PIXELS
        }
        if (w < width - 3) {
            val |= (clip(*u++) << 10) | (clip(*y++) << 20);
            put_le32(&data, val);

            val = clip(*v++) | (clip(*y++) << 10);
            put_le32(&data, val);
        }

        memset(data, 0, line_padding);
        data += line_padding;

        y += pic->p[0].i_pitch / 2 - width;
        u += pic->p[1].i_pitch / 2 - width / 2;
        v += pic->p[2].i_pitch / 2 - width / 2;
    }
}

static void send_AFD(uint8_t afdcode, uint8_t ar, uint8_t *buf)
{
    const size_t len = 6 /* vanc header */ + 8 /* AFD data */ + 1 /* csum */;
    const size_t s = ((len + 5) / 6) * 6; // align for v210

    uint16_t afd[s];

    afd[0] = 0x000;
    afd[1] = 0x3ff;
    afd[2] = 0x3ff;
    afd[3] = 0x41; // DID
    afd[4] = 0x05; // SDID
    afd[5] = 8; // Data Count

    int bar_data_flags = 0;
    int bar_data_val1 = 0;
    int bar_data_val2 = 0;

    afd[ 6] = ((afdcode & 0x0F) << 3) | ((ar & 0x01) << 2); /* SMPTE 2016-1 */
    afd[ 7] = 0; // reserved
    afd[ 8] = 0; // reserved
    afd[ 9] = bar_data_flags << 4;
    afd[10] = bar_data_val1 << 8;
    afd[11] = bar_data_val1 & 0xff;
    afd[12] = bar_data_val2 << 8;
    afd[13] = bar_data_val2 & 0xff;

    /* parity bit */
    for (size_t i = 3; i < len - 1; i++)
        afd[i] |= vlc_parity((unsigned)afd[i]) ? 0x100 : 0x200;

    /* vanc checksum */
    uint16_t vanc_sum = 0;
    for (size_t i = 3; i < len - 1; i++) {
        vanc_sum += afd[i];
        vanc_sum &= 0x1ff;
    }

    afd[len - 1] = vanc_sum | ((~vanc_sum & 0x100) << 1);

    /* pad */
    for (size_t i = len; i < s; i++)
        afd[i] = 0x040;

    /* convert to v210 and write into VANC */
    for (size_t w = 0; w < s / 6 ; w++) {
        put_le32(&buf, afd[w*6+0] << 10);
        put_le32(&buf, afd[w*6+1] | (afd[w*6+2] << 20));
        put_le32(&buf, afd[w*6+3] << 10);
        put_le32(&buf, afd[w*6+4] | (afd[w*6+5] << 20));
    }
}

int DBMSDIOutput::Process(picture_t *picture)
{
    mtime_t now = vlc_tick_now();

    if (!picture)
        return VLC_EGENERIC;

    if( picture->date - now >  5000 )
        vlc_msleep_i11e( picture->date - now );

    if (now - picture->date > vlc_tick_from_sec( video.nosignal_delay ))
    {
        msg_Dbg(p_stream, "no signal");
        if (video.pic_nosignal) {
            picture = video.pic_nosignal;
        } else {
            if (video.tenbits) { // I422_10L
                plane_t *y = &picture->p[0];
                memset(y->p_pixels, 0x0, y->i_lines * y->i_pitch);
                for (int i = 1; i < picture->i_planes; i++) {
                    plane_t *p = &picture->p[i];
                    size_t len = p->i_lines * p->i_pitch / 2;
                    int16_t *data = (int16_t*)p->p_pixels;
                    for (size_t j = 0; j < len; j++) // XXX: SIMD
                        data[j] = 0x200;
                }
            } else { // UYVY
                size_t len = picture->p[0].i_lines * picture->p[0].i_pitch;
                for (size_t i = 0; i < len; i+= 2) { // XXX: SIMD
                    picture->p[0].p_pixels[i+0] = 0x80;
                    picture->p[0].p_pixels[i+1] = 0;
                }
            }
        }
        picture->date = now;
    }

    HRESULT result;
    int w, h, stride, length;
//    w = i_width;
//    h = i_height;

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
        int line;
        void *buf;

        result = p_output->CreateAncillaryData(
                    video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV, &vanc);
        if (result != S_OK) {
            msg_Err(p_stream, "Failed to create vanc: %d", result);
            goto end;
        }

        line = var_InheritInteger(p_stream, VIDEO_CFG_PREFIX "afd-line");
        result = vanc->GetBufferForVerticalBlankingLine(line, &buf);
        if (result != S_OK) {
            msg_Err(p_stream, "Failed to get VBI line %d: %d", line, result);
            goto end;
        }
        send_AFD(video.afd, video.ar, (uint8_t*)buf);

        v210_convert(frame_bytes, picture, stride);

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

    now = vlc_tick_now() - offset;

    BMDTimeValue decklink_now;
    double speed;
    p_output->GetScheduledStreamTime (CLOCK_FREQ, &decklink_now, &speed);

    if ((now - decklink_now) > 400000) {
        /* XXX: workaround card clock drift */
        offset += 50000;
        msg_Err(p_stream, "Delaying: offset now %" PRId64, offset);
    }

    return VLC_SUCCESS;

end:
    if (pDLVideoFrame)
        pDLVideoFrame->Release();
    return VLC_EGENERIC;
}
