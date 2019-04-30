#include <stdio.h>
#include "utlist.h"
#include "utils.h"

#include "memory_controller.h"

extern long long int CYCLE_VAL;

// these are all tunables
#define QUANTUM_SIZE 1000000
#define EPOCH_SIZE 5000
#define MOPL 0.3      // MOPL * PLNUM = 1 (ideally)
#define PLNUM 3       // we will try 4 as well

#define TRUE 1
#define FALSE 0

#include <math.h>
#include <unistd.h>
#include "params.h"
#include <limits.h>

#define MAX_NUMCORES 16

//int Ncore = NUMCORES;
//int Nchannel = NUM_CHANNELS;

int quantum_counter;
int epoch_counter;

int TotalReqCnt;
int IniPri[MAX_NUMCORES][MAX_NUM_CHANNELS];
int DynPri[MAX_NUMCORES][MAX_NUM_CHANNELS];
int ReqCnt[MAX_NUMCORES];
int EReqCnt[MAX_NUMCORES][MAX_NUM_CHANNELS];
int QReqCnt[MAX_NUMCORES][MAX_NUM_CHANNELS];

int row_hit[MAX_NUMCORES][MAX_NUM_CHANNELS];

int NxtGroup[MAX_NUMCORES]; 
int PreGroup[MAX_NUMCORES];
int CurGroup[MAX_NUMCORES];

int ReqPL;
int GroupTh;

long long last_quantum = -1000000;
long long last_epoch = -5000;

void init_scheduler_vars()
{
    // initialize all scheduler variables here

        quantum_counter = 0;
        epoch_counter = 0;

    return;
}

// write queue high water mark; begin draining writes if write queue exceeds this value
#define HI_WM 40

// end write queue drain once write queue has this many writes in it
#define LO_WM 20

// 1 means we are in write-drain mode for that channel
int drain_writes[MAX_NUM_CHANNELS];

/* Each cycle it is possible to issue a valid command from the read or write queues
   OR
   a valid precharge command to any bank (issue_precharge_command())
   OR 
   a valid precharge_all bank command to a rank (issue_all_bank_precharge_command())
   OR
   a power_down command (issue_powerdown_command()), programmed either for fast or slow exit mode
   OR
   a refresh command (issue_refresh_command())
   OR
   a power_up command (issue_powerup_command())
   OR
   an activate to a specific row (issue_activate_command()).

   If a COL-RD or COL-WR is picked for issue, the scheduler also has the
   option to issue an auto-precharge in this cycle (issue_autoprecharge()).

   Before issuing a command it is important to check if it is issuable. For the RD/WR queue resident commands, checking the "command_issuable" flag is necessary. To check if the other commands (mentioned above) can be issued, it is important to check one of the following functions: is_precharge_allowed, is_all_bank_precharge_allowed, is_powerdown_fast_allowed, is_powerdown_slow_allowed, is_powerup_allowed, is_refresh_allowed, is_autoprecharge_allowed, is_activate_allowed.
   */

void grouping_algorithm() {

    int i, j;

    TotalReqCnt = 0;

    for(i = 0; i < NUMCORES; i++) {
        ReqCnt[i] = 0;
        for(j = 0; j < NUM_CHANNELS; j++) {
            ReqCnt[i] += QReqCnt[i][j];
//            printf("QReqCnt[%d][%d] = %d; ", i, j, QReqCnt[i][j]);
        }
        TotalReqCnt += ReqCnt[i];
//        printf("\n");
    }

    GroupTh = (TotalReqCnt * MOPL)/NUMCORES;

    for(i = 0; i < NUMCORES; i++) {
        if (ReqCnt[i] < GroupTh) {
            CurGroup[i] = 0; 
        }
        else {
            CurGroup[i] = 1;
        }
        NxtGroup[i] = PreGroup[i] && CurGroup[i];
        PreGroup[i] = CurGroup[i];
    }
//    ReqPL = TotalReqCnt * MOPL * EPOCH_SIZE / QUANTUM_SIZE / Ncore / NUM_CHANNELS;
    ReqPL = (int)((float)TotalReqCnt*MOPL*(float)EPOCH_SIZE/(float)QUANTUM_SIZE/(float)NUMCORES/(float)NUM_CHANNELS);
    printf("ReqPL = %d = %d * %f *  %d / %d / %d / %d\n\n", ReqPL, TotalReqCnt, MOPL, EPOCH_SIZE, QUANTUM_SIZE, NUMCORES, NUM_CHANNELS);
//    sleep(1);

}

