/*****************************************************************************
 * SDIOutput.hpp: SDI sout module for vlc
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
#ifndef SDIOUTPUT_HPP
#define SDIOUTPUT_HPP

#include "SDIStream.hpp"
#include <vlc_common.h>

namespace sdi_sout
{
    class SDIAudioMultiplex;

    class SDIOutput
    {
        public:
            SDIOutput(sout_stream_t *, uint8_t);
            virtual ~SDIOutput();
            virtual int Open() = 0;
            virtual int Process() = 0;
            virtual AbstractStream *Add(const es_format_t *);
            virtual int   Send(AbstractStream *, block_t *);
            virtual void  Del(AbstractStream *);
            virtual int   Control(int, va_list);

        protected:
            sout_stream_t *p_stream;
            PictureStreamOutputBuffer videoBuffer;
            BlockStreamOutputBuffer captionsBuffer;
            SDIAudioMultiplex *audioMultiplex;

        private:
            static void *SoutCallback_Add(sout_stream_t *, const es_format_t *);
            static void  SoutCallback_Del(sout_stream_t *, void *);
            static int   SoutCallback_Send(sout_stream_t *, void *, block_t*);
            static int   SoutCallback_Control(sout_stream_t *, int, va_list);
            static void  SoutCallback_Flush(sout_stream_t *, void *);
    };
}

#endif
