/*
 *  mod_cluster
 *
 *  Copyright(c) 2008 Red Hat Middleware, LLC,
 *  and individual contributors as indicated by the @authors tag.
 *  See the copyright.txt in the distribution for a
 *  full listing of individual contributors. 
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library in the file COPYING.LIB;
 *  if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * @author Jean-Frederic Clere
 * @version $Revision$
 */

/**
 * @file  balancer.c
 * @brief balancer description Storage Module for Apache
 *
 * @defgroup MEM balancers
 * @ingroup  APACHE_MODS
 * @{
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_manager_module.h"
#include "../include/ngx_utils.h"

static mem_t * create_attach_mem_balancer(u_char *string, int *num, int type, ngx_pool_t *p, slotmem_storage_method *storage) {
    mem_t *ptr;
    const u_char *storename;
    ngx_int_t rv;

    ptr = ngx_pcalloc(p, sizeof(mem_t));
    if (!ptr) {
        return NULL;
    }
    ptr->storage =  storage;
    storename = ngx_pstrcat(p, string, BALANCEREXE, NULL); 
    if (type)
        rv = ptr->storage->ap_slotmem_create(&ptr->slotmem, storename, sizeof(balancerinfo_t), *num, type, p, storage->cf, storage->ngx_http_module);
    else {
        size_t size = sizeof(balancerinfo_t);
        rv = ptr->storage->ap_slotmem_attach(&ptr->slotmem, storename, &size, num, p);
    }
    if (rv != NGX_OK) {
        return NULL;
    }
    ptr->num = *num;
    ptr->p = p;
    return ptr;
}

ngx_int_t init_mem_balancer(mem_t *ptr, u_char *string, int *num, ngx_pool_t *p) {
    const u_char *storename;
    ngx_int_t rv;

    if (!ptr) {
        return !NGX_OK;
    }
    
    storename = ngx_pstrcat(p, string, BALANCEREXE, NULL); 
      
    rv = ptr->storage->ap_slotmem_init(&ptr->slotmem, storename, sizeof(balancerinfo_t), *num, p);
   
    if (rv != NGX_OK) {
        return !NGX_OK;
    }

    return NGX_OK;
}
/**
 * Insert(alloc) and update a balancer record in the shared table
 * @param pointer to the shared table.
 * @param balancer balancer to store in the shared table.
 * @return NGX_OK if all went well
 *
 */
static ngx_int_t insert_update(void* mem, void **data, int id, ngx_pool_t *pool)
{
    balancerinfo_t *in = (balancerinfo_t *)*data;
    balancerinfo_t *ou = (balancerinfo_t *)mem;
    if (ngx_strcmp(in->balancer, ou->balancer) == 0) {
        memcpy(ou, in, sizeof(balancerinfo_t));
        ou->id = id;
        ou->updatetime = time(NULL);
        *data = ou;
        return NGX_OK;
    }
    return NGX_NONE;
}
ngx_int_t insert_update_balancer(mem_t *s, balancerinfo_t *balancer)
{
    ngx_int_t rv;
    balancerinfo_t *ou, *ou_tmp=balancer;
    int ident;
    
    balancer->id = 0;
    s->storage->ap_slotmem_lock(s->slotmem);
    rv = s->storage->ap_slotmem_do(s->slotmem, insert_update, &balancer, 1, s->p);
    if (balancer->id != 0 && rv == NGX_OK) {
        ou_tmp->id = balancer->id;
        s->storage->ap_slotmem_unlock(s->slotmem);
        return NGX_OK; /* updated */
    }

    /* we have to insert it */
    rv = s->storage->ap_slotmem_alloc(s->slotmem, &ident, (void **) &ou);
    if (rv != NGX_OK) {
        s->storage->ap_slotmem_unlock(s->slotmem);
        return rv;
    }
    memcpy(ou, balancer, sizeof(balancerinfo_t));
    ou->id = ident;
    s->storage->ap_slotmem_unlock(s->slotmem);
    ou->updatetime = time(NULL);

    /*
     * Fill the incoming value 
     */
    balancer->id = ident;
    
    return NGX_OK;
}

