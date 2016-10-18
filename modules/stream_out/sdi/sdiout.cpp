/*****************************************************************************
 * sdiout.cpp: SDI sout module for vlc
 *****************************************************************************
 * Copyright © 2014-2016 VideoLAN and VideoLAN Authors
 *                  2018 VideoLabs
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

#include "sdiout.hpp"

#include "DBMSDIOutput.hpp"

#include <vlc_common.h>
#include <vlc_sout.h>
#include <vlc_plugin.h>

#define NOSIGNAL_INDEX_TEXT N_("Timelength after which we assume there is no signal.")
#define NOSIGNAL_INDEX_LONGTEXT N_(\
    "Timelength after which we assume there is no signal.\n"\
    "After this delay we black out the video."\
    )

#define AFD_INDEX_TEXT N_("Active Format Descriptor value")

#define AR_INDEX_TEXT N_("Aspect Ratio")
#define AR_INDEX_LONGTEXT N_("Aspect Ratio of the source picture.")

#define AFDLINE_INDEX_TEXT N_("Active Format Descriptor line")
#define AFDLINE_INDEX_LONGTEXT N_("VBI line on which to output Active Format Descriptor.")

#define NOSIGNAL_IMAGE_TEXT N_("Picture to display on input signal loss")
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

#define VIDEO_CONNECTION_TEXT N_("Video connection")
#define VIDEO_CONNECTION_LONGTEXT N_(\
    "Video connection for DeckLink output.")

#define VIDEO_TENBITS_TEXT N_("10 bits")
#define VIDEO_TENBITS_LONGTEXT N_(\
    "Use 10 bits per pixel for video frames.")

/* Video Connections */
static const char *const ppsz_videoconns[] = {
    "sdi",
    "hdmi",
    "opticalsdi",
    "component",
    "composite",
    "svideo"
};
static const char *const ppsz_videoconns_text[] = {
    "SDI",
    "HDMI",
    "Optical SDI",
    "Component",
    "Composite",
    "S-video",
};
static const BMDVideoConnection rgbmd_videoconns[] =
{
    bmdVideoConnectionSDI,
    bmdVideoConnectionHDMI,
    bmdVideoConnectionOpticalSDI,
    bmdVideoConnectionComponent,
    bmdVideoConnectionComposite,
    bmdVideoConnectionSVideo,
};
static_assert(ARRAY_SIZE(rgbmd_videoconns) == ARRAY_SIZE(ppsz_videoconns), "videoconn arrays messed up");
static_assert(ARRAY_SIZE(rgbmd_videoconns) == ARRAY_SIZE(ppsz_videoconns_text), "videoconn arrays messed up");

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
static_assert(ARRAY_SIZE(rgi_afd_values) == ARRAY_SIZE(rgsz_afd_text), "afd arrays messed up");

static const int rgi_ar_values[] = {
    0, 1,
};
static const char * const rgsz_ar_text[] = {
    "0:   4:3",
    "1:  16:9",
};
static_assert(ARRAY_SIZE(rgi_ar_values) == ARRAY_SIZE(rgsz_ar_text), "afd arrays messed up");


/*****************************************************************************
 * Sout callbacks
 *****************************************************************************/

static void CloseSDIOutput(vlc_object_t *p_this)
{
    sout_stream_t *p_stream = reinterpret_cast<sout_stream_t*>(p_this);
    sdi_sout::DBMSDIOutput *sdi =
            reinterpret_cast<sdi_sout::DBMSDIOutput *>(p_stream->p_sys);
    delete sdi;
}

static int OpenSDIOutput(vlc_object_t *p_this)
{
    sout_stream_t *p_stream = reinterpret_cast<sout_stream_t*>(p_this);
    sdi_sout::DBMSDIOutput *output = new sdi_sout::DBMSDIOutput(p_stream);

    if(output->Open() != VLC_SUCCESS && !FORCE_INPUT)
    {
        delete output;
        return VLC_EGENERIC;
    }
    p_stream->p_sys = output;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()

    set_shortname(N_("SDI output"))
    set_description(N_("SDI stream output"))
    set_capability("sout stream", 0)
    add_shortcut("sdiout")
    set_category(CAT_SOUT)
    set_subcategory(SUBCAT_SOUT_STREAM)
    set_callbacks(OpenSDIOutput, CloseSDIOutput)

    set_section(N_("DeckLink General Options"), NULL)
    add_integer(CFG_PREFIX "card-index", 0,
                CARD_INDEX_TEXT, CARD_INDEX_LONGTEXT, true)

    set_section(N_("DeckLink Video Options"), NULL)
    add_string(CFG_PREFIX "video-connection", "sdi",
                VIDEO_CONNECTION_TEXT, VIDEO_CONNECTION_LONGTEXT, true)
                change_string_list(ppsz_videoconns, ppsz_videoconns_text)
    add_string(CFG_PREFIX "mode", "",
                MODE_TEXT, MODE_LONGTEXT, true)
    add_bool(CFG_PREFIX "tenbits", true,
                VIDEO_TENBITS_TEXT, VIDEO_TENBITS_LONGTEXT, true)
    add_integer(CFG_PREFIX "nosignal-delay", 5,
                NOSIGNAL_INDEX_TEXT, NOSIGNAL_INDEX_LONGTEXT, true)
    add_integer(CFG_PREFIX "afd-line", 16,
                AFDLINE_INDEX_TEXT, AFDLINE_INDEX_LONGTEXT, true)
    add_integer_with_range(CFG_PREFIX "afd", 8, 0, 16,
                AFD_INDEX_TEXT, AFD_INDEX_TEXT, true)
                change_integer_list(rgi_afd_values, rgsz_afd_text)
    add_integer_with_range(CFG_PREFIX "ar", 1, 0, 1,
                AR_INDEX_TEXT, AR_INDEX_LONGTEXT, true)
                change_integer_list(rgi_ar_values, rgsz_ar_text)
    add_loadfile(CFG_PREFIX "nosignal-image", NULL,
                 NOSIGNAL_IMAGE_TEXT, NOSIGNAL_IMAGE_LONGTEXT)

    set_section(N_("DeckLink Audio Options"), NULL)
    add_obsolete_string("audio-connection")
    add_integer(CFG_PREFIX "audio-rate", 48000,
                RATE_TEXT, RATE_LONGTEXT, true)

vlc_module_end ()
