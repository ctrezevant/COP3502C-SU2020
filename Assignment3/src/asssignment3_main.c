/*
   COP 3502C Assignment 3
   This program is written by: Charlton Trezevant
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "leak_detector_c.h"

////////////////////////// Global Project Configuation //////////////////////////
#define CONFIG_INFILE_NAME "in.txt"
#define CONFIG_OUTFILE_NAME "out.txt"

// Maximum values and limits as given in the project spec
#define CONFIG_MAX_COORD_VALUE 10000
#define CONFIG_INFECTED_LBOUND 2
#define CONFIG_INFECTED_UBOUND 1E6
#define CONFIG_SEARCH_POINT_LBOUND 1
#define CONFIG_SEARCH_POINT_UBOUND 2E5
#define CONFIG_SEARCH_FUNC_LBOUND 1
#define CONFIG_SEARCH_FUNC_UBOUND 30

// Global debug levels
#define DEBUG_LEVEL_ALL 0
#define DEBUG_LEVEL_TRACE 1
#define DEBUG_LEVEL_INFO 2

// Functionality-specific debug levels
// These enable debug output only for specific sections of the program
#define DEBUG_TRACE_MMGR -7

// Current debug level set/disable
//#define DEBUG DEBUG_LEVEL_INFO

////////////////////////// Assignment 3 Prototypes and Global //////////////////////////

// Point typedef and associated methods
typedef struct Point {
  int x;
  int y;
} Point;

// Empty point marker
Point EMPTY_POINT = {0, 0};

// Point type methods
Point *point_create(int x, int y);
void point_destroy(Point *p);
int point_distance(Point *p1, Point *p2);

// Global user location (the single global allowed for the assignment)
Point *MY_LOCATION;

// Input file handling
Point **ReadData(FILE *infp, Point *myloc, int *n_infected, int *n_search, Point ***search_points, int *sort_thresh);

// Sorting util prototypes
int compareTo(Point *p1, Point *p2);
void sort(Point **arr, int len, int alg_thresh);

// Binary search prototype
int binary_search(Point **arr, int min, int max, Point *val);

// Merge sort prototypes
void ms__merge(Point **p1, int p1_len, Point **p2, int p2len, Point **dst);
void merge_sort(Point **points, int len);

// Insertion sort prototype
void insertion_sort(Point **arr, int len);

////////////////////////// Debug Output //////////////////////////
// (c) Charlton Trezevant - 2018

#ifdef DEBUG
#define debugf(lvl, fmt, ...)                                                   \
  ({                                                                            \
    if (DEBUG == 0 || (lvl > 0 && lvl >= DEBUG) || (lvl < 0 && lvl == DEBUG)) { \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                      \
      fflush(stderr);                                                           \
    }                                                                           \
  })
#else
#define debugf(lvl, fmt, ...) ((void)0)
#endif

////////////////////////// Memory Manager Prototypes //////////////////////////
// (c) Charlton Trezevant - 2020

typedef struct MMGR_Entry {
  void *handle;
  size_t size;
} MMGR_Entry;

typedef struct MMGR {
  MMGR_Entry **entries;
  int numEntries;
  int *free; // available recyclable slots
  int numFree;
  volatile int mutex;
} MMGR;

MMGR *mmgr_init();
void *mmgr_malloc(MMGR *tbl, size_t size);
void mmgr_free(MMGR *tbl, void *handle);
void mmgr_cleanup(MMGR *tbl);
void mmgr_mutex_acquire(MMGR *tbl);
void mmgr_mutex_release(MMGR *tbl);

MMGR *g_MEM;

////////////////////////// Utility //////////////////////////
FILE *g_outfp = NULL; // output file pointer (for test harness compatibility)

void panic(const char *format, ...);
void write_out(const char *format, ...);

////////////////////////// Entry //////////////////////////

int main(int argc, char **argv) {
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
  if (argc > 1 && strcmp(argv[1], "use_default") != 0) {
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
  if (argc > 2) {
    if (strcmp(argv[2], "stdout") == 0) {
      debugf(DEBUG_LEVEL_INFO, "Writing output to stdout\n");
    } else {
      g_outfp = fopen(argv[2], "w+");
      debugf(DEBUG_LEVEL_INFO, "Output file name: %s\n", argv[2]);
    }
  } else {
    g_outfp = fopen(CONFIG_OUTFILE_NAME, "w+");
    debugf(DEBUG_LEVEL_INFO, "Output file name: %s\n", CONFIG_OUTFILE_NAME);
  }

  int num_infected, num_search, sort_thresh;
  Point **search_points = NULL;

  // Being able to read/parse these values inside of the main function would have been preferable
  // since you end up having to pass a bunch of state information around anyways and would thus avoid
  // having to mess around with triple pointers
  Point **infected_points = ReadData(infile, MY_LOCATION, &num_infected, &num_search, &search_points, &sort_thresh);

  // Sort the array with the required function
  sort(infected_points, num_infected, sort_thresh);

  // Print the sorted points
  for (int i = 0; i < num_infected; i++)
    write_out("%d %d\n", infected_points[i]->x, infected_points[i]->y);

  // Search for and print the provided coordinates
  for (int i = 0; i < num_search; i++) {
    int index = binary_search(infected_points, 0, num_infected - 1, search_points[i]);
    if (index != -1)
      write_out("%d %d found at rank %d\n", search_points[i]->x, search_points[i]->y, index + 1);
    else
      write_out("%d %d not found\n", search_points[i]->x, search_points[i]->y);
  }

  // Clean up allocated memory
  debugf(DEBUG_LEVEL_TRACE, "MMGR Cleanup.\n");
  mmgr_cleanup(g_MEM); // #noleaks

  // Close input and output files
  debugf(DEBUG_LEVEL_TRACE, "Infile close.\n");
  fclose(infile);

  debugf(DEBUG_LEVEL_TRACE, "Outfile close.\n");
  if (g_outfp != NULL)
    fclose(g_outfp);

  debugf(DEBUG_LEVEL_TRACE, "Exiting.\n");
  return 0;
}

// ReadData does exactly what the name implies
// It also enforces limits defined in the project specification before anything is instantiated
Point **ReadData(FILE *infp, Point *myloc, int *n_infected, int *n_search, Point ***search_points, int *sort_thresh) {

  // Read in the file metadata
  if (!feof(infp)) {
    MY_LOCATION = (Point *)mmgr_malloc(g_MEM, sizeof(Point));
    fscanf(infp, "%d %d %d %d %d", &MY_LOCATION->x, &MY_LOCATION->y, n_infected, n_search, sort_thresh);
  } else {
    panic("Malformed input file: hit EOF parsing metadata\n");
  }

  if (*n_infected < CONFIG_INFECTED_LBOUND || *n_infected > CONFIG_INFECTED_UBOUND)
    panic("Malformed input file: %d infected is invalid\n", *n_infected);

  if (*n_search < CONFIG_SEARCH_POINT_LBOUND || *n_search > CONFIG_SEARCH_POINT_UBOUND)
    panic("Malformed input file: %d search points is invalid\n", *n_search);

  if (*sort_thresh < CONFIG_SEARCH_FUNC_LBOUND || *sort_thresh > CONFIG_SEARCH_FUNC_UBOUND)
    panic("Malformed input file: %d search threshold is invalid\n", *sort_thresh);

  // Read and parse the infected points list
  Point **infected_points = mmgr_malloc(g_MEM, sizeof(Point *) * (*n_infected));

  for (int i = 0; i < (*n_infected); i++) {
    if (!feof(infp)) {
      Point *tmp;
      int x, y;

      fscanf(infp, "%d %d", &x, &y);

      tmp = point_create(x, y);

      if (tmp == &EMPTY_POINT)
        panic("Malformed input file: invalid infected coordinate %d %d\n", x, y);

      infected_points[i] = tmp;
    } else {
      panic("Malformed input file: hit EOF parsing infected coordinates\n");
    }
  }

  // Read and parse the search points list
  (*search_points) = mmgr_malloc(g_MEM, sizeof(Point *) * (*n_search));

  for (int i = 0; i < (*n_search); i++) {
    if (!feof(infp)) {
      Point *tmp;
      int x, y;

      fscanf(infp, "%d %d", &x, &y);

      tmp = point_create(x, y);

      if (tmp == &EMPTY_POINT)
        panic("Malformed input file: invalid search coordinate %d %d\n", x, y);

      (*search_points)[i] = tmp;
    } else {
      panic("Malformed input file: hit EOF parsing search coordinates\n");
    }
  }

  return infected_points;
}

// point_create creates a point
// if the coordinates are invalid, a pointer to the empty point sentinel value
// is returned
Point *point_create(int x, int y) {
  if (abs(x) > CONFIG_MAX_COORD_VALUE || abs(y) > CONFIG_MAX_COORD_VALUE)
    return &EMPTY_POINT;

  Point *p = (Point *)mmgr_malloc(g_MEM, sizeof(Point));
  p->x = x;
  p->y = y;

  return p;
}

// point_destroy destroys a point
void point_destroy(Point *p) {
  if (p == NULL || p == &EMPTY_POINT)
    return;

  //	"Death am I, and my present task
  //   Destruction."
  mmgr_free(g_MEM, p);
}

// point_distance deturns the integer distance between two points
// using the distance formula
int point_distance(Point *p1, Point *p2) {
  return sqrt(((p1->x - p2->x) * (p1->x - p2->x)) + ((p1->y - p2->y) * (p1->y - p2->y)));
}

// compareTo compares two points based on the criteria provided in the project
// specification
int compareTo(Point *p1, Point *p2) {
  if (p1 == &EMPTY_POINT || p2 == &EMPTY_POINT || MY_LOCATION == &EMPTY_POINT)
    return 0;

  // Points are equal
  if (p1->x == p2->x && p1->y == p2->y)
    return 0;

  int dist_p1 = point_distance(p1, MY_LOCATION);
  int dist_p2 = point_distance(p2, MY_LOCATION);

  // Point 2 is farther away
  if (dist_p1 < dist_p2)
    return -1;

  if (dist_p1 > dist_p2)
    return 1;

  // Same distance, but point 2 is farther on the x axis
  if (dist_p1 == dist_p2 && p1->x < p2->x)
    return -2;

  if (dist_p1 == dist_p2 && p1->x > p2->x)
    return 2;

  // Same distance, but point 2 is farther on the y axis
  if (dist_p1 == dist_p2 && p1->y < p2->y)
    return -3;

  if (dist_p1 == dist_p2 && p1->y > p2->y)
    return 3;

  return 0;
}

// Sort dispatches the appropriate sorting algorithm depending on the
// current threshold.
// The purpose and use of the threshold value was not clearly explained,
// so I assumed that it refers to the total length of the given array and
// that you want to run merge sort for larger arrays
void sort(Point **arr, int len, int alg_thresh) {
  if (arr == NULL || len > CONFIG_SEARCH_FUNC_UBOUND)
    return;

  if (len < alg_thresh)
    insertion_sort(arr, len);
  else
    merge_sort(arr, len);
}

//////////////// Merge Sort

// Merge sort helper
void ms__merge(Point **p1, int p1_len, Point **p2, int p2len, Point **dst) {
  int i1 = 0, i2 = 0, j = 0;

  // I didn't bother with making multiple temp arrays. Instead,
  // the p1 and p2 partitions are actually split out of the
  // whole input array, and the sorted values are then copied
  // to a fresh destination array.
  while (i1 < p1_len && i2 < p2len) {
    if (compareTo(p1[i1], p2[i2]) < 0) {
      dst[j++] = p1[i1++];
    } else {
      dst[j++] = p2[i2++];
    }
  }

  // Any remaining elements in the source array are
  // copied to the destination
  while (i1 < p1_len)
    dst[j++] = p1[i1++];

  while (i2 < p2len)
    dst[j++] = p2[i2++];
}

void merge_sort(Point **points, int len) {
  if (points == NULL || len < 2)
    return;

  // Divide and conquer!
  // First, we've gotta sort the first and second halves of the input
  merge_sort(points, len / 2);
  merge_sort(points + (len / 2), len - (len / 2));

  // Next we instantiate the destination array and merge the sorted partitions
  Point **tmp = mmgr_malloc(g_MEM, len * sizeof(Point *));
  ms__merge(points, len / 2, points + (len / 2), len - (len / 2), tmp);

  // Overwrite the contents of the original unsorted array with the sorted
  // contents of the temporary array that we just filled up
  for (int i = 0; i < len; i++)
    points[i] = tmp[i];

  // Free the temporary array
  mmgr_free(g_MEM, tmp);
}

//////////////// Insertion Sort
// Insertion sort does what it says on the tin
void insertion_sort(Point **points, int len) {
  Point *val;
  int j;
  for (int i = 1; i < len; i++) {
    val = points[i];
    j = i;
    while (j > 0 && compareTo(points[j - 1], val) > 0) {
      points[j] = points[j - 1];
      j--;
    }
    points[j] = val;
  }
}
//////////////// End Insertion Sort

//////////////// Binary Search
// This one also does what you would expect
int binary_search(Point **arr, int min, int max, Point *val) {
  if (max < min || arr == NULL || val == &EMPTY_POINT)
    return -1;

  int midpt = min + (max - min) / 2;

  if (arr[midpt]->x == val->x && arr[midpt]->y == val->y)
    return midpt;

  if (compareTo(arr[midpt], val) > 0)
    return binary_search(arr, min, midpt - 1, val);

  return binary_search(arr, midpt + 1, max, val);
}
//////////////// End Binary Search

////////////////////////// Util //////////////////////////

// Panic is called when something goes wrong and we have to die for the greater good
void panic(const char *fmt, ...) {
  // Vargs to behave like printf (but not to make black metal)
  va_list vargs;
  va_start(vargs, fmt);
  debugf(DEBUG_LEVEL_ALL, "panic called\n");
  // Print error message to stderr
  vfprintf(stderr, fmt, vargs);
  fflush(stderr);
  va_end(vargs);
  // Politely clean up any allocated memory and exit
  mmgr_cleanup(g_MEM);
  exit(1);
}

// Write_out prints messages to the global output file
// This shim is more for my own use as I require stdout for compatibility with my
// automated test harness
void write_out(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);

  if (g_outfp == NULL)
    g_outfp = stdout;

  vfprintf(g_outfp, fmt, vargs);
  fflush(g_outfp);

  va_end(vargs);
}

////////////////////////// Memory manager //////////////////////////

// Initializes the memory manager's global state table. This tracks all allocated
// memory, reallocates freed entries, and ensures that all allocated memory is
// completely cleaned up on program termination.
MMGR *mmgr_init() {
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
void mmgr_mutex_acquire(MMGR *tbl) {
  while (tbl->mutex == 1)
    ;
  tbl->mutex = 1;
}

void mmgr_mutex_release(MMGR *tbl) {
  tbl->mutex = 0;
}

// Allocate memory and maintain a reference in the memory manager's state table
// If any previously allocated entries have since been freed, these will be
// resized and reallocated to serve the request
void *mmgr_malloc(MMGR *tbl, size_t size) {
  if (tbl == NULL)
    return NULL;

  // State table access is atomic. You'll see this around a lot.
  mmgr_mutex_acquire(tbl);

  void *handle = NULL;

  // If freed entries are available, we can recycle them
  if (tbl->numFree > 0) {
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
    tbl->free = (int *)realloc(tbl->free, (sizeof(int) * (tbl->numFree - 1)));
    tbl->numFree--;

    debugf(DEBUG_TRACE_MMGR, "mmgr: reallocated %lu bytes\n", size);

    // Otherwise the state table will need to be resized to accommodate a new
    // entry
  } else {
    debugf(DEBUG_TRACE_MMGR, "mmgr: no recyclable entries available, increasing table size\n");

    // Resize occupied entry table to accommodate an additional entry
    tbl->entries = (MMGR_Entry **)realloc(tbl->entries, (sizeof(MMGR_Entry *) * (tbl->numEntries + 1)));

    // Allocate a new entry to include in the state table
    tbl->entries[tbl->numEntries] = (MMGR_Entry *)calloc(1, sizeof(MMGR_Entry) + sizeof(void *));

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
void mmgr_free(MMGR *tbl, void *handle) {
  if (tbl == NULL)
    return;

  if (handle == NULL) {
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
  for (int i = tbl->numEntries; i-- > 0;) {
    if (tbl->entries[i]->handle == handle) {
      debugf(DEBUG_TRACE_MMGR, "mmgr: found handle %p at index %d\n", handle, i);

      tbl->numFree++;

      // Free and nullify table entry
      free(tbl->entries[i]->handle);
      tbl->entries[i]->handle = NULL;
      tbl->entries[i]->size = 0;

      // Resize free entry list, ad freed entry
      tbl->free = (int *)realloc(tbl->free, (sizeof(int) * (tbl->numFree + 1)));
      tbl->free[tbl->numFree - 1] = i;

      found = 1;

      debugf(DEBUG_TRACE_MMGR, "mmgr: freed %p at index %d, %d entries remain active\n", handle, i, (tbl->numEntries - tbl->numFree));
    }
  }

  if (found == 0)
    debugf(DEBUG_TRACE_MMGR, "mmgr: called to free %p but couldn't find it, no-op\n", handle);

  mmgr_mutex_release(tbl);
}

// Cleans up any remaining allocated memory, then frees the state table
void mmgr_cleanup(MMGR *tbl) {
  if (tbl == NULL)
    return;

  mmgr_mutex_acquire(tbl);

  int deEn = 0;

  debugf(DEBUG_TRACE_MMGR, "mmgr: cleaning up %d active entries\n", (tbl->numEntries - tbl->numFree));

  // Free all active entries and what they're pointing to
  for (int i = 0; i < tbl->numEntries; i++) {
    if (tbl->entries[i] != NULL) {
      if (tbl->entries[i]->handle != NULL)
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
