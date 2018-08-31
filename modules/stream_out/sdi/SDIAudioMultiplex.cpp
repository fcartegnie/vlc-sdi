/*****************************************************************************
 * SDIAudioMultiplex.cpp: SDI Audio Multiplexing
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "SDIAudioMultiplex.hpp"
#include <vlc_es.h>
#include <limits>
#include <cstring>
#include <cctype>
#include <algorithm>

using namespace sdi_sout;

SDIAudioMultiplexBuffer::SDIAudioMultiplexBuffer()
    : AES3AudioBuffer(2), AbstractStreamOutputBuffer()
{

}

SDIAudioMultiplexBuffer::~SDIAudioMultiplexBuffer()
{
    FlushQueued();
}

void SDIAudioMultiplexBuffer::FlushQueued()
{

}

void SDIAudioMultiplexBuffer::Enqueue(void *p)
{
    AES3AudioBuffer::push(reinterpret_cast<block_t *>(p));
}

void * SDIAudioMultiplexBuffer::Dequeue()
{
    return NULL;
}

static void ConfigureChannels(unsigned i, es_format_t *fmt)
{
    if( i>=8 )
    {
        i = 8;
        fmt->audio.i_physical_channels = AOUT_CHANS_7_1;
    }
    else if( i>2 )
    {
        i = 6;
        fmt->audio.i_physical_channels = AOUT_CHANS_5_1;
    }
    else
    {
        fmt->audio.i_physical_channels = AOUT_CHANS_STEREO;
    }
    fmt->audio.i_channels = i;
    fmt->audio.i_blockalign = i * 16 / 8;
}

SDIAudioMultiplexConfig::Mapping::Mapping(const StreamID &id)
    : id(id)
{
    es_format_Init(&fmt, AUDIO_ES, VLC_CODEC_S16N);
    fmt.audio.i_format = VLC_CODEC_S16N;
    fmt.audio.i_rate = 48000;
    fmt.audio.i_bitspersample = 16;
    ConfigureChannels(2, &fmt);
}

SDIAudioMultiplexConfig::Mapping::~Mapping()
{
    es_format_Clean(&fmt);
}

SDIAudioMultiplexConfig::SDIAudioMultiplexConfig(uint8_t channels)
{
    subframeslotbitmap = 0;
    if(channels > 4)
        framewidth = 8;
    else if(channels > 2)
        framewidth = 4;
    else
        framewidth = 1;
    b_accept_any = true;
}

SDIAudioMultiplexConfig::~SDIAudioMultiplexConfig()
{
    for(size_t i=0; i<mappings.size(); i++)
        delete mappings[i];
}

bool SDIAudioMultiplexConfig::SubFrameSlotUsed(uint8_t i) const
{
    return (1 << i) & subframeslotbitmap;
}

void SDIAudioMultiplexConfig::setSubFrameSlotUsed(uint8_t i)
{
    subframeslotbitmap |= (1 << i);
}

void SDIAudioMultiplexConfig::parseConfiguration(const char *psz)
{
    char *name = NULL;
    char *psz_in = (char*)psz;
    config_chain_t *p_config_chain = NULL;
    while(psz_in)
    {
        char *psz_next = config_ChainCreate(&name, &p_config_chain, psz_in);
        if(name)
        {
            if(!std::strcmp(name, "only"))
            {
                b_accept_any = false;
            }
            else /* try mapping decl */
            {
                int i_id = -1;
                int i_seqid = -1;
                int *pi_id = &i_seqid;
                const char *psz_id = name;
                if(psz_id[0]=='#')
                {
                    psz_id++;
                    pi_id = &i_id;
                }
                for(const char *p = psz_id; *p;)
                {
                    if(!std::isdigit(*p++))
                        break;
                    if(*p == '\0')
                        *pi_id = atoi(psz_id);
                }
                if(i_id != -1 || i_seqid != -1)
                {
                    printf("chain %d %d %s next %s\n", i_id, i_seqid, name, psz_next);
                    int i_reserved_chans = 0;
                    std::vector<uint8_t> subframeslots;
                    for(config_chain_t *p = p_config_chain; p; p = p->p_next)
                    {
                        if(!std::strcmp("chans", p->psz_name))
                        {
                            char *end = NULL;
                            int i_val = std::strtol(p->psz_value, &end, 10);
                            if(end != NULL && *end == '\0')
                                i_reserved_chans = i_val;
                        }
                        else
                        {
                            char *end = NULL;
                            int i_slot = std::strtol(p->psz_name, &end, 10);
                            if(end != NULL && *end == '\0')
                            {
                                if(i_slot < MAX_AES3_AUDIO_SUBFRAMES && i_slot < (2 * framewidth) &&
                                std::find(subframeslots.begin(), subframeslots.end(), i_slot) == subframeslots.end())
                                    subframeslots.push_back(i_slot);
                            }
                        }
                    }

                    if(subframeslots.empty() && i_reserved_chans)
                        addMapping(StreamID(i_id, i_seqid), i_reserved_chans);
                    else if(!subframeslots.empty())
                        addMapping(StreamID(i_id, i_seqid), subframeslots);
                }
            }
            free(name);
        }
        config_ChainDestroy(p_config_chain);
        if(psz != psz_in)
            free(psz_in);
        psz_in = psz_next;
    }
}

