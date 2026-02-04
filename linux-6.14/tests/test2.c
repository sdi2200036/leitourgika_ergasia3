// // SPDX-License-Identifier: GPL-2.0-only
// #include <fcntl.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <sys/mman.h>
// #include <time.h>
// #include <unistd.h>

// /* * Ορίζουμε X=20 για να έχουμε ένα mapping 1MB (2^20 bytes).
//  * Αυτό είναι αρκετό για να καλύψει πολλαπλές σελίδες και να ελέγξουμε τα PTEs.
//  */
// #define X 20

// #define PERR_RET(cond, str) \
//     do {                    \
//         if (cond) {         \
//             perror(str);    \
//             return 1;       \
//         }                   \
//     } while (0)             \

// static inline uint64_t clock_gettime_ns() {
//     struct timespec ts;
//     clock_gettime(CLOCK_MONOTONIC, &ts);
//     return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
// }

// int main(void) {
//     uint64_t start_time, end_time;

//     // Άνοιγμα του device που υλοποιήσαμε στον kernel
//     int ept_fd = open("/dev/ept", O_RDONLY);
//     PERR_RET(ept_fd < 0, "open /dev/ept - Are you root?");

//     /* * Mapping του EPT flat table. 
//      * 1ULL << 39 αντιστοιχεί στο μέγεθος για 48-bit εικονικό χώρο διευθύνσεων.
//      */
//     unsigned long *ept = mmap(NULL, 1ULL << 39, PROT_READ, MAP_PRIVATE, ept_fd, 0);
//     PERR_RET(ept == MAP_FAILED, "mmap ept");
//     PERR_RET(close(ept_fd), "close");

//     /* * Δέσμευση ανώνυμης μνήμης χωρίς άμεσο allocation (lazy allocation).
//      * Χρησιμοποιούμε MADV_NOHUGEPAGE για να αποφύγουμε τα huge pages που αγνοεί το module μας.
//      */
//     char *p = mmap(NULL, 1ULL << X, PROT_READ | PROT_WRITE,
//                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
//     PERR_RET(p == MAP_FAILED, "mmap p");
//     madvise(p, 1ULL << X, MADV_NOHUGEPAGE);

//     // 1. Πρώτη ανάγνωση μέσω EPT: Προκαλεί το πρώτο Page Fault στον ept_fault.
//     start_time = clock_gettime_ns();
//     unsigned long pte = ept[(unsigned long)p >> 12];
//     end_time = clock_gettime_ns();
//     printf("1. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

//     // 2. Δεύτερη ανάγνωση: Η τιμή είναι πλέον cached στο TLB, άρα ο χρόνος θα είναι ελάχιστος.
//     start_time = clock_gettime_ns();
//     pte = ept[(unsigned long)p >> 12];
//     end_time = clock_gettime_ns();
//     printf("2. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

//     /* * 3. Εγγραφή στη μνήμη: Προκαλεί page fault στην 'p', άρα ο kernel 
//      * θα κάνει allocate μια φυσική σελίδα και θα ενημερώσει το PTE.
//      */
//     p[0] = 'C';

//     /* * ΕΠΙΒΟΛΗ ΣΥΓΧΡΟΝΙΣΜΟΥ: 
//      * Επειδή στον πυρήνα χρησιμοποιούμε trylock, αναγκάζουμε το hardware να κάνει flush 
//      * το TLB του EPT mapping χειροκίνητα. Έτσι, η επόμενη ανάγνωση θα αναγκάσει 
//      * τον ept_fault να τρέξει ξανά και να βρει το νέο PTE.
//      */
//     mprotect(ept, 1ULL << 39, PROT_NONE);
//     mprotect(ept, 1ULL << 39, PROT_READ);

//     // 4. Τρίτη ανάγνωση: Πρέπει να δούμε υψηλό χρόνο και την ενημερωμένη τιμή (PFN != 0).
//     start_time = clock_gettime_ns();
//     pte = ept[(unsigned long)p >> 12];
//     end_time = clock_gettime_ns();
//     printf("3. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

//     // 5. Τέταρτη ανάγνωση: Ξανά cached τιμή.
//     start_time = clock_gettime_ns();
//     pte = ept[(unsigned long)p >> 12];
//     end_time = clock_gettime_ns();
//     printf("4. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

//     // Cleanup
//     PERR_RET(munmap(p, 1ULL << X), "munmap p");
//     PERR_RET(munmap(ept, 1ULL << 39), "munmap ept");
    
//     return 0;
// }

// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define X 30

#define PERR_RET(cond, str) \
    do {                    \
        if (cond) {         \
            perror(str);    \
            return 1;       \
        }                   \
    } while (0)             \

static inline uint64_t clock_gettime_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(void) {
    uint64_t start_time, end_time;

    int ept_fd = open("/dev/ept", O_RDONLY);
    PERR_RET(ept_fd < 0, "open");

    unsigned long *ept = mmap(NULL, 1ULL << 39, PROT_READ, MAP_PRIVATE, ept_fd, 0);
    PERR_RET(ept == MAP_FAILED, "mmap");
    PERR_RET(close(ept_fd), "close");

    char *p = mmap(NULL, 1ULL << X, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    PERR_RET(p == MAP_FAILED, "mmap");
    madvise(p, 1ULL << X, MADV_NOHUGEPAGE);

    start_time = clock_gettime_ns();
    unsigned long pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("1. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    start_time = clock_gettime_ns();
    pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("2. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    p[0] = 'C';
    start_time = clock_gettime_ns();
    pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("3. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    start_time = clock_gettime_ns();
    pte = ept[(unsigned long)p >> 12];
    end_time = clock_gettime_ns();
    printf("4. Time: %lu (ns) -> <0x%lx>\n", end_time - start_time, pte);

    PERR_RET(munmap(p, 1ULL << X), "munmap");
    PERR_RET(munmap(ept, 1ULL << 39), "munmap");
    return 0;
}