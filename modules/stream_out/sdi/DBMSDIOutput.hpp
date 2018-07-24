#ifndef DBMSDIOUTPUT_HPP
#define DBMSDIOUTPUT_HPP

#include "SDIOutput.hpp"

#include <vlc_es.h>

#include <DeckLinkAPI.h>

namespace sdi_sout
{
    class VideoDecodedStream;
    class AudioDecodedStream;

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
            AudioDecodedStream *audioStream;
            int ProcessVideo(picture_t *);
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
                es_format_t configuredfmt;
                int i_rate;
            } audio;

            struct
            {
                uint8_t afd, ar;
                unsigned afd_line;
            } ancillary;

            bool b_running;
            int ConfigureVideo(const video_format_t *);
            int ConfigureAudio(const audio_format_t *);
            int Start();
            const char *ErrorToString(long i_code);
            IDeckLinkDisplayMode * MatchDisplayMode(const video_format_t *,
                                                    BMDDisplayMode = bmdDisplayModeNotSupported);
            int doProcessVideo(picture_t *);
            picture_t * CreateNoSignalPicture(const char*, const video_format_t *);
    };
}

#endif // DBMSDIOUTPUT_HPP
