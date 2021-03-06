/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/import/chips/p9/procedures/hwp/nest/p9_chiplet_fabric_scominit.C $ */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2017,2018                        */
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
///
/// @file p9_chiplet_scominit.C
///
/// @brief apply fabric SCOM inits
///

//
// *HWP HW Owner : Joe McGill <jmcgill@us.ibm.com>
// *HWP FW Owner : Thi N. Tran <thi@us.ibm.com>
// *HWP Team : Nest
// *HWP Level : 3
// *HWP Consumed by : HB
//

//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------
#include <p9_chiplet_fabric_scominit.H>
#include <p9_fbc_no_hp_scom.H>
#include <p9_fbc_ioe_tl_scom.H>
#include <p9_fbc_ioe_dl_scom.H>
#include <p9_xbus_fir_utils.H>
#include <p9_fbc_smp_utils.H>

#include <p9_xbus_scom_addresses.H>
#include <p9_xbus_scom_addresses_fld.H>
#include <p9_obus_scom_addresses.H>
#include <p9_obus_scom_addresses_fld.H>
#include <p9_misc_scom_addresses.H>
#include <p9_perv_scom_addresses.H>

//------------------------------------------------------------------------------
// Function definitions
//------------------------------------------------------------------------------

fapi2::ReturnCode p9_chiplet_fabric_scominit(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::ReturnCode l_rc;
    char l_procTargetStr[fapi2::MAX_ECMD_STRING_LEN];
    char l_chipletTargetStr[fapi2::MAX_ECMD_STRING_LEN];
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_XBUS>> l_xbus_chiplets;
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_OBUS>> l_obus_chiplets;
    fapi2::buffer<uint64_t> l_fbc_cent_fir_data;

    fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_Type l_fbc_optics_cfg_mode = { fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_SMP };
    FAPI_DBG("Start");

    // Get proc target string
    fapi2::toString(i_target, l_procTargetStr, sizeof(l_procTargetStr));

    // apply FBC non-hotplug initfile
    FAPI_DBG("Invoking p9.fbc.no_hp.scom.initfile on target %s...", l_procTargetStr);
    FAPI_EXEC_HWP(l_rc, p9_fbc_no_hp_scom, i_target, FAPI_SYSTEM);

    if (l_rc)
    {
        FAPI_ERR("Error from p9_fbc_no_hp_scom");
        fapi2::current_err = l_rc;
        goto fapi_try_exit;
    }

    // setup IOE (XBUS FBC IO) TL SCOMs
    FAPI_DBG("Invoking p9.fbc.ioe_tl.scom.initfile on target %s...", l_procTargetStr);
    FAPI_EXEC_HWP(l_rc, p9_fbc_ioe_tl_scom, i_target, FAPI_SYSTEM);

    if (l_rc)
    {
        FAPI_ERR("Error from p9_fbc_ioe_tl_scom");
        fapi2::current_err = l_rc;
        goto fapi_try_exit;
    }

    l_xbus_chiplets = i_target.getChildren<fapi2::TARGET_TYPE_XBUS>();

    // configure TL FIR, only if not already setup by SBE
    FAPI_TRY(fapi2::getScom(i_target, PU_PB_CENT_SM0_PB_CENT_FIR_REG, l_fbc_cent_fir_data),
             "Error from getScom (PU_PB_CENT_SM0_PB_CENT_FIR_REG)");

    if (!l_fbc_cent_fir_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>())
    {
        FAPI_TRY(fapi2::putScom(i_target, PU_PB_IOE_FIR_ACTION0_REG, FBC_IOE_TL_FIR_ACTION0),
                 "Error from putScom (PU_PB_IOE_FIR_ACTION0_REG)");
        FAPI_TRY(fapi2::putScom(i_target, PU_PB_IOE_FIR_ACTION1_REG, FBC_IOE_TL_FIR_ACTION1),
                 "Error from putScom (PU_PB_IOE_FIR_ACTION1_REG)");

        fapi2::buffer<uint64_t> l_fir_mask = FBC_IOE_TL_FIR_MASK;

        if (!l_xbus_chiplets.size())
        {
            // no valid links, mask
            l_fir_mask.flush<1>();
        }
        else
        {
            bool l_x_functional[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] =
            {
                false,
                false,
                false
            };
            uint64_t l_x_non_functional_mask[P9_FBC_UTILS_MAX_ELECTRICAL_LINKS] =
            {
                FBC_IOE_TL_FIR_MASK_X0_NF,
                FBC_IOE_TL_FIR_MASK_X1_NF,
                FBC_IOE_TL_FIR_MASK_X2_NF
            };

            for (auto l_iter = l_xbus_chiplets.begin();
                 l_iter != l_xbus_chiplets.end();
                 l_iter++)
            {
                uint8_t l_unit_pos;
                FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS,
                                       *l_iter,
                                       l_unit_pos),
                         "Error from FAPI_ATTR_GET (ATTR_CHIP_UNIT_POS)");
                l_x_functional[l_unit_pos] = true;
            }

            for (uint8_t ll = 0;
                 ll < P9_FBC_UTILS_MAX_ELECTRICAL_LINKS;
                 ll++)
            {
                if (!l_x_functional[ll])
                {
                    l_fir_mask |= l_x_non_functional_mask[ll];
                }
            }

            FAPI_TRY(fapi2::putScom(i_target, PU_PB_IOE_FIR_MASK_REG, l_fir_mask),
                     "Error from putScom (PU_PB_IOE_FIR_MASK_REG)");
        }
    }

    // setup IOE (XBUS FBC IO) DL SCOMs
    for (auto l_iter = l_xbus_chiplets.begin();
         l_iter != l_xbus_chiplets.end();
         l_iter++)
    {
        fapi2::toString(*l_iter, l_chipletTargetStr, sizeof(l_chipletTargetStr));
        FAPI_DBG("Invoking p9.fbc.ioe_dl.scom.initfile on target %s...", l_chipletTargetStr);
        FAPI_EXEC_HWP(l_rc, p9_fbc_ioe_dl_scom, *l_iter, i_target);

        if (l_rc)
        {
            FAPI_ERR("Error from p9_fbc_ioe_dl_scom");
            fapi2::current_err = l_rc;
            goto fapi_try_exit;
        }

        // configure DL FIR, only if not already setup by SBE
        if (!l_fbc_cent_fir_data.getBit<PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13>())
        {
            FAPI_TRY(fapi2::putScom(*l_iter, XBUS_LL0_IOEL_FIR_ACTION0_REG, FBC_IOE_DL_FIR_ACTION0),
                     "Error from putScom (XBUS_LL0_IOEL_FIR_ACTION0_REG)");
            FAPI_TRY(fapi2::putScom(*l_iter, XBUS_LL0_IOEL_FIR_ACTION1_REG, FBC_IOE_DL_FIR_ACTION1),
                     "Error from putScom (XBUS_LL0_IOEL_FIR_ACTION1_REG)");
            FAPI_TRY(fapi2::putScom(*l_iter, XBUS_LL0_LL0_LL0_IOEL_FIR_MASK_REG, FBC_IOE_DL_FIR_MASK),
                     "Error from putScom (XBUS_LL0_LL0_LL0_IOEL_FIR_MASK_REG)");
        }
    }

    // set FBC optics config mode attribute
    l_obus_chiplets = i_target.getChildren<fapi2::TARGET_TYPE_OBUS>();

    for (auto l_iter = l_obus_chiplets.begin();
         l_iter != l_obus_chiplets.end();
         l_iter++)
    {
        uint8_t l_unit_pos;
        FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, *l_iter, l_unit_pos),
                 "Error from FAPI_ATTR_GET(ATTR_CHIP_UNIT_POS)");
        FAPI_INF("Updating index: %d\n", l_unit_pos);
        FAPI_INF("  before: %d\n", l_fbc_optics_cfg_mode[l_unit_pos]);
        FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_OPTICS_CONFIG_MODE, *l_iter, l_fbc_optics_cfg_mode[l_unit_pos]),
                 "Error from FAPI_ATTR_GET(ATTR_OPTICS_CONFIG_MODE)");
        FAPI_INF("  after: %d\n", l_fbc_optics_cfg_mode[l_unit_pos]);
    }

    FAPI_TRY(FAPI_ATTR_SET(fapi2::ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE, i_target, l_fbc_optics_cfg_mode),
             "Error from FAPI_ATTR_SET(ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE)");

fapi_try_exit:
    FAPI_DBG("End");
    return fapi2::current_err;
}
