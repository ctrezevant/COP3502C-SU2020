/* COP 3502C Assignment 1
   This program is written by: Charlton Trezevant (PID 4383060)
 */


/*
   The provided leak detector causes compiler warnings and, somewhat ironically,
   appears to contain a memory leak of its own.

   This is apparently because fclose() is never called on the leak detector's
   output file.

   Valgrind output:
   ==2024== HEAP SUMMARY:
   ==2024==     in use at exit: 552 bytes in 1 blocks
   ==2024==   total heap usage: 5 allocs, 4 frees, 5,512 bytes allocated
   ==2024==
   ==2024== 552 bytes in 1 blocks are still reachable in loss record 1 of 1
   ==2024==    at 0x4C2FB0F: malloc (in /usr/lib/valgrind/vgpreload_memcheck-amd64-linux.so)
   ==2024==    by 0x4EBAE49: __fopen_internal (iofopen.c:65)
   ==2024==    by 0x4EBAE49: fopen@@GLIBC_2.2.5 (iofopen.c:89)
   ==2024==    by 0x109765: report_mem_leak (in /home/net/ch123722/COP3502C/Assignment1/bin/assignment1)
   ==2024==    by 0x4E7F040: __run_exit_handlers (exit.c:108)
   ==2024==    by 0x4E7F139: exit (exit.c:139)
   ==2024==    by 0x4E5DB9D: (below main) (libc-start.c:344)

   My static analyzer also complains about this format string error:

   /home/net/ch123722/COP3502C/Assignment1/src/leak_detector_c.c: In function ‘report_mem_leak’:
   /home/net/ch123722/COP3502C/Assignment1/src/leak_detector_c.c:190:30: warning: format ‘%d’ expects argument of type ‘int’, but argument 3 has type ‘void *’ [-Wformat=]
    sprintf(info, "address : %d\n", leak_info->mem_info.address);
                             ~^     ~~~~~~~~~~~~~~~~~~~~~~~~~~~
                             %p

   On another note, the leak detector doesn't run on macOS at all. I'm looking
   into it.
 */

#include <stdio.h>
#include  <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include  "leak_detector_c.h"

// debugf.h
// (c) Charlton Trezevant - 2018
#define DEBUG_LEVEL_ALL 0
#define DEBUG_LEVEL_MMGR 1
#define DEBUG_LEVEL_INFO 2
#define DEBUG_LEVEL_LOGIC 3
#define DEBUG_LEVEL_STATE 4
#define DEBUG_LEVEL_DISABLED 99

#define DEBUG DEBUG_LEVEL_MMGR

