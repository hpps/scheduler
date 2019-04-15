#include <stdio.h>
#include "utlist.h"
#include "utils.h"

#include "memory_controller.h"

extern long long int CYCLE_VAL;

// these are all tunables
#define QUANTUM_SIZE 100000
#define EPOCH_SIZE 5000
#define MOPL 0.3      // MOPL * PLNUM = 1 (ideally)
#define PLNUM 3       // we will try 4 as well

#define TRUE 1
#define FALSE 0

#include <math.h>
#include <unistd.h>
#include "params.h"

#define MAX_NUMCORES 16

int Ncore = MAX_NUMCORES;
//int Nchannel = NUM_CHANNELS;

int quantum_counter;
int epoch_counter;

int TotalReqCnt;
int IniPri[MAX_NUMCORES][MAX_NUM_CHANNELS];
int DynPri[MAX_NUMCORES][MAX_NUM_CHANNELS];
int ReqCnt[MAX_NUMCORES];
int EReqCnt[MAX_NUMCORES][MAX_NUM_CHANNELS];
int QReqCnt[MAX_NUMCORES][MAX_NUM_CHANNELS];

int NxtGroup[MAX_NUMCORES]; 
int PreGroup[MAX_NUMCORES];
int CurGroup[MAX_NUMCORES];

int ReqPL;
int GroupTh;

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

    for(i = 0; i < Ncore; i++) {
        ReqCnt[i] = 0;
        for(j = 0; j < NUM_CHANNELS; j++) {
            ReqCnt[i] += QReqCnt[i][j];
        }
        TotalReqCnt += ReqCnt[i];
    }

    GroupTh = (TotalReqCnt * MOPL)/Ncore;

    for(i = 0; i < Ncore; i++) {
        if (ReqCnt[i] < GroupTh) {
            CurGroup[i] = 0; 
        }
        else {
            CurGroup[i] = 1;
        }
        NxtGroup[i] = PreGroup[i] && CurGroup[i];
        PreGroup[i] = CurGroup[i];
    }
    ReqPL = TotalReqCnt * MOPL * EPOCH_SIZE / QUANTUM_SIZE / Ncore / NUM_CHANNELS;

}

void prioritization_algorithm()
{
    //(1) Highest level first: Requests from applications at higher priority levels are prioritized.
    //—The priority level of latency-sensitive applications is higher than that of bandwidth-sensitive applications by one level initially.
    //—The priority of an application is lowered by one level when the number of served read requests is over predetermined thresholds.

    //(2) Row-hit first: Row-hit requests are prioritized over row-closed and row-conflict requests.

    //(3) Oldest first: Older requests are prioritized over younger requests.

}

void schedule(int channel)
{

    int i;
    int j = channel;

    request_t * curr = NULL;

    if (quantum_counter == 0) {

        // Initialization at the beginning of every quantum
        for(i = 0; i < Ncore; i++) {
            QReqCnt[i][j] = 0;
            EReqCnt[i][j] = 0;
            IniPri[i][j] = PLNUM - NxtGroup[i];
        }
    }


    if (channel == 0) {
        quantum_counter++;
        epoch_counter++;
    }

//    printf("channel = %d, quantum = %d, epoch = %d\n", j, quantum_counter, epoch_counter);

    //counting
    for(int j=0; j<NUM_CHANNELS; j++){
        LL_FOREACH(read_queue_head[j],curr){
            for(int i=0; i<NUMCORES; i++){
                printf("%d.%d: thread_id = %d,", i, j, curr->thread_id);
                printf("request_served = %d\n", curr->request_served);
            }
        }
    }


    // Memory Occupancy Monitor
    for(i = 0; i < Ncore; i++) {
        LL_FOREACH(read_queue_head[channel], curr) {
//            printf("channel %d: read_queue_head[%d] = %x\n", channel, channel, read_queue_head[channel]);
            if (read_queue_head[channel]->request_served == TRUE) {
//                printf("TRUE\n");
                QReqCnt[i][j]++;
                EReqCnt[i][j]++;
            }
            else {
//                printf("FALSE\n");
            }
        }
    }

    // Multi-Level Comparator
    for(i = 0; i < Ncore; i++) {
        if (EReqCnt[i][j] < ReqPL) {
            DynPri[i][j] = IniPri[i][j]; // Dynamic priority
        }
        else if (EReqCnt[i][j] >= (PLNUM-1)*ReqPL) {
            DynPri[i][j] = 1;
        }
        else {
            DynPri[i][j] = PLNUM - floor(EReqCnt[i][j]/ReqPL);
        }
    }

    // Cleared every epoch
    if(epoch_counter == EPOCH_SIZE) {

        prioritization_algorithm();

        for(i = 0; i < Ncore; i++) {
            EReqCnt[i][j] = 0;
            DynPri[i][j] = IniPri[i][j];
        }

        epoch_counter = 0;
    }

     
//    printf("channel = %d\n", j);
    if (quantum_counter == QUANTUM_SIZE) {

        printf("j = %d, NUM_CHANNELS-1 = %d\n", j, NUM_CHANNELS-1);
        for(i = 0; i < Ncore; i++) {
            printf("QReqCnt[%d][%d] = %d; ", i, j, QReqCnt[i][j]);
            printf("EReqCnt[%d][%d] = %d\n", i, j, EReqCnt[i][j]);

        }

        sleep(10);

        if (j == NUM_CHANNELS-1) {
            grouping_algorithm();
            quantum_counter = 0;
        }

    }

}

void scheduler_stats()
{
  /* Nothing to print for now. */
}
