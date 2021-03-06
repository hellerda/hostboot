/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/import/chips/p9/procedures/hwp/pm/p9_setup_evid.C $       */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2016,2018                        */
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
/// @file  p9_setup_evid.C
/// @brief Setup External Voltage IDs
///
// *HWP HW Owner        : Greg Still <stillgs@us.ibm.com>
// *HWP FW Owner        : Prasad Bg Ranganath <prasadbgr@in.ibm.com>
// *Team                : PM
// *Consumed by         : HB
// *Level               : 3
///
/// @verbatim
///
/// Procedure Summary:
///   - Use Attributes to send VDD, VDN and VCS via the AVS bus to VRMs
///
/// @endverbatim

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <fapi2.H>
#include <p9_setup_evid.H>
#include <p9_avsbus_lib.H>
#include <p9_avsbus_scom.H>
#include <p9_quad_scom_addresses.H>
#include <p9_quad_scom_addresses_fld.H>


//-----------------------------------------------------------------------------
// Procedure
//-----------------------------------------------------------------------------


// Substep indicators
// const uint32_t STEP_SBE_EVID_START              = 0x1;
// const uint32_t STEP_SBE_EVID_CONFIG             = 0x2;
// const uint32_t STEP_SBE_EVID_WRITE_VDN          = 0x3;
// const uint32_t STEP_SBE_EVID_POLL_VDN_STATUS    = 0x4;
// const uint32_t STEP_SBE_EVID_WRITE_VDD          = 0x5;
// const uint32_t STEP_SBE_EVID_POLL_VDD_STATUS    = 0x6;
// const uint32_t STEP_SBE_EVID_WRITE_VCS          = 0x7;
// const uint32_t STEP_SBE_EVID_POLL_VCS_STATUS    = 0x8;
// const uint32_t STEP_SBE_EVID_TIMEOUT            = 0x9;
// const uint32_t STEP_SBE_EVID_BOOT_FREQ          = 0xA;
// const uint32_t STEP_SBE_EVID_COMPLETE           = 0xB;


struct avsbus_attrs_t
{
    uint8_t vdd_bus_num;
    uint8_t vdd_rail_select;
    uint8_t vdn_bus_num;
    uint8_t vdn_rail_select;
    uint8_t vcs_bus_num;
    uint8_t vcs_rail_select;
    uint32_t vcs_voltage_mv;
    uint32_t vdd_voltage_mv;
    uint32_t vdn_voltage_mv;
    uint32_t r_loadline_vdd_uohm;
    uint32_t r_distloss_vdd_uohm;
    uint32_t vrm_voffset_vdd_uv;
    uint32_t r_loadline_vdn_uohm;
    uint32_t r_distloss_vdn_uohm;
    uint32_t vrm_voffset_vdn_uv;
    uint32_t r_loadline_vcs_uohm;
    uint32_t r_distloss_vcs_uohm;
    uint32_t vrm_voffset_vcs_uv;
    uint32_t freq_proc_refclock_khz;
    uint32_t proc_dpll_divider;
};