#ifdef DEBUG
#define debugf(lvl, fmt, ...) \
        ({ \
                if (DEBUG == 0 || (lvl) >= DEBUG) { \
                        fprintf(stderr, fmt, ## __VA_ARGS__); fflush(stderr); \
                } \
        })
#else
  #define debugf(lvl, fmt, ...) ((void)0)
#endif

#define INFILE_NAME "in.txt"

//// Project spec type definitions

typedef struct student {
        int id;
        char *lname; //stores last name of student
        float *scores; //stores scores of the student. Size is taken from num_scores array.
        float std_avg; //average score of the student (to be calculated)
} student;

typedef struct course {
        char *course_name; //stores course name
        int num_sections; //number of sections
        student **sections;//stores array of student arrays(2D array). Size is num_sections;
        int *num_students;//stores array of number of students in each section. Size is num_sections;
        int *num_scores; //stores array of number of assignments in each section. Size is num_sections;
} course;

//// Project spec function prototypes

course *read_courses(FILE *fp, int *num_courses);
student **read_sections(FILE *fp, int num_students[], int num_scores[], int num_sections);
void process_courses(course *courses, int num_courses);
void release_courses(course *courses, int num_courses);

// MMGR Type definitions/function prototypes
// (c) Charlton Trezevant 2020
// Rave reviews:
// 10/10
//    - IGN
// ==3291== All heap blocks were freed -- no leaks are possible
//    - Valgrind
// ~ it's pronounced "mumger" ~
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

// Utility
void panic(const char * format, ...);

// Global memory manager instance
MMGR* g_MEM;

////////////////////////// Entry //////////////////////////

int main(int argc, char **argv){
        FILE *infile;

        debugf(DEBUG_LEVEL_MMGR, "Memory leak detector init.\n");
        atexit(report_mem_leak); //memory leak detection

        g_MEM = mmgr_init();

        if(argc > 1) {
                infile = fopen(argv[1], "r");
                debugf(DEBUG_LEVEL_INFO, "Input file name: %s\n", argv[1]);
        } else {
                infile = fopen(INFILE_NAME, "r");
                debugf(DEBUG_LEVEL_INFO, "Input file name: %s\n", INFILE_NAME);
        }

        if (infile == NULL) {
                panic("Failed to open input file\n");
                return 1;
        }

        int num_cases = -1;

        if(!feof(infile)) {
                fscanf(infile, "%d", &num_cases);
                debugf(DEBUG_LEVEL_LOGIC, "Infile contains %d cases\n", num_cases);
        } else {
                panic("invalid input file format: number of test cases unknown\n");
        }

        // Fetch number of test cases

        course** test_courses = mmgr_malloc(g_MEM, sizeof(course) * num_cases);

        for(int case_n = 0; case_n < num_cases; case_n++) {
                int case_num_courses;

                if(!feof(infile)) {
                        // Fetch number of courses in current case
                        fscanf(infile, "%d", &case_num_courses);
                        debugf(DEBUG_LEVEL_LOGIC, "Infile contains %d courses for test case %d\n", case_num_courses, case_n);

                        test_courses[case_n] = read_courses(infile, &case_num_courses);
                        debugf(DEBUG_LEVEL_MMGR, "read courses for case %d\n", case_n);

                        release_courses(test_courses[case_n], case_num_courses);
                        mmgr_free(g_MEM, test_courses);

                        debugf(DEBUG_LEVEL_MMGR, "released courses for case %d\n", case_n);

                        debugf(DEBUG_LEVEL_LOGIC, "Test case %d ended.\n", case_n);
                } else {
                        panic("reached EOF while attempting to run test case %d", case_n);
                }
        }

        debugf(DEBUG_LEVEL_LOGIC, "Exiting...\n");

        mmgr_cleanup(g_MEM);
        fclose(infile);

        return 0;
}

void panic(const char * fmt, ...){
        va_list vargs;
        va_start(vargs, fmt);
        debugf(DEBUG_LEVEL_ALL, "panic called\n");
        vfprintf(stderr, fmt, vargs);
        fflush(stderr);
        va_end(vargs);
        mmgr_cleanup(g_MEM);
        exit(1);
}

/*
   Spec:
   This function takes a file pointer and reference of an integer to track how may
   courses the file has. Then it reads the data for an entire test case and return
   the allocated memory for all the courses (including sections) for a test case.
   Note that you can call other functions from this function as needed.
 */
course *read_courses(FILE *fp, int *num_courses){
        course *courses = (course*) mmgr_malloc(g_MEM, (sizeof(course) * *num_courses));

        debugf(DEBUG_LEVEL_LOGIC, "will now parse %d courses\n", *num_courses);
        for(int i = 0; i < *num_courses; i++) {
                debugf(DEBUG_LEVEL_LOGIC, "begin parsing course %d of %d\n", i, *num_courses);

                if(feof(fp))
                        panic("invalid input file format: course %d\n", i+1);

                course* cur_course = (course*) mmgr_malloc(g_MEM, sizeof(course));
                debugf(DEBUG_LEVEL_MMGR, "allocated memory to hold course %d\n", i);

                cur_course->course_name = (char*) mmgr_malloc(g_MEM, (sizeof(char) * 21));
                debugf(DEBUG_LEVEL_MMGR, "course %d name array allocated\n", i);

                // Fetch number of courses in current case
                fscanf(fp, "%s", cur_course->course_name);
                debugf(DEBUG_LEVEL_LOGIC, "read course name for %d: %s\n", i, cur_course->course_name);
                fscanf(fp, "%d", &cur_course->num_sections);
                debugf(DEBUG_LEVEL_LOGIC, "course %s has %d sections\n", cur_course->course_name, cur_course->num_sections);

                cur_course->num_scores = mmgr_malloc(g_MEM, (sizeof(int) * cur_course->num_sections));
                debugf(DEBUG_LEVEL_MMGR, "course %s num_scores array allocated\n", cur_course->course_name);

                cur_course->num_students = mmgr_malloc(g_MEM, (sizeof(int) * cur_course->num_sections));
                debugf(DEBUG_LEVEL_MMGR, "course %s num_students array allocated\n", cur_course->course_name);

                cur_course->sections = read_sections(fp, cur_course->num_students, cur_course->num_scores, cur_course->num_sections);

                courses[i] = *cur_course;
                debugf(DEBUG_LEVEL_LOGIC, "course %d appended to courses array\n", i);
        }

        debugf(DEBUG_LEVEL_LOGIC, "returning\n");
        return courses;
}

/*
   Spec:
   This function takes the file pointer, references of two arrays, one for number
   of students, and one for number of scores for a course. The function also takes
   an integer that indicates the number of sections the course has. The function
   reads all the data for all the sections of a course, fill-up the num_students
   and num_socres array of the course and returns 2D array of students that contains
   all the data for all the sections of a course. A good idea would be calling
   this function from the read_course function.

   Translation: Primarily sounds like this is going to be the file parser.
 */
student **read_sections(FILE *fp, int num_students[], int num_scores[], int num_sections){
        student **sections = (student**) mmgr_malloc(g_MEM, (sizeof(student*) * num_sections));
        float avg = 0, readScore = 0;

        for(int sect = 0; sect < num_sections; sect++) {

                fscanf(fp, "%d %d", &num_students[sect], &num_scores[sect]);
                debugf(DEBUG_LEVEL_LOGIC, "expecting %d students and %d scores in section %d\n", num_students[sect], num_scores[sect], sect);

                sections[sect] = (student*) mmgr_malloc(g_MEM, (sizeof(student) * num_students[sect]));
                debugf(DEBUG_LEVEL_MMGR, "allocated student section %d\n", sect);

                debugf(DEBUG_LEVEL_LOGIC, "will now parse %d students in section %d\n", num_students[sect], sect);
                for(int stu = 0; stu < num_students[sect]; stu++) {
                        sections[sect][stu].scores = (float*) mmgr_malloc(g_MEM, (sizeof(float) * num_scores[sect]));
                        debugf(DEBUG_LEVEL_MMGR, "allocated student scores\n");
                        sections[sect][stu].lname = (char*) mmgr_malloc(g_MEM, (sizeof(char) * 21));
                        debugf(DEBUG_LEVEL_MMGR, "allocated student lname\n");

                        fscanf(fp, " %d", &sections[sect][stu].id);
                        debugf(DEBUG_LEVEL_LOGIC, "read student ID: %d\n", sections[sect][stu].id);

                        fscanf(fp, " %s", sections[sect][stu].lname);
                        debugf(DEBUG_LEVEL_LOGIC, "read student name: %s\n", sections[sect][stu].lname);

                        for(int score = 0; score < num_scores[sect]; score++) {
                                fscanf(fp, " %f", &readScore);
                                sections[sect][stu].scores[score] = readScore;
                                avg += readScore;
                                readScore = 0;
                        }
                        
                        avg /= num_scores[sect];
                        sections[sect][stu].std_avg = avg;
                        
                        debugf(DEBUG_LEVEL_LOGIC, "completed processing scores for student %d\n", sections[sect][stu].id);
                }
        }

        return sections;
}

/*
   Spec:
   This function takes the array of courses produced and filled by all the courses
   of a test case and also takes the size of the array. Then it displays the
   required data in the same format discussed in the sample output. You can write
   and use more functions in this process as you wish.

   Translation: calculates course statistics and prints the apropriate output
 */
void process_courses(course *courses, int num_courses){

/*
   Output specification:

   course_name pass_count list_of_averages_section(separated by space up to two decimal places) id lname avg_score (up to two decimal places)

   test case 1
   cs1 2 70.41 74.15 105 edward 83.07 math2 5 72.90 74.15 76.72 110 kyle 94.35 physics3 2 71.12 105 edward 79.35
 */

}

/*
   Spec:
   This function takes the array of courses produced and filled by all the
   courses of a test case and also takes the size of the array. Then it free up
   all the memory allocated within it. You can create more function as needed to
   ease the process.
 */

// N.B. anything not explicitly removed by release_courses will be garbage collected
// in the memory manager's cleanup routine.
void release_courses(course *courses, int num_courses){
        debugf(DEBUG_LEVEL_LOGIC, "releasing courses\n");

        for(int i = 0; i < num_courses; i++) {

                debugf(DEBUG_LEVEL_MMGR, "releasing course %d\n", i);

                for(int j = 0; j < courses[i].num_sections; j++) {

                        debugf(DEBUG_LEVEL_MMGR, "releasing course %d section %d\n", i, j);
                        debugf(DEBUG_LEVEL_MMGR, "%d students to release in course %d section %d\n", courses[i].num_students[j], i, j);

                        for(int k = 0; k < courses[i].num_students[j]; k++) {
                                debugf(DEBUG_LEVEL_MMGR, "LOOP %d\n", k);
                                mmgr_free(g_MEM, courses[i].sections[j][k].scores);
                                debugf(DEBUG_LEVEL_MMGR, "released course %d section %d student %d scores\n", i, j, k);
                                mmgr_free(g_MEM, courses[i].sections[j][k].lname);
                                debugf(DEBUG_LEVEL_MMGR, "released course %d section %d student %d lname\n", i, j, k);
                        }

                }

                mmgr_free(g_MEM, courses[i].sections);
                debugf(DEBUG_LEVEL_MMGR, "released course %d sections array\n", i);

                mmgr_free(g_MEM, courses[i].course_name);
                debugf(DEBUG_LEVEL_MMGR, "released course %d name\n", i);

                mmgr_free(g_MEM, courses[i].num_students);
                debugf(DEBUG_LEVEL_MMGR, "released course %d num_students\n", i);

                mmgr_free(g_MEM, courses[i].num_scores);
                debugf(DEBUG_LEVEL_MMGR, "released course %d num_scores\n", i);
        }
}

////////////////////////// Memory manager //////////////////////////

// This is the initial version of my C memory manager.
// At the moment it's very rudimentary.

// Initializes the memory manager's global state table. This tracks all allocated
// memory, reallocates freed entries, and ensures that all allocated memory is
// completely cleaned up on program termination.
// In the future, I'll spawn a worker thread to performs automated garbage
// collection through mark and sweep or simple refcounting.
MMGR *mmgr_init(){
        MMGR *state_table = calloc(1, sizeof(MMGR));

        state_table->free = NULL;
        state_table->numFree = 0;

        state_table->entries = NULL;
        state_table->numEntries = 0;

        state_table->mutex = 0;

        debugf(DEBUG_LEVEL_MMGR, "mmgr: initialized\n");

        return state_table;
}

// Allocate memory and maintain a reference in the memory manager's state table
// If any previously allocated entries have since been freed, these will be
// resized and reallocated to serve the request
void *mmgr_malloc(MMGR *tbl, size_t size){
        if(tbl == NULL)
                return NULL;

        mmgr_mutex_acquire(tbl);

        void* handle = NULL;

        // If freed entries are available, we can recycle them
        if(tbl->numFree > 0) {
                int tgt_idx = tbl->free[tbl->numFree - 1];

                debugf(DEBUG_LEVEL_MMGR, "mmgr: found reusable previously allocated entry %d\n", tgt_idx);

                tbl->entries[tgt_idx]->size = size;
                tbl->entries[tgt_idx]->handle = malloc(size);
                memset(tbl->entries[tgt_idx]->handle, 0, size);

                handle = tbl->entries[tgt_idx]->handle;

                tbl->free = (int*) realloc(tbl->free, (sizeof(int) * (tbl->numFree - 1)));

                tbl->numFree--;

                debugf(DEBUG_LEVEL_MMGR, "mmgr: reallocated %lu bytes\n", size);

                // Otherwise the state table will need to be resized to accommodate a new
                // entry
        } else {
                debugf(DEBUG_LEVEL_MMGR, "mmgr: no recyclable entries available, increasing table size\n");

                tbl->entries = (MMGR_Entry**) realloc(tbl->entries, (sizeof(MMGR_Entry*) * (tbl->numEntries + 1)));

                tbl->entries[tbl->numEntries] = (MMGR_Entry*) calloc(1, sizeof(MMGR_Entry) + sizeof(void*));

                tbl->entries[tbl->numEntries]->handle = malloc(size);
                memset(tbl->entries[tbl->numEntries]->handle, 0, size);

                handle = tbl->entries[tbl->numEntries]->handle;
                tbl->entries[tbl->numEntries]->size = size;

                tbl->numEntries++;

                debugf(DEBUG_LEVEL_MMGR, "mmgr: allocated %lu bytes, handle is %p\n", size, handle);
        }

        mmgr_mutex_release(tbl);

        return handle;
}


// Frees the provided pointer and checks out the active entry in the g_MEM
// state table so that it can be reallocated
void mmgr_free(MMGR *tbl, void* handle){
        if(tbl == NULL)
                return;

        mmgr_mutex_acquire(tbl);

        int found = 0;

        debugf(DEBUG_LEVEL_MMGR, "mmgr: num active entries is %d, called to free %p\n", (tbl->numEntries - tbl->numFree), handle);

        for(int i = tbl->numEntries; i--> 0; ) {
                if(handle == NULL) {
                        debugf(DEBUG_LEVEL_MMGR, "mmgr: provided NULL handle! no-op\n");
                        break;
                }

                if(tbl->entries[i]->handle == handle) {
                        debugf(DEBUG_LEVEL_MMGR, "mmgr: found handle %p at index %d\n", handle, i);

                        tbl->numFree++;

                        free(tbl->entries[i]->handle);
                        tbl->entries[i]->handle = NULL;
                        tbl->entries[i]->size = 0;

                        tbl->free = (int*) realloc(tbl->free, (sizeof(int) * (tbl->numFree + 1)));
                        tbl->free[tbl->numFree - 1] = i;

                        found = 1;

                        debugf(DEBUG_LEVEL_MMGR, "mmgr: freed %p at index %d, %d entries remain active\n", handle, i, (tbl->numEntries - tbl->numFree));
                        break;
                }
        }

        if(found == 0)
                debugf(DEBUG_LEVEL_MMGR, "mmgr: called to free %p but couldn't find it, no-op\n", handle);

        mmgr_mutex_release(tbl);
}

// Cleans up any remaining allocated memory, then frees the state table
void mmgr_cleanup(MMGR *tbl){
        if(tbl == NULL)
                return;

        mmgr_mutex_acquire(tbl);

        int deEn = 0;

        debugf(DEBUG_LEVEL_MMGR, "mmgr: cleaning up %d active entries\n", (tbl->numEntries - tbl->numFree));

        // Free all active entries and what they're pointing to
        for(int i = 0; i < tbl->numEntries; i++) {
                if(tbl->entries[i] != NULL) {
                        if(tbl->entries[i]->handle != NULL)
                                free(tbl->entries[i]->handle);

                        free(tbl->entries[i]);
                        deEn++;
                }
        }

        debugf(DEBUG_LEVEL_MMGR, "mmgr: cleanup deallocd %d entries\n", deEn);

        free(tbl->entries);
        free(tbl->free);
        free(tbl);

        tbl = NULL;
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
