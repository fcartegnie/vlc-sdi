
#include <vlc_fixups.h>
#include <cinttypes>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_threads.h>

#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>

#include <vlc_block.h>
#include <vlc_image.h>
#include <vlc_aout.h>
#include <arpa/inet.h>

#include <DeckLinkAPI.h>
#include <DeckLinkAPIDispatch.cpp>

#define FRAME_SIZE 1920
#define CHANNELS_MAX 6

#if 0
static const int pi_channels_maps[CHANNELS_MAX+1] =
{
    0,
    AOUT_CHAN_CENTER,
    AOUT_CHANS_STEREO,
    AOUT_CHANS_3_0,
    AOUT_CHANS_4_0,
    AOUT_CHANS_5_0,
    AOUT_CHANS_5_1,
};
#endif

#define NOSIGNAL_INDEX_TEXT N_("Timelength after which we assume there is no signal.")
#define NOSIGNAL_INDEX_LONGTEXT N_(\
    "Timelength after which we assume there is no signal.\n"\
    "After this delay we black out the video."\
    )

#define AFD_INDEX_TEXT "Active Format Descriptor"
#define AFD_INDEX_LONGTEXT AFD_INDEX_TEXT " value"

#define AR_INDEX_TEXT "Aspect Ratio"
#define AR_INDEX_LONGTEXT AR_INDEX_TEXT " of the source picture"

#define AFDLINE_INDEX_TEXT N_("Active Format Descriptor line.")
#define AFDLINE_INDEX_LONGTEXT N_("VBI line on which to output Active Format Descriptor.")

#define NOSIGNAL_IMAGE_TEXT N_("Picture to display on input signal loss.")
#define NOSIGNAL_IMAGE_LONGTEXT NOSIGNAL_IMAGE_TEXT

#define CARD_INDEX_TEXT N_("Output card")
#define CARD_INDEX_LONGTEXT N_(\
    "DeckLink output card, if multiple exist. " \
    "The cards are numbered from 0.")

#define MODE_TEXT N_("Desired output mode")
#define MODE_LONGTEXT N_(\
    "Desired output mode for DeckLink output. " \
    "This value should be a FOURCC code in textual " \
    "form, e.g. \"ntsc\".")

#define AUDIO_CONNECTION_TEXT N_("Audio connection")
#define AUDIO_CONNECTION_LONGTEXT N_(\
    "Audio connection for DeckLink output.")


#define RATE_TEXT N_("Audio samplerate (Hz)")
#define RATE_LONGTEXT N_(\
    "Audio sampling rate (in hertz) for DeckLink output. " \
    "0 disables audio output.")

#define CHANNELS_TEXT N_("Number of audio channels")
#define CHANNELS_LONGTEXT N_(\
    "Number of output channels for DeckLink output. " \
    "Must be 2, 8 or 16. 0 disables audio output.")

#define VIDEO_CONNECTION_TEXT N_("Video connection")
#define VIDEO_CONNECTION_LONGTEXT N_(\
    "Video connection for DeckLink output.")

#define VIDEO_TENBITS_TEXT N_("10 bits")
#define VIDEO_TENBITS_LONGTEXT N_(\
    "Use 10 bits per pixel for video frames.")

#define CFG_PREFIX "decklink-output-"
#define VIDEO_CFG_PREFIX "decklink-vout-"
#define AUDIO_CFG_PREFIX "decklink-aout-"

static const char *const ppsz_videoconns[] = {
    "sdi", "hdmi", "opticalsdi", "component", "composite", "svideo"
};
static const char *const ppsz_videoconns_text[] = {
    N_("SDI"), N_("HDMI"), N_("Optical SDI"), N_("Component"), N_("Composite"), N_("S-video")
};

static const int rgi_afd_values[] = {
    0, 2, 3, 4, 8, 9, 10, 11, 13, 14, 15,
};
static const char * const rgsz_afd_text[] = {
    "0:  Undefined",
    "2:  Box 16:9 (top aligned)",
    "3:  Box 14:9 (top aligned)",
    "4:  Box > 16:9 (centre aligned)",
    "8:  Same as coded frame (full frame)",
    "9:   4:3 (centre aligned)",
    "10: 16:9 (centre aligned)",
    "11: 14:9 (centre aligned)",
    "13:  4:3 (with shoot and protect 14:9 centre)",
    "14: 16:9 (with shoot and protect 14:9 centre)",
    "15: 16:9 (with shoot and protect  4:3 centre)",
};