//##############################################################################
//@brief Compute the boot and safe frequencies and voltages
//@param[in] i_target       Proc Chip target
//@param[in] attrs          Attributes
//@param[in] l_globalppb    Global Pstate Parameter Block
//@param[in] i_action       Voltage Config action
//@return fapi::ReturnCode: FAPI2_RC_SUCCESS if success, else error code.
//##############################################################################
fapi2::ReturnCode
compute_boot_safe(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                  AttributeList* attrs,
                  GlobalPstateParmBlock* l_globalppb,
                  const VoltageConfigActions_t i_action)
{
    fapi2::ReturnCode l_rc;

    uint32_t attr_mvpd_poundv[PV_D][PV_W];
    uint32_t valid_pdv_points;
    uint8_t present_chiplets;

    PSTATE_attribute_state l_state;
    l_state.iv_pstates_enabled = true;
    l_state.iv_resclk_enabled  = true;
    l_state.iv_vdm_enabled     = true;
    l_state.iv_ivrm_enabled    = true;
    l_state.iv_wof_enabled     = true;

    do
    {

        //We only wish to compute voltage setting defaults if the action
        //inputed to the HWP tells us to
        if(i_action == COMPUTE_VOLTAGE_SETTINGS)
        {

            // query VPD if any of the voltage attributes are zero
            if (!attrs->vdd_voltage_mv ||
                !attrs->vcs_voltage_mv ||
                !attrs->vdn_voltage_mv)
            {

                uint8_t l_poundv_bucketId = 0;
                fapi2::voltageBucketData_t l_poundv_data;
                LP_VDMParmBlock   l_lp_vdmpb;

                PoundW_data l_poundw_data;
                memset (&l_poundw_data, 0, sizeof(PoundW_data));

                LocalPstateParmBlock l_localppb;
                memset(&l_localppb, 0, sizeof(LocalPstateParmBlock));

                Safe_mode_parameters l_safe_mode_values;

                uint8_t l_ps_pstate = 0xFF;

                VpdBias l_vpdbias[NUM_OP_POINTS];
                memset (l_vpdbias, 0, sizeof(VpdBias));

                // Get #V data from MVPD for VDD/VDN and VCS voltage values
                FAPI_TRY(proc_get_mvpd_data(i_target,
                                            attr_mvpd_poundv,
                                            &valid_pdv_points,
                                            &present_chiplets,
                                            l_poundv_bucketId,
                                            &l_poundv_data,
                                            &l_state          ));

                if (!present_chiplets)
                {
                    FAPI_IMP("**** WARNING : There are no EQ chiplets present which means there is no valid #V VPD");
                    break;
                }

                //set to default value if dpll divider is 0
                if (!attrs->proc_dpll_divider)
                {
                    attrs->proc_dpll_divider = 8;
                }

                // Apply Bias values
                FAPI_TRY(proc_get_extint_bias(attr_mvpd_poundv,
                                              attrs, l_vpdbias),
                         "Bias application function failed");

                VpdOperatingPoint l_raw_operating_points[NUM_OP_POINTS];
                FAPI_INF("Load RAW VPD");
                FAPI_TRY(load_mvpd_operating_point(attr_mvpd_poundv,
                                                   l_raw_operating_points,
                                                   revle32(l_globalppb->frequency_step_khz)),
                         "Loading MVPD operating points failed");

                // Put raw operating points into the Global Parameter Block
                FAPI_INF("Place Raw VPD into GPPB");
                FAPI_TRY(load_mvpd_operating_point(attr_mvpd_poundv,
                                                   l_globalppb->operating_points,
                                                   revle32(l_globalppb->frequency_step_khz)),
                         "Putting MVPD into GPPB operating points failed");

                // Compute the VPD operating points
                VpdOperatingPoint l_operating_points[NUM_VPD_PTS_SET][NUM_OP_POINTS];
                p9_pstate_compute_vpd_pts(l_operating_points,
                                          l_globalppb,
                                          l_raw_operating_points);

                uint32_t l_ps_freq_khz = l_operating_points[VPD_PT_SET_BIASED][POWERSAVE].frequency_mhz * 1000;
                freq2pState(l_globalppb, l_ps_freq_khz, &l_ps_pstate);

                FAPI_INF ("l_frequency_step_khz %08x", revle32(l_globalppb->frequency_step_khz));
                FAPI_INF ("l_ref_freq_khz %08X", revle32(l_globalppb->reference_frequency_khz));
                FAPI_INF ("l_ps_pstate %x", l_ps_pstate);

                l_rc = proc_get_mvpd_poundw(i_target,
                                            l_poundv_bucketId,
                                            &l_lp_vdmpb,
                                            &l_poundw_data,
                                            l_poundv_data,
                                            &l_state);

                if (l_rc)
                {
                    FAPI_ASSERT_NOEXIT(false,
                                       fapi2::PSTATE_PB_POUND_W_ACCESS_FAIL(fapi2::FAPI2_ERRL_SEV_RECOVERED)
                                       .set_CHIP_TARGET(i_target)
                                       .set_FAPI_RC(l_rc),
                                       "Pstate Parameter Block proc_get_mvpd_poundw function failed");
                    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;
                }

                //Compute safe mode values
                FAPI_TRY(p9_pstate_safe_mode_computation (
                             i_target,
                             l_operating_points,
                             revle32(l_globalppb->reference_frequency_khz),
                             revle32(l_globalppb->frequency_step_khz),
                             l_ps_pstate,
                             &l_safe_mode_values,
                             l_poundw_data),
                         "Error from p9_pstate_safe_mode_computation function");


                // set VDD voltage to PowerSave Voltage from MVPD data (if no override)
                if (attrs->vdd_voltage_mv)
                {
                    FAPI_INF("VDD boot voltage override set.");
                }
                else
                {
                    attrs->vdd_voltage_mv = l_safe_mode_values.boot_mode_mv;
                    FAPI_INF("VDD boot voltage set to %d mV.", attrs->vdd_voltage_mv);
                }



                // set VCS voltage to UltraTurbo Voltage from MVPD data (if no override)
                if (attrs->vcs_voltage_mv)
                {
                    FAPI_INF("VCS boot voltage override set.");
                }
                else
                {
                    FAPI_INF("VCS boot voltage override not set, using VPD value and correcting for applicable load line setting");
                    uint32_t l_int_vcs_mv = (attr_mvpd_poundv[POWERSAVE][VPD_PV_VCS_MV]);
                    uint32_t l_ics_ma = (attr_mvpd_poundv[POWERSAVE][VPD_PV_ICS_100MA]) * 100;

                    uint32_t l_ext_vcs_mv = sysparm_uplift(l_int_vcs_mv,
                                                           l_ics_ma,
                                                           attrs->r_loadline_vcs_uohm,

                                                           attrs->r_distloss_vcs_uohm,
                                                           attrs->vrm_voffset_vcs_uv);


                    FAPI_INF("VCS VPD voltage %d mV; Corrected voltage: %d mV; ICS: %d mA; LoadLine: %d uOhm; DistLoss: %d uOhm;  Offst: %d uOhm",
                             l_int_vcs_mv,
                             revle32(l_ext_vcs_mv),
                             l_ics_ma,
                             attrs->r_loadline_vcs_uohm,
                             attrs->r_distloss_vcs_uohm,
                             attrs->vrm_voffset_vcs_uv);


                    attrs->vcs_voltage_mv = revle32(l_ext_vcs_mv);

                }

                // set VDN voltage to PowerSave Voltage from MVPD data (if no override)
                if (attrs->vdn_voltage_mv)
                {
                    FAPI_INF("VDN boot voltage override set");
                }
                else
                {
                    FAPI_INF("VDN boot voltage override not set, using VPD value and correcting for applicable load line setting");
                    uint32_t l_int_vdn_mv = (attr_mvpd_poundv[POWERBUS][VPD_PV_VDN_MV]);
                    uint32_t l_idn_ma = (attr_mvpd_poundv[POWERBUS][VPD_PV_IDN_100MA]) * 100;
                    // Returns revle32
                    uint32_t l_ext_vdn_mv = sysparm_uplift(l_int_vdn_mv,
                                                           l_idn_ma,
                                                           attrs->r_loadline_vdn_uohm,
                                                           attrs->r_distloss_vdn_uohm,
                                                           attrs->vrm_voffset_vdn_uv);

                    FAPI_INF("VDN VPD voltage %d mV; Corrected voltage: %d mV; IDN: %d mA; LoadLine: %d uOhm; DistLoss: %d uOhm;  Offst: %d uOhm",
                             l_int_vdn_mv,
                             revle32(l_ext_vdn_mv),
                             l_idn_ma,
                             attrs->r_loadline_vdn_uohm,
                             attrs->r_distloss_vdn_uohm,
                             attrs->vrm_voffset_vdn_uv);

                    attrs->vdn_voltage_mv = revle32(l_ext_vdn_mv);
                }
            }
            else
            {
                FAPI_INF("Using overrides for all boot voltages (VDD/VCS/VDN) and core frequency");

                // Set safe frequency to the default BOOT_FREQ_MULT
                fapi2::ATTR_BOOT_FREQ_MULT_Type l_boot_freq_mult;
                FAPI_TRY(FAPI_ATTR_GET( fapi2::ATTR_BOOT_FREQ_MULT,
                                        i_target,
                                        l_boot_freq_mult));

                uint32_t l_boot_freq_mhz =
                    ((l_boot_freq_mult * attrs->freq_proc_refclock_khz ) /
                     attrs->proc_dpll_divider )
                    / 1000;

                FAPI_TRY(FAPI_ATTR_SET(fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ,
                                       i_target,
                                       l_boot_freq_mhz));
                FAPI_TRY(FAPI_ATTR_SET(fapi2::ATTR_SAFE_MODE_VOLTAGE_MV,
                                       i_target,
                                       attrs->vdd_voltage_mv));
                FAPI_INF("Safe mode Frequency = %d MHz (0x%x), Safe mode voltage = %d mV (0x%x)",
                         l_boot_freq_mhz, l_boot_freq_mhz,
                         attrs->vdd_voltage_mv, attrs->vdd_voltage_mv);
            }

            FAPI_INF("Setting Boot Voltage attributes: VDD = %dmV; VCS = %dmV; VDN = %dmV",
                     attrs->vdd_voltage_mv, attrs->vcs_voltage_mv, attrs->vdn_voltage_mv);
            FAPI_TRY(FAPI_ATTR_SET(fapi2::ATTR_VDD_BOOT_VOLTAGE, i_target, attrs->vdd_voltage_mv),
                     "Error from FAPI_ATTR_SET (ATTR_VDD_BOOT_VOLTAGE)");
            FAPI_TRY(FAPI_ATTR_SET(fapi2::ATTR_VCS_BOOT_VOLTAGE, i_target, attrs->vcs_voltage_mv),
                     "Error from FAPI_ATTR_SET (ATTR_VCS_BOOT_VOLTAGE)");
            FAPI_TRY(FAPI_ATTR_SET(fapi2::ATTR_VDN_BOOT_VOLTAGE, i_target, attrs->vdn_voltage_mv),
                     "Error from FAPI_ATTR_SET (ATTR_VDN_BOOT_VOLTAGE)");
        }  // COMPUTE_VOLTAGE_SETTINGS
    }
    while(0);

    // trace values to be used
    FAPI_INF("VDD boot voltage = %d mV (0x%x)",
             attrs->vdd_voltage_mv, attrs->vdd_voltage_mv);
    FAPI_INF("VCS boot voltage = %d mV (0x%x)",
             attrs->vcs_voltage_mv, attrs->vcs_voltage_mv);
    FAPI_INF("VDN boot voltage = %d mV (0x%x)",
             attrs->vdn_voltage_mv, attrs->vdn_voltage_mv);

fapi_try_exit:
    return fapi2::current_err;
} // compute_boot_safe


