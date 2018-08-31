/*****************************************************************************
 * DBMSDIOutput.hpp: Decklink SDI Output
 *****************************************************************************
 * Copyright © 2018 VideoLabs, VideoLAN and VideoLAN Authors
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
#ifndef DBMSDIOUTPUT_HPP
#define DBMSDIOUTPUT_HPP

#include "SDIOutput.hpp"

#include <vlc_es.h>

#include <DeckLinkAPI.h>
#include <list>

namespace sdi_sout
{
    class DBMSDIOutput : public SDIOutput
    {
        public:
            DBMSDIOutput(sout_stream_t *);
            ~DBMSDIOutput();
            virtual AbstractStream *Add(const es_format_t *); /* reimpl */
            virtual void Del(AbstractStream *); /* reimpl */
            virtual int Open(); /* impl */
            virtual int Process(); /* impl */

        protected:
            VideoDecodedStream *videoStream;
            std::list<AudioDecodedStream *> audioStreams;
            CaptionsStream *captionsStream;
            int ProcessVideo(picture_t *, block_t *);
            int ProcessAudio(block_t *);

        private:
            IDeckLink *p_card;
            IDeckLinkOutput *p_output;

            BMDTimeScale timescale;
            BMDTimeValue frameduration;
            vlc_tick_t lasttimestamp;
            /* XXX: workaround card clock drift */
            vlc_tick_t offset;

            struct
            {
                es_format_t configuredfmt;
                bool tenbits;
                int nosignal_delay;
                picture_t *pic_nosignal;
            } video;

            struct
            {
                uint8_t i_channels;
                bool b_configured;
            } audio;

            struct
            {
                uint8_t afd, ar;
                unsigned afd_line;
                unsigned captions_line;
            } ancillary;

            bool b_running;
            int ConfigureVideo(const video_format_t *);
            int ConfigureAudio(const audio_format_t *);
            int Start();
            const char *ErrorToString(long i_code);
            IDeckLinkDisplayMode * MatchDisplayMode(const video_format_t *,
                                                    BMDDisplayMode = bmdDisplayModeNotSupported);
            int doProcessVideo(picture_t *, block_t *);
            picture_t * CreateNoSignalPicture(const char*, const video_format_t *);
    };
}

#endif // DBMSDIOUTPUT_HPP
