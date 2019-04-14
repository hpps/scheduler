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

#define MAX_NUMCORES 16

int Ncore = MAX_NUMCORES;
int Nchannel = MAX_NUM_CHANNELS;

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
        for(j = 0; j < Nchannel; j++) {
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
    ReqPL = TotalReqCnt * MOPL * EPOCH_SIZE / QUANTUM_SIZE / Ncore / Nchannel;

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

    if (quantum_counter == 0) {

        // Initialization at the beginning of every quantum
        for(i = 0; i < Ncore; i++) {
            QReqCnt[i][j] = 0;
            EReqCnt[i][j] = 0;
            IniPri[i][j] = PLNUM - NxtGroup[i];
        }
    }

    quantum_counter++;
    epoch_counter++;

    // Memory Occupancy Monitor
    for(i = 0; i < Ncore; i++) {
        if (read_queue_head[channel]->request_served == TRUE) {
            QReqCnt[i][j]++;
            EReqCnt[i][j]++;
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

     
    if (quantum_counter == QUANTUM_SIZE) {

        for(i = 0; i < Ncore; i++) {
            printf("QReqCnt[%d][%d] = %d; ", i, j, QReqCnt[i][j]);
            printf("EReqCnt[%d][%d] = %d\n", i, j, EReqCnt[i][j]);

        }

        if (channel == MAX_NUM_CHANNELS-1) {
            grouping_algorithm();
        }

        quantum_counter = 0;

    }

}



/*

    // OLD FCFS CODE BELOW (to be removed)


	request_t * rd_ptr = NULL;
	request_t * wr_ptr = NULL;


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
		LL_FOREACH(read_queue_head[channel],rd_ptr)
		{
			if(rd_ptr->command_issuable)
			{
				issue_request_command(rd_ptr);
				break;
			}
		}
		return;
	}

*/

void scheduler_stats()
{
  /* Nothing to print for now. */
}