fapi2::ReturnCode
p9_setup_evid(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target, const VoltageConfigActions_t i_action)
{



    AttributeList attrs;
    GlobalPstateParmBlock l_globalppb;
    memset (&l_globalppb, 0, sizeof(GlobalPstateParmBlock));

    // Load the attributes
    FAPI_TRY(proc_get_attributes(i_target, &attrs),
             "Get attributes function failed");

    // Create Global Parameter Block from attribute structure
    load_gppb_attrs(&attrs, &l_globalppb);

    // Compute the boot/safe values
    FAPI_TRY(compute_boot_safe(i_target, &attrs, &l_globalppb, i_action));

    //We only wish to apply settings if i_action says to
    if(i_action == APPLY_VOLTAGE_SETTINGS)
    {
        // Set the DPLL frequency values to safe mode values
        FAPI_TRY (p9_setup_dpll_values(i_target,
                                       attrs.freq_proc_refclock_khz,
                                       attrs.proc_dpll_divider),
                  "Error from p9_setup_dpll_values function");

        if (attrs.vdd_voltage_mv)
        {
            FAPI_TRY(p9_setup_evid_voltageWrite(i_target,
                                                attrs.vdd_bus_num,
                                                attrs.vdd_rail_select,
                                                attrs.vdd_voltage_mv,
                                                attrs.attr_ext_vrm_step_size_mv,
                                                VDD_SETUP),
                     "Error from VDD setup function");
        }

        if (attrs.vdn_voltage_mv)
        {
            FAPI_TRY(p9_setup_evid_voltageWrite(i_target,
                                                attrs.vdn_bus_num,
                                                attrs.vdn_rail_select,
                                                attrs.vdn_voltage_mv,
                                                attrs.attr_ext_vrm_step_size_mv,
                                                VDN_SETUP),
                     "error from VDN setup function");
        }

        // Set Boot VCS Voltage
        if(attrs.vcs_bus_num == 0xFF)
        {

            FAPI_INF("VCS rail is not connected to AVSBus. Skipping VCS programming");
        }
        else
        {
            if (attrs.vcs_voltage_mv)
            {
                FAPI_TRY(p9_setup_evid_voltageWrite(i_target,
                                                    attrs.vcs_bus_num,
                                                    attrs.vcs_rail_select,
                                                    attrs.vcs_voltage_mv,
                                                    attrs.attr_ext_vrm_step_size_mv,
                                                    VCS_SETUP),
                         "error from VCS setup function");
            }
        }
    }

fapi_try_exit:
    return fapi2::current_err;
} // Procedure


