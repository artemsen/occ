/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/dcom/dcomMasterTx.c $                             */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2017                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

#ifndef _DCOMMASTERTX_C
#define _DCOMMASTERTX_C

#include "ssx.h"
#include "occhw_pba.h"
#include <rtls.h>
#include <apss.h>
#include <dcom.h>
#include <dcom_service_codes.h>
#include <occ_service_codes.h>
#include <trac.h>
#include <proc_pstate.h>
#include <amec_sys.h>
#include <amec_master_smh.h>

extern UINT8 g_amec_tb_record; // From amec_amester.c for syncronized traces
extern PWR_READING_TYPE  G_pwr_reading_type;

// SSX Block Copy Request for the Slave Inbox Transmit Queue
BceRequest G_slv_inbox_tx_pba_request;

// Used by the master to house the doorbell data that is sent in
// the master multicast doorbell, stating that it put slave inbox in main memory.
dcom_slv_inbox_doorbell_t G_dcom_slv_inbox_doorbell_tx;

// Make sure that the Slave Inbox TX Buffer is 256B, otherwise cause
// error on the compile.
STATIC_ASSERT(  (NUM_BYTES_IN_SLAVE_INBOX != (sizeof(G_dcom_slv_inbox_tx)/MAX_OCCS))  );

// Store return code and failed packet # from pbax_send so we can trace it
uint32_t G_pbax_rc = 0;
uint32_t G_pbax_packet = 0xffffffff;

// Used to keep count of number of APSS data collection fails.
uint16_t G_apss_fail_updown_count = 0x0000;

#ifdef DEBUG_APSS_SEQ
// used to keep track of the APSS complete sequence number
extern uint32_t G_savedCompleteSeq;
#endif

// Function Specification
//
// Name: dcom_build_slv_inbox
//
// Description: The purpose of this function is to copy the Control Data into
//              the Slave inbox structures so that the Master can send it out.
//              Build the slave inboxes so master can send them to slaves
//
// End Function Specification

