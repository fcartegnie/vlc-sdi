/*****************************************************************************
 * TimedFifo.cpp:
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

#include "TimedFifo.hpp"

#include <vlc_block.h>
#include <limits>

using namespace sdi_sout;

TimedFifo::TimedFifo()
{
    offset = 0;
}

TimedFifo::~TimedFifo()
{
    flush(std::numeric_limits<size_t>::max());
}

void TimedFifo::push(block_t *p)
{
    blocks.push_back(p);
}

size_t TimedFifo::bytesRemaining() const
{
    size_t total = 0;
    for(auto it = blocks.begin(); it != blocks.end(); ++it)
        total += (*it)->i_buffer;
    return total;
}

size_t TimedFifo::blockBytesRemaining() const
{
    if(blocks.empty())
        return 0;
    return blocks.front()->i_buffer;
}

size_t TimedFifo::read(uint8_t *p_dst, size_t skip, size_t count) const
{
    size_t totalread = 0;
    for(auto it = blocks.begin(); it != blocks.end() && count; ++it)
    {
        const uint8_t *p_src = (*it)->p_buffer;
        size_t readfromthisblock = std::min((*it)->i_buffer, count);
        if(skip > 0)
        {
            if(skip > readfromthisblock)
            {
                readfromthisblock = 0;
                skip -= readfromthisblock;
            }
            else
            {
                p_src += skip;
                readfromthisblock -= skip;
                skip = 0;
            }
        }

        if(readfromthisblock > 0)
        {
            if(p_dst)
                memcpy(p_dst, p_src, readfromthisblock);
            p_dst += readfromthisblock;
            count -= readfromthisblock;
            totalread += readfromthisblock;
        }
    }
    return totalread;
}

void TimedFifo::flush(size_t count)
{
    while(!blocks.empty() && count)
    {
        block_t *p_block = blocks.front();
        if(p_block->i_buffer <= count)
        {
            count -= std::min(count, p_block->i_buffer);
            block_Release(p_block);
            blocks.pop_front();
            offset = 0;
        }
        else
        {
            p_block->i_buffer -= count;
            offset += count;
            break;
        }
    }
}

bool TimedFifo::empty() const
{
    return blocks.empty();
}

size_t TimedFifo::getBlockConsumed() const
{
    return offset;
}

vlc_tick_t TimedFifo::head() const
{
    if(blocks.empty())
        return VLC_TICK_INVALID;
    return blocks.front()->i_pts;
}
