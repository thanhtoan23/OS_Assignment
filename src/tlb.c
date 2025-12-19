/*
 * Translation Lookaside Buffer (TLB) Implementation
 * Using LRU replacement policy
 */

#include "common.h"
#include "os-mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static pthread_mutex_t tlb_lock = PTHREAD_MUTEX_INITIALIZER;

/* Hash function for TLB - simple modulo */
static int tlb_hash(addr_t vpn, uint32_t pid) {
    return ((vpn ^ pid) % TLB_SIZE);
}

/* Initialize TLB */
struct tlb_t* tlb_init() {
    struct tlb_t* tlb = malloc(sizeof(struct tlb_t));
    if (!tlb) return NULL;
    
    memset(tlb->entries, 0, sizeof(tlb->entries));
    tlb->hits = 0;
    tlb->misses = 0;
    tlb->size = TLB_SIZE;
    tlb->access_counter = 0;
    
    return tlb;
}

/* Find TLB entry by VPN and PID */
static struct tlb_entry_t* tlb_find_entry(struct tlb_t* tlb, addr_t vpn, uint32_t pid) {
    int index = tlb_hash(vpn, pid);
    struct tlb_entry_t* entry = tlb->entries[index];
    
    while (entry != NULL) {
        if (entry->valid && entry->vpn == vpn && entry->pid == pid) {
            return entry;
        }
        entry = entry->next;
    }
    
    return NULL;
}

/* Find LRU victim in a hash chain */
static struct tlb_entry_t** tlb_find_lru_victim(struct tlb_t* tlb, int index) {
    struct tlb_entry_t** victim = &tlb->entries[index];
    struct tlb_entry_t* current = tlb->entries[index];
    uint64_t oldest_time = tlb->access_counter + 1; /* Initialize to future time */
    
    while (current != NULL) {
        if (!current->valid) {
            /* Found invalid entry, use it immediately */
            victim = &tlb->entries[index];
            struct tlb_entry_t* curr = tlb->entries[index];
            while (curr != NULL) {
                if (curr == current) break;
                victim = &(*victim)->next;
                curr = curr->next;
            }
            return victim;
        }
        
        if (current->last_used < oldest_time) {
            oldest_time = current->last_used;
            victim = &tlb->entries[index];
            struct tlb_entry_t* curr = tlb->entries[index];
            while (curr != NULL) {
                if (curr == current) break;
                victim = &(*victim)->next;
                curr = curr->next;
            }
        }
        current = current->next;
    }
    
    return victim;
}

/* Insert/Update TLB entry */
int tlb_insert(struct tlb_t* tlb, addr_t vpn, int fpn, uint32_t pid, 
               uint8_t dirty, uint8_t referenced) {
    pthread_mutex_lock(&tlb_lock);
    
    int index = tlb_hash(vpn, pid);
    struct tlb_entry_t* entry = tlb_find_entry(tlb, vpn, pid);
    
    if (entry != NULL) {
        /* Update existing entry */
        entry->fpn = fpn;
        entry->dirty = dirty;
        entry->referenced = referenced;
        entry->last_used = ++tlb->access_counter;
    } else {
        /* Find LRU victim */
        struct tlb_entry_t** victim_ptr = tlb_find_lru_victim(tlb, index);
        struct tlb_entry_t* victim = *victim_ptr;
        
        if (victim == NULL) {
            /* No entry in this chain, create new */
            victim = malloc(sizeof(struct tlb_entry_t));
            if (!victim) {
                pthread_mutex_unlock(&tlb_lock);
                return -1;
            }
            *victim_ptr = victim;
            victim->next = NULL;
        } else if (!victim->valid) {
            /* Reuse invalid entry */
        } else {
            /* Replace LRU victim */
            printf("TLB LRU replacement: VPN %lu (PID %d) -> VPN %lu (PID %d)\n",
                  victim->vpn, victim->pid, vpn, pid);
        }
        
        /* Initialize new entry */
        victim->vpn = vpn;
        victim->fpn = fpn;
        victim->pid = pid;
        victim->valid = TLB_ENTRY_VALID;
        victim->dirty = dirty;
        victim->referenced = referenced;
        victim->last_used = ++tlb->access_counter;
    }
    
    pthread_mutex_unlock(&tlb_lock);
    return 0;
}

/* Lookup TLB - returns 1 if hit, 0 if miss */
int tlb_lookup(struct tlb_t* tlb, addr_t vpn, uint32_t pid, int* fpn) {
    pthread_mutex_lock(&tlb_lock);
    
    struct tlb_entry_t* entry = tlb_find_entry(tlb, vpn, pid);
    
    if (entry != NULL && entry->valid) {
        /* TLB hit */
        *fpn = entry->fpn;
        entry->last_used = ++tlb->access_counter; /* Update LRU timestamp */
        tlb->hits++;
        
        pthread_mutex_unlock(&tlb_lock);
        return 1; /* Hit */
    }
    
    /* TLB miss */
    tlb->misses++;
    pthread_mutex_unlock(&tlb_lock);
    return 0; /* Miss */
}

