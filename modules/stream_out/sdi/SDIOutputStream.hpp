#ifndef SDIOUTPUTSTREAM_HPP
#define SDIOUTPUTSTREAM_HPP

#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_aout.h>
#include <queue>

namespace sdi_sout
{
    template<typename Type>
    class QueuedData
    {
        public:
            QueuedData() {}
            virtual ~QueuedData() {}
            Type *Dequeue()
            {
                Type *p = NULL;
                if(!queued.empty())
                {
                    p = queued.front();
                    queued.pop();
                }
                return p;
            }
        protected:
            virtual void FlushQueued() = 0;
            void Enqueue(Type *p)
            {
                queued.push(p);
            }
        private:
            std::queue<Type *> queued;
    };

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
            void setOutputFormat(const es_format_t *);

        protected:
            decoder_t *p_decoder;
            virtual void setCallbacks() = 0;
            es_format_t requestedoutput;
    };

    class VideoDecodedStream : public AbstractDecodedStream,
                               public QueuedData<picture_t>
    {
        public:
            VideoDecodedStream(vlc_object_t *);
            virtual ~VideoDecodedStream();
            virtual void setCallbacks();

        protected:
            void FlushQueued();

        private:
            static void VideoDecCallback_queue(decoder_t *, picture_t *);
            static int VideoDecCallback_update_format(decoder_t *);
            static picture_t *VideoDecCallback_new_buffer(decoder_t *);
            filter_chain_t * VideoFilterCreate(const es_format_t *);
            void Output(picture_t *);
            filter_chain_t *p_filters_chain;
    };

#   define FRAME_SIZE 1920
    class AudioDecodedStream : public AbstractDecodedStream,
                               public QueuedData<block_t>
    {
        public:
            AudioDecodedStream(vlc_object_t *);
            virtual ~AudioDecodedStream();
            virtual void setCallbacks();

        protected:
            void FlushQueued();

        private:
            static void AudioDecCallback_queue(decoder_t *, block_t *);
            static int AudioDecCallback_update_format(decoder_t *);
            aout_filters_t *AudioFiltersCreate(const es_format_t *);
            void Output(block_t *);
            aout_filters_t *p_filters;
    };
}

#endif