static const int rgi_ar_values[] = {
    0, 1,
};
static const char * const rgsz_ar_text[] = {
    "0:   4:3",
    "1:  16:9",
};

/* Only one audio output module and one video output module
 * can be used per process.
 * We use a static mutex in audio/video submodules entry points.  */
struct decklink_sys_t
{
    IDeckLinkOutput *p_output;

    struct
    {
        picture_pool_t *pool;
        bool tenbits;
        uint8_t afd, ar;
        int nosignal_delay;
        picture_t *pic_nosignal;
    } video;

    /*
     * Synchronizes aout and vout modules:
     * vout module waits until aout has been initialized.
     * That means video-only output is NOT supported.
     */
    uint8_t users;

    //int i_channels;
    int i_rate;

    int i_width;
    int i_height;

    BMDTimeScale timescale;
    BMDTimeValue frameduration;

    /* XXX: workaround card clock drift */
    mtime_t offset;
};

/*****************************************************************************
 * Local prototypes.
 *****************************************************************************/

static int  OpenVideo           (vlc_object_t *);
static void CloseVideo          (vlc_object_t *);

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
/*
static void ReleaseDLSys(vlc_object_t *obj)
{
    vlc_object_t *libvlc = VLC_OBJECT(obj->obj.libvlc);

    vlc_mutex_lock(&sys_lock);

    struct decklink_sys_t *sys = (struct decklink_sys_t*)var_GetAddress(libvlc, "decklink-sys");

    if (--sys->users == 0) {
        msg_Dbg(obj, "Destroying decklink data");

        if (sys->p_output) {
            sys->p_output->StopScheduledPlayback(0, NULL, 0);
            sys->p_output->DisableVideoOutput();
            sys->p_output->DisableAudioOutput();
            sys->p_output->Release();
        }

        free(sys);
        var_Destroy(libvlc, "decklink-sys");
    }

    vlc_mutex_unlock(&sys_lock);
}

static BMDVideoConnection getVConn(vout_display_t *vd)
{
    BMDVideoConnection conn = bmdVideoConnectionSDI;
    char *psz = var_InheritString(vd, VIDEO_CFG_PREFIX "video-connection");
    if (!psz)
        goto end;

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

end:
    free(psz);
    return conn;
}
*/
/*****************************************************************************
 *
 *****************************************************************************/

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

static const char * lookup_error_string(long i_code)
{
    for(size_t i=0; i<ARRAY_SIZE(errors_to_string); i++)
    {
        if(errors_to_string[i].i_return_code == i_code)
            return errors_to_string[i].psz_string;
    }
    return NULL;
}

