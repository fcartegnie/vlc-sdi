#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "SDIAudioMultiplex.hpp"

using namespace sdi_sout;

SDIAudioMultiplexAudioStream::SDIAudioMultiplexAudioStream(vlc_object_t *obj)
    :AudioDecodedStream(obj), AES3AudioBuffer(2)
{

}

SDIAudioMultiplexAudioStream::~SDIAudioMultiplexAudioStream()
{

}

void SDIAudioMultiplexAudioStream::Enqueue(block_t *p)
{
    msg_Err(p_stream, "AES3 Push");
    AES3AudioBuffer::push(p);
}

SDIAudioMultiplex::SDIAudioMultiplex()
{

}

SDIAudioMultiplex::~SDIAudioMultiplex()
{

}

vlc_tick_t SDIAudioMultiplex::bufferStart() const
{
    vlc_tick_t start = VLC_TICK_INVALID;
    for(size_t i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
    {
        vlc_tick_t t = framesources[i].bufferStartTime();
        if(t != VLC_TICK_INVALID && t<start)
            start = t;
    }
    return start;
}

void SDIAudioMultiplex::Extract(unsigned samples, uint16_t *p_buf,
                                uint8_t interleavewidthinframes)
{
    vlc_tick_t start = bufferStart();

    for(unsigned i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
    {
        AES3AudioFrameSource *source = &framesources[i];
        unsigned avail = source->subFramesCount();
        if(avail == 0)
            continue;

        unsigned toskip = 0;
        unsigned tocopy = std::min(samples, avail);

        toskip = source->samplesUpToTime(start);
        if(toskip > tocopy)
            continue;
        tocopy -= toskip;

        source->subframe0.copy(p_buf, tocopy, interleavewidthinframes);
        source->subframe1.copy(p_buf, tocopy, interleavewidthinframes);
    }
}
