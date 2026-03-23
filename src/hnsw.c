/*
 * hnsw.c - Hierarchical Navigable Small World index for Racecar
 *
 * Full implementation of the HNSW algorithm (Malkov & Yashunin, 2016).
 * Correctness first, speed second.
 */
#include "racecar.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <time.h>

/* ================================================================
 * Internal types
 * ================================================================ */

/* Single node in the HNSW graph */
typedef struct {
    uint64_t   id;               /* record ID */
    float     *vector;           /* copy of vector data, dim floats */
    int        level;            /* max level this node exists on */
    uint32_t  *neighbor_counts;  /* [level+1] count of neighbors per level */
    uint64_t **neighbors;        /* [level+1][max_neighbors] neighbor IDs */
} hnsw_node_t;

/* Min-heap / max-heap candidate */
typedef struct {
    uint64_t id;                 /* node index (NOT record ID) */
    float    dist;
} hnsw_candidate_t;

/* Priority queue (binary heap) */
typedef struct {
    hnsw_candidate_t *data;
    uint32_t          size;
    uint32_t          cap;
} hnsw_pq_t;

/* The HNSW index (definition of the opaque struct from racecar.h) */
struct rc_hnsw {
    uint32_t    dim;
    rc_metric_t metric;
    uint32_t    m;               /* max neighbors per layer (M) */
    uint32_t    m_max0;          /* max neighbors at layer 0 = 2*M */
    uint32_t    ef_construction; /* ef during construction */
    int         max_level;       /* current max level in graph */
    int64_t     entry_point;     /* node index of entry point (-1 = empty) */
    double      level_mult;      /* 1.0 / ln(M) */

    hnsw_node_t *nodes;
    uint64_t     node_count;
    uint64_t     node_cap;
};

/* ================================================================
 * Priority queue (min-heap)
 * ================================================================ */

static void
pq_init(hnsw_pq_t *pq, uint32_t cap)
{
    pq->cap  = cap < 16 ? 16 : cap;
    pq->size = 0;
    pq->data = (hnsw_candidate_t *)malloc(sizeof(hnsw_candidate_t) * pq->cap);
}

static void
pq_free(hnsw_pq_t *pq)
{
    free(pq->data);
    pq->data = NULL;
    pq->size = pq->cap = 0;
}

static void
pq_grow(hnsw_pq_t *pq)
{
    pq->cap *= 2;
    pq->data = (hnsw_candidate_t *)realloc(pq->data,
                    sizeof(hnsw_candidate_t) * pq->cap);
}

/* --- min-heap push ------------------------------------------------ */
static void
pq_push_min(hnsw_pq_t *pq, uint64_t id, float dist)
{
    if (pq->size == pq->cap) pq_grow(pq);
    uint32_t i = pq->size++;
    pq->data[i].id   = id;
    pq->data[i].dist = dist;
    /* bubble up */
    while (i > 0) {
        uint32_t p = (i - 1) / 2;
        if (pq->data[p].dist <= pq->data[i].dist) break;
        hnsw_candidate_t tmp = pq->data[p];
        pq->data[p] = pq->data[i];
        pq->data[i] = tmp;
        i = p;
    }
}

/* pop smallest element */
static hnsw_candidate_t
pq_pop_min(hnsw_pq_t *pq)
{
    hnsw_candidate_t top = pq->data[0];
    pq->data[0] = pq->data[--pq->size];
    /* bubble down */
    uint32_t i = 0;
    for (;;) {
        uint32_t l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < pq->size && pq->data[l].dist < pq->data[smallest].dist)
            smallest = l;
        if (r < pq->size && pq->data[r].dist < pq->data[smallest].dist)
            smallest = r;
        if (smallest == i) break;
        hnsw_candidate_t tmp = pq->data[i];
        pq->data[i] = pq->data[smallest];
        pq->data[smallest] = tmp;
        i = smallest;
    }
    return top;
}

/* --- max-heap push ------------------------------------------------ */
static void
pq_push_max(hnsw_pq_t *pq, uint64_t id, float dist)
{
    if (pq->size == pq->cap) pq_grow(pq);
    uint32_t i = pq->size++;
    pq->data[i].id   = id;
    pq->data[i].dist = dist;
    while (i > 0) {
        uint32_t p = (i - 1) / 2;
        if (pq->data[p].dist >= pq->data[i].dist) break;
        hnsw_candidate_t tmp = pq->data[p];
        pq->data[p] = pq->data[i];
        pq->data[i] = tmp;
        i = p;
    }
}

