#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** A node in a priority queue / stack: a value and the next node. */
typedef struct _PQNode
{
  void *a_value;
  struct _PQNode *next;
} PQNode;

/**
 * @brief Insert `a_value` into the queue at `a_head`, ordered by `cmp_fn`.
 *
 * A NULL `cmp_fn` inserts at the front.
 *
 * @return the newly allocated node
 */
PQNode *pq_enqueue(PQNode **a_head, void *a_value, int (*cmp_fn)(const void *, const void *));

/**
 * @brief Detach and return the head of the queue at `a_head`, or NULL if empty.
 *
 * The caller owns the returned node and must free it.
 */
PQNode *pq_dequeue(PQNode **a_head);

/** @brief Push `a_value` onto the stack at `stack`. */
PQNode *stack_push(PQNode **stack, void *a_value);

/** @brief Pop and return the top node of the stack at `stack`, or NULL. */
PQNode *stack_pop(PQNode **stack);

/**
 * @brief Free every node in the list at `a_head`, along with its value.
 */
void destroy_list(PQNode **a_head);

#endif // PRIORITY_QUEUE_H