uint32_t dcom_build_slv_inbox(void)
{
    // Locals
    uint32_t l_addr_of_slv_inbox_in_main_mem = 0;
    uint32_t l_slv_idx = 0;

    static uint8_t      L_seq = 0xFF;
    static bool         L_traced = FALSE;

    L_seq++;

    // If there was a pbax_send failure, trace it here since we can't do it in the critical
    // interrupt context.
    if(G_pbax_rc)
    {
        INCREMENT_ERR_HISTORY(ERRH_DCOM_MASTER_PBAX_SEND_FAIL);

        if (!L_traced)
        {
            TRAC_INFO("PBAX Send Failure in transimitting multicast doorbell - RC[%08X], packet[%d]", G_pbax_rc, G_pbax_packet);
            L_traced = TRUE;
        }
    }
    else
    {
        L_traced = FALSE;
    }


    // INBOX...............
    // For each occ slave collect its occ data.
    for(; l_slv_idx < MAX_OCCS; l_slv_idx++)
    {
        G_dcom_slv_inbox_tx[l_slv_idx].seq = L_seq;
        G_dcom_slv_inbox_tx[l_slv_idx].version = 0;

        memcpy( G_dcom_slv_inbox_tx[l_slv_idx].adc,
                G_apss_pwr_meas.adc,
                sizeof( G_dcom_slv_inbox_tx[l_slv_idx].adc));

         memcpy( G_dcom_slv_inbox_tx[l_slv_idx].gpio,
                G_apss_pwr_meas.gpio,
                sizeof( G_dcom_slv_inbox_tx[l_slv_idx].gpio));

         memcpy( G_dcom_slv_inbox_tx[l_slv_idx].tod,
                &G_apss_pwr_meas.tod,
                sizeof( G_dcom_slv_inbox_tx[l_slv_idx].tod));

        memset( G_dcom_slv_inbox_tx[l_slv_idx].occ_fw_mailbox, 0, sizeof( G_dcom_slv_inbox_tx[l_slv_idx].occ_fw_mailbox ));


        // Collect mnfg parameters that need to be sent to slaves
        G_dcom_slv_inbox_tx[l_slv_idx].foverride_enable = g_amec->mnfg_parms.auto_slew;
        G_dcom_slv_inbox_tx[l_slv_idx].foverride = g_amec->mnfg_parms.foverride;
        G_dcom_slv_inbox_tx[l_slv_idx].emulate_oversub = AMEC_INTF_GET_OVERSUBSCRIPTION_EMULATION();

        // Collect Idle Power Saver parameters to be sent to slaves
        G_dcom_slv_inbox_tx[l_slv_idx].ips_freq_request = g_amec->mst_ips_parms.freq_request;

        // Collect Tunable Paramaters to be sent to slaves
        // Tunable IDs defined in G_mst_tunable_parameter_table[]
        // G_mst_tunable_parameter_table_ext index = Tunable ID - 1
        G_dcom_slv_inbox_tx[l_slv_idx].alpha_up = G_mst_tunable_parameter_table_ext[0].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].alpha_down = G_mst_tunable_parameter_table_ext[1].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].sample_count_util = G_mst_tunable_parameter_table_ext[2].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].step_up = G_mst_tunable_parameter_table_ext[3].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].step_down = G_mst_tunable_parameter_table_ext[4].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].epsilon_perc = G_mst_tunable_parameter_table_ext[5].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].tlutil = G_mst_tunable_parameter_table_ext[6].adj_value;
        // parameters at index 7 and 8 (f delta between cores) are used by master only and not sent to slaves
        G_dcom_slv_inbox_tx[l_slv_idx].wof_enable = G_mst_tunable_parameter_table_ext[9].adj_value;
        G_dcom_slv_inbox_tx[l_slv_idx].tunable_param_overwrite = G_mst_tunable_parameter_overwrite;

        // Collect soft frequency bondaries to be sent to slaves
        G_dcom_slv_inbox_tx[l_slv_idx].soft_fmin = G_mst_soft_fmin;
        G_dcom_slv_inbox_tx[l_slv_idx].soft_fmax = G_mst_soft_fmax;

        // Send trace recording bit to slaves for synchronized tracing.
        G_dcom_slv_inbox_tx[l_slv_idx].tb_record = g_amec_tb_record;

        G_dcom_slv_inbox_tx[l_slv_idx].counter++;

        memcpy( &G_dcom_slv_inbox_tx[l_slv_idx].sys_mode_freq,
                &G_sysConfigData.sys_mode_freq,
                sizeof( freqConfig_t ));
    }

    // Clear the tunable parameter overwrite once we collect the new values
    G_mst_tunable_parameter_overwrite = 0;
    dcom_build_occfw_msg( SLAVE_INBOX );

    l_addr_of_slv_inbox_in_main_mem = dcom_which_buffer();

    //DOORBELL.................
    //Prepare data for doorbell.  This is sent to all OCCs

    G_dcom_slv_inbox_doorbell_tx.pob_id = G_pbax_id;
    G_dcom_slv_inbox_doorbell_tx.magic1 = PBAX_MAGIC_NUMBER2_32B;
    G_dcom_slv_inbox_doorbell_tx.addr_slv_inbox_buffer0 = l_addr_of_slv_inbox_in_main_mem;

    memcpy( (void *) &G_dcom_slv_inbox_doorbell_tx.pcap,
            (void *) &G_master_pcap_data,
            sizeof(pcap_config_data_t));

    G_dcom_slv_inbox_doorbell_tx.ppb_fmax = G_sysConfigData.master_ppb_fmax; // Master ppb fmax is calculated in amec_ppb_fmax_calc

    memcpy( (void *) &G_dcom_slv_inbox_doorbell_tx.adc[0],
            (void *) &G_apss_pwr_meas.adc[0],
            sizeof( G_dcom_slv_inbox_doorbell_tx.adc ));

    G_dcom_slv_inbox_doorbell_tx.gpio[0] = G_apss_pwr_meas.gpio[0];
    G_dcom_slv_inbox_doorbell_tx.gpio[1] = G_apss_pwr_meas.gpio[1];
    G_dcom_slv_inbox_doorbell_tx.tod = G_apss_pwr_meas.tod;
    G_dcom_slv_inbox_doorbell_tx.apss_recovery_in_progress = G_apss_recovery_requested;

    G_dcom_slv_inbox_doorbell_tx.magic_counter++;
    G_dcom_slv_inbox_doorbell_tx.magic2 = PBAX_MAGIC_NUMBER_32B;

    return l_addr_of_slv_inbox_in_main_mem;
}