/* pop largest element */
static hnsw_candidate_t
pq_pop_max(hnsw_pq_t *pq)
{
    hnsw_candidate_t top = pq->data[0];
    pq->data[0] = pq->data[--pq->size];
    uint32_t i = 0;
    for (;;) {
        uint32_t l = 2 * i + 1, r = 2 * i + 2, largest = i;
        if (l < pq->size && pq->data[l].dist > pq->data[largest].dist)
            largest = l;
        if (r < pq->size && pq->data[r].dist > pq->data[largest].dist)
            largest = r;
        if (largest == i) break;
        hnsw_candidate_t tmp = pq->data[i];
        pq->data[i] = pq->data[largest];
        pq->data[largest] = tmp;
        i = largest;
    }
    return top;
}

static hnsw_candidate_t
pq_top_max(const hnsw_pq_t *pq)
{
    return pq->data[0];
}

/* ================================================================
 * Helpers
 * ================================================================ */

/* Generate a random level with exponential distribution */
static int
random_level(rc_hnsw_t *idx)
{
    double r = (double)rand() / (double)RAND_MAX;
    if (r == 0.0) r = 1e-9;             /* avoid log(0) */
    int level = (int)(-log(r) * idx->level_mult);
    if (level > RC_HNSW_MAX_LEVEL - 1)
        level = RC_HNSW_MAX_LEVEL - 1;
    return level;
}

/* Look up the internal node index for a record ID (linear scan).
 * Returns -1 if not found.                                         */
static int64_t
find_node_index(const rc_hnsw_t *idx, uint64_t id)
{
    for (uint64_t i = 0; i < idx->node_count; i++) {
        if (idx->nodes[i].id == id)
            return (int64_t)i;
    }
    return -1;
}

/* Compute distance between a raw query vector and a stored node */
static inline float
node_dist(const rc_hnsw_t *idx, const float *query, uint64_t node_idx)
{
    return rc_vec_distance(query, idx->nodes[node_idx].vector,
                           idx->dim, idx->metric);
}

/* ================================================================
 * search_layer  --  core HNSW layer search
 *
 * Returns results as a dynamic array of candidates sorted closest-first.
 * Caller must free the returned array.
 * *out_count receives the number of results.
 * ================================================================ */
