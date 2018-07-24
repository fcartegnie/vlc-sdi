#ifndef SDISTREAM_HPP
#define SDISTREAM_HPP

#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_aout.h>
#include <vlc_codec.h>
#include <queue>

namespace sdi_sout
{
    class AbstractStreamOutputBuffer
    {
        public:
            AbstractStreamOutputBuffer();
            virtual ~AbstractStreamOutputBuffer();
            virtual void FlushQueued() = 0;
            void Enqueue(void *);
            void * Dequeue();

        private:
            std::queue<void *> queued;
    };

    class BlockStreamOutputBuffer : public AbstractStreamOutputBuffer
    {
        public:
            BlockStreamOutputBuffer();
            virtual ~BlockStreamOutputBuffer();
            virtual void FlushQueued();
    };

    class PictureStreamOutputBuffer : public AbstractStreamOutputBuffer
    {
        public:
            PictureStreamOutputBuffer();
            virtual ~PictureStreamOutputBuffer();
            virtual void FlushQueued();
    };

    class AbstractStream
    {
        public:
            AbstractStream(vlc_object_t *, AbstractStreamOutputBuffer *);
            virtual ~AbstractStream();
            virtual bool init(const es_format_t *) = 0;
            virtual int Send(block_t*) = 0;
            virtual void Drain() = 0;
            virtual void Flush() = 0;

        protected:
            vlc_object_t *p_stream;
            AbstractStreamOutputBuffer *outputbuffer;
    };

    class AbstractDecodedStream : public AbstractStream
    {
        public:
            AbstractDecodedStream(vlc_object_t *, AbstractStreamOutputBuffer *);
            virtual ~AbstractDecodedStream();
            virtual bool init(const es_format_t *); /* impl */
            virtual int Send(block_t*);
            virtual void Flush();
            virtual void Drain();
            void setOutputFormat(const es_format_t *);

        protected:
            decoder_t *p_decoder;
            virtual void setCallbacks() = 0;
            es_format_t requestedoutput;
    };

    class VideoDecodedStream : public AbstractDecodedStream
    {
        public:
            VideoDecodedStream(vlc_object_t *, AbstractStreamOutputBuffer *);
            virtual ~VideoDecodedStream();
            virtual void setCallbacks();
            void setCaptionsOutputBuffer(AbstractStreamOutputBuffer *);

        private:
            static void VideoDecCallback_queue(decoder_t *, picture_t *);
            static void VideoDecCallback_queue_cc( decoder_t *, block_t *,
                                                   const decoder_cc_desc_t * );
            static int VideoDecCallback_update_format(decoder_t *);
            static picture_t *VideoDecCallback_new_buffer(decoder_t *);
            filter_chain_t * VideoFilterCreate(const es_format_t *);
            void Output(picture_t *);
            void QueueCC(block_t *);
            filter_chain_t *p_filters_chain;
            AbstractStreamOutputBuffer *captionsOutputBuffer;
    };

#   define FRAME_SIZE 1920
    class AudioDecodedStream : public AbstractDecodedStream
    {
        public:
            AudioDecodedStream(vlc_object_t *, AbstractStreamOutputBuffer *);
            virtual ~AudioDecodedStream();
            virtual void setCallbacks();

        private:
            static void AudioDecCallback_queue(decoder_t *, block_t *);
            static int AudioDecCallback_update_format(decoder_t *);
            aout_filters_t *AudioFiltersCreate(const es_format_t *);
            void Output(block_t *);
            aout_filters_t *p_filters;
    };

    class CaptionsStream : public AbstractStream
    {
        public:
            CaptionsStream(vlc_object_t *, AbstractStreamOutputBuffer *);
            virtual ~CaptionsStream();
            virtual bool init(const es_format_t *); /* impl */
            virtual int Send(block_t*);
            virtual void Flush();
            virtual void Drain();

        protected:
            void FlushQueued();

        private:
            void Output(block_t *);
    };
}

#endif