// Function Specification
//
// Name:  dcom_which_buffer
//
// Description: Determines which buffer in the 'double buffer'
//              or ping/pong to use. Basically alternates between
//              returning the ping or the pong address
//
// End Function Specification

uint32_t dcom_which_buffer(void)
{
    //Locals
    uint32_t l_mem_address = SLAVE_INBOX_PONG_COMMON_ADDRESS;

    // Switch back and forth based on tick
    if( CURRENT_TICK & 1 )
    {
        l_mem_address = SLAVE_INBOX_PING_COMMON_ADDRESS;
    }

    return l_mem_address;
}

// Function Specification
//
// Name: task_dcom_tx_slv_inbox
//
// Description: Copy slave inboxes from SRAM to main memory
//              so master can send data to slave
//
// Task Flags: RTL_FLAG_MSTR, RTL_FLAG_OBS, RTL_FLAG_ACTIVE
//
// End Function Specification
void task_dcom_tx_slv_inbox( task_t *i_self)
{
    static bool L_error = FALSE;
    static uint8_t L_bce_not_ready_count = 0;
    uint32_t    l_orc = OCC_SUCCESS_REASON_CODE;
    uint32_t    l_orc_ext = OCC_NO_EXTENDED_RC;
    uint64_t    l_start = ssx_timebase_get();
    bool        l_pwr_meas = FALSE;
    bool        l_pwr_meas_complete_invalid = FALSE;
    bool        l_request_reset = FALSE;
    bool        l_ssx_failure = FALSE;
    // Use a static local bool to track whether the BCE request used
    // here has ever been successfully created at least once
    static bool L_bce_slv_inbox_tx_request_created_once = FALSE;

    DCOM_DBG("4. TX Slave Inbox\n");

    do
    {
        // If we are in standby or no APSS present, we need to fake out
        // the APSS data since we aren't talking to APSS.
        if( (OCC_STATE_STANDBY == CURRENT_STATE()) ||
            (G_pwr_reading_type != PWR_READING_TYPE_APSS) ||
            G_apss_recovery_requested )
        {
           G_ApssPwrMeasCompleted = TRUE;
        }

        l_pwr_meas = G_ApssPwrMeasCompleted;
        l_pwr_meas_complete_invalid = G_ApssPwrMeasDoneInvalid;

        // Did APSS power complete AND valid?
        if( l_pwr_meas == TRUE )
        {
#if !defined(DEBUG_APSS_SEQ) && defined(DCOM_DEBUG)
            uint64_t l_end = ssx_timebase_get();
            DCOM_DBG("Got APSS after waiting %d us\n",(int)( (l_end-l_start) / ( SSX_TIMEBASE_FREQUENCY_HZ / 1000000 ) ));
#endif
#if defined(DEBUG_APSS_SEQ) && !defined(DCOM_DEBUG)
            uint64_t l_end = ssx_timebase_get();
            TRAC_INFO("Got APSS after waiting %d us (complete seq %d)\n",
                      (int)( (l_end-l_start) / ( SSX_TIMEBASE_FREQUENCY_HZ / 1000000 ) ), G_savedCompleteSeq);
#endif

            APSS_SUCCESS();

            // Build/setup inboxes
            uint32_t l_addr_in_mem = dcom_build_slv_inbox();
            uint32_t l_ssxrc = 0;

            // See dcomMasterRx.c/task_dcom_rx_slv_outboxes for details on the
            // checking done here before creating and scheduling the request.
            bool l_proceed_with_request_and_schedule = FALSE;
            int l_req_idle = async_request_is_idle(&(G_slv_inbox_tx_pba_request.request));
            int l_req_complete = async_request_completed(&(G_slv_inbox_tx_pba_request.request));

            if (!L_bce_slv_inbox_tx_request_created_once)
            {
                // Do this case first, all other cases assume that this is
                // true!
                // This is the first time we have created a request so
                // always proceed with request create and schedule
                l_proceed_with_request_and_schedule = TRUE;
            }
            else if (l_req_idle && l_req_complete)
            {
                // Most likely case first.  The request was created
                // and scheduled and has completed without error.  Proceed.
                // Proceed with request create and schedule.
                l_proceed_with_request_and_schedule = TRUE;
            }
            else if (l_req_idle && !l_req_complete)
            {
                // There was an error on the schedule request or the request
                // was scheduled but was canceled, killed or errored out.
                // Proceed with request create and schedule.
                l_proceed_with_request_and_schedule = TRUE;

                // Trace important information from the request
                TRAC_INFO("BCE slv inbox tx request idle but not complete, \
                          callback_rc=%d options=0x%x state=0x%x abort_state=0x%x \
                          completion_state=0x%x",
                          G_slv_inbox_tx_pba_request.request.callback_rc,
                          G_slv_inbox_tx_pba_request.request.options,
                          G_slv_inbox_tx_pba_request.request.state,
                          G_slv_inbox_tx_pba_request.request.abort_state,
                          G_slv_inbox_tx_pba_request.request.completion_state);
                TRAC_INFO("Proceeding with BCE slv inbox tx request and schedule");
            }
            else if (!l_req_idle && !l_req_complete)
            {
                // The request was created and scheduled but is still in
                // progress or still enqueued OR there was some error
                // creating the request so it was never scheduled.  The latter
                // case is unlikely and will generate an error message when
                // it occurs.  It will also have to happen after the request
                // was created at least once or we'll never get here.  If the
                // request does fail though before the state parms in the
                // request are reset (like a bad parameter error), then this
                // represents a hang condition that we can't recover from.
                // DO NOT proceed with request create and schedule.
                l_proceed_with_request_and_schedule = FALSE;

                if(L_bce_not_ready_count == DCOM_TRACE_NOT_IDLE_AFTER_CONSEC_TIMES)
                {
                   // Trace important information from the request
                   TRAC_INFO("BCE slv inbox tx request not idle and not complete: callback_rc[%d] options[0x%x] state[0x%x] abort_state[0x%x] completion_state[0x%x]",
                             G_slv_inbox_tx_pba_request.request.callback_rc,
                             G_slv_inbox_tx_pba_request.request.options,
                             G_slv_inbox_tx_pba_request.request.state,
                             G_slv_inbox_tx_pba_request.request.abort_state,
                             G_slv_inbox_tx_pba_request.request.completion_state);
                   TRAC_INFO("NOT proceeding with BCE slv inbox tx request and schedule");
                }
            }
            else
            {
                // This case is not possible. Ignore it.
            }

            // Only proceed if the BCE request state checked out
            if (l_proceed_with_request_and_schedule)
            {
                if(L_bce_not_ready_count >= DCOM_TRACE_NOT_IDLE_AFTER_CONSEC_TIMES)  // previously not idle
                {
                   TRAC_INFO("BCE slv inbox tx request idle and complete after %d times", L_bce_not_ready_count);
                }

                L_bce_not_ready_count = 0;

                // Set up inboxes copy request
                l_ssxrc = bce_request_create(
                                &G_slv_inbox_tx_pba_request,        // Block copy object
                                &G_pba_bcue_queue,                  // Mainstore to sram copy engine
                                l_addr_in_mem,                      // Mainstore address
                                (uint32_t) &G_dcom_slv_inbox_tx[0], // SRAM starting address
                                sizeof(G_dcom_slv_inbox_tx),        // Size of copy
                                SSX_WAIT_FOREVER,                   // No timeout
                                (AsyncRequestCallback)dcom_tx_slv_inbox_doorbell, // Call back
                                NULL,                               // Call back arguments
                                ASYNC_CALLBACK_IMMEDIATE            // Callback mask
                                );

                if(l_ssxrc != SSX_OK)
                {
                    /* @
                     * @errortype
                     * @moduleid    DCOM_MID_TASK_TX_SLV_INBOX
                     * @reasoncode  SSX_GENERIC_FAILURE
                     * @userdata1   N/A
                     * @userdata4   ERC_BCE_REQUEST_CREATE_FAILURE
                     * @devdesc     SSX BCE related failure
                     */
                    TRAC_ERR("PBA request create failure rc=[%08X]",l_ssxrc);
                    l_orc = SSX_GENERIC_FAILURE;
                    l_orc_ext = ERC_BCE_REQUEST_CREATE_FAILURE;
                    l_ssx_failure = TRUE;
                    break;
                }

                // Request created at least once
                L_bce_slv_inbox_tx_request_created_once = TRUE;
                DCOM_DBG("4.1.1 Scheduling G_slv_inbox_tx_pba_request");
                l_ssxrc = bce_request_schedule(&G_slv_inbox_tx_pba_request); // Actual copying

                if(l_ssxrc != SSX_OK)
                {
                    /* @
                     * @errortype
                     * @moduleid    DCOM_MID_TASK_TX_SLV_INBOX
                     * @reasoncode  SSX_GENERIC_FAILURE
                     * @userdata1   N/A
                     * @userdata4   ERC_BCE_REQUEST_SCHEDULE_FAILURE
                     * @devdesc     SSX BCE related failure
                     */
                    TRAC_ERR("PBA request schedule failure rc=[%08X]",l_ssxrc);
                    l_orc = SSX_GENERIC_FAILURE;
                    l_orc_ext = ERC_BCE_REQUEST_SCHEDULE_FAILURE;
                    l_ssx_failure = TRUE;
                    break;
                }
            }
            else
            {
                L_bce_not_ready_count++;
                INCREMENT_ERR_HISTORY(ERRH_DCOM_TX_SLV_INBOX);
            }

            // Moved the break statement here in case we decide not to
            // schedule the BCE request.
            break;
        }
        // Is APSS power collection done but failed?  Stop waiting since no valid data will be coming
        else if(l_pwr_meas_complete_invalid == TRUE)
        {
            //Failure occurred, step up the FAIL_COUNT
            APSS_FAIL();

            if (G_apss_fail_updown_count >= APSS_DATA_FAIL_MAX)
            {
                if (FALSE == isSafeStateRequested())
                {
                        /* @
                         * @errortype
                         * @moduleid    DCOM_MID_TASK_TX_SLV_INBOX
                         * @reasoncode  APSS_HARD_FAILURE
                         * @userdata1   N/A
                         * @userdata4   ERC_APSS_NO_VALID_DATA
                         * @devdesc     No valid APSS data (hard time-out)
                         */
                        TRAC_ERR("No valid apss data: #complete errors = %i  #invalid = %i",
                                 G_error_history[ERRH_APSS_COMPLETE_ERROR],
                                 G_error_history[ERRH_INVALID_APSS_DATA]);
                        l_orc = APSS_HARD_FAILURE;
                        l_orc_ext = ERC_APSS_NO_VALID_DATA;
                        l_request_reset = TRUE;
                }
            }

            break;
        }
        else
        {
            // check time and break out if we reached limit
            if ((ssx_timebase_get() - l_start) < SSX_MICROSECONDS(DCOM_TX_APSS_WAIT_TIME))
            {
                continue;
            }
            else
            {
                INCREMENT_ERR_HISTORY(ERRH_APSS_COMPLETE_TIMEOUT);

                //Failure occurred, step up the FAIL_COUNT
                APSS_FAIL();

                if (G_apss_fail_updown_count >= APSS_DATA_FAIL_MAX)
                {
                    if (FALSE == isSafeStateRequested())
                    {
                        /* @
                         * @errortype
                         * @moduleid    DCOM_MID_TASK_TX_SLV_INBOX
                         * @reasoncode  APSS_HARD_FAILURE
                         * @userdata1   N/A
                         * @userdata4   OCC_NO_EXTENDED_RC
                         * @devdesc     Time out waiting on power measurement completion (hard time-out)
                         */
                        TRAC_ERR("Timed out waiting apss meas completion (dcom_start:%d us, apss_start:%d us, apss_end:%d us)",
                                 (int) ((l_start)/(SSX_TIMEBASE_FREQUENCY_HZ/1000000)),
                                 (int) ((G_gpe_apss_time_start)/(SSX_TIMEBASE_FREQUENCY_HZ/1000000)),
                                 (int) ((G_gpe_apss_time_end)/(SSX_TIMEBASE_FREQUENCY_HZ/1000000)));
                        l_orc = APSS_HARD_FAILURE;
                        l_orc_ext = OCC_NO_EXTENDED_RC;
                        l_request_reset = TRUE;
                    }
                }

                break;
            }
        }

    } while (1);

    //If an error exists and
    //we have not logged one before or there's a new request to reset,
    //and a reset has not already been requested, then log error.
    if ( (l_orc != OCC_SUCCESS_REASON_CODE) &&
         ((L_error == FALSE) || (l_request_reset == TRUE)) &&
         (FALSE == isSafeStateRequested()) )
    {
        // create and commit error only once.
        errlHndl_t  l_errl = createErrl(
                                        DCOM_MID_TASK_TX_SLV_INBOX,     //ModId
                                        l_orc,                          //Reasoncode
                                        l_orc_ext,                      //Extended reasoncode
                                        ERRL_SEV_UNRECOVERABLE,         //Severity
                                        NULL,                           //Trace Buf
                                        DEFAULT_TRACE_SIZE,             //Trace Size
                                        0,                              //Userdata1
                                        0                               //Userdata2
                                       );

        // Callout firmware
        addCalloutToErrl(l_errl,
                         ERRL_CALLOUT_TYPE_COMPONENT_ID,
                         ERRL_COMPONENT_ID_FIRMWARE,
                         ERRL_CALLOUT_PRIORITY_HIGH);

        if ( FALSE == l_ssx_failure )
        {
            // Callout processor
            addCalloutToErrl(l_errl,
                             ERRL_CALLOUT_TYPE_HUID,
                             G_sysConfigData.proc_huid,
                             ERRL_CALLOUT_PRIORITY_LOW);

            // Callout APSS
            addCalloutToErrl(l_errl,
                             ERRL_CALLOUT_TYPE_HUID,
                             G_sysConfigData.apss_huid,
                             ERRL_CALLOUT_PRIORITY_LOW);
        }

        if (l_request_reset)
        {
            REQUEST_RESET(l_errl);
        }
        else
        {
            commitErrl(&l_errl);
        }

        L_error = TRUE;
    }

}


