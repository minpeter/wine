/*
 * Copyright 2026 Peter Min
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_NETPROFM_UNIXLIB_H
#define __WINE_NETPROFM_UNIXLIB_H

enum reachability_state
{
    REACHABILITY_INDETERMINATE,
    REACHABILITY_OFFLINE,
    REACHABILITY_ONLINE,
};

struct reachability_start_params
{
    UINT64 handle;
    UINT32 state;
};

struct reachability_wait_params
{
    UINT64 handle;
    UINT32 state;
    UINT32 changed;
};

struct reachability_stop_params
{
    UINT64 handle;
};

C_ASSERT( sizeof(struct reachability_start_params) == 16 );
C_ASSERT( FIELD_OFFSET(struct reachability_start_params, state) == 8 );
C_ASSERT( sizeof(struct reachability_wait_params) == 16 );
C_ASSERT( FIELD_OFFSET(struct reachability_wait_params, state) == 8 );
C_ASSERT( FIELD_OFFSET(struct reachability_wait_params, changed) == 12 );
C_ASSERT( sizeof(struct reachability_stop_params) == 8 );

enum unix_funcs
{
    unix_reachability_start,
    unix_reachability_wait,
    unix_reachability_stop,
    unix_funcs_count,
};

#define UNIX_CALL(func, params) \
    (__wine_unixlib_handle ? WINE_UNIX_CALL( unix_##func, params ) : STATUS_NOT_SUPPORTED)

#endif /* __WINE_NETPROFM_UNIXLIB_H */
