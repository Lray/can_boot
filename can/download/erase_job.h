#ifndef ERASE_JOB_H
#define ERASE_JOB_H

#include <stdint.h>

typedef enum
{
    ERASE_JOB_STATUS_PENDING = 0,
    ERASE_JOB_STATUS_COMPLETE,
    ERASE_JOB_STATUS_FAILED,
} erase_job_status_t;

/** Clear the RAM-resident Flash erase job. */
void EraseJob_Reset(void);

/** Start erasing the selected application slot in full. */
int EraseJob_Start(uint8_t target_slot);

/** Erase one mapped Flash sector and report the job result. */
erase_job_status_t EraseJob_Poll(void);

#endif /* ERASE_JOB_H */
