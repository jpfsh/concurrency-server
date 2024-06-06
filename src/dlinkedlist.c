#include "dlinkedlist.h"
#include <stdio.h>
/*
    What is a linked list?
    A linked list is a set of dynamically allocated nodes, arranged in
    such a way that each node contains one value and one pointer.
    The pointer always points to the next member of the list.
    If the pointer is NULL, then it is the last node in the list.

    A linked list is held using a local pointer variable which
    points to the first item of the list. If that pointer is also NULL,
    then the list is considered to be empty.
    -------------------------------               ------------------------------              ------------------------------
    |HEAD                         |             \ |              |             |            \ |              |             |
    |                             |-------------- |     DATA     |     NEXT    |--------------|     DATA     |     NEXT    |
    |-----------------------------|             / |              |             |            / |              |             |
    |LENGTH                       |               ------------------------------              ------------------------------
    |COMPARATOR                   |
    |PRINTER                      |
    |DELETER                      |
    -------------------------------                                         
*/

dlist_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*, void*),
                   void (*delete)(void*)) {
    dlist_t* list = malloc(sizeof(dlist_t));
    list->comparator = compare;
    list->printer = print;
    list->deleter = delete;
    list->length = 0;
    list->head = NULL;
    return list;
}

void InsertAtHead(dlist_t* list, void* val_ref) {
    if(list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) list->head = NULL;

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));

    new_node->data = val_ref;
    new_node->next = *head;
    new_node->prev = NULL;

    // moves list head to the new node
    if(*head != NULL)
        (*head)->prev = new_node;
    *head = new_node;
    list->length++;
}

void InsertAtTail(dlist_t* list, void* val_ref) {
    if (list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) {
        InsertAtHead(list, val_ref);
        return;
    }

    node_t* head = list->head;
    node_t* current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = malloc(sizeof(node_t));
    current->next->data = val_ref;
    current->next->next = NULL;
    current->next->prev = current;
    list->length++;
}


void InsertInOrder(dlist_t* list, void* val_ref) {
    if(list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) {
        InsertAtHead(list, val_ref);
        return;
    }

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));
    new_node->data = val_ref;
    new_node->next = NULL;
    new_node->prev = NULL;

    if (list->comparator(new_node->data, (*head)->data) < 0) {
        new_node->next = *head;
        (*head)->prev = new_node;
        *head = new_node;
    } else if ((*head)->next == NULL) {
        (*head)->next = new_node;
        new_node->prev = *head;
    } else {
        node_t* current = (*head)->next;

        while (current != NULL) {
            if (list->comparator(new_node->data, current->data) > 0) {
                if (current->next != NULL) {
                    current = current->next;
                } else {
                    current->next = new_node;
                    new_node->prev = current;
                    break;
                }
            } else {
                current->prev->next = new_node;
                new_node->prev = current->prev;
                current->prev = new_node;
                new_node->next = current;
                break;
            }
        }
    }
    list->length++;
}

void* RemoveFromHead(dlist_t* list) {
    if(list == NULL || list->length == 0)
        return NULL;

    node_t* curhead = list->head;
    void* retval = NULL;
    node_t* next_node = NULL;

    next_node = curhead->next;
    retval = curhead->data;
    list->length--;

    list->head = next_node;
    if(list->head != NULL)
        list->head->prev = NULL;
    
    free(curhead);
    return retval;
}

void* RemoveFromTail(dlist_t* list) {
    if(list == NULL || list->length == 0) {
        return NULL; 
    } else if (list->length == 1) {
        return RemoveFromHead(list);
    }

    void* retval = NULL;
    node_t* head = list->head;
    node_t* current = head;

    while (current->next->next != NULL) { 
        current = current->next;
    }

    retval = current->next->data;
    free(current->next);
    current->next = NULL;

    list->length--;

    return retval;
}

/* indexed by 0 */
void* RemoveByIndex(dlist_t* list, int index) {
    if(list == NULL || list->length == 0 || index < 0 || list->length <= index) {
        return NULL;
    }

    if (index == 0) {
        return RemoveFromHead(list);
    }

    void* retval = NULL;
    node_t* current = list->head;
    int i = 0;

    while (i++ != index) {
        current = current->next;
    }

    if(current->next != NULL)
        current->next->prev = current->prev;
        
    current->prev->next = current->next;
    
    retval = current->data;
    free(current);

    list->length--;
    return retval;
}

void DeleteList(dlist_t* list) {
    if (list == NULL || list->length == 0)
        return;
    while (list->head != NULL){
        void* retval = RemoveFromHead(list);
        list->deleter(retval);
    }
    list->length = 0;
}

 void SortList(dlist_t* list) {
    if (list == NULL)
        return;

    dlist_t* new_list = malloc(sizeof(dlist_t));	
    
	new_list->length = 0;
    new_list->comparator = list->comparator;
    new_list->head = NULL;

    int i = 0;
    int len = list->length;
    for (; i < len; ++i)
    {
        void* val = RemoveFromTail(list);
        InsertInOrder(new_list, val); 
    }

    node_t* temp = list->head;
    list->head = new_list->head;

    new_list->head = temp;
    list->length = new_list->length;  

    
    free(new_list);  
}

void PrintLinkedList(dlist_t* list, FILE* fp) {
    if(list == NULL)
        return;

    node_t* head = list->head;
    while (head != NULL) {
        list->printer(head->data, fp);
        fprintf(fp, "\n");
        head = head->next;
    }
}

