#ifndef SDIOUTPUTSTREAM_HPP
#define SDIOUTPUTSTREAM_HPP

#include <vlc_common.h>
#include <vlc_filter.h>

namespace sdi_sout
{
    class AbstractStream
    {
        public:
            AbstractStream(vlc_object_t *);
            virtual ~AbstractStream();
            virtual bool init(const es_format_t *) = 0;
            virtual int Send(block_t*) = 0;
            virtual void Flush() = 0;

        protected:
            vlc_object_t *p_stream;
    };

    class AbstractDecodedStream : public AbstractStream
    {
        public:
            AbstractDecodedStream(vlc_object_t *);
            virtual ~AbstractDecodedStream();
            virtual bool init(const es_format_t *); /* impl */
            virtual int Send(block_t*);
            virtual void Flush();

        protected:
            decoder_t *p_decoder;
            virtual void setCallbacks() = 0;
    };

    class VideoDecodedStream : public AbstractDecodedStream
    {
        public:
            VideoDecodedStream(vlc_object_t *);
            virtual ~VideoDecodedStream();
            virtual void setCallbacks();

        private:
            static void VideoDecCallback_queue(decoder_t *, picture_t *);
            static int VideoDecCallback_update_format(decoder_t *);
            static picture_t *VideoDecCallback_new_buffer(decoder_t *);
            filter_chain_t * VideoFilterCreate(const es_format_t *);
            void Queue(picture_t *);
            filter_chain_t  *p_filters_chain;
    };


    class AudioDecodedStream : public AbstractDecodedStream
    {
        public:
            AudioDecodedStream(vlc_object_t *);
            virtual ~AudioDecodedStream();
            virtual void setCallbacks();
    };
}

#endif
