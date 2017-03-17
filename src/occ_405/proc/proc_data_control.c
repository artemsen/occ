/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/proc/proc_data_control.c $                        */
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

#include "proc_data.h"
#include "proc_data_control.h"
#include "occhw_async.h"
#include "threadSch.h"
#include "pmc_register_addresses.h"
#include "proc_data_service_codes.h"
#include "occ_service_codes.h"
#include "errl.h"
#include "trac.h"
#include "rtls.h"
#include "apss.h"
#include "state.h"
#include "occ_sys_config.h"
#include "p9_pstates_common.h"
#include "pgpe_interface.h"
#include "rtls_service_codes.h"
#include "proc_pstate.h"

// The the GPE parameter fields for PGPE IPC calls.
extern GPE_BUFFER(ipcmsg_clip_update_t*  G_clip_update_parms_ptr);
extern GPE_BUFFER(ipcmsg_set_pmcr_t*     G_pmcr_set_parms_ptr);

extern GpeRequest G_clip_update_req;
extern GpeRequest G_pmcr_set_req;

extern bool G_state_transition_occuring;     // A state transition is currently going on?

// a global flag used by task_core_data_control() to indicate
// that the OCC is ready to transition to observation state
// (after initiatibg a clip update IPC task if needed)
bool G_active_to_observation_ready = false;

// create gpe request for GPE job. The initialization will be done one time
// during gpe request create.
GpeRequest G_core_data_control_req;

// Global double buffering for core data control
GPE_BUFFER(PstatesClips G_quads_data_control[2]) = {{{{0}}}};

// Pointer to the core data control that will be written to by the OCC FW.
GPE_BUFFER(PstatesClips* G_core_data_control_occwrite_ptr) = { &G_quads_data_control[0] };

// Pointer to the core data control that will be used by GPE engine.
GPE_BUFFER(PstatesClips* G_core_data_control_gpewrite_ptr) = { &G_quads_data_control[1] };