static hnsw_candidate_t *
search_layer(rc_hnsw_t *idx, const float *query,
             const uint64_t *entry_indices, uint32_t n_entries,
             uint32_t ef, int level,
             uint32_t *out_count)
{
    /* visited bitmap (indexed by node index) */
    uint64_t bm_words = (idx->node_count + 63) / 64;
    uint64_t *visited = (uint64_t *)calloc(bm_words, sizeof(uint64_t));
    if (!visited) { *out_count = 0; return NULL; }

    hnsw_pq_t candidates;  /* min-heap: closest first */
    hnsw_pq_t results;     /* max-heap: furthest first (so we can pop worst) */
    pq_init(&candidates, ef + 16);
    pq_init(&results,    ef + 16);

    for (uint32_t i = 0; i < n_entries; i++) {
        uint64_t ep = entry_indices[i];
        if (ep >= idx->node_count) continue;
        visited[ep / 64] |= (1ULL << (ep % 64));
        float d = node_dist(idx, query, ep);
        pq_push_min(&candidates, ep, d);
        pq_push_max(&results,    ep, d);
    }

    while (candidates.size > 0) {
        hnsw_candidate_t c = pq_pop_min(&candidates);
        /* furthest in results */
        hnsw_candidate_t f = pq_top_max(&results);
        if (c.dist > f.dist) break;

        /* expand neighbors of c at this level */
        hnsw_node_t *cn = &idx->nodes[c.id];
        if (level > cn->level) continue;       /* safety */
        uint32_t n_nbrs = cn->neighbor_counts[level];
        for (uint32_t j = 0; j < n_nbrs; j++) {
            uint64_t nbr_id = cn->neighbors[level][j];
            /* nbr_id is a record-ID; translate to node index */
            int64_t ni = find_node_index(idx, nbr_id);
            if (ni < 0) continue;
            uint64_t nidx = (uint64_t)ni;
            /* check visited */
            if (visited[nidx / 64] & (1ULL << (nidx % 64))) continue;
            visited[nidx / 64] |= (1ULL << (nidx % 64));

            float d = node_dist(idx, query, nidx);
            f = pq_top_max(&results);
            if (d < f.dist || results.size < ef) {
                pq_push_min(&candidates, nidx, d);
                pq_push_max(&results,    nidx, d);
                if (results.size > ef)
                    pq_pop_max(&results);
            }
        }
    }

    /* extract results sorted closest-first */
    uint32_t n = results.size;
    hnsw_candidate_t *out = (hnsw_candidate_t *)malloc(
                                sizeof(hnsw_candidate_t) * (n ? n : 1));
    /* drain max-heap into array, then reverse */
    for (uint32_t i = 0; i < n; i++)
        out[n - 1 - i] = pq_pop_max(&results);

    /* insertion sort (small n, almost sorted) */
    for (uint32_t i = 1; i < n; i++) {
        hnsw_candidate_t key = out[i];
        int64_t j = (int64_t)i - 1;
        while (j >= 0 && out[j].dist > key.dist) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    *out_count = n;
    pq_free(&candidates);
    pq_free(&results);
    free(visited);
    return out;
}

/* ================================================================
 * greedy_closest  --  walk greedily to the closest node at a level
 *
 * Returns the node INDEX of the closest node found.
 * ================================================================ */
static uint64_t
greedy_closest(rc_hnsw_t *idx, const float *query,
               uint64_t start_idx, int level)
{
    uint64_t curr = start_idx;
    float    best = node_dist(idx, query, curr);

    for (;;) {
        uint64_t next = curr;
        float    next_dist = best;
        hnsw_node_t *cn = &idx->nodes[curr];
        if (level > cn->level) break;

        uint32_t n_nbrs = cn->neighbor_counts[level];
        for (uint32_t j = 0; j < n_nbrs; j++) {
            int64_t ni = find_node_index(idx, cn->neighbors[level][j]);
            if (ni < 0) continue;
            float d = node_dist(idx, query, (uint64_t)ni);
            if (d < next_dist) {
                next = (uint64_t)ni;
                next_dist = d;
            }
        }
        if (next == curr) break;         /* no improvement */
        curr = next;
        best = next_dist;
    }
    return curr;
}

/* ================================================================
 * Neighbor management helpers
 * ================================================================ */

/* Add a neighbor ID to a node at a given level.  If already present, no-op. */
static void
add_neighbor(hnsw_node_t *node, int level, uint64_t nbr_record_id,
             uint32_t max_neighbors)
{
    /* already present? */
    for (uint32_t i = 0; i < node->neighbor_counts[level]; i++) {
        if (node->neighbors[level][i] == nbr_record_id) return;
    }
    if (node->neighbor_counts[level] < max_neighbors) {
        node->neighbors[level][node->neighbor_counts[level]++] = nbr_record_id;
    }
}

/* Shrink a node's neighbor list at a level to keep only the closest
 * max_neighbors neighbors (measured from the node's own vector).      */
static void
shrink_neighbors(rc_hnsw_t *idx, hnsw_node_t *node, int level,
                 uint32_t max_neighbors)
{
    uint32_t cnt = node->neighbor_counts[level];
    if (cnt <= max_neighbors) return;

    /* compute distances from node to each neighbor */
    typedef struct { uint64_t id; float dist; } nd_t;
    nd_t *arr = (nd_t *)malloc(sizeof(nd_t) * cnt);
    for (uint32_t i = 0; i < cnt; i++) {
        arr[i].id = node->neighbors[level][i];
        int64_t ni = find_node_index(idx, arr[i].id);
        arr[i].dist = (ni >= 0)
            ? rc_vec_distance(node->vector, idx->nodes[ni].vector,
                              idx->dim, idx->metric)
            : FLT_MAX;
    }

    /* selection: partial sort to find max_neighbors closest */
    for (uint32_t i = 0; i < max_neighbors; i++) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < cnt; j++) {
            if (arr[j].dist < arr[best].dist) best = j;
        }
        if (best != i) {
            nd_t tmp = arr[i]; arr[i] = arr[best]; arr[best] = tmp;
        }
    }

    for (uint32_t i = 0; i < max_neighbors; i++)
        node->neighbors[level][i] = arr[i].id;
    node->neighbor_counts[level] = max_neighbors;
    free(arr);
}

/* ================================================================
 * rc_hnsw_create
 * ================================================================ */
