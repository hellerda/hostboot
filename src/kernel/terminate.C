/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/kernel/terminate.C $                                      */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2012,2017                        */
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

#include <kernel/hbdescriptor.H>
#include <kernel/hbterminatetypes.H>
#include <kernel/terminate.H>
#ifndef BOOTLOADER
#include <stdint.h>
#include <kernel/console.H>
#include <kernel/ipc.H>

#include <builtins.h>
#include <kernel/kernel_reasoncodes.H>
#endif // BOOTLOADER

extern "C" void p8_force_attn() NO_RETURN;


#ifndef BOOTLOADER
/* Instance of the TI Data Area */
HB_TI_DataArea kernel_TIDataArea;

/* Instance of the HB descriptor struct */
HB_Descriptor kernel_hbDescriptor =
{
    &kernel_TIDataArea,
    &KernelIpc::ipc_data_area,
    0
};
#endif // BOOTLOADER



void terminateExecuteTI()
{
    // Call the function that actually executes the TI code.
    p8_force_attn();
}

#ifndef BOOTLOADER
void termWritePlid(uint16_t i_source, uint32_t plid)
{
    kernel_TIDataArea.type = TI_WITH_PLID;
    kernel_TIDataArea.source = i_source;
    kernel_TIDataArea.plid = plid;
}
#endif // BOOTLOADER

void termWriteSRC(uint16_t i_source, uint16_t i_reasoncode,uint64_t i_failAddr,
                  uint32_t i_error_data)
{
    // Update the TI structure with the type of TI, who called,
    // and extra error data (if applicable, otherwise 0)
    kernel_TIDataArea.type = TI_WITH_SRC;
    kernel_TIDataArea.source = i_source;
    kernel_TIDataArea.error_data = i_error_data;

    // Update TID data area with the SRC info we have avail
    kernel_TIDataArea.src.ID = 0xBC;
    kernel_TIDataArea.src.subsystem = 0x8A;
    kernel_TIDataArea.src.reasoncode = i_reasoncode;
    kernel_TIDataArea.src.moduleID = 0;
    kernel_TIDataArea.src.iType = TI_WITH_SRC;
    kernel_TIDataArea.src.iSource = i_source;

    // Update User Data with address of fail location
    kernel_TIDataArea.src.word6 = i_failAddr;
}

void termModifySRC(uint8_t i_moduleID, uint32_t i_word7, uint32_t i_word8)
{
    // Update module ID
    kernel_TIDataArea.src.moduleID = i_moduleID;

    // Update User Data with anything supplied for word7 or word8
    kernel_TIDataArea.src.word7 = i_word7;
    kernel_TIDataArea.src.word8 = i_word8;
}