// Function Specification
//
// Name: task_core_data_control
//
// Description: Control quad actuation for all configured cores on every tick.
//
// End Function Specification
void task_core_data_control( task_t * i_task )
{
    errlHndl_t      err            = NULL;   //Error handler
    PstatesClips*   l_temp         = NULL;
    static bool     L_trace_logged = false;  // trace logging to avoid unnecessarily repeatig logs
    Pstate          l_pstate;

    // Once a state transition process starts, task data control
    // stops updating the PMCR/CLIPS updates, this way, the state
    // transition protocol can set the clips without colliding
    // with the task_core_data_control IPC tasks.
    if(G_state_transition_occuring)
    {
        if(L_trace_logged == false)
        {
            TRAC_INFO("task_core_data_control: Pstate Control stopped because a state transition started.");
            L_trace_logged = true;
        }

        // Only Transitioning to Observation state necessitates clip update
        // (if the last clip update was higher than legacy turbo).
        if ((G_occ_master_state == OCC_STATE_OBSERVATION) &&
            !G_active_to_observation_ready)
        {
            // confirm that the last clip update IPC successfully completed on PGPE (with no errors)
            if( async_request_is_idle(&G_clip_update_req.request) &&  //clip_update/set_clip_ranges completed
                (G_clip_update_parms_ptr->msg_cb.rc == PGPE_RC_SUCCESS) ) // with no errors
            {
                uint8_t quad = 0;
                Pstate pclip = 0xff;    // Initialize pclip to 0xff (lowest possible frequency)

                // Only if last clip update sent to PGPE is larger than legacy turbo,
                // send new clips with legacy turbo values, otherwise, no action needed.
                for (quad = 0; quad < MAXIMUM_QUADS; quad++)
                {
                    if(G_clip_update_parms_ptr->ps_val_clip_max[quad] < pclip)
                    {
                        // minimum pclip value corresponds to pstate of maximum frequency
                        pclip = G_clip_update_parms_ptr->ps_val_clip_max[quad];
                    }
                }
                l_pstate = proc_freq2pstate(G_sysConfigData.sys_mode_freq.table[OCC_MODE_TURBO]);
                // pclip of highest quad frequency corresponds to a frequency higher than legacy turbo
                if(pclip < l_pstate)
                {
                    pgpe_set_clip_ranges(l_pstate);
                }

                //Whether clips have been lowered from frequencies higher than legacy turbo
                //frequency, or already lower than turbo frequency, OCC is now ready to
                //transition to Observation state.
                G_active_to_observation_ready = true;
            }
        }
    }
    else
    {
        L_trace_logged = false;

        // perform Pstate/clip control if previous IPC call completed successfully
        // if not idle, ignore cycle
        // if an error was returned, log an error, and request reset
        if(G_sysConfigData.system_type.kvm) // OPAL system
        {
            // confirm that the clip update IPC from last cycle
            // has successfully completed on PGPE (with no errors)
            if( async_request_is_idle(&G_clip_update_req.request) &&  //clip_update/set_clip_ranges completed
                (G_clip_update_parms_ptr->msg_cb.rc == PGPE_RC_SUCCESS) ) // with no errors
            {
                //The previous OPAL PGPE request succeeded:

                //1) swap gpewrite ptr with the occwrite ptr (double buffering).
                l_temp = G_core_data_control_occwrite_ptr;
                G_core_data_control_occwrite_ptr = G_core_data_control_gpewrite_ptr;
                G_core_data_control_gpewrite_ptr = l_temp;

                //2) Set clip values from gpewrite's quad clips data-structure
                G_clip_update_parms_ptr = &G_core_data_control_gpewrite_ptr->clips;

                //call PGPE IPC function to update the clips
                pgpe_clip_update();
            }
            else if(G_clip_update_parms_ptr->msg_cb.rc != PGPE_RC_SUCCESS)
            {
                // an earlier clip update IPC call has not completed, trace and log an error
                TRAC_ERR("task_core_data_control: clip update IPC task returned an error, %d",
                         G_clip_update_parms_ptr->msg_cb.rc);

                /*
                 * @errortype
                 * @moduleid    RTLS_TASK_CORE_DATA_CONTROL_MOD
                 * @reasoncode  PGPE_FAILURE
                 * @userdata1   rc
                 * @userdata2   clip update task idle?
                 * @userdata4   ERC_PGPE_CLIP_UNSUCCESSFULL
                 * @devdesc     pgpe clip update returned an error
                 */
                err = createErrl(
                    RTLS_TASK_CORE_DATA_CONTROL_MOD,                  //ModId
                    PGPE_FAILURE,                                     //Reasoncode
                    ERC_PGPE_CLIP_UNSUCCESSFULL,                      //Extended reason code
                    ERRL_SEV_PREDICTIVE,                              //Severity
                    NULL,                                             //Trace Buf
                    DEFAULT_TRACE_SIZE,                               //Trace Size
                    G_clip_update_parms_ptr->msg_cb.rc,                    //Userdata1
                    async_request_is_idle(&G_clip_update_req.request) //Userdata2
                    );
            }
        }
        else
        {
            // NON OPAL System, OCC owns PMCR:
            if( async_request_is_idle(&G_pmcr_set_req.request) &&     // PMCR IPC from last TICK completed
                (G_pmcr_set_parms_ptr->msg_cb.rc == PGPE_RC_SUCCESS) )     // with no errors
            {
                //The previous Non-OPAL PGPE request succeeded:

                //1) swap gpewrite ptr with the occwrite ptr (double buffering).
                l_temp = G_core_data_control_occwrite_ptr;
                G_core_data_control_occwrite_ptr = G_core_data_control_gpewrite_ptr;
                G_core_data_control_gpewrite_ptr = l_temp;

                //2) Set Pstate values from gpewrite's quad pstates data-structure
                G_pmcr_set_parms_ptr = &G_core_data_control_gpewrite_ptr->pstates;

                //call PGPE IPC function to update Pstates
                pgpe_pmcr_set();
            }
            else if(G_pmcr_set_parms_ptr->msg_cb.rc != PGPE_RC_SUCCESS)
            {
                // an earlier clip update IPC call has not completed, trace and log an error
                TRAC_ERR("task_core_data_control: pstate update IPC task returned an error, %d",
                         G_pmcr_set_parms_ptr->msg_cb.rc);

                /*
                 * @errortype
                 * @moduleid    RTLS_TASK_CORE_DATA_CONTROL_MOD
                 * @reasoncode  PGPE_FAILURE
                 * @userdata1   rc
                 * @userdata2   pmcr set task idle?
                 * @userdata4   ERC_PGPE_PMCR_UNSUCCESSFULL
                 * @devdesc     pgpe PMCR set returned an error
                 */
                err = createErrl(
                    RTLS_TASK_CORE_DATA_CONTROL_MOD,               //ModId
                    PGPE_FAILURE,                                  //Reasoncode
                    ERC_PGPE_PMCR_UNSUCCESSFULL,                   //Extended reason code
                    ERRL_SEV_PREDICTIVE,                           //Severity
                    NULL,                                          //Trace Buf
                    DEFAULT_TRACE_SIZE,                            //Trace Size
                    G_pmcr_set_parms_ptr->msg_cb.rc,               //Userdata1
                    async_request_is_idle(&G_pmcr_set_req.request) //Userdata2
                    );
            }
        }

        if(err)
        {
            // commit error log
            REQUEST_RESET(err);
        }
    }

    return;
}