/**
 * read a balancer record from the shared table
 * @param pointer to the shared table.
 * @param balancer balancer to read from the shared table.
 * @return address of the read balancer or NULL if error.
 */
static ngx_int_t loc_read_balancer(void* mem, void **data, int id, ngx_pool_t *pool) {
    balancerinfo_t *in = (balancerinfo_t *)*data;
    balancerinfo_t *ou = (balancerinfo_t *)mem;

    if (ngx_strcmp(in->balancer, ou->balancer) == 0) {
        *data = ou;
        return NGX_OK;
    }
    return NGX_NONE;
}
balancerinfo_t * read_balancer(mem_t *s, balancerinfo_t *balancer)
{
    ngx_int_t rv;
    balancerinfo_t *ou = balancer;

    if (balancer->id)
        rv = s->storage->ap_slotmem_mem(s->slotmem, balancer->id, (void **) &ou);
    else {
        rv = s->storage->ap_slotmem_do(s->slotmem, loc_read_balancer, &ou, 0, s->p);
    }
    if (rv == NGX_OK)
        return ou;
    return NULL;
}
/**
 * get a balancer record from the shared table
 * @param pointer to the shared table.
 * @param balancer address where the balancer is locate in the shared table.
 * @param ids  in the balancer table.
 * @return NGX_OK if all went well
 */
ngx_int_t get_balancer(mem_t *s, balancerinfo_t **balancer, int ids)
{
  return(s->storage->ap_slotmem_mem(s->slotmem, ids, (void **) balancer));
}

/**
 * remove(free) a balancer record from the shared table
 * @param pointer to the shared table.
 * @param balancer balancer to remove from the shared table.
 * @return NGX_OK if all went well
 */
ngx_int_t remove_balancer(mem_t *s, balancerinfo_t *balancer)
{
    ngx_int_t rv;
    balancerinfo_t *ou = balancer;
    if (balancer->id)
        rv = s->storage->ap_slotmem_free(s->slotmem, balancer->id, balancer);
    else {
        /* XXX: for the moment January 2007 ap_slotmem_free only uses ident to remove */
        rv = s->storage->ap_slotmem_do(s->slotmem, loc_read_balancer, &ou, 0, s->p);
        if (rv == NGX_OK)
            rv = s->storage->ap_slotmem_free(s->slotmem, ou->id, balancer);
    }
    return rv;
}

/*
 * get the ids for the used (not free) balancers in the table
 * @param pointer to the shared table.
 * @param ids array of int to store the used id (must be big enough).
 * @return number of balancer existing or -1 if error.
 */
int get_ids_used_balancer(mem_t *s, int *ids)
{
    return (s->storage->ap_slotmem_get_used(s->slotmem, ids));
}

/*
 * read the size of the table.
 * @param pointer to the shared table.
 * @return number of balancer existing or -1 if error.
 */
int get_max_size_balancer(mem_t *s)
{
    return (s->storage->ap_slotmem_get_max_size(s->slotmem));
}

/**
 * attach to the shared balancer table
 * @param name of an existing shared table.
 * @param address to store the size of the shared table.
 * @param p pool to use for allocations.
 * @param storage slotmem logic provider.
 * @return address of struct used to access the table.
 */
mem_t * get_mem_balancer(u_char *string, int *num, ngx_pool_t *p, slotmem_storage_method *storage) {
    return(create_attach_mem_balancer(string, num, 0, p, storage));
}
/**
 * create a shared balancer table
 * @param name to use to create the table.
 * @param size of the shared table.
 * @param persist tell if the slotmem element are persistent.
 * @param p pool to use for allocations.
 * @param storage slotmem logic provider.
 * @return address of struct used to access the table.
 */
mem_t * create_mem_balancer(u_char *string, int *num, int persist, ngx_pool_t *p,  slotmem_storage_method *storage) {
    return(create_attach_mem_balancer(string, num, CREATE_SLOTMEM|persist, p, storage));
}
