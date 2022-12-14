/*
   COP 3502C Assignment 2
   This program is written by: Charlton Trezevant
 */

#include <stdio.h>
#include  <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "leak_detector_c.h"

/*
   ~ ~ FYI ~ ~

   The leak detector continues to cause errors and leak memory. Please refer to
   my WebCourses submission for documentation of this problem, as well as
   recommended remediary steps.
 */

////////////////////////// Global Project Configuation //////////////////////////

#define CONFIG_INFILE_NAME "file_in.txt"
#define CONFIG_OUTFILE_NAME "out.txt"
#define CONFIG_NUM_LANES 12
#define CONFIG_CHECKOUT_TIME_BASE 30
#define CONFIG_CHECKOUT_TIME_PER_ITEM 5

// Maximum values and limits as specified in the project spec
#define CONFIG_MAX_TEST_CASES 25
#define CONFIG_MAX_CUSTOMERS 500000
#define CONFIG_MAX_CUST_ITEMS 100
#define CONFIG_MAX_NAME_LEN 10
#define CONFIG_MAX_TIME 1000000000

// Global debug levels
#define DEBUG_LEVEL_ALL 0
#define DEBUG_LEVEL_TRACE 1
#define DEBUG_LEVEL_INFO 2

// Functionality-specific debug levels
// These enable debug output only for specific sections of the program
#define DEBUG_TRACE_CUSTOMER -1
#define DEBUG_TRACE_LANE -2
#define DEBUG_TRACE_CHECKOUT -3
#define DEBUG_TRACE_MMGR -7

// Current debug level set/disable
//#define DEBUG DEBUG_LEVEL_INFO

////////////////////////// Assignment 2 Prototypes //////////////////////////

typedef struct Customer {
        char *name;
        int num_items;
        int line_number;
        int time_enter;
} Customer;

Customer* customer_create(char *name, int num_items, int line_number, int time_enter);
void customer_destroy(Customer *c);

typedef struct Node {
        Customer *cust;
        struct Node *next;
} Node;

Node* node_create(Customer *c, Node *next);
void node_destroy(Node *n);

// Stores our queue.
typedef struct Lane {
        Node *front;
        Node *back;
} Lane;

Lane* lane_create();
void lane_destroy(Lane *l);
void lane_enqueue(Lane *l, Customer *c);
Customer* lane_dequeue(Lane *l);
Customer* lane_peek(Lane *l);
int lane_empty(Lane *l);

Lane* store_lanes[CONFIG_NUM_LANES];
void store_lanes_create();
void store_lanes_destroy();
void store_lanes_enqueue(int laneIdx, Customer* c);
Customer* store_lanes_dequeue(int laneIdx);
Customer* store_lanes_peek(int laneIdx);
int store_lanes_empty(int laneIdx);
int store_lanes_all_empty();

int checkout_get_next_lane();
Customer* checkout_get_next_cust();
int checkout_time(Customer *c);

////////////////////////// Debug Output //////////////////////////
// (c) Charlton Trezevant - 2018