std::vector<uint8_t> SDIAudioMultiplexConfig::getFreeSubFrameSlots() const
{
    std::vector<uint8_t> slots;
    for(uint8_t i=0; i<getMultiplexedFramesCount() * 2; i++)
    {
        if(!SubFrameSlotUsed(i))
            slots.push_back(i);
    }

    return slots;
}

std::vector<uint8_t> SDIAudioMultiplexConfig::getConfiguredSlots(const StreamID &id) const
{
    for(size_t i=0; i<mappings.size(); i++)
    {
        if(mappings[i]->id == id)
            return mappings[i]->subframesslots;
    }
    return std::vector<uint8_t>();
}

bool SDIAudioMultiplexConfig::addMapping(const StreamID &id, const es_format_t *fmt)
{
    if(!fmt->audio.i_channels || !b_accept_any)
        return false;
    return addMapping(id, fmt->audio.i_channels);
}

bool SDIAudioMultiplexConfig::addMapping(const StreamID &id, unsigned channels)
{
    std::vector<uint8_t> slots = getFreeSubFrameSlots();
    if(slots.size() < channels)
        return false;
    slots.resize(channels);
    return addMapping(id, slots);
}

bool SDIAudioMultiplexConfig::addMapping(const StreamID &id, std::vector<uint8_t> subframeslots)
{
    for(size_t i=0; i<mappings.size(); i++)
        if(mappings[i]->id == id)
            return false;
    for(size_t i=0; i<subframeslots.size(); i++)
        if(SubFrameSlotUsed(subframeslots[i]))
            return false;

    Mapping *assoc = new Mapping(id);
    assoc->subframesslots = subframeslots;

    mappings.push_back(assoc);

    for(size_t i=0; i<subframeslots.size(); i++)
        setSubFrameSlotUsed(subframeslots[i]);

    return true;
}

unsigned SDIAudioMultiplexConfig::getMaxSamplesForBlockSize(size_t s) const
{
    return s / (2 * sizeof(uint16_t) * getMultiplexedFramesCount());
}

SDIAudioMultiplexBuffer *
    SDIAudioMultiplexConfig::getBufferForStream(const StreamID &id)
{
    for(size_t i=0; i<mappings.size(); i++)
    {
        if(mappings[i]->id == id)
            return &mappings[i]->buffer;
    }
    return NULL;
}

const es_format_t * SDIAudioMultiplexConfig::getConfigurationForStream(const StreamID &id) const
{
    auto it = std::find_if(mappings.begin(), mappings.end(),
                           [&id](Mapping *e) { return e->id == id; });
    return (it != mappings.end()) ? &(*it)->fmt : NULL;
}

