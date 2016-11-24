/*****************************************************************************
 * vlc_ancillary.h: Ancillary data declarations
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
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
#ifndef VLC_ANCILLARY_H
#define VLC_ANCILLARY_H

# ifdef __cplusplus
extern "C" {
# endif

typedef enum
{
    ANCILLARY_UNDEFINED       = 0,
    ANCILLARY_CLOSED_CAPTIONS = 1 << 0,
    ANCILLARY_AFD             = 1 << 1,
    ANCILLARY_BAR             = 1 << 2,

} vlc_ancillary_type_t;

struct vlc_ancillary_t
{
    vlc_ancillary_t *p_next;
    void  *p_data;
    size_t i_data;
    vlc_ancillary_type_t type;

    /* toremove below */
    union
    {
        struct
        {
            int8_t val;
        } afd;
        struct
        {
            int16_t top;
            int16_t bottom;
            int16_t left;
            int16_t right;
        } bar;
    };

    mtime_t i_date;
};

static inline void vlc_ancillary_Delete( vlc_ancillary_t *p_anc )
{
    free( p_anc );
}

static inline vlc_ancillary_t * vlc_ancillary_New( vlc_ancillary_type_t type, size_t i_data )
{
    vlc_ancillary_t *p_anc = (vlc_ancillary_t *) malloc(sizeof(*p_anc) + i_data);
    if(p_anc)
    {
        p_anc->type = type;
        p_anc->i_data = i_data;
        p_anc->p_data = &p_anc[1];
        p_anc->p_next = NULL;
    }
    return p_anc;
}

static inline void vlc_ancillary_StoragePrepend( vlc_ancillary_t **pp_stor,
                                                 vlc_ancillary_t *p_anc )
{
    p_anc->p_next = *pp_stor;
    *pp_stor = p_anc;
}

static inline void vlc_ancillary_StorageRemove( vlc_ancillary_t **pp_stor,
                                                vlc_ancillary_type_t type )
{
    while( *pp_stor )
    {
        vlc_ancillary_t *p_cur = *pp_stor;
        if( p_cur->type == type )
        {
            *pp_stor = p_cur->p_next;
            vlc_ancillary_Delete( p_cur );
        }
        else pp_stor = &p_cur->p_next;
    }
}

static inline void vlc_ancillary_StorageMerge( vlc_ancillary_t **pp_stor,
                                               vlc_ancillary_t *p_anc )
{
    vlc_ancillary_StorageRemove( pp_stor, p_anc->type );
    vlc_ancillary_StoragePrepend( pp_stor, p_anc );
}

static inline void vlc_ancillary_StorageEmpty( vlc_ancillary_t **pp_stor )
{
    while( *pp_stor )
    {
        vlc_ancillary_t *p_cur = *pp_stor;
        *pp_stor = p_cur->p_next;
        vlc_ancillary_Delete( p_cur );
    }
}

# ifdef __cplusplus
}
# endif

#endif
