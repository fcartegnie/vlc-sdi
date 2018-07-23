#ifndef SDIAUDIOMULTIPLEX_HPP
#define SDIAUDIOMULTIPLEX_HPP

#include "AES3Audio.hpp"
#include "SDIStream.hpp"

#include <vector>

namespace sdi_sout
{
    class SDIAudioMultiplexBuffer : public AES3AudioBuffer,
                                    public AbstractStreamOutputBuffer
    {
        public:
            SDIAudioMultiplexBuffer();
            virtual ~SDIAudioMultiplexBuffer();
            virtual void FlushQueued(); /* impl */
            virtual void Enqueue(void *); /* impl */
            virtual void * Dequeue(); /* impl */
    };

    class SDIAudioMultiplexConfig
    {
        public:
            SDIAudioMultiplexConfig(uint8_t channels = 2);
            ~SDIAudioMultiplexConfig();
            SDIAudioMultiplexBuffer *getBufferForStream(int i_id);
            bool SubFrameSlotUsed(uint8_t) const;
            void setSubFrameSlotUsed(uint8_t);
            uint8_t getMultiplexedFramesCount() const { return framewidth; }
            std::vector<uint8_t> getFreeSubFrameSlots() const;

            bool addAssociation(int, std::vector<uint8_t>);
            unsigned getMaxSamplesForBlockSize(size_t) const;

        private:
            class Assoc
            {
                public:
                    int i_stream_id;
                    SDIAudioMultiplexBuffer buffer;
                    std::vector<uint8_t> subframesslots;
            };
            std::vector<Assoc *> mappings;
            unsigned subframeslotbitmap;
            uint8_t framewidth;
    };

    class SDIAudioMultiplex
    {
        public:
            SDIAudioMultiplex(uint8_t channels);
            ~SDIAudioMultiplex();
            vlc_tick_t bufferStart() const;
            unsigned availableSamples() const;
            block_t * Extract(unsigned);
            unsigned getFreeSubFrameSlots() const;
            void SetSubFrameSource(uint8_t, AES3AudioBuffer *, AES3AudioSubFrameIndex);

            SDIAudioMultiplexConfig config;

        private:
            unsigned count;
            AES3AudioFrameSource framesources[MAX_AES3_AUDIO_FRAMES];
    };
}


#endif // SDIAUDIOMULTIPLEX_HPP
