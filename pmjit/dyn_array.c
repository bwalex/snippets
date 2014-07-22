#include <string.h>
#include <stdlib.h>
#include "dyn_array.h"

void
dyn_array_init(struct dyn_array *a, size_t elem_size, size_t incr, void *priv, dyn_array_ctor ctor, dyn_array_dtor dtor)
{
	a->elem_size = elem_size;
	a->incr = incr;
	a->priv = priv;
	a->ctor = ctor;
	a->dtor = dtor;

	a->elem_cnt = a->elem_max = 0;
	a->elems = NULL;
	a->elems_end = NULL;
}

void *
dyn_array_new_elem2(struct dyn_array *a, int *p_idx)
{
	void *elem;

	if (a->elem_cnt == a->elem_max) {
		a->elem_max += a->incr;
		a->elems = realloc(a->elems, a->elem_size * a->elem_max);
		if (a->elems == NULL)
			return NULL;
	}

	elem = (void *)((char *)a->elems + (a->elem_cnt * a->elem_size));

	memset(elem, 0, a->elem_size);
	if (a->ctor)
		a->ctor(a->priv, elem);

	if (p_idx != NULL)
		*p_idx = (int)a->elem_cnt;

	++a->elem_cnt;

	a->elems_end = (void *)((char *)a->elems + (a->elem_cnt * a->elem_size));

	return elem;
}

void
dyn_array_free_all(struct dyn_array *a)
{
	void *elem;
	char *elems = a->elems;
	size_t i;

	for (i = 0; i < a->elem_cnt; i++) {
		elem = (void *)elems;
		elems += a->elem_size;

		if (a->dtor)
			a->dtor(a->priv, elem);
	}

	free(a->elems);
	dyn_array_init(a, a->elem_size, a->incr, a->priv, a->ctor, a->dtor);
}