#ifdef DEBUG
#define debugf(lvl, fmt, ...) \
        ({ \
                if (DEBUG == 0 || (lvl > 0 && lvl >= DEBUG) || (lvl < 0 && lvl == DEBUG)) { \
                        fprintf(stderr, fmt, ## __VA_ARGS__); fflush(stderr); \
                } \
        })
#else
  #define debugf(lvl, fmt, ...) ((void)0)
#endif

////////////////////////// Memory Manager Prototypes //////////////////////////
// (c) Charlton Trezevant - 2020

typedef struct MMGR_Entry {
        void* handle;
        size_t size;
        // todo: gc via refcounts or mark+sweep
        // allow entries to be marked/given affinities for batched release
} MMGR_Entry;

typedef struct MMGR {
        // todo: benchmark current realloc strat vs lili vs pointer hash table
        MMGR_Entry **entries;
        int numEntries;
        int *free; // available recyclable slots
        int numFree;
        volatile int mutex;
} MMGR;

MMGR *mmgr_init();
void *mmgr_malloc(MMGR *tbl, size_t size);
void mmgr_free(MMGR *tbl, void* handle);
void mmgr_cleanup(MMGR *tbl);
void mmgr_mutex_acquire(MMGR *tbl);
void mmgr_mutex_release(MMGR *tbl);

// Global MMGR instance
MMGR* g_MEM;

////////////////////////// Utility //////////////////////////
int stdout_enabled = 0;
FILE *g_outfp; // output file pointer

void panic(const char * format, ...);
void write_out(const char * format, ...);

////////////////////////// Entry //////////////////////////

int main(int argc, char **argv){
        debugf(DEBUG_LEVEL_TRACE, "Ahmed memory leak detector init.\n");
        atexit(report_mem_leak); //Ahmed's memory leak detection

        // Initialize MMGR
        debugf(DEBUG_LEVEL_TRACE, "MMGR init.\n");
        g_MEM = mmgr_init();

        // You can override the default input filename if you'd like by
        // passing a command line argument
        // This is for my own use in an automated testing harness and will not impact any
        // tests performed by TAs
        FILE *infile;
        if(argc > 1 && strcmp(argv[1], "use_default") != 0) {
                infile = fopen(argv[1], "r");
                debugf(DEBUG_LEVEL_INFO, "Input file name: %s\n", argv[1]);
        } else {
                infile = fopen(CONFIG_INFILE_NAME, "r");
                debugf(DEBUG_LEVEL_INFO, "Input file name: %s\n", CONFIG_INFILE_NAME);
        }

        // Panic if opening the input file failed
        if (infile == NULL) {
                panic("Failed to open input file\n");
                return 1;
        }

        // You can override the default output file name if you'd like to send output to either
        // a different file or to stdout
        // This is for my own use in an automated testing harness and will not impact any
        // tests performed by TAs
        if(argc > 2) {
                if(strcmp(argv[2], "stdout") == 0) {
                        g_outfp = NULL;
                        stdout_enabled = 1;
                        debugf(DEBUG_LEVEL_INFO, "Writing output to stdout\n");
                } else {
                        g_outfp = fopen(argv[2], "w+");
                        debugf(DEBUG_LEVEL_INFO, "Output file name1: %s\n", argv[2]);
                }
        } else {
                g_outfp = fopen(CONFIG_OUTFILE_NAME, "w+");
                debugf(DEBUG_LEVEL_INFO, "Output file name2: %s\n", CONFIG_OUTFILE_NAME);
        }

        // Attempt to read the number of test cases from the input file, panic
        // if EOF is encountered
        int num_cases = -1;
        if(!feof(infile)) {
                fscanf(infile, "%d", &num_cases);
                debugf(DEBUG_LEVEL_INFO, "Infile contains %d cases\n", num_cases);
        } else {
                panic("invalid input file format: number of test cases unknown\n");
        }

        // Sanity check enforcement per project spec
        if(num_cases > CONFIG_MAX_TEST_CASES)
                panic("%d customers greater than maximum of %d\n", num_cases, CONFIG_MAX_TEST_CASES);

        // Run test cases from input file, panic if EOF is encountered
        for(int case_n = 0; case_n < num_cases; case_n++) {
                // Initialize all store lanes (queues 0 - CONFIG_NUM_LANES)
                store_lanes_create();
                int case_num_customers = 0;
                int first_cust_lane = -1;

                if(!feof(infile)) {
                        // Fetch number of courses in current case
                        fscanf(infile, "%d", &case_num_customers);

                        // Sanity check enforcement per project spec
                        if(case_num_customers > CONFIG_MAX_CUSTOMERS)
                                panic("%d customers greater than maximum of %d\n", case_num_customers, CONFIG_MAX_CUSTOMERS);

                        debugf(DEBUG_LEVEL_INFO, "Infile contains %d customers for test case %d\n", case_num_customers, case_n);

                        // Tmp vars populated for each customer as they're read from the file
                        int time_enter_tmp, line_num_tmp, num_items_tmp;
                        char cust_name_tmp[CONFIG_MAX_NAME_LEN];

                        for(int cust_n = 0; cust_n < case_num_customers; cust_n++) {
                                time_enter_tmp = 0;
                                line_num_tmp = 0;
                                num_items_tmp = 0;

                                debugf(DEBUG_LEVEL_INFO, "Scanning customer line\n");
                                fscanf(infile, "%d %d %s %d", &time_enter_tmp, &line_num_tmp, cust_name_tmp, &num_items_tmp);
                                debugf(DEBUG_LEVEL_INFO, "Enqueueing customer %s in lane %d\n", cust_name_tmp, (line_num_tmp - 1));

                                // Keeping tabs on which customer was first in line is important once checkout starts
                                if(first_cust_lane == -1)
                                        first_cust_lane = (line_num_tmp - 1);

                                // Create a new customer instance and enqueue it into the appropriate lane (queue)
                                store_lanes_enqueue((line_num_tmp - 1), customer_create(cust_name_tmp, num_items_tmp, line_num_tmp, time_enter_tmp));
                        }

                        debugf(DEBUG_LEVEL_INFO, "Finished populating customer queues\n");

                        debugf(DEBUG_LEVEL_INFO, "Starting checkout\n");

                        // Dequeue the first customer
                        Customer *cust = store_lanes_dequeue(first_cust_lane);
                        int checkout_time_total = 0;

                        for(int cust_n = 0; cust_n < case_num_customers; cust_n++) {
                                // Resets the running total of time spent checking customers out if the most recently dequeued
                                // customer entered the store after the tally
                                // This had me scratching my head for a bit, nice one
                                if(cust->time_enter > checkout_time_total)
                                        checkout_time_total = cust->time_enter + checkout_time(cust);
                                else
                                        checkout_time_total += checkout_time(cust);

                                // This is a wrapper around fprintf/stdout that allows me to enable or disable stdout for testing
                                // Come to think of it, I could have just set g_outfp = stdout and that probably would have been better
                                // Can't win them all
                                write_out("%s from line %d checks out at time %d.\r\n", cust->name, cust->line_number, checkout_time_total);

                                // Once customers have been checked out we delete the instance and free the memory
                                customer_destroy(cust);

                                // Then the next customer is dequeued
                                cust = checkout_get_next_cust();
                        }

                } else {
                        panic("reached EOF while attempting to run test case %d", case_n);
                }

                // Burn the store down, lots of fire
                // jk we're just deallocating the queues we've made
                store_lanes_destroy();
        }

        debugf(DEBUG_LEVEL_TRACE, "MMGR Cleanup.\n");
        mmgr_cleanup(g_MEM); // #noleaks

        debugf(DEBUG_LEVEL_TRACE, "Infile close.\n");
        if(infile != NULL)
                fclose(infile);

        debugf(DEBUG_LEVEL_TRACE, "Outfile close.\n");
        if(g_outfp != NULL)
                fclose(g_outfp);

        debugf(DEBUG_LEVEL_TRACE, "Exiting.\n");
        return 0;
}

////////////////////////// Customers, Lanes, and Checkout //////////////////////////

// I hope you like helpers as much as I do

// Create a customer, enforcing a number of sanity checks in the process
Customer* customer_create(char *name, int num_items, int line_number, int time_enter){
        int malformed = 0;
        if(num_items > CONFIG_MAX_CUST_ITEMS) {
                debugf(DEBUG_TRACE_CUSTOMER, "customer_create line number %d > maximum of %d\n", num_items, CONFIG_MAX_CUST_ITEMS);
                malformed = 1;
        }

        if(time_enter > CONFIG_MAX_TIME) {
                debugf(DEBUG_TRACE_CUSTOMER, "customer_create time enter %d > maximum of %d\n", time_enter, CONFIG_MAX_TIME);
                malformed = 1;
        }

        if(line_number > CONFIG_NUM_LANES) {
                debugf(DEBUG_TRACE_CUSTOMER, "customer_create line number %d > maximum of %d\n", line_number, CONFIG_NUM_LANES);
                malformed = 1;
        }

        // Don't make any ~illegal~ customer instances!
        if(malformed == 1) {
                debugf(DEBUG_TRACE_CUSTOMER, "customer_create Refusing to instantiate malformed customer.\n");
                return NULL;
        }

        Customer *cust = mmgr_malloc(g_MEM, sizeof(Customer));
        debugf(DEBUG_TRACE_CUSTOMER, "customer_create allocated customer struct\n");

        cust->name = mmgr_malloc(g_MEM, sizeof(char*) * CONFIG_MAX_NAME_LEN);
        strcpy(cust->name, name);

        cust->num_items = num_items;
        cust->line_number = line_number;
        cust->time_enter = time_enter;
        debugf(DEBUG_TRACE_CUSTOMER, "customer_create populated customer struct\n");

        return cust;
}

// Destroy/deallocate a customer instance
void customer_destroy(Customer *c){
        if(c == NULL)
                return;

        debugf(DEBUG_TRACE_CUSTOMER, "Destroying customer %s\n", c->name);
        mmgr_free(g_MEM, c->name);
        mmgr_free(g_MEM, c);

        return;
}

// Creates an instance of a Node, which represent entries in the linked list stored in
// each queue
Node* node_create(Customer *c, Node *next){
        debugf(DEBUG_TRACE_LANE, "node_create creating lane queue node\n");
        Node *node = mmgr_malloc(g_MEM, sizeof(Node));
        node->cust = c;
        node->next = next;

        return node;
}

// Destroy/deallocate a node instance
void node_destroy(Node *n){
        if(n == NULL) {
                debugf(DEBUG_TRACE_LANE, "node_destroy called to destroy NULL node! Returning\n");
                return;
        }
        mmgr_free(g_MEM, n);
        debugf(DEBUG_TRACE_LANE, "node_destroy destroyed a node\n");
}

// Create an instance of an empty queue
Lane* lane_create(){
        debugf(DEBUG_TRACE_LANE, "lane_create initializing new lane\n");
        Lane *lane = mmgr_malloc(g_MEM, sizeof(Lane));
        lane->front = NULL;
        lane->back = NULL;

        return lane;
}

// Destroy an instance of a queue, along with any Nodes/Customers contained within it (if not empty)
void lane_destroy(Lane *l){
        if(l == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_destroy called to destroy NULL lane! Returning\n");
                return;
        }

        Node *cursor, *tmp;
        cursor = l->front;

        debugf(DEBUG_TRACE_LANE, "lane_destroy cleaning up...\n");
        while(cursor != NULL) {
                customer_destroy(cursor->cust);

                tmp = cursor;
                customer_destroy(tmp->cust);
                node_destroy(tmp);

                cursor = cursor->next;
        }

        mmgr_free(g_MEM, l);

}

// Enqueue a Customer into a Lane
// This creates a Node with the appropriate pointers and updates the front/back of the queue
// as appropriate
void lane_enqueue(Lane *l, Customer *c){
        if(l == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_enqueue NULL lane provided! returning...\n");
                return;
        }

        if(c == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_enqueue NULL customer provided! returning...\n");
                return;
        }

        Node *node = node_create(c, NULL);

        if(l->front == NULL) {
                // If initially empty, the front pointer should point to the first node in the queue
                debugf(DEBUG_TRACE_LANE, "lane_dequeue queue is empty, front pointer updated\n");
                l->front = node;
        }

        if(l->back == NULL) {
                // If the queue is empty, the back pointer should point to the first node
                debugf(DEBUG_TRACE_LANE, "lane_dequeue queue is empty, back pointer updated\n");
                l->back = node;
        } else {
                // If the queue isn't empty
                debugf(DEBUG_TRACE_LANE, "lane_dequeue enqueued node\n");
                l->back->next = node;
                l->back = node;
        }
}

// Dequeue a Customer from a Lane
// This removes a Node and performs the appropriate operations to manage pointers the front/back of the queue
Customer* lane_dequeue(Lane *l){
        debugf(DEBUG_TRACE_LANE, "lane_dequeue called\n");
        if(l == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_dequeue NULL lane provided! returning...\n");
                return NULL;
        }

        Node *n;
        Customer *cust;

        n = l->front;
        l->front = n->next;

        if(l->front == l->back) {
                debugf(DEBUG_TRACE_LANE, "lane_deque dequeued last customer, now empty\n");
                l->back = NULL;
        }

        // Deallocate the node containg the queue entry
        cust = n->cust;
        node_destroy(n);
        debugf(DEBUG_TRACE_LANE, "lane_dequeue destroyed node containing dequeued customer\n");

        return cust;
}

// Returns the Customer at the front of a queue without dequeueing it
Customer* lane_peek(Lane *l){
        if(l == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_peek called on NULL lane! returning...\n");
                return NULL;
        }

        if(l->front == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_peek called on empty lane\n");
                return NULL;
        }

        return l->front->cust;
}

// Returns 1 or 0 if a queue contains elements or is empty (respectively), -1 if
// the queue is NULL
int lane_empty(Lane *l){
        if(l == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_empty called on NULL lane!\n");
                return -1;
        }

        if(l->front == NULL && l->back == NULL) {
                debugf(DEBUG_TRACE_LANE, "lane_empty called on non-empty lane\n");
                return 1;
        }

        debugf(DEBUG_TRACE_LANE, "lane_empty called on empty lane\n");
        return 0;
}

// Initializes the array of lanes (queues) used in the entire store
void store_lanes_create(){
        for(int i = 0; i < CONFIG_NUM_LANES; i++) {
                debugf(DEBUG_TRACE_LANE, "Creating lane %d\n", i);
                store_lanes[i] = lane_create();
        }
}

// Deallocates the array of lanes (queues) used in the entire store
void store_lanes_destroy(){
        for(int i = 0; i < CONFIG_NUM_LANES; i++) {
                debugf(DEBUG_TRACE_LANE, "Destroying lane %d\n", i);
                lane_destroy(store_lanes[i]);
        }
}

// Enqueues a Customer into the queue at index laneIdx in the store
void store_lanes_enqueue(int laneIdx, Customer* c){
        if(store_lanes[laneIdx] == NULL) {
                debugf(DEBUG_TRACE_LANE, "store_lanes_enqueue warning: lane ID %d is NULL\n", laneIdx);
        }

        lane_enqueue(store_lanes[laneIdx], c);
}

// Dequeues a Customer from the queue at index laneIdx in the store
Customer* store_lanes_dequeue(int laneIdx){
        if(store_lanes[laneIdx] == NULL) {
                debugf(DEBUG_TRACE_LANE, "store_lanes_dequeue warning: lane ID %d is NULL\n", laneIdx);
        }

        return lane_dequeue(store_lanes[laneIdx]);
}

// Returns the Customer located in the queue at index laneIdx in the store without
// dequeueing it
Customer* store_lanes_peek(int laneIdx){
        if(store_lanes[laneIdx] == NULL) {
                debugf(DEBUG_TRACE_LANE, "store_lanes_peek warning: lane ID %d is NULL\n", laneIdx);
        }

        return lane_peek(store_lanes[laneIdx]);
}

// Returns 1 or 0 if a queue at index laneIdx contains elements or is empty (respectively), -1 if
// the queue is NULL
int store_lanes_empty(int laneIdx){
        if(store_lanes[laneIdx] == NULL) {
                debugf(DEBUG_TRACE_LANE, "store_lanes_empty warning: lane ID %d is NULL\n", laneIdx);
        }

        return lane_empty(store_lanes[laneIdx]);
}

// Returns 0 if all lanes in the store are currently empty
int store_lanes_all_empty(){
        for(int i = 0; i < CONFIG_NUM_LANES; i++) {
                if(store_lanes_empty(i) == 1) {
                        debugf(DEBUG_TRACE_LANE, "store_lanes_empty Lane %d still has customers in it, returning...\n", i);
                        return 1;
                }
        }

        debugf(DEBUG_TRACE_LANE, "store_lanes_all_empty all lanes are empty\n");
        return 0;
}

// Iterates over each queue in the store to determine which customer should be checked out next
int checkout_get_next_lane(){
        int nextcust_items = CONFIG_MAX_CUST_ITEMS + 1, nextcust_lane = -1;
        Customer *tmp;

        // Lookping backwards guarantees that the customer from the smaller line number
        // will be selected if two customers have the same number of items
        for(int i = CONFIG_NUM_LANES; i--> 0; ) {
                tmp = store_lanes_peek(i);
                if(tmp == NULL)
                        continue;

                if(tmp->num_items <= nextcust_items) {
                        debugf(DEBUG_TRACE_CHECKOUT, "checkout_get_next_lane found candidate in lane %d with %d items\n", i, tmp->num_items);
                        nextcust_items = tmp->num_items;
                        nextcust_lane = i;
                }
        }

        if(nextcust_lane == -1)
                debugf(DEBUG_TRACE_CHECKOUT, "checkout_get_next_lane all lanes appear to be empty\n");
        else
                debugf(DEBUG_TRACE_CHECKOUT, "checkout_get_next_lane selecting customer from lane %d\n", nextcust_lane);

        return nextcust_lane;
}

// Determines the next customer to dequeue based on criteria in the specification and dequeues it
// This is just sugar
Customer* checkout_get_next_cust(){
        return store_lanes_dequeue(checkout_get_next_lane());
}

// Calculates the time required to check out a customer
int checkout_time(Customer *c){
        if(c == NULL) {
                debugf(DEBUG_TRACE_CHECKOUT, "checkout_time called with NULL customer! returning...\n");
                return 0;
        }

        return CONFIG_CHECKOUT_TIME_BASE + (c->num_items * CONFIG_CHECKOUT_TIME_PER_ITEM);
}

////////////////////////// Util //////////////////////////

// Panic is called when something goes wrong and we have to die for the greater good
void panic(const char * fmt, ...){
        // Vargs to behave like printf (but not to make black metal)
        va_list vargs;
        va_start(vargs, fmt);
        debugf(DEBUG_LEVEL_ALL, "panic called\n");
        // Print error message to stderr
        vfprintf(stderr, fmt, vargs);
        fflush(stderr);
        va_end(vargs);
        // Clean up any allocated memory and exit
        mmgr_cleanup(g_MEM);
        exit(1);
}

// Write_out prints messages to the global output file
// This shim is more for my own use as I require stdout for compatibility with my
// automated test harness
void write_out(const char * fmt, ...){
        va_list vargs;
        va_start(vargs, fmt);
        // Print message to outfile
        if(g_outfp != NULL) {
                vfprintf(g_outfp, fmt, vargs);
                fflush(g_outfp);
        }
        // if stdout is enabled (in the case of testing), print msg to stdout as well
        if(stdout_enabled == 1) {
                vfprintf(stdout, fmt, vargs);
                fflush(stdout);
        }
        va_end(vargs);
}

////////////////////////// Memory manager //////////////////////////

// This is the initial version of my C memory manager.
// At the moment it's very rudimentary.

// Initializes the memory manager's global state table. This tracks all allocated
// memory, reallocates freed entries, and ensures that all allocated memory is
// completely cleaned up on program termination.

// In the future, MMGR will spawn a worker thread to perform automated garbage
// collection through mark and sweep or simple refcounting. It'll also be
// refactored to utilize hashmaps once I figure out how to hash pointers.
MMGR *mmgr_init(){
        MMGR *state_table = calloc(1, sizeof(MMGR));

        state_table->free = NULL;
        state_table->numFree = 0;

        state_table->entries = NULL;
        state_table->numEntries = 0;

        state_table->mutex = 0;

        debugf(DEBUG_TRACE_MMGR, "mmgr: initialized\n");

        return state_table;
}

// Mutex acquisition/release helpers to atomicize access to the memory manager's
// state table. Not really necessary atm but eventually I might play with threading
void mmgr_mutex_acquire(MMGR *tbl){
        while (tbl->mutex == 1);
        tbl->mutex = 1;
}

void mmgr_mutex_release(MMGR *tbl){
        tbl->mutex = 0;
}

// Allocate memory and maintain a reference in the memory manager's state table
// If any previously allocated entries have since been freed, these will be
// resized and reallocated to serve the request
void *mmgr_malloc(MMGR *tbl, size_t size){
        if(tbl == NULL)
                return NULL;

        // State table access is atomic. You'll see this around a lot.
        mmgr_mutex_acquire(tbl);

        void* handle = NULL;

        // If freed entries are available, we can recycle them
        if(tbl->numFree > 0) {
                // Fetch index of last reallocatable table entry
                int tgt_idx = tbl->free[tbl->numFree - 1];

                debugf(DEBUG_TRACE_MMGR, "mmgr: found reusable previously allocated entry %d\n", tgt_idx);

                // Update table entry with new pointer and size, memset to 0
                tbl->entries[tgt_idx]->size = size;
                tbl->entries[tgt_idx]->handle = malloc(size);
                memset(tbl->entries[tgt_idx]->handle, 0, size);

                // Copy freshly allocated region pointer to handle
                handle = tbl->entries[tgt_idx]->handle;

                // Resize free index array
                tbl->free = (int*) realloc(tbl->free, (sizeof(int) * (tbl->numFree - 1)));
                tbl->numFree--;

                debugf(DEBUG_TRACE_MMGR, "mmgr: reallocated %lu bytes\n", size);

                // Otherwise the state table will need to be resized to accommodate a new
                // entry
        } else {
                debugf(DEBUG_TRACE_MMGR, "mmgr: no recyclable entries available, increasing table size\n");

                // Resize occupied entry table to accommodate an additional entry
                tbl->entries = (MMGR_Entry**) realloc(tbl->entries, (sizeof(MMGR_Entry*) * (tbl->numEntries + 1)));

                // Allocate a new entry to include in the state table
                tbl->entries[tbl->numEntries] = (MMGR_Entry*) calloc(1, sizeof(MMGR_Entry) + sizeof(void*));

                tbl->entries[tbl->numEntries]->handle = malloc(size);
                memset(tbl->entries[tbl->numEntries]->handle, 0, size);

                // Copy freshly allocated region pointer to handle
                handle = tbl->entries[tbl->numEntries]->handle;
                tbl->entries[tbl->numEntries]->size = size;

                tbl->numEntries++;

                debugf(DEBUG_TRACE_MMGR, "mmgr: allocated %lu bytes, handle is %p\n", size, handle);
        }

        mmgr_mutex_release(tbl);

        return handle;
}


// Frees the provided pointer and checks out the active entry in the global
// state table so that it can be reallocated
void mmgr_free(MMGR *tbl, void* handle){
        if(tbl == NULL)
                return;

        if(handle == NULL) {
                debugf(DEBUG_TRACE_MMGR, "mmgr: provided NULL handle! no-op\n");
                return;
        }

        mmgr_mutex_acquire(tbl);

        // Whether the provided pointer exists in the MMGR state table
        int found = 0;

        debugf(DEBUG_TRACE_MMGR, "mmgr: num active entries is %d, called to free %p\n", (tbl->numEntries - tbl->numFree), handle);

        // Search backwards through the state table for the provided pointer
        // The idea being that the memory you allocate the last will be the first
        // you want to free (e.g. struct members, then structs)
        // Eventually this will be overhauled with hash maps
        for(int i = tbl->numEntries; i--> 0; ) {
                if(tbl->entries[i]->handle == handle) {
                        debugf(DEBUG_TRACE_MMGR, "mmgr: found handle %p at index %d\n", handle, i);

                        tbl->numFree++;

                        // Free and nullify table entry
                        free(tbl->entries[i]->handle);
                        tbl->entries[i]->handle = NULL;
                        tbl->entries[i]->size = 0;

                        // Resize free entry list, ad freed entry
                        tbl->free = (int*) realloc(tbl->free, (sizeof(int) * (tbl->numFree + 1)));
                        tbl->free[tbl->numFree - 1] = i;

                        found = 1;

                        debugf(DEBUG_TRACE_MMGR, "mmgr: freed %p at index %d, %d entries remain active\n", handle, i, (tbl->numEntries - tbl->numFree));

                }
        }

        if(found == 0)
                debugf(DEBUG_TRACE_MMGR, "mmgr: called to free %p but couldn't find it, no-op\n", handle);

        mmgr_mutex_release(tbl);
}

// Cleans up any remaining allocated memory, then frees the state table
void mmgr_cleanup(MMGR *tbl){
        if(tbl == NULL)
                return;

        mmgr_mutex_acquire(tbl);

        int deEn = 0;

        debugf(DEBUG_TRACE_MMGR, "mmgr: cleaning up %d active entries\n", (tbl->numEntries - tbl->numFree));

        // Free all active entries and what they're pointing to
        for(int i = 0; i < tbl->numEntries; i++) {
                if(tbl->entries[i] != NULL) {
                        if(tbl->entries[i]->handle != NULL)
                                free(tbl->entries[i]->handle);

                        free(tbl->entries[i]);
                        deEn++;
                }
        }

        debugf(DEBUG_TRACE_MMGR, "mmgr: cleanup deallocd %d entries\n", deEn);

        // Deallocate occupied + free table entries, table itself
        free(tbl->entries);
        free(tbl->free);
        free(tbl);

        tbl = NULL;
}