fapi2::ReturnCode
p9_setup_evid_voltageWrite(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                           const uint8_t i_bus_num,
                           const uint8_t i_rail_select,
                           const uint32_t i_voltage_mv,
                           const uint32_t i_ext_vrm_step_size_mv,
                           const P9_SETUP_EVID_CONSTANTS i_evid_value)

{

    uint8_t     l_goodResponse = 0;
    uint8_t     l_throwAssert = true;
    uint32_t    l_present_voltage_mv;
    uint32_t    l_target_mv;
    uint32_t    l_count;
    int32_t     l_delta_mv = 0;
    char        rail_str[8];

    switch (i_evid_value)
    {
        case VCS_SETUP:
            strcpy(rail_str, "VCS");
            break;

        case VDD_SETUP:
            strcpy(rail_str, "VDD");
            break;

        case VDN_SETUP:
            strcpy(rail_str, "VDN");
            break;

        default:
            ;
    }

    if (i_evid_value != VCS_SETUP)
    {
        // Initialize the buses
        FAPI_TRY(avsInitExtVoltageControl(i_target,
                                          i_bus_num, BRIDGE_NUMBER),
                 "Initializing avsBus VDD/VDN, bridge %d", BRIDGE_NUMBER);
    }

    FAPI_INF("Setting voltage for %s to %d mV", rail_str, i_voltage_mv);

    // Drive AVS Bus with a frame value 0xFFFFFFFF (idle frame) to
    // initialize the AVS slave
    FAPI_TRY(avsIdleFrame(i_target, i_bus_num, BRIDGE_NUMBER));

    // Read the present voltage

    // This loop is to ensrue AVSBus Master and Slave are in sync
    l_count = 0;

    do
    {

        FAPI_TRY(avsVoltageRead(i_target, i_bus_num, BRIDGE_NUMBER,
                                i_rail_select, l_present_voltage_mv),
                 "AVS Voltage read transaction failed to %d, Bridge %d",
                 i_bus_num,
                 BRIDGE_NUMBER);
        // Throw an assertion if we don't get a good response.
        l_throwAssert =  (l_count >= AVSBUS_RETRY_COUNT);
        FAPI_TRY(avsValidateResponse(i_target,  i_bus_num, BRIDGE_NUMBER,
                                     l_throwAssert, l_goodResponse));

        if (!l_goodResponse)
        {
            FAPI_TRY(avsIdleFrame(i_target, i_bus_num, BRIDGE_NUMBER));
        }

        l_count++;
    }
    while (!l_goodResponse);

    // Compute the delta
    l_delta_mv = (int32_t)l_present_voltage_mv - (int32_t)i_voltage_mv;

    if (l_delta_mv > 0)
    {
        FAPI_INF("Decreasing voltage - delta = %d", l_delta_mv );
    }
    else if (l_delta_mv < 0)
    {
        FAPI_INF("Increasing voltage - delta = %d", l_delta_mv );
    }
    else
    {
        FAPI_INF("Voltage to be set equals the initial voltage");
    }

    // Break into steps limited by attr.attr_ext_vrm_step_size_mv
    while (l_delta_mv)
    {
        // Hostboot doesn't support abs()
        uint32_t l_abs_delta_mv = l_delta_mv < 0 ? -l_delta_mv : l_delta_mv;

        if (i_ext_vrm_step_size_mv > 0 && l_abs_delta_mv > i_ext_vrm_step_size_mv )
        {
            if (l_delta_mv > 0)  // Decreasing
            {
                l_target_mv = l_present_voltage_mv - i_ext_vrm_step_size_mv;
            }
            else
            {
                l_target_mv = l_present_voltage_mv + i_ext_vrm_step_size_mv;
            }
        }
        else
        {
            l_target_mv = i_voltage_mv;
        }

        FAPI_INF("Target voltage = %d; Present voltage = %d",
                 l_target_mv, l_present_voltage_mv);

        l_count = 0;

        do
        {
            FAPI_INF("Moving voltage to %d; retry count = %d", l_target_mv, l_count);
            // Set Boot voltage
            FAPI_TRY(avsVoltageWrite(i_target,
                                     i_bus_num,
                                     BRIDGE_NUMBER,
                                     i_rail_select,
                                     l_target_mv),
                     "Setting voltage via AVSBus %d, Bridge %d",
                     i_bus_num,
                     BRIDGE_NUMBER);

            // Throw an assertion if we don't get a good response.
            l_throwAssert =  l_count >= AVSBUS_RETRY_COUNT;
            FAPI_TRY(avsValidateResponse(i_target,
                                         i_bus_num,
                                         BRIDGE_NUMBER,
                                         l_throwAssert,
                                         l_goodResponse));

            if (!l_goodResponse)
            {
                FAPI_TRY(avsIdleFrame(i_target, i_bus_num, BRIDGE_NUMBER));
            }

            l_count++;
        }
        while (!l_goodResponse);

        l_present_voltage_mv = l_target_mv;
        l_delta_mv = (int32_t)l_present_voltage_mv - (int32_t)i_voltage_mv;
        FAPI_INF("New delta = %d", l_delta_mv );
    }

fapi_try_exit:
    return fapi2::current_err;
} // Procedure

