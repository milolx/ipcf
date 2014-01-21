#ifndef __LIST_H__
#define __LIST_H__

#include <stdbool.h>
#include <stddef.h>
#include "util.h"

struct list {
    struct list *prev;     /* Previous list element. */
    struct list *next;     /* Next list element. */
};

#define LIST_INITIALIZER(LIST) { LIST, LIST }

void list_init(struct list *);

/* List insertion. */
void list_insert(struct list *, struct list *);
void list_push_front(struct list *, struct list *);
void list_push_back(struct list *, struct list *);

/* List removal. */
struct list *list_remove(struct list *);
struct list *list_pop_front(struct list *);
struct list *list_pop_back(struct list *);

/* List elements. */
struct list *list_front(const struct list *);
struct list *list_back(const struct list *);

/* List properties. */
size_t list_size(const struct list *);
bool list_is_empty(const struct list *);

#define LIST_FOR_EACH(ITER, MEMBER, LIST)                               \
    for (ASSIGN_CONTAINER(ITER, (LIST)->next, MEMBER);                  \
         &(ITER)->MEMBER != (LIST);                                     \
         ASSIGN_CONTAINER(ITER, (ITER)->MEMBER.next, MEMBER))
#define LIST_FOR_EACH_SAFE(ITER, NEXT, MEMBER, LIST)               \
    for (ASSIGN_CONTAINER(ITER, (LIST)->next, MEMBER);             \
         (&(ITER)->MEMBER != (LIST)                                \
          ? ASSIGN_CONTAINER(NEXT, (ITER)->MEMBER.next, MEMBER), 1 \
          : 0);                                                    \
         (ITER) = (NEXT))

#endif /* __LIST_H__ */
