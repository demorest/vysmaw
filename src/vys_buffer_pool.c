//
// Copyright © 2016 Associated Universities, Inc. Washington DC, USA.
//
// This file is part of vysmaw.
//
// vysmaw is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// vysmaw is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// vysmaw.  If not, see <http://www.gnu.org/licenses/>.
//
#include <buffer_pool.h>
#include <glib.h>

struct buffer_pool *
buffer_pool_new(size_t num_buffers, size_t buffer_size)
{
	g_assert(buffer_size >= sizeof(struct buffer_stack));
	struct buffer_pool *result = g_new(struct buffer_pool, 1);
	result->buffer_size = buffer_size;
	result->pool = g_malloc_n(num_buffers, buffer_size);
	result->pool_size = num_buffers * buffer_size;
	result->root = (struct buffer_stack *)result->pool;
	struct buffer_stack *buff = result->root;
	struct buffer_stack *next_buff = (void *)buff + buffer_size;
	for (size_t i = num_buffers; i > 1; --i) {
		buff->next = next_buff;
		buff = next_buff;
		next_buff = (void *)buff + buffer_size;
	}
	buff->next = NULL;
#if BUFFER_POOL_LOCK
	g_mutex_init(&result->lock);
#endif
	return result;
}

void
buffer_pool_free(struct buffer_pool *buffer_pool)
{
	g_free(buffer_pool->pool);
#if BUFFER_POOL_LOCK
	g_mutex_clear(&buffer_pool->lock);
#endif
	g_free(buffer_pool);
}
