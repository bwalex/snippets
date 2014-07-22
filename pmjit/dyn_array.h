#ifndef _DYN_ARRAY_H
#define _DYN_ARRAY_H

typedef void (*dyn_array_ctor)(void *, void *);
typedef void (*dyn_array_dtor)(void *, void *);

struct dyn_array {
	size_t		elem_size;
	size_t		incr;
	void		*priv;
	dyn_array_ctor	ctor;
	dyn_array_dtor	dtor;

	size_t		elem_cnt;
	size_t		elem_max;

	void		*elems;
	void		*elems_end;
};

void dyn_array_init(struct dyn_array *a, size_t elem_size, size_t incr,
    void *priv, dyn_array_ctor ctor, dyn_array_dtor dtor);
void dyn_array_free_all(struct dyn_array *a);

static
inline
int dyn_array_is_empty(struct dyn_array *a)
{
	return (a->elem_cnt != 0);
}

static
inline
size_t dyn_array_size(struct dyn_array *a)
{
	return a->elem_cnt;
}

static
inline
void *dyn_array_get(struct dyn_array *a, size_t idx)
{
	if (idx >= a->elem_cnt)
		return NULL;

	return (void *)((char *)a->elems + (a->elem_size * idx));
}

static
inline
size_t dyn_array_get_index(struct dyn_array *a, void *elem)
{
	return (((char *)elem - (char *)a->elems)/(a->elem_size));
}

void *dyn_array_new_elem2(struct dyn_array *a, int *p_idx);

#define dyn_array_new_elem(a) \
    dyn_array_new_elem2(a, NULL)


#define dyn_array_foreach(a, elem_ptr)			\
		for (elem_ptr = (a)->elems;		\
		     elem_ptr != (a)->elems_end;	\
		     elem_ptr++)

#endif