rc_hnsw_t *
rc_hnsw_create(uint32_t dim, rc_metric_t metric,
               uint32_t m, uint32_t ef_construction)
{
    rc_hnsw_t *idx = (rc_hnsw_t *)calloc(1, sizeof(rc_hnsw_t));
    if (!idx) return NULL;

    idx->dim             = dim;
    idx->metric          = metric;
    idx->m               = m;
    idx->m_max0          = 2 * m;
    idx->ef_construction = ef_construction;
    idx->max_level       = 0;
    idx->entry_point     = -1;
    idx->level_mult      = 1.0 / log((double)(m > 1 ? m : 2));

    idx->node_cap   = 1024;
    idx->node_count = 0;
    idx->nodes      = (hnsw_node_t *)calloc(idx->node_cap, sizeof(hnsw_node_t));
    if (!idx->nodes) { free(idx); return NULL; }

    srand((unsigned)time(NULL));
    return idx;
}

/* ================================================================
 * rc_hnsw_free
 * ================================================================ */
void
rc_hnsw_free(rc_hnsw_t *idx)
{
    if (!idx) return;
    for (uint64_t i = 0; i < idx->node_count; i++) {
        hnsw_node_t *n = &idx->nodes[i];
        free(n->vector);
        if (n->neighbors) {
            for (int l = 0; l <= n->level; l++)
                free(n->neighbors[l]);
            free(n->neighbors);
        }
        free(n->neighbor_counts);
    }
    free(idx->nodes);
    free(idx);
}

/* ================================================================
 * rc_hnsw_insert
 * ================================================================ */
rc_status_t
rc_hnsw_insert(rc_hnsw_t *idx, uint64_t id, const float *vec)
{
    if (!idx || !vec) return RC_ERR_INVALID;

    /* Grow node array if needed */
    if (idx->node_count == idx->node_cap) {
        uint64_t new_cap = idx->node_cap * RC_GROW_FACTOR;
        hnsw_node_t *tmp = (hnsw_node_t *)realloc(idx->nodes,
                                sizeof(hnsw_node_t) * new_cap);
        if (!tmp) return RC_ERR_MEM;
        memset(tmp + idx->node_cap, 0,
               sizeof(hnsw_node_t) * (new_cap - idx->node_cap));
        idx->nodes    = tmp;
        idx->node_cap = new_cap;
    }

    int new_level = random_level(idx);
    uint64_t new_idx = idx->node_count;

    /* initialise node */
    hnsw_node_t *node = &idx->nodes[new_idx];
    node->id    = id;
    node->level = new_level;

    node->vector = (float *)malloc(sizeof(float) * idx->dim);
    if (!node->vector) return RC_ERR_MEM;
    memcpy(node->vector, vec, sizeof(float) * idx->dim);

    node->neighbor_counts = (uint32_t *)calloc(new_level + 1, sizeof(uint32_t));
    node->neighbors       = (uint64_t **)calloc(new_level + 1, sizeof(uint64_t *));
    if (!node->neighbor_counts || !node->neighbors) return RC_ERR_MEM;

    for (int l = 0; l <= new_level; l++) {
        uint32_t max_nbrs = (l == 0) ? idx->m_max0 : idx->m;
        node->neighbors[l] = (uint64_t *)calloc(max_nbrs, sizeof(uint64_t));
        if (!node->neighbors[l]) return RC_ERR_MEM;
    }

    idx->node_count++;

    /* ---------- first node in the graph ---------- */
    if (idx->entry_point < 0) {
        idx->entry_point = (int64_t)new_idx;
        idx->max_level   = new_level;
        return RC_OK;
    }

    /* ---------- Phase 1: greedy descent from top to new_level+1 ---------- */
    uint64_t curr = (uint64_t)idx->entry_point;
    for (int level = idx->max_level; level > new_level; level--) {
        curr = greedy_closest(idx, vec, curr, level);
    }

    /* ---------- Phase 2: search & connect at each layer ---------- */
    int start_level = new_level < idx->max_level ? new_level : idx->max_level;
    uint64_t ep_arr[1];
    ep_arr[0] = curr;
    uint64_t *ep_list   = ep_arr;
    uint32_t  ep_count  = 1;
    uint64_t *dyn_ep    = NULL;     /* dynamically allocated ep list */

    for (int level = start_level; level >= 0; level--) {
        uint32_t w_count = 0;
        hnsw_candidate_t *W = search_layer(idx, vec, ep_list, ep_count,
                                           idx->ef_construction, level,
                                           &w_count);

        uint32_t m_use = (level == 0) ? idx->m_max0 : idx->m;
        uint32_t n_sel = w_count < m_use ? w_count : m_use;

        /* W is sorted closest-first; take the first n_sel as neighbors */
        for (uint32_t i = 0; i < n_sel; i++) {
            /* W[i].id is a node index; get the record id */
            uint64_t nbr_node_idx = W[i].id;
            uint64_t nbr_rec_id   = idx->nodes[nbr_node_idx].id;

            /* set bidirectional connections */
            add_neighbor(node, level, nbr_rec_id, m_use);
            add_neighbor(&idx->nodes[nbr_node_idx], level, id, m_use);

            /* shrink neighbor if over capacity */
            if (idx->nodes[nbr_node_idx].neighbor_counts[level] > m_use)
                shrink_neighbors(idx, &idx->nodes[nbr_node_idx], level, m_use);
        }

        /* use W as entry points for next lower level */
        free(dyn_ep);
        dyn_ep = (uint64_t *)malloc(sizeof(uint64_t) * w_count);
        if (dyn_ep) {
            for (uint32_t i = 0; i < w_count; i++)
                dyn_ep[i] = W[i].id;
            ep_list  = dyn_ep;
            ep_count = w_count;
        }
        free(W);
    }
    free(dyn_ep);

    /* ---------- update entry point if this node is the new tallest ---------- */
    if (new_level > idx->max_level) {
        idx->entry_point = (int64_t)new_idx;
        idx->max_level   = new_level;
    }

    return RC_OK;
}