fapi2::ReturnCode
p9_setup_dpll_values (const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                      const uint32_t  i_freq_proc_refclock_khz,
                      const uint32_t i_proc_dpll_divider)

{
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_EQ>> l_eqChiplets;
    fapi2::Target<fapi2::TARGET_TYPE_EQ> l_firstEqChiplet;
    l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>(fapi2::TARGET_STATE_FUNCTIONAL);
    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint64_t> l_fmult;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    uint8_t l_chipNum = 0xFF;
    fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ_Type l_attr_safe_mode_freq;
    fapi2::ATTR_SAFE_MODE_VOLTAGE_MV_Type l_attr_safe_mode_mv;

    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_SAFE_MODE_FREQUENCY_MHZ, i_target, l_attr_safe_mode_freq));
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_SAFE_MODE_VOLTAGE_MV, i_target, l_attr_safe_mode_mv));

    do
    {

        if (!l_attr_safe_mode_freq || !l_attr_safe_mode_mv)
        {
            break;
        }

        for ( auto l_itr = l_eqChiplets.begin(); l_itr != l_eqChiplets.end(); ++l_itr)
        {
            l_fmult.flush<0>();
            FAPI_TRY(fapi2::getScom(*l_itr, EQ_QPPM_DPLL_FREQ , l_data64),
                     "ERROR: Failed to read EQ_QPPM_DPLL_FREQ");

            l_data64.extractToRight<EQ_QPPM_DPLL_FREQ_FMULT,
                                    EQ_QPPM_DPLL_FREQ_FMULT_LEN>(l_fmult);

            // Convert back to the complete frequency value
            l_fmult =  ((l_fmult * i_freq_proc_refclock_khz ) / i_proc_dpll_divider ) / 1000;

            // Convert frequency value to a format that needs to be written to the
            // register
            uint32_t l_safe_mode_dpll_value = ((l_attr_safe_mode_freq * 1000) * i_proc_dpll_divider) /
                                              i_freq_proc_refclock_khz;

            FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, *l_itr, l_chipNum));
            FAPI_INF("For EQ number %u, l_fmult 0x%08X l_safe_mode_dpll_value 0x%08X (%d)",
                     l_chipNum, l_fmult, l_safe_mode_dpll_value, l_safe_mode_dpll_value);

            if (l_fmult > l_safe_mode_dpll_value)
            {
                FAPI_INF("DPLL setting: Lowering the dpll frequency");
            }
            else if (l_fmult < l_safe_mode_dpll_value)
            {
                FAPI_INF("DPLL setting: Raising the dpll frequency");
            }
            else
            {
                FAPI_INF("DPLL setting: Leaving the dpll frequency as it is");
            }

            //FMax
            l_data64.insertFromRight<EQ_QPPM_DPLL_FREQ_FMAX,
                                     EQ_QPPM_DPLL_FREQ_FMAX_LEN>(l_safe_mode_dpll_value);
            //FMin
            l_data64.insertFromRight<EQ_QPPM_DPLL_FREQ_FMIN,
                                     EQ_QPPM_DPLL_FREQ_FMIN_LEN>(l_safe_mode_dpll_value);
            //FMult
            l_data64.insertFromRight<EQ_QPPM_DPLL_FREQ_FMULT,
                                     EQ_QPPM_DPLL_FREQ_FMULT_LEN>(l_safe_mode_dpll_value);

            FAPI_TRY(fapi2::putScom(*l_itr, EQ_QPPM_DPLL_FREQ, l_data64),
                     "ERROR: Failed to write for EQ_QPPM_DPLL_FREQ");


            //Update VDM VID compare to safe mode value
            const uint16_t VDM_VOLTAGE_IN_MV = 512;
            const uint16_t VDM_GRANULARITY = 4;

            //Convert same mode value to a format that needs to be written to
            //the register
            uint32_t l_vdm_vid_value = (l_attr_safe_mode_mv - VDM_VOLTAGE_IN_MV) / VDM_GRANULARITY;

            FAPI_INF ("l_vdm_vid_value 0x%x (%d), i_safe_mode_values.safe_mode_mv 0x%x (%d)",
                      l_vdm_vid_value, l_vdm_vid_value,
                      l_attr_safe_mode_mv, l_attr_safe_mode_mv);

            FAPI_TRY(fapi2::getScom(*l_itr, EQ_QPPM_VDMCFGR, l_data64),
                     "ERROR: Failed to read EQ_QPPM_VDMCFGR");

            l_data64.insertFromRight<0, 8>(l_vdm_vid_value);

            FAPI_TRY(fapi2::putScom(*l_itr, EQ_QPPM_VDMCFGR, l_data64),
                     "ERROR: Failed to write for EQ_QPPM_VDMCFGR");
        } //end of eq list
    }
    while (0);

fapi_try_exit:
    return fapi2::current_err;
}


