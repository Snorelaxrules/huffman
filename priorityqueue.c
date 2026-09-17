#include "priority_queue.h"

#include <stdlib.h>

PQNode *pq_enqueue(PQNode **a_head, void *a_value, int (*cmp_fn)(const void *, const void *))
{
    PQNode *new_node = malloc(sizeof(PQNode));
    new_node->a_value = a_value;
    new_node->next = NULL;

    // A NULL cmp_fn means "push at the front", which is what stack_push wants.
    if (*a_head == NULL || cmp_fn == NULL || cmp_fn(a_value, (*a_head)->a_value) < 0) {
        new_node->next = *a_head;
        *a_head = new_node;
    }
    else {
        PQNode *current = *a_head;
        while (current->next != NULL && cmp_fn(a_value, current->next->a_value) >= 0) {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }
    return new_node;
}

PQNode *pq_dequeue(PQNode **a_head) {
    PQNode *removed = *a_head;
    if (removed != NULL) {
        *a_head = removed->next;
        removed->next = NULL;
    }
    return removed;
}

PQNode *stack_push(PQNode **stack, void *a_value) {
    return pq_enqueue(stack, a_value, NULL);
}

PQNode *stack_pop(PQNode **stack) {
    return pq_dequeue(stack);
}

void destroy_list(PQNode **a_head) {
    while (*a_head != NULL) {
        PQNode *node = pq_dequeue(a_head);
        free(node->a_value);
        free(node);
    }
}