void schedule(int channel)
{

    int i;
    int j;

    request_t * rd_ptr = NULL;
    request_t * wr_ptr = NULL;

    request_t * rd_next = NULL;

    int highest_pri;
    int row_hit_found;

    long long int earliest_time;

    // Cleared every epoch
    if(CYCLE_VAL - last_epoch >= EPOCH_SIZE) {
        printf("%lld\n", CYCLE_VAL);

        for(j = 0; j < NUM_CHANNELS; j++) {
//            printf("EReqCnt[j=%d] = ", j);
            printf("DynPri[j=%d] = ", j);
            for(i = 0; i < NUMCORES; i++) {
//                printf("%d ", EReqCnt[i][j]);
                printf("%d/%d ", DynPri[i][j],row_hit[i][j]);
                row_hit[i][j] = 0;
                EReqCnt[i][j] = 0;
                DynPri[i][j] = IniPri[i][j];
            }
            printf("\n");
        }
        printf("\n");

//        if (j == 0) {
            last_epoch = floor(CYCLE_VAL/EPOCH_SIZE)*EPOCH_SIZE;
//        }
    }
    // Cleared every quantum
    if (CYCLE_VAL - last_quantum >= QUANTUM_SIZE) {

        grouping_algorithm();
        for(j = 0; j < NUM_CHANNELS; j++) {
            printf("QReqCnt[j=%d] = ", j);
            for(i = 0; i < NUMCORES; i++) {
                printf("%d ", QReqCnt[i][j]);
                QReqCnt[i][j] = 0;
                IniPri[i][j] = PLNUM - NxtGroup[i];
            }
            printf("\n");
        }
        printf("\n");
//        sleep(1);

//        if (j == 0) {
            last_quantum = floor(CYCLE_VAL/QUANTUM_SIZE)*QUANTUM_SIZE;
//        }
        // Initialization at the beginning of every quantum
    }
//    sleep(1);




     

/*
    //counting
    for(int j=0; j<NUM_CHANNELS; j++){
        LL_FOREACH(read_queue_head[j],rd_ptr){
            for(int i=0; i<NUMCORES; i++){
                printf("%d.%d: thread_id = %d,", i, j, rd_ptr->thread_id);
                printf("request_served = %d\n", rd_ptr->request_served);
            }
        }
    }

*/

    // Memory Occupancy Monitor
//    for(i = 0; i < Ncore; i++) {

//        int k = 0;
        LL_FOREACH(read_queue_head[channel], rd_ptr) {
//            printf("CV=%d, l_e=%lld, l_q=%lld, ch %d: %x @ %lld\n", CYCLE_VAL, last_epoch, last_quantum, channel, rd_ptr, rd_ptr->arrival_time);
//            printf("k = %d, rd_ptr->thread_id = %d\n", k++, rd_ptr->thread_id);
//            printf("channel %d: read_queue_head[%d] = %0x\n", channel, channel, read_queue_head[channel]);
            if (read_queue_head[channel]->request_served == TRUE) {
 //               printf("TRUE\n");
                QReqCnt[rd_ptr->thread_id][channel]++;
                EReqCnt[rd_ptr->thread_id][channel]++;
            }
            else {
//                printf("FALSE\n");
            }

            if (rd_ptr->next_command == COL_READ_CMD) {
                row_hit[rd_ptr->thread_id][channel] = 1;
            }
        }
//        printf("---\n", channel, rd_ptr);
//        sleep(1);
 //   }

    // Multi-Level Comparator
    for(i = 0; i < NUMCORES; i++) {
        if (EReqCnt[i][channel] < ReqPL) {
            DynPri[i][channel] = IniPri[i][channel]; // Dynamic priority
        }
        else if (EReqCnt[i][channel] >= (PLNUM-1)*ReqPL) {
            DynPri[i][channel] = 1;
        }
        else {
            DynPri[i][channel] = PLNUM - floor(EReqCnt[i][channel]/ReqPL);
        }
    }


    // ALGORITHM 3: PRIORITIZATION
    
    // if in write drain mode, keep draining writes until the
    // write queue occupancy drops to LO_WM
    if (drain_writes[channel] && (write_queue_length[channel] > LO_WM)) {
      drain_writes[channel] = 1; // Keep draining.
    }
    else {
      drain_writes[channel] = 0; // No need to drain.
    }

    // initiate write drain if either the write queue occupancy
    // has reached the HI_WM , OR, if there are no pending read
    // requests
    if(write_queue_length[channel] > HI_WM)
    {
        drain_writes[channel] = 1;
    }
    else {
      if (!read_queue_length[channel])
        drain_writes[channel] = 1;
    }


    // If in write drain mode, look through all the write queue
    // elements (already arranged in the order of arrival), and
    // issue the command for the first request that is ready
    if(drain_writes[channel])
    {

        LL_FOREACH(write_queue_head[channel], wr_ptr)
        {
            if(wr_ptr->command_issuable)
            {
                issue_request_command(wr_ptr);
                break;
            }
        }
        return;
    }

    // Draining Reads
    // look through the queue and find the first request whose
    // command can be issued in this cycle and issue it 
    // Simple FCFS 
    if(!drain_writes[channel])
    {
        rd_next = NULL;
        earliest_time = LLONG_MAX;
        highest_pri = 0;
        row_hit_found = 0;

        LL_FOREACH(read_queue_head[channel],rd_ptr)
        {
            if(rd_ptr->command_issuable)
            {
                if(DynPri[rd_ptr->thread_id][channel] > highest_pri)
                {
                    highest_pri = DynPri[rd_ptr->thread_id][channel];
                    rd_next = rd_ptr;
                    earliest_time = rd_ptr->arrival_time;
                    row_hit_found = 0;
//                    printf("thread %d got priority\n", rd_ptr->thread_id);
                }
                if(row_hit[rd_ptr->thread_id][channel] == 1)
                {
                    row_hit_found = 1;
                    if(rd_ptr->arrival_time < earliest_time)    
                    {
                        rd_next = rd_ptr;
                        earliest_time = rd_ptr->arrival_time;
//                        printf("thread %d got priority\n", rd_ptr->thread_id);
                    }
                }
                else if(row_hit_found == 0)
                {
                    if(rd_ptr->arrival_time < earliest_time)    
                    {
                        rd_next = rd_ptr;
                        earliest_time = rd_ptr->arrival_time;
//                        printf("thread %d got priority\n", rd_ptr->thread_id);
                    }
                }
//                printf("      thr i=%d, ch j=%d: pri=%d, row_hit=%d, time=%lld\n", rd_ptr->thread_id, channel, DynPri[rd_ptr->thread_id][channel], row_hit[rd_ptr->thread_id][channel], rd_ptr->arrival_time);
            }
        }

        if(rd_next != NULL)
        {
//            printf("NEXT: thr i=%d,\tch j=%d:\tpri=%d,\trow_hit=%d,\ttime=%lld\n", rd_next->thread_id, channel, DynPri[rd_next->thread_id][channel], row_hit[rd_next->thread_id][channel], rd_next->arrival_time);
            issue_request_command(rd_next);
        }
    }

//    printf("channel = %d, quantum = %d, epoch = %d\n", j, quantum_counter, epoch_counter);


    return;
}

void scheduler_stats()
{
  /* Nothing to print for now. */
}
