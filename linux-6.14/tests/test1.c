// // SPDX-License-Identifier: GPL-2.0-only
// #include <fcntl.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/mman.h>
// #include <time.h>
// #include <unistd.h>

// #define STR_SIZE (1ULL << 8)
// // 2^39 bytes για 48-bit address space, όπως ορίσαμε στο module
// #define EPT_SIZE (1ULL << 39) 
// // MEM_SIZE: Πρέπει να είναι αρκετά μεγάλο για να καλύψει το μήνυμα (STR_SIZE * 8 σελίδες)
// #define MEM_SIZE (STR_SIZE << 15) 

// #define PERR_RET(cond, str) \
//     do {                    \
//         if (cond) {         \
//             perror(str);    \
//             return 1;       \
//         }                   \
//     } while (0)             \

// /**
//  * decode - Ανακατασκευάζει το μήνυμα διαβάζοντας τα PTEs από το EPT.
//  */
// void decode(char *msg, char *ptr, unsigned long *ept) {
//     for (size_t i = 0; i < STR_SIZE; i++) {
//         msg[i] = 0;
//         for (int j = 0; j < 8; j++) {
//             // Υπολογίζουμε τη VA της σελίδας που αντιστοιχεί στο j-οστό bit του i-οστού χαρακτήρα
//             unsigned long va = (unsigned long)&ptr[(8 * i + j) << 12];
            
//             // Διαβάζουμε το PTE από το "flat table" χρησιμοποιώντας τη VA ως index
//             unsigned long pte = ept[va >> 12];
            
//             // Αν το PTE δεν είναι 0, σημαίνει ότι η σελίδα έχει γίνει allocate/mapped
//             // (άρα το bit στην encode ήταν 1)
//             if (pte != 0) {
//                 msg[i] |= (1 << j);
//             }
//         }
//     }
// }

// void encode(char *msg, char *mem) {
//     for (size_t i = 0; i < STR_SIZE; i++)
//         for (int j = 0; j < 8; j++)
//             if (msg[i] & (1 << j))
//                 mem[(8 * i + j) << 12] = 0x10;
// }

// void encode2(char *msg, char *mem) {
//     for (size_t i = 0; i < STR_SIZE; i++) {
//         for (int j = 0; j < 8; j++) {
//             mem[(8 * i + j) << 12] = 0x10;
//             if ((msg[i] & (1 << j)) == 0)
//                 munmap(&mem[(8 * i + j) << 12], 1ULL << 12);
//         }
//     }
// }

// void init(char *msg, size_t len) {
//     srand(time(NULL));
//     for (int i = 0; i < len; i++)
//         msg[i] = rand() % 255;
// }

// int main(void) {
//     char buf[STR_SIZE] = {0}, buf2[STR_SIZE] = {0}, buf3[STR_SIZE] = {0};

//     // Mapping για την πρώτη κωδικοποίηση (Demand Paging)
//     char *ptr1 = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//     PERR_RET(ptr1 == MAP_FAILED, "mmap ptr1");
//     madvise(ptr1, MEM_SIZE, MADV_NOHUGEPAGE);

//     // Mapping για τη δεύτερη κωδικοποίηση (Unmapping)
//     char *ptr2 = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//     PERR_RET(ptr2 == MAP_FAILED, "mmap ptr2");
//     madvise(ptr2, MEM_SIZE, MADV_NOHUGEPAGE);

//     int fd_ept = open("/dev/ept", O_RDONLY);
//     PERR_RET(fd_ept < 0, "open /dev/ept (run as root!)");

//     // Mapping του Flat Page Table από το module μας
//     unsigned long *ept = mmap(NULL, EPT_SIZE, PROT_READ, MAP_PRIVATE, fd_ept, 0);
//     PERR_RET(ept == MAP_FAILED, "mmap ept");
//     close(fd_ept);

//     init(buf, STR_SIZE);

//     // Δοκιμή 1: Encode μέσω εγγραφής (Demand Paging)
//     encode(buf, ptr1);
    
//     // Επιβολή TLB Flush για το EPT mapping
//     mprotect(ept, EPT_SIZE, PROT_NONE);
//     mprotect(ept, EPT_SIZE, PROT_READ);
    