static IDeckLinkDisplayMode * MatchDisplayMode(vlc_object_t *obj,
                                               IDeckLinkOutput *output,
                                               const video_format_t *fmt,
                                               BMDDisplayMode forcedmode = bmdDisplayModeNotSupported)
{
    HRESULT result;
    IDeckLinkDisplayMode *p_selected = NULL;
    IDeckLinkDisplayModeIterator *p_iterator = NULL;

    for(int i=0; i<4 && p_selected==NULL; i++)
    {
        int i_width = (i % 2 == 0) ? fmt->i_width : fmt->i_visible_width;
        int i_height = (i % 2 == 0) ? fmt->i_height : fmt->i_visible_height;
        int i_div = (i > 2) ? 4 : 0;

        result = output->GetDisplayModeIterator(&p_iterator);
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
                        msg_Dbg(obj, "Found mode '%4.4s': %s (%ldx%ld, %.3f fps, %4.4s, scale %ld dur %ld)",
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
                    msg_Dbg(obj, "Forced mode '%4.4s'", (char *)&modenl);
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
                            msg_Info(obj, "Matches incoming stream");
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

static int OpenDecklink(vlc_object_t *obj, decklink_sys_t *sys, const video_format_t *fmt)
{

#define CHECK(message) do { \
    if (result != S_OK) \
    { \
        const char *psz_err = lookup_error_string(result); \
        if(psz_err)\
            msg_Err(obj, message ": %s", psz_err); \
        else \
            msg_Err(obj, message ": 0x%X", result); \
        goto error; \
    } \
} while(0)

    HRESULT result;
    IDeckLinkIterator *decklink_iterator = NULL;
    IDeckLinkDisplayMode *p_display_mode = NULL;
    IDeckLinkConfiguration *p_config = NULL;
    IDeckLink *p_card = NULL;
    BMDDisplayMode wanted_mode_id = bmdDisplayModeNotSupported;

    int i_card_index = 0;//var_InheritInteger(vd, CFG_PREFIX "card-index");
    BMDVideoConnection vconn = bmdVideoConnectionSDI; //getVConn(obj);
    char *mode = strdup("hp50");//var_InheritString(vd, VIDEO_CFG_PREFIX "mode");
    if(mode)
    {
        size_t len = strlen(mode);
        if (len > 4)
        {
            free(mode);
            msg_Err(obj, "Invalid mode %s", mode);
            goto error;
        }
        memset(&wanted_mode_id, ' ', 4);
        strncpy((char*)&wanted_mode_id, mode, 4);
        wanted_mode_id = ntohl(wanted_mode_id);
        free(mode);
    }

    if (i_card_index < 0)
    {
        msg_Err(obj, "Invalid card index %d", i_card_index);
        goto error;
    }

    decklink_iterator = CreateDeckLinkIteratorInstance();
    if (!decklink_iterator)
    {
        msg_Err(obj, "DeckLink drivers not found.");
        goto error;
    }

    for(int i = 0; i <= i_card_index; ++i)
    {
        if (p_card)
            p_card->Release();
        result = decklink_iterator->Next(&p_card);
        CHECK("Card not found");
    }

    const char *psz_model_name;
    result = p_card->GetModelName(&psz_model_name);
    CHECK("Unknown model name");

    msg_Dbg(obj, "Opened DeckLink PCI card %s", psz_model_name);

    result = p_card->QueryInterface(IID_IDeckLinkOutput,
        (void**)&sys->p_output);
    CHECK("No outputs");

    result = p_card->QueryInterface(IID_IDeckLinkConfiguration,
        (void**)&p_config);
    CHECK("Could not get config interface");

    if (vconn)
    {
        result = p_config->SetInt(
            bmdDeckLinkConfigVideoOutputConnection, vconn);
        CHECK("Could not set video output connection");
    }

    p_display_mode = MatchDisplayMode(obj, sys->p_output,
                                          fmt, wanted_mode_id);
    if(p_display_mode == NULL)
    {
        msg_Err(obj, "Could not negociate a compatible display mode");
        goto error;
    }
    else
    {
        BMDDisplayMode mode_id = p_display_mode->GetDisplayMode();
        BMDDisplayMode modenl = htonl(mode_id);
        msg_Dbg(obj, "Selected mode '%4.4s'", (char *) &modenl);

        BMDVideoOutputFlags flags = bmdVideoOutputVANC;
        if (mode_id == bmdModeNTSC ||
            mode_id == bmdModeNTSC2398 ||
            mode_id == bmdModePAL)
        {
            flags = bmdVideoOutputVITC;
        }

        BMDDisplayModeSupport support;
        IDeckLinkDisplayMode *resultMode;

        result = sys->p_output->DoesSupportVideoMode(mode_id,
                                                              sys->video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV,
                                                              flags, &support, &resultMode);
        CHECK("Does not support video mode");
        if (support == bmdDisplayModeNotSupported)
        {
            msg_Err(obj, "Video mode not supported");
            goto error;
        }

        sys->i_width = p_display_mode->GetWidth();
        sys->i_height = p_display_mode->GetHeight();
        if (sys->i_width <= 0 || sys->i_width & 1)
        {
             msg_Err(obj, "Unknown video mode specified.");
             goto error;
        }

        result = p_display_mode->GetFrameRate(&sys->frameduration,
                                              &sys->timescale);
        CHECK("Could not read frame rate");

        result = sys->p_output->EnableVideoOutput(mode_id, flags);
        CHECK("Could not enable video output");
    }

    /* start */
    result = sys->p_output->StartScheduledPlayback(
        (mdate() * sys->timescale) / CLOCK_FREQ, sys->timescale, 1.0);
    CHECK("Could not start playback");

    p_config->Release();
    p_display_mode->Release();
    p_card->Release();
    decklink_iterator->Release();

    return VLC_SUCCESS;

error:
    if (sys->p_output) {
        sys->p_output->Release();
        sys->p_output = NULL;
    }
    if (p_card)
        p_card->Release();
    if (p_config)
        p_config->Release();
    if (decklink_iterator)
        decklink_iterator->Release();
    if (p_display_mode)
        p_display_mode->Release();

    return VLC_EGENERIC;
#undef CHECK
}

/*****************************************************************************
 * Video
 *****************************************************************************/
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
        afd[i] |= parity(afd[i]) ? 0x100 : 0x200;

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

static void DisplayVideo(vlc_object_t *obj, decklink_sys_t *sys, picture_t *picture, subpicture_t *)
{
    mtime_t now = mdate();

    if (!picture)
        return;

    picture_t *orig_picture = picture;
if( picture->date - now >  5000 )
{
    msleep( picture->date - now );
}
    if (now - picture->date > sys->video.nosignal_delay * CLOCK_FREQ) {
        msg_Dbg(obj, "no signal");
        if (sys->video.pic_nosignal) {
            picture = sys->video.pic_nosignal;
        } else {
            if (sys->video.tenbits) { // I422_10L
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
    w = sys->i_width;
    h = sys->i_height;

    IDeckLinkMutableVideoFrame *pDLVideoFrame;
    result = sys->p_output->CreateVideoFrame(w, h, w*3,
        sys->video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV,
        bmdFrameFlagDefault, &pDLVideoFrame);

    if (result != S_OK) {
        msg_Err(obj, "Failed to create video frame: 0x%X", result);
        pDLVideoFrame = NULL;
        goto end;
    }

    void *frame_bytes;
    pDLVideoFrame->GetBytes((void**)&frame_bytes);
    stride = pDLVideoFrame->GetRowBytes();

    if (sys->video.tenbits) {
        IDeckLinkVideoFrameAncillary *vanc;
        int line;
        void *buf;

        result = sys->p_output->CreateAncillaryData(
                sys->video.tenbits ? bmdFormat10BitYUV : bmdFormat8BitYUV, &vanc);
        if (result != S_OK) {
            msg_Err(obj, "Failed to create vanc: %d", result);
            goto end;
        }

        line = var_InheritInteger(obj, VIDEO_CFG_PREFIX "afd-line");
        result = vanc->GetBufferForVerticalBlankingLine(line, &buf);
        if (result != S_OK) {
            msg_Err(obj, "Failed to get VBI line %d: %d", line, result);
            goto end;
        }
        send_AFD(sys->video.afd, sys->video.ar, (uint8_t*)buf);

        v210_convert(frame_bytes, picture, stride);

        result = pDLVideoFrame->SetAncillaryData(vanc);
        vanc->Release();
        if (result != S_OK) {
            msg_Err(obj, "Failed to set vanc: %d", result);
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
    length = (sys->frameduration * CLOCK_FREQ) / sys->timescale;

    picture->date -= sys->offset;
    result = sys->p_output->ScheduleVideoFrame(pDLVideoFrame,
        picture->date, length, CLOCK_FREQ);

    if (result != S_OK) {
        msg_Err(obj, "Dropped Video frame %" PRId64 ": 0x%x",
            picture->date, result);
        goto end;
    }

    now = mdate() - sys->offset;

    BMDTimeValue decklink_now;
    double speed;
    sys->p_output->GetScheduledStreamTime (CLOCK_FREQ, &decklink_now, &speed);

    if ((now - decklink_now) > 400000) {
        /* XXX: workaround card clock drift */
        sys->offset += 50000;
        msg_Err(obj, "Delaying: offset now %" PRId64, sys->offset);
    }

end:
    if (pDLVideoFrame)
        pDLVideoFrame->Release();
    picture_Release(orig_picture);
}

static decklink_sys_t * OpenVideo(vlc_object_t *p_this, const video_format_t *fmt)
{
    decklink_sys_t *sys = (decklink_sys_t *) calloc( 1, sizeof(*sys) );

    sys->video.tenbits = true;//var_InheritBool(p_this, VIDEO_CFG_PREFIX "tenbits");
    /*sys->nosignal_delay = var_InheritInteger(p_this, VIDEO_CFG_PREFIX "nosignal-delay");
    sys->afd = var_InheritInteger(p_this, VIDEO_CFG_PREFIX "afd");
    sys->ar = var_InheritInteger(p_this, VIDEO_CFG_PREFIX "ar");
    sys->pic_nosignal = NULL;*/

    int ret = OpenDecklink(p_this, sys, fmt);
    if (ret) {
        free(sys);
        return NULL;
    }

    sys->video.pool = NULL;

//    fmt->i_chroma = sys->video.tenbits
//        ? VLC_CODEC_I422_10L /* we will convert to v210 */
//        : VLC_CODEC_UYVY;
//    //video_format_FixRgb(fmt);

//    fmt->i_width = p_sys->i_width;
//    fmt->i_height = p_sys->i_height;

    return sys;
}

static void CloseVideo(vlc_object_t *p_this)
{
    struct decklink_sys_t *sys;

    if (sys->video.pool)
        picture_pool_Release(sys->video.pool);

    if (sys->video.pic_nosignal)
        picture_Release(sys->video.pic_nosignal);

    free(sys);
}
