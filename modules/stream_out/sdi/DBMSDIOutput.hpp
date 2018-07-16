#ifndef DBMSDIOUTPUT_HPP
#define DBMSDIOUTPUT_HPP

#include "SDIOutput.hpp"

#include <vlc_es.h>

#include <DeckLinkAPI.h>

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

        protected:
            AbstractStream *videoStream;
            AbstractStream *audioStream;
            int Process(picture_t *);

        private:
            IDeckLink *p_card;
            IDeckLinkOutput *p_output;

            BMDTimeScale timescale;
            BMDTimeValue frameduration;
            /* XXX: workaround card clock drift */
            vlc_tick_t offset;

            struct
            {
                video_format_t currentfmt;
//                picture_pool_t *pool;
                bool tenbits;
                uint8_t afd, ar;
                int nosignal_delay;
                picture_t *pic_nosignal;
            } video;

            struct
            {
                int i_rate;
//                int i_channels;
            } audio;

            bool b_running;
            int ConfigureVideo(const video_format_t *);
            int ConfigureAudio(const audio_format_t *);
            int Start();
            const char *ErrorToString(long i_code);
            IDeckLinkDisplayMode * MatchDisplayMode(const video_format_t *,
                                                    BMDDisplayMode = bmdDisplayModeNotSupported);
    };
}

#endif // DBMSDIOUTPUT_HPP