const es_format_t *
    SDIAudioMultiplexConfig::updateFromRealESConfig(const StreamID &id,
                                                    const es_format_t *fmt)
{
    auto it = std::find_if(mappings.begin(), mappings.end(),
                           [&id](Mapping *e) { return e->id == id; });
    if(it != mappings.end())
    {
        Mapping *mapping = (*it);
        if(mapping->subframesslots.size() > 2 && fmt->audio.i_channels > 2)
            ConfigureChannels(fmt->audio.i_channels, &mapping->fmt);
        mapping->buffer.setSubFramesCount(mapping->fmt.audio.i_channels);
        return &mapping->fmt;
    }
    assert(0);
    return NULL;
}

SDIAudioMultiplex::SDIAudioMultiplex(uint8_t channels)
{
    config = SDIAudioMultiplexConfig(channels);
}

SDIAudioMultiplex::~SDIAudioMultiplex()
{

}

unsigned SDIAudioMultiplex::availableSamples() const
{
    unsigned samples = std::numeric_limits<unsigned>::max();
    for(size_t i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
    {
        if(framesources[i].subframe0.available() &&
           framesources[i].subframe1.available())
            continue;
        samples = std::min(samples, framesources[i].availableSamples());
    }
    return samples < std::numeric_limits<unsigned>::max() ? samples : 0;
}

vlc_tick_t SDIAudioMultiplex::bufferStart() const
{
    vlc_tick_t start = VLC_TICK_INVALID;
    for(size_t i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
    {
        if(framesources[i].subframe0.available() &&
           framesources[i].subframe1.available())
            continue;
        vlc_tick_t t = framesources[i].bufferStartTime();
        if(start == VLC_TICK_INVALID ||
           (t != VLC_TICK_INVALID && t<start))
            start = t;
    }
    return start;
}

unsigned SDIAudioMultiplex::getFreeSubFrameSlots() const
{
    unsigned bitfield = 0;
    for(unsigned i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
    {
        const AES3AudioFrameSource *source = &framesources[i];
        if(source->subframe0.available())
            bitfield |= (1 << (i * 2 + 0));
        if(source->subframe1.available())
            bitfield |= (1 << (i * 2 + 1));
    }
    return bitfield;
}

void SDIAudioMultiplex::SetSubFrameSource(uint8_t n, AES3AudioBuffer *buf,
                                          AES3AudioSubFrameIndex idx)
{
    assert(n<MAX_AES3_AUDIO_SUBFRAMES);
    AES3AudioFrameSource *f = &framesources[n / 2];
    AES3AudioSubFrameSource *s = (n & 1) ? &f->subframe1 : &f->subframe0;
    printf("mapped slot %d channel %d -> %p\n", n, idx.index(), s );
    assert(s->available());
    *s = AES3AudioSubFrameSource(buf, idx);
}

block_t * SDIAudioMultiplex::Extract(unsigned samples)
{
    vlc_tick_t start = bufferStart();

    uint8_t interleavedframes = config.getMultiplexedFramesCount();

    block_t *p_block = block_Alloc( interleavedframes * 2 * sizeof(uint16_t) * samples );
    if(!p_block)
        return NULL;
    memset(p_block->p_buffer, 0, p_block->i_buffer);

    p_block->i_pts = p_block->i_dts = start;
    p_block->i_nb_samples = samples;

    for(unsigned i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
    {
        AES3AudioFrameSource *source = &framesources[i];
        unsigned avail = source->availableSamples();
        if(avail == 0)
            continue;

        unsigned toskip = 0;
        unsigned tocopy = std::min(samples, avail);

        toskip = source->samplesUpToTime(start);
        if(toskip > tocopy)
            continue;
        tocopy -= toskip;

        source->subframe0.copy(p_block->p_buffer, tocopy, (i * 2 + 0), interleavedframes);
        source->subframe1.copy(p_block->p_buffer, tocopy, (i * 2 + 1), interleavedframes);
    }

    for(unsigned i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
        framesources[i].tagConsumed(samples);
    for(unsigned i=0; i<MAX_AES3_AUDIO_FRAMES; i++)
        framesources[i].flushConsumed();

    return p_block;
}