/* ================================================================
 * rc_hnsw_search
 * ================================================================ */
rc_status_t
rc_hnsw_search(rc_hnsw_t *idx, const float *query,
               uint32_t top_k, uint32_t ef_search,
               rc_result_t *results, uint32_t *result_count)
{
    if (!idx || !query || !results || !result_count)
        return RC_ERR_INVALID;

    if (idx->entry_point < 0 || idx->node_count == 0) {
        *result_count = 0;
        return RC_OK;
    }

    uint32_t ef = ef_search > top_k ? ef_search : top_k;

    /* greedy descent from top level to level 1 */
    uint64_t curr = (uint64_t)idx->entry_point;
    for (int level = idx->max_level; level >= 1; level--) {
        curr = greedy_closest(idx, query, curr, level);
    }

    /* search at level 0 */
    uint64_t ep_arr[1] = { curr };
    uint32_t w_count = 0;
    hnsw_candidate_t *W = search_layer(idx, query, ep_arr, 1, ef, 0, &w_count);

    /* copy top_k closest into results */
    uint32_t n_out = w_count < top_k ? w_count : top_k;
    for (uint32_t i = 0; i < n_out; i++) {
        results[i].id       = idx->nodes[W[i].id].id;  /* record id */
        results[i].distance = W[i].dist;
    }
    *result_count = n_out;
    free(W);

    return RC_OK;
}

/* ================================================================
 * rc_hnsw_save  --  serialise to file
 *
 * Format:
 *   magic(4) dim(4) metric(4) m(4) ef_construction(4) max_level(4)
 *   node_count(8) entry_point_id(8)           = 40 bytes header
 *
 *   per node:
 *     id(8) level(4) vector(dim*4)
 *     for each level 0..level:
 *       neighbor_count(4) neighbor_ids(count*8)
 * ================================================================ */
rc_status_t
rc_hnsw_save(rc_hnsw_t *idx, const char *path)
{
    if (!idx || !path) return RC_ERR_INVALID;

    FILE *fp = fopen(path, "wb");
    if (!fp) return RC_ERR_IO;

    /* header */
    uint32_t magic = RC_MAGIC_IDX;
    fwrite(&magic,              4, 1, fp);
    fwrite(&idx->dim,           4, 1, fp);
    fwrite(&idx->metric,        4, 1, fp);
    fwrite(&idx->m,             4, 1, fp);
    fwrite(&idx->ef_construction, 4, 1, fp);
    fwrite(&idx->max_level,     4, 1, fp);
    fwrite(&idx->node_count,    8, 1, fp);

    /* entry_point stored as record ID (or ~0 if empty) */
    uint64_t ep_id = (idx->entry_point >= 0)
                     ? idx->nodes[idx->entry_point].id
                     : UINT64_MAX;
    fwrite(&ep_id, 8, 1, fp);

    /* nodes */
    for (uint64_t i = 0; i < idx->node_count; i++) {
        hnsw_node_t *n = &idx->nodes[i];
        fwrite(&n->id,    8, 1, fp);
        int32_t lvl = n->level;
        fwrite(&lvl,      4, 1, fp);
        fwrite(n->vector, sizeof(float), idx->dim, fp);
        for (int l = 0; l <= n->level; l++) {
            fwrite(&n->neighbor_counts[l], 4, 1, fp);
            fwrite(n->neighbors[l], 8, n->neighbor_counts[l], fp);
        }
    }

    fclose(fp);
    return RC_OK;
}