// Function Specification
//
// Name: dcom_tx_slv_inbox_doorbell
//
// Description: transmit doorbells to slaves
//              from master
//
// NOTE: runs at crit interrupt (adding traces will cause crash)
//
// End Function Specification
void dcom_tx_slv_inbox_doorbell( void )
{
    int         l_pbarc      = 0;
    int         l_tmp        = 0;
    int         l_jj         = 0;
    uint64_t    l_start      = ssx_timebase_get();

    // Caclulate how many 8 byte packets are in the doorbell
    l_tmp = sizeof( G_dcom_slv_inbox_doorbell_tx ) / sizeof(uint64_t);

    // Loop through all packets, sending one at a time.  It should send
    // the previous packet almost immediately, but it is worth noting that
    // it is *possible* that the PowerBus is backed up, in which case it may
    // take a short amount of time (~1us <TBD>) to send each packet.
    // Estimated transfer time, under normal circumstances is 1kB/1us.
    for(l_jj=0; l_jj<l_tmp; l_jj++)
    {
        // Send 8 bytes of multicast doorbell
        l_pbarc = _pbax_send( &G_pbax_multicast_target,
                              G_dcom_slv_inbox_doorbell_tx.words[l_jj],
                              SSX_MICROSECONDS(15));

        // Set this global so we know to trace this in the non-critical interrupt context
        G_pbax_rc = l_pbarc;
        if ( (l_pbarc != 0 ) )
        {
            G_pbax_packet = l_jj;
            // Trace causes a panic in a critical interrupt! Don't trace here!(tries to pend a semaphore)

            // Break out of for loop and stop sending the rest of the doorbell
            // packets, since this likely occured b/c of a timeout.
            break;
        }
    }

    uint64_t l_delta = (ssx_timebase_get() - l_start);
    G_dcomTime.master.doorbellStartTx = l_start;
    G_dcomTime.master.doorbellStopTx = ssx_timebase_get();
    G_dcomTime.master.doorbellMaxDeltaTx = (l_delta > G_dcomTime.master.doorbellMaxDeltaTx) ?
        l_delta : G_dcomTime.master.doorbellMaxDeltaTx;
    G_dcomTime.master.doorbellSeq = G_dcom_slv_inbox_doorbell_tx.magic_counter;
    G_dcomTime.master.doorbellNumSent++;
}
#endif //_DCOMMASTERTOSLAVE_C