/* Invalidate TLB entry (on page swap out, free, etc.) */
int tlb_invalidate_entry(struct tlb_t* tlb, addr_t vpn, uint32_t pid) {
    pthread_mutex_lock(&tlb_lock);
    
    int index = tlb_hash(vpn, pid);
    struct tlb_entry_t* entry = tlb->entries[index];
    
    while (entry != NULL) {
        if (entry->valid && entry->vpn == vpn && entry->pid == pid) {
            entry->valid = TLB_ENTRY_INVALID;
            pthread_mutex_unlock(&tlb_lock);
            return 0;
        }
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&tlb_lock);
    return -1; /* Entry not found */
}

/* Invalidate all TLB entries for a process (on context switch) */
int tlb_invalidate_process(struct tlb_t* tlb, uint32_t pid) {
    pthread_mutex_lock(&tlb_lock);
    
    for (int i = 0; i < TLB_SIZE; i++) {
        struct tlb_entry_t* entry = tlb->entries[i];
        while (entry != NULL) {
            if (entry->valid && entry->pid == pid) {
                entry->valid = TLB_ENTRY_INVALID;
            }
            entry = entry->next;
        }
    }
    
    pthread_mutex_unlock(&tlb_lock);
    return 0;
}

/* Set dirty bit in TLB */
int tlb_set_dirty(struct tlb_t* tlb, addr_t vpn, uint32_t pid) {
    pthread_mutex_lock(&tlb_lock);
    
    struct tlb_entry_t* entry = tlb_find_entry(tlb, vpn, pid);
    if (entry != NULL && entry->valid) {
        entry->dirty = 1;
    }
    
    pthread_mutex_unlock(&tlb_lock);
    return (entry != NULL) ? 0 : -1;
}

/* Set referenced bit in TLB */
int tlb_set_referenced(struct tlb_t* tlb, addr_t vpn, uint32_t pid) {
    pthread_mutex_lock(&tlb_lock);
    
    struct tlb_entry_t* entry = tlb_find_entry(tlb, vpn, pid);
    if (entry != NULL && entry->valid) {
        entry->referenced = 1;
    }
    
    pthread_mutex_unlock(&tlb_lock);
    return (entry != NULL) ? 0 : -1;
}

/* Get TLB statistics */
void tlb_get_stats(struct tlb_t* tlb, int* hits, int* misses, float* hit_rate) {
    pthread_mutex_lock(&tlb_lock);
    
    *hits = tlb->hits;
    *misses = tlb->misses;
    
    int total = tlb->hits + tlb->misses;
    if (total > 0) {
        *hit_rate = (float)tlb->hits / total * 100.0;
    } else {
        *hit_rate = 0.0;
    }
    
    pthread_mutex_unlock(&tlb_lock);
}

/* Print TLB contents for debugging */
void tlb_dump(struct tlb_t* tlb) {
    pthread_mutex_lock(&tlb_lock);
    
    printf("===== TLB DUMP =====\n");
    printf("Size: %d entries\n", TLB_SIZE);
    printf("Hits: %d, Misses: %d\n", tlb->hits, tlb->misses);
    
    if (tlb->hits + tlb->misses > 0) {
        printf("Hit Rate: %.2f%%\n", 
               (float)tlb->hits / (tlb->hits + tlb->misses) * 100.0);
    }
    
    printf("Entries:\n");
    int valid_count = 0;
    for (int i = 0; i < TLB_SIZE; i++) {
        struct tlb_entry_t* entry = tlb->entries[i];
        while (entry != NULL) {
            if (entry->valid) {
                printf("  [%d] VPN: %lu -> FPN: %u (PID: %d, Age: %lu)\n",
                       i, entry->vpn, entry->fpn, entry->pid, 
                       tlb->access_counter - entry->last_used);
                valid_count++;
            }
            entry = entry->next;
        }
    }
    printf("Valid entries: %d\n", valid_count);
    printf("====================\n");
    
    pthread_mutex_unlock(&tlb_lock);
}

/* Free TLB memory */
void tlb_free(struct tlb_t* tlb) {
    if (!tlb) return;
    
    pthread_mutex_lock(&tlb_lock);
    
    for (int i = 0; i < TLB_SIZE; i++) {
        struct tlb_entry_t* entry = tlb->entries[i];
        while (entry != NULL) {
            struct tlb_entry_t* next = entry->next;
            free(entry);
            entry = next;
        }
        tlb->entries[i] = NULL;
    }
    
    pthread_mutex_unlock(&tlb_lock);
    free(tlb);
}