/* ================================================================
 * rc_hnsw_load  --  deserialise from file
 * ================================================================ */
rc_hnsw_t *
rc_hnsw_load(const char *path)
{
    if (!path) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    uint32_t magic;
    if (fread(&magic, 4, 1, fp) != 1 || magic != RC_MAGIC_IDX) {
        fclose(fp);
        return NULL;
    }

    rc_hnsw_t *idx = (rc_hnsw_t *)calloc(1, sizeof(rc_hnsw_t));
    if (!idx) { fclose(fp); return NULL; }

    fread(&idx->dim,              4, 1, fp);
    fread(&idx->metric,           4, 1, fp);
    fread(&idx->m,                4, 1, fp);
    fread(&idx->ef_construction,  4, 1, fp);
    fread(&idx->max_level,        4, 1, fp);
    fread(&idx->node_count,       8, 1, fp);

    uint64_t ep_id;
    fread(&ep_id, 8, 1, fp);

    idx->m_max0    = 2 * idx->m;
    idx->level_mult = 1.0 / log((double)(idx->m > 1 ? idx->m : 2));
    idx->node_cap  = idx->node_count > 0 ? idx->node_count : 1;
    idx->nodes     = (hnsw_node_t *)calloc(idx->node_cap, sizeof(hnsw_node_t));
    if (!idx->nodes) { free(idx); fclose(fp); return NULL; }

    idx->entry_point = -1;

    for (uint64_t i = 0; i < idx->node_count; i++) {
        hnsw_node_t *n = &idx->nodes[i];
        fread(&n->id, 8, 1, fp);
        int32_t lvl;
        fread(&lvl, 4, 1, fp);
        n->level = lvl;

        n->vector = (float *)malloc(sizeof(float) * idx->dim);
        fread(n->vector, sizeof(float), idx->dim, fp);

        n->neighbor_counts = (uint32_t *)calloc(n->level + 1, sizeof(uint32_t));
        n->neighbors       = (uint64_t **)calloc(n->level + 1, sizeof(uint64_t *));

        for (int l = 0; l <= n->level; l++) {
            fread(&n->neighbor_counts[l], 4, 1, fp);
            uint32_t max_nbrs = (l == 0) ? idx->m_max0 : idx->m;
            /* allocate at least max_nbrs to allow future insertions */
            uint32_t alloc = n->neighbor_counts[l] > max_nbrs
                             ? n->neighbor_counts[l] : max_nbrs;
            n->neighbors[l] = (uint64_t *)calloc(alloc, sizeof(uint64_t));
            fread(n->neighbors[l], 8, n->neighbor_counts[l], fp);
        }

        /* resolve entry point */
        if (n->id == ep_id)
            idx->entry_point = (int64_t)i;
    }

    fclose(fp);
    return idx;
}

/* ================================================================
 * rc_hnsw_build_from_table  --  build index from an open table
 * ================================================================ */
rc_status_t
rc_hnsw_build_from_table(rc_table_t *t,
                         uint32_t m, uint32_t ef_construction,
                         const char *index_path)
{
    if (!t || !index_path) return RC_ERR_INVALID;

    uint32_t    dim    = t->header.dimensions;
    rc_metric_t metric = (rc_metric_t)t->header.metric;

    rc_hnsw_t *idx = rc_hnsw_create(dim, metric, m, ef_construction);
    if (!idx) return RC_ERR_MEM;

    /* iterate all records; skip deleted ones */
    uint64_t cap = t->header.capacity;
    for (uint64_t i = 0; i < cap; i++) {
        uint8_t *ptr = t->records + (size_t)i * t->record_size;
        uint64_t rec_id = *(uint64_t *)(ptr);
        uint32_t flags  = *(uint32_t *)(ptr + 8);
        if (!(flags & RC_RECORD_ACTIVE)) continue;

        const float *vec = (const float *)(ptr + 16);
        rc_status_t s = rc_hnsw_insert(idx, rec_id, vec);
        if (s != RC_OK) {
            rc_hnsw_free(idx);
            return s;
        }
    }

    rc_status_t s = rc_hnsw_save(idx, index_path);
    rc_hnsw_free(idx);
    return s;
}
