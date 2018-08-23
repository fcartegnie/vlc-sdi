#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "AES3Audio.hpp"
#include <cassert>

using namespace sdi_sout;

AES3AudioBuffer::AES3AudioBuffer(unsigned count)
{
    subframescount = count;
    block_BytestreamInit(&bytestream);
}

AES3AudioBuffer::~AES3AudioBuffer()
{
    block_BytestreamRelease(&bytestream);
}

void AES3AudioBuffer::push(block_t *p_block)
{
    block_BytestreamPush(&bytestream, p_block);
}

void AES3AudioBuffer::read(void *buf, unsigned count,
                           const AES3AudioSubFrameIndex &buffersubframeidx,
                           unsigned targetframecount)
{
    if(!buffersubframeidx.isValid() || buffersubframeidx.index() >= subframescount)
    {
        assert(buffersubframeidx.index() < subframescount);
        return;
    }

    if(buf == NULL)
        return;
    uint8_t *dst = reinterpret_cast<uint8_t *>(buf);
    for(unsigned i=0; i<count; i++)
    {
       size_t srcoffset = 2 * i * subframescount + buffersubframeidx.index();
       size_t dstoffset = 2 * i * targetframecount;
       block_PeekOffsetBytes(&bytestream, srcoffset, &dst[dstoffset], 2);
    }
}

size_t AES3AudioBuffer::FramesToBytes(unsigned f) const
{
    return (size_t) f * 2 * subframescount;
}

int64_t AES3AudioBuffer::FramesToDuration(unsigned f) const
{
    return CLOCK_FREQ * f / 48000;
}

unsigned AES3AudioBuffer::BytesToFrames(size_t s) const
{
    return s / (2 * subframescount);
}

unsigned AES3AudioBuffer::TicksDurationToFrames(int64_t t) const
{
    return t * 48000 / CLOCK_FREQ;
}

void AES3AudioBuffer::forwardBy(unsigned f)
{
    size_t bytes = FramesToBytes(f);
    block_SkipBytes(&bytestream, bytes);
    block_BytestreamFlush(&bytestream);
}

void AES3AudioBuffer::forwardTo(vlc_tick_t t)
{
    if(bufferStart() == VLC_TICK_INVALID)
        return;

    forwardBy(TicksDurationToFrames(t - bytestream.p_block->i_pts));
}

vlc_tick_t AES3AudioBuffer::bufferStart() const
{
    if(bytestream.p_block)
        return bytestream.p_block->i_pts +
               FramesToDuration(BytesToFrames(bytestream.i_block_offset));
    else
        return VLC_TICK_INVALID;
}

vlc_tick_t AES3AudioBuffer::bufferEnd() const
{
    vlc_tick_t start = bufferStart();
    if(start != VLC_TICK_INVALID)
        start += CLOCK_FREQ * block_BytestreamRemaining(&bytestream) / 96000;
     return start;
}


void AES3AudioSubFrameSource::copy(void *buf, unsigned count, unsigned widthinsubframes)
{
    if(aes3AudioBuffer == NULL)
        return;
    aes3AudioBuffer->read(buf, count, bufferSubFrameIdx, widthinsubframes);
}

const AES3AudioSubFrameIndex & AES3AudioSubFrameSource::index() const
{
    return bufferSubFrameIdx;
}

unsigned AES3AudioFrameSource::samplesUpToTime(vlc_tick_t t) const
{
    int64_t diff = t - bufferStartTime();
    if(diff <= 0)
        return 0;
    return diff / (48000 * 2 * 2);
}

AES3AudioSubFrameIndex::AES3AudioSubFrameIndex(uint8_t v)
{
    subframeindex = v;
}

uint8_t AES3AudioSubFrameIndex::index() const
{
    return subframeindex;
}

bool AES3AudioSubFrameIndex::isValid() const
{
    return subframeindex < MAX_AES3_AUDIO_SUBFRAMES;
}