//     decode(buf2, ptr1, ept);

//     // Δοκιμή 2: Encode μέσω αφαίρεσης σελίδων (Unmapping)
//     encode2(buf, ptr2);
    
//     // Επιβολή TLB Flush ξανά
//     mprotect(ept, EPT_SIZE, PROT_NONE);
//     mprotect(ept, EPT_SIZE, PROT_READ);
    
//     decode(buf3, ptr2, ept);

//     // Έλεγχος αν το αρχικό μήνυμα ταυτίζεται με τα αποκωδικοποιημένα
//     if (memcmp(buf, buf2, STR_SIZE) || memcmp(buf, buf3, STR_SIZE))
//         printf("error\n");
//     else
//         printf("success\n");

//     munmap(ept, EPT_SIZE);
//     munmap(ptr1, MEM_SIZE);
//     munmap(ptr2, MEM_SIZE);

//     return 0;
// }


// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define STR_SIZE (1ULL << 8)
#define EPT_SIZE (1ULL << 39)  // 48-bit virtual address space
#define MEM_SIZE (STR_SIZE << (12))

#define PERR_RET(cond, str) \
    do {                    \
        if (cond) {         \
            perror(str);    \
            return 1;       \
        }                   \
    } while (0)             \

void decode(char *msg, char *ptr, unsigned long *ept) {
    for (size_t i = 0; i < STR_SIZE; i++) {
        msg[i] = 0;
        for (int j = 0; j < 8; j++) {
            // Ο σωστός υπολογισμός της VA που έγινε encode
            unsigned long va = (unsigned long)&ptr[(8 * i + j) << 12];
            // Αν το PTE στον flat table δεν είναι 0, το bit είναι 1
            if (ept[va >> 12] != 0) {
                msg[i] |= (1 << j);
            }
        }
    }
}

void encode(char *msg, char *mem) {
    for (size_t i = 0; i < STR_SIZE; i++)
        for (int j = 0; j < 8; j++)
            if (msg[i] & (1 << j))
                mem[(8 * i + j) << 12] = 0x10;
}

void encode2(char *msg, char *mem) {
    for (size_t i = 0; i < STR_SIZE; i++) {
        for (int j = 0; j < 8; j++) {
            mem[(8 * i + j) << 12] = 0x10;
            if ((msg[i] & (1 << j)) == 0)
                munmap(&mem[(8 * i + j) << 12], 1ULL << 12);
        }
    }
}

void init(char *msg, size_t len) {
    srand(time(NULL));
    for (int i = 0; i < len; i++)
        msg[i] = rand() % 255;
}


int main(void) {
    char buf[STR_SIZE] = {0}, buf2[STR_SIZE] = {0}, buf3[STR_SIZE] = {0};

    char *ptr1 = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    PERR_RET(ptr1 == MAP_FAILED, "mmap");

    char *ptr2 = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    PERR_RET(ptr2 == MAP_FAILED, "mmap");

    int fd_ept = open("/dev/ept", O_RDONLY);
    PERR_RET(fd_ept < 0, "open");

    unsigned long *ept = mmap(NULL, EPT_SIZE, PROT_READ, MAP_PRIVATE, fd_ept, 0);
    PERR_RET(ept == MAP_FAILED, "mmap");
    PERR_RET(close(fd_ept), "close");

    PERR_RET(mprotect(ept, EPT_SIZE, PROT_NONE), "mprotect");
    PERR_RET(mprotect(ept, EPT_SIZE, PROT_READ), "mprotect");

    init(buf, STR_SIZE);

    encode(buf, ptr1);
    decode(buf2, ptr1, ept);

    encode2(buf, ptr2);
    decode(buf3, ptr2, ept);

    if (memcmp(buf, buf2, STR_SIZE) || memcmp(buf, buf3, STR_SIZE))
        printf("error\n");
    else
        printf("success\n");

    PERR_RET(munmap(ept, EPT_SIZE), "munmap");
    PERR_RET(munmap(ptr1, MEM_SIZE), "munmap");
    PERR_RET(munmap(ptr2, MEM_SIZE), "munmap");

    return 0;
}