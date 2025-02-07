#include <assert.h>
#include "mgpriv.h"
#include "khash.h"
#include "kthread.h"
#include "kvec.h"

#define idx_hash(a) ((a)>>1)
#define idx_eq(a, b) ((a)>>1 == (b)>>1)
KHASH_INIT(idx, uint64_t, uint64_t, 1, idx_hash, idx_eq)
typedef khash_t(idx) idxhash_t;

typedef struct mg_idx_bucket_s {
	mg128_v a;   // (minimizer, position) array
	int32_t n;   // size of the _p_ array
	uint64_t *p; // position array for minimizers appearing >1 times
	void *h;     // hash table indexing _p_ and minimizers appearing once
} mg_idx_bucket_t;

mg_idx_t *mg_idx_init(int k, int w, int b)
{
	mg_idx_t *gi;
	if (k*2 < b) b = k * 2;
	if (w < 1) w = 1;
	KCALLOC(0, gi, 1);
	gi->w = w, gi->k = k, gi->b = b;
	KCALLOC(0, gi->B, 1<<b);
	return gi;
}

void mg_idx_destroy(mg_idx_t *gi)
{
	uint32_t i;
	if (gi == 0) return;
	if (gi->B) {
		for (i = 0; i < 1U<<gi->b; ++i) {
			free(gi->B[i].p);
			free(gi->B[i].a.a);
			kh_destroy(idx, (idxhash_t*)gi->B[i].h);
		}
		free(gi->B);
	}
	free(gi);
}

/****************
 * Index access *
 ****************/

const uint64_t *mg_idx_hget(const void *h_, const uint64_t *q, int suflen, uint64_t minier, int *n)
{
	khint_t k;
	const idxhash_t *h = (const idxhash_t*)h_;
	*n = 0;
	if (h == 0) return 0;
	k = kh_get(idx, h, minier>>suflen<<1);
	if (k == kh_end(h)) return 0;
	if (kh_key(h, k)&1) { // special casing when there is only one k-mer
		*n = 1;
		return &kh_val(h, k);
	} else {
		*n = (uint32_t)kh_val(h, k);
		return &q[kh_val(h, k)>>32];
	}
}

const uint64_t *mg_idx_get(const mg_idx_t *gi, uint64_t minier, int *n)
{
	int mask = (1<<gi->b) - 1;
	mg_idx_bucket_t *b = &gi->B[minier&mask];
	return mg_idx_hget(b->h, b->p, gi->b, minier, n);
}

int32_t mg_idx_cal_max_occ(const mg_idx_t *gi, float f)
{
	int i;
	size_t n = 0;
	uint32_t thres;
	khint_t *a, k;
	if (f <= 0.) return INT32_MAX;
	for (i = 0; i < 1<<gi->b; ++i)
		if (gi->B[i].h) n += kh_size((idxhash_t*)gi->B[i].h);
	a = (uint32_t*)malloc(n * 4);
	for (i = n = 0; i < 1<<gi->b; ++i) {
		idxhash_t *h = (idxhash_t*)gi->B[i].h;
		if (h == 0) continue;
		for (k = 0; k < kh_end(h); ++k) {
			if (!kh_exist(h, k)) continue;
			a[n++] = kh_key(h, k)&1? 1 : (uint32_t)kh_val(h, k);
		}
	}
	thres = ks_ksmall_uint32_t(n, a, (uint32_t)((1. - f) * n)) + 1;
	free(a);
	return thres;
}

/***************
 * Index build *
 ***************/

static void mg_idx_add(mg_idx_t *gi, int n, const mg128_t *a)
{
	int i, mask = (1<<gi->b) - 1;
	for (i = 0; i < n; ++i) {
		mg128_v *p = &gi->B[a[i].x>>8&mask].a;
		kv_push(mg128_t, 0, *p, a[i]);
	}
}

void *mg_idx_a2h(void *km, int32_t n_a, mg128_t *a, int suflen, uint64_t **q_, int32_t *n_)
{
	int32_t N, n, n_keys;
	int32_t j, start_a, start_q;
	idxhash_t *h;
	uint64_t *q;

	*q_ = 0, *n_ = 0;
	if (n_a == 0) return 0;

	// sort by minimizer
	radix_sort_128x(a, a + n_a);

	// count and preallocate
	for (j = 1, n = 1, n_keys = 0, N = 0; j <= n_a; ++j) {
		if (j == n_a || a[j].x>>8 != a[j-1].x>>8) {
			++n_keys;
			if (n > 1) N += n;
			n = 1;
		} else ++n;
	}
	h = kh_init2(idx, km);
	kh_resize(idx, h, n_keys);
	KCALLOC(km, q, N);
	*q_ = q, *n_ = N;

	// create the hash table
	for (j = 1, n = 1, start_a = start_q = 0; j <= n_a; ++j) {
		if (j == n_a || a[j].x>>8 != a[j-1].x>>8) {
			khint_t itr;
			int absent;
			mg128_t *p = &a[j-1];
			itr = kh_put(idx, h, p->x>>8>>suflen<<1, &absent);
			assert(absent && j == start_a + n);
			if (n == 1) {
				kh_key(h, itr) |= 1;
				kh_val(h, itr) = p->y;
			} else {
				int k;
				for (k = 0; k < n; ++k)
					q[start_q + k] = a[start_a + k].y;
				radix_sort_64(&q[start_q], &q[start_q + n]); // sort by position; needed as in-place radix_sort_128x() is not stable
				kh_val(h, itr) = (uint64_t)start_q<<32 | n;
				start_q += n;
			}
			start_a = j, n = 1;
		} else ++n;
	}
	assert(N == start_q);
	return h;
}

static void worker_post(void *g, long i, int tid)
{
	mg_idx_t *gi = (mg_idx_t*)g;
	mg_idx_bucket_t *b = &gi->B[i];
	if (b->a.n == 0) return;
	b->h = (idxhash_t*)mg_idx_a2h(0, b->a.n, b->a.a, gi->b, &b->p, &b->n);
	kfree(0, b->a.a);
	b->a.n = b->a.m = 0, b->a.a = 0;
}

int mg_gfa_overlap(const gfa_t *g)
{
	int64_t i;
	for (i = 0; i < g->n_arc; ++i) // non-zero overlap
		if (g->arc[i].ov != 0 || g->arc[i].ow != 0)
			return 1;
	return 0;
}

mg_idx_t *mg_index(gfa_t *g, int k, int w, int b, int n_threads)
{
	mg_idx_t *gi;
	mg128_v a = {0,0,0};
	int i;

	if (mg_gfa_overlap(g)) {
		if (mg_verbose >= 1)
			fprintf(stderr, "[E::%s] minigraph doesn't work with graphs containing overlapping segments\n", __func__);
		return 0;
	}
	gi = mg_idx_init(k, w, b);
	gi->g = g;

	for (i = 0; i < g->n_seg; ++i) {
		gfa_seg_t *s = &g->seg[i];
		a.n = 0;
		mg_sketch(0, s->seq, s->len, w, k, i, &a); // TODO: this can be parallelized
		mg_idx_add(gi, a.n, a.a);
	}
	free(a.a);
	kt_for(n_threads, worker_post, gi, 1<<gi->b);
	return gi;
}
