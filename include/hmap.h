#ifndef __HMAP_H__
#define __HMAP_H__

#include <stdbool.h>
#include <stdlib.h>
#include "util.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct hmap_node {
    size_t hash;                /* Hash value. */
    struct hmap_node *next;     /* Next in linked list. */
};

static inline size_t hmap_node_hash(const struct hmap_node *node)
{
    return node->hash;
}

struct hmap {
    struct hmap_node **buckets;
    struct hmap_node *one;
    size_t mask;
    size_t n;
};

void hmap_init(struct hmap *);
void hmap_destroy(struct hmap *);
void hmap_swap(struct hmap *a, struct hmap *b);
void hmap_moved(struct hmap *hmap);
static inline size_t hmap_count(const struct hmap *);
static inline bool hmap_is_empty(const struct hmap *);

void hmap_expand(struct hmap *);

static inline void hmap_insert(struct hmap *,
                               struct hmap_node *, size_t hash);
static inline void hmap_remove(struct hmap *, struct hmap_node *);
static inline void hmap_replace(struct hmap *, const struct hmap_node *old,
                                struct hmap_node *new_node);
static inline void hmap_insert_fast(struct hmap *,
                                    struct hmap_node *, size_t hash);

#define HMAP_FOR_EACH_WITH_HASH(NODE, MEMBER, HASH, HMAP)		   \
    for (ASSIGN_CONTAINER(NODE, hmap_first_with_hash(HMAP, HASH), MEMBER); \
         NODE != OBJECT_CONTAINING(NULL, NODE, MEMBER);                    \
         ASSIGN_CONTAINER(NODE, hmap_next_with_hash(&(NODE)->MEMBER),      \
                          MEMBER))

static inline struct hmap_node *hmap_first_with_hash(const struct hmap *,
                                                     size_t hash);
static inline struct hmap_node *hmap_next_with_hash(const struct hmap_node *);

#define HMAP_FOR_EACH(NODE, MEMBER, HMAP)                                  \
    for (ASSIGN_CONTAINER(NODE, hmap_first(HMAP), MEMBER);                 \
         NODE != OBJECT_CONTAINING(NULL, NODE, MEMBER);                    \
         ASSIGN_CONTAINER(NODE, hmap_next(HMAP, &(NODE)->MEMBER), MEMBER))

#define HMAP_FOR_EACH_SAFE(NODE, NEXT, MEMBER, HMAP)                       \
    for (ASSIGN_CONTAINER(NODE, hmap_first(HMAP), MEMBER);                 \
         (NODE != OBJECT_CONTAINING(NULL, NODE, MEMBER)                    \
          ? ASSIGN_CONTAINER(NEXT, hmap_next(HMAP, &(NODE)->MEMBER), MEMBER), 1 \
          : 0);                                                            \
         (NODE) = (NEXT))

static inline struct hmap_node *hmap_first(const struct hmap *);
static inline struct hmap_node *hmap_next(const struct hmap *,
                                          const struct hmap_node *);

/* Returns the number of nodes currently in 'hmap'. */
static inline size_t
hmap_count(const struct hmap *hmap)
{
    return hmap->n;
}

/* Returns true if 'hmap' currently contains no nodes,
 * false otherwise. */
static inline bool
hmap_is_empty(const struct hmap *hmap)
{
    return hmap->n == 0;
}

static inline void
hmap_insert_fast(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    struct hmap_node **bucket = &hmap->buckets[hash & hmap->mask];
    node->hash = hash;
    node->next = *bucket;
    *bucket = node;
    hmap->n++;
}

static inline void
hmap_insert(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    hmap_insert_fast(hmap, node, hash);
    if (hmap->n / 2 > hmap->mask) {
        hmap_expand(hmap);
    }
}

static inline void
hmap_remove(struct hmap *hmap, struct hmap_node *node)
{
    struct hmap_node **bucket = &hmap->buckets[node->hash & hmap->mask];
    while (*bucket != node) {
        bucket = &(*bucket)->next;
    }
    *bucket = node->next;
    hmap->n--;
}

static inline void
hmap_replace(struct hmap *hmap,
             const struct hmap_node *old_node, struct hmap_node *new_node)
{
    struct hmap_node **bucket = &hmap->buckets[old_node->hash & hmap->mask];
    while (*bucket != old_node) {
        bucket = &(*bucket)->next;
    }
    *bucket = new_node;
    new_node->hash = old_node->hash;
    new_node->next = old_node->next;
}

static inline struct hmap_node *
hmap_next_with_hash__(const struct hmap_node *node, size_t hash)
{
    while (node != NULL && node->hash != hash) {
        node = node->next;
    }
    return node;
}

/* Returns the first node in 'hmap' with the given 'hash', or a null pointer if
 * no nodes have that hash value. */
static inline struct hmap_node *
hmap_first_with_hash(const struct hmap *hmap, size_t hash)
{
    return hmap_next_with_hash__(hmap->buckets[hash & hmap->mask], hash);
}

static inline struct hmap_node *
hmap_next_with_hash(const struct hmap_node *node)
{
    return hmap_next_with_hash__(node->next, node->hash);
}

static inline struct hmap_node *
hmap_next__(const struct hmap *hmap, size_t start)
{
    size_t i;
    for (i = start; i <= hmap->mask; i++) {
        struct hmap_node *node = hmap->buckets[i];
        if (node) {
            return node;
        }
    }
    return NULL;
}

/* Returns the first node in 'hmap', in arbitrary order, or a null pointer if
 * 'hmap' is empty. */
static inline struct hmap_node *
hmap_first(const struct hmap *hmap)
{
    return hmap_next__(hmap, 0);
}

static inline struct hmap_node *
hmap_next(const struct hmap *hmap, const struct hmap_node *node)
{
    return (node->next
            ? node->next
            : hmap_next__(hmap, (node->hash & hmap->mask) + 1));
}

#ifdef  __cplusplus
}
#endif

#endif /*__HMAP_H__ */
