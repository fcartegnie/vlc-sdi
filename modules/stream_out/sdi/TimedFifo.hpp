/*****************************************************************************
 * TimedFifo.hpp:
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
#ifndef TIMEDFIFO_HPP
#define TIMEDFIFO_HPP

#include <vlc_common.h>
#include <list>

namespace sdi_sout
{
    class TimedFifo
    {
        public:
            TimedFifo();
            ~TimedFifo();

            size_t bytesRemaining() const;
            size_t blockBytesRemaining() const;
            void push(block_t *);
            size_t read(uint8_t *, size_t, size_t) const;
            void flush(size_t);
            bool empty() const;
            size_t getBlockConsumed() const;
            vlc_tick_t head() const;

        private:
            size_t offset;
            std::list<block_t *> blocks;
    };
}

#endif
