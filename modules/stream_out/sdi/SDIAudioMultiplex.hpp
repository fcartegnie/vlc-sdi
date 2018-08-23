#ifndef SDIAUDIOMULTIPLEX_HPP
#define SDIAUDIOMULTIPLEX_HPP

#include "AES3Audio.hpp"
#include "SDIStream.hpp"

#define MAX_AUDIO_PAIRS 8

namespace sdi_sout
{
    class SDIAudioMultiplexAudioStream : public AudioDecodedStream,
                                         public AES3AudioBuffer
    {
        public:
            SDIAudioMultiplexAudioStream(vlc_object_t *);
            virtual ~SDIAudioMultiplexAudioStream();

        protected:
            virtual void Enqueue(block_t *); /* reimpl */
    };

    class SDIAudioMultiplex
    {
        public:
            SDIAudioMultiplex();
            ~SDIAudioMultiplex();
            vlc_tick_t bufferStart() const;
            void Extract( unsigned, uint16_t *, uint8_t );

        private:
            unsigned count;
            AES3AudioFrameSource framesources[MAX_AES3_AUDIO_FRAMES];
    };
}


#endif // SDIAUDIOMULTIPLEX_HPP
