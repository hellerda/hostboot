/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/include/usr/fapi2/plat_attr_override_sync.H $             */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2015,2017                        */
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
/// @file plat_attr_override_sync.H
///
/// @brief Defines the AttrOverrideSync class that contains functions for
///        Attribute Override and Sync.
///

#ifndef PLATATTROVERRIDESYNC_H_
#define PLATATTROVERRIDESYNC_H_

//******************************************************************************
// Includes
//******************************************************************************
#include <stdint.h>
#include <mbox/mboxif.H>
#include <targeting/common/attributeTank.H>
#include <pnor/pnorif.H>

#include <attribute_ids.H>
#include <return_code.H>
#include <target.H>
#include <target_types.H>


//******************************************************************************
// Interface
//******************************************************************************
namespace TARGETING
{
    errlHndl_t getAttrOverrides(PNOR::SectionInfo_t &i_sectionInfo,
                      AttributeTank* io_tanks[AttributeTank::TANK_LAYER_LAST]);

    class AttrRP;
}

namespace RT_TARG
{
    int apply_attr_override(uint8_t* i_data,
                            size_t i_size );
}

namespace fapi2
{

// Forward references
class AttrOverrideSync;
//class Target;

/**
 *  @brief Return the AttrOverrideSync Singleton. Other modules must call this
 *         rather than using Singleton<>
 *
 *  @return Reference to the AttrOverrideSync Singleton
 */
AttrOverrideSync & theAttrOverrideSync();

/**
 * @class AttrOverrideSync
 *
 * This class contains the FAPI Attribute Override and Sync tanks. It provides
 * functions to:
 * - Monitor for incoming attribute override/sync messages from the FSP
 * - Send attribute override/syncs to the FSP
 * - Return any attribute override on an attribute get
 * - Cancel any non-const attribute override and save the attribute in the sync
 *   tank on an attribute set
 */
class AttrOverrideSync
{
public:
    /**
     * @brief Allow a debug tool to directly access the override tank
     */
    friend void directOverride();

    /**
     * @brief Allow a attribute override to directly access the override tank
     */
    friend errlHndl_t TARGETING::getAttrOverrides(
                    PNOR::SectionInfo_t &i_sectionInfo,
                    TARGETING::AttributeTank*
                        io_tanks[TARGETING::AttributeTank::TANK_LAYER_LAST]);

    friend int RT_TARG::apply_attr_override(uint8_t* i_data,
                                            size_t i_size );

    friend class TARGETING::AttrRP;

    /**
     * @brief Maximum size of a direct attribute override
     */
    static const size_t MAX_DIRECT_OVERRIDE_ATTR_SIZE_BYTES = 64;

    /**
     * @brief Attribute Override/Sync Mailbox Message Type Constants
     *        These must be kept in sync with FSP firmware
     */
    enum MAILBOX_MSG_TYPE
    {
        MSG_SET_OVERRIDES       = MBOX::FIRST_UNSECURE_MSG + 0x10, // FSP<->Hb
        MSG_CLEAR_ALL_OVERRIDES = MBOX::FIRST_UNSECURE_MSG + 0x11, // FSP<->Hb
        MSG_SET_SYNC_ATTS       = MBOX::FIRST_UNSECURE_MSG + 0x12, // FSP<--Hb
        MSG_GET_OVERRIDES       = MBOX::FIRST_UNSECURE_MSG + 0x13, // FSP<--Hb
    };

    /**
     * @brief Default constructor
     */
    AttrOverrideSync();

    /**
     * @brief Destructor
     */
    ~AttrOverrideSync();

    /**
     * @brief Monitors for incoming attribute override messages from the FSP.
     *        This function never returns and must be called by a task
     *        specifically started to monitor for these messages
     */
    void monitorForFspMessages();

    /**
     * @brief Sends Attribute Overrides and Syncs to the FSP
     *
     * This is called at the end of an IStep. For both FAPI/TARG tanks it:
     * - Clears the FSP Attribute Overrides
     * - Sends the Hostboot Attribute Overrides to the FSP
     * - Sends the Hostboot Attribute Syncs to the FSP
     */
    void sendAttrOverridesAndSyncsToFsp();

    /**
     * @brief Gets Attribute Overrides and Syncs from the FSP
     *
     * This is called at the start of a normal (non-istep) IPL if an attribute
     * is set indicating that the FSP has attribute overrides in place
     */
    void getAttrOverridesFromFsp();

    /**
     * @brief This function gets any Attribute Override on an attribute get
     *
     * This is called for those FAPI Attributes that do not map to Targeting
     * attributes - their overrides live in the FAPI Attribute tanks.
     *
     * @param[in] i_attrId  FAPI Attribute ID
     * @param[in] i_pTarget Pointer to FAPI Target
     * @param[in] o_pVal    Pointer to attribute value
     *
     * @return true if an override exists and was written to o_pVal
     */
    bool getAttrOverride(const AttributeId i_attrId,
                         const Target<TARGET_TYPE_ALL>* i_pTarget,
                         void * o_pVal) const;

    /**
     * @brief This function gets any Attribute Override on an attribute get
     *
     * This is a wrapper that calls getAttrOverride on the AttrOverrideSync
     * singleton, it should be called by external modules to avoid the
     * performance penalty of calling theAttrOverrideSync() then getAttrOverride
     *
     * @param[in] i_attrId  FAPI Attribute ID
     * @param[in] i_pTarget Reference to FAPI2 Target
     * @param[in] o_pVal    Pointer to attribute value
     *
     * @return true if an override exists and was written to o_pVal
     */
    static bool getAttrOverrideFunc(const AttributeId i_attrId,
                                const Target<TARGET_TYPE_ALL>& i_pTarget,
                                void * o_pVal);

    /**
     * @brief This function performs the actions required on an attribute set
     *
     * This is called for those FAPI Attributes that do not map to Targeting
     * attributes - their overrides/syncs live in the FAPI Attribute tanks.
     *
     * - Any non-const attribute override is cleared
     * - The attribute is saved to be synced to Cronus (if Cronus Sync enabled)
     *
     * @param[in] i_attrId  FAPI Attribute ID
     * @param[in] i_pTarget Pointer to FAPI Target
     * @param[in] i_size    Size of attribute value
     * @param[in] i_pVal    Pointer to attribute value
     */
    void setAttrActions(const AttributeId i_attrId,
                        const Target<TARGET_TYPE_ALL>* i_pTarget,
                        const uint32_t i_size,
                        const void * i_pVal);

    /**
     * @brief This will force a sync of all FAPI attr to the FSP or the
     *  debug interface using the built in attribute sync tank
     *
     * This is called for ALL FAPI Attributes even if they are
     * directly backed into HB targeting
     *
     * @param[in] i_type     FAPI type to limit scope to, default none
     * @param[in] i_fapiPos  FAPI pos to limit scope to, default none
     * @param[in] i_attrHash Attribute hash to limit scope to, default none
     *
     * - The attribute is saved to be synced to debug interface (Cronus)
     *
     */
    void triggerAttrSync(fapi2::TargetType i_type = fapi2::TARGET_TYPE_NONE,
                         uint32_t i_fapiPos = TARGETING::FAPI_POS_NA,
                         uint32_t i_attrHash = 0x0);

    /**
     * @brief This function will clear all attribute overrides in effect.
     *        This function is called by the debug interface
     *
     */
    void clearAttrOverrides();

    /**
     * @brief This function will allow the debug interface to set
     *        attribute overrides "dynamically" at any point in the IPL
     *        between isteps.  The debug interface is used to process
     *        the attributes in the same format as the ATTR_TMP and ATTR_PERM
     *        PNOR partition binary blobs
     *
     */
    void dynSetAttrOverrides();

    /**
     * @brief This function will allow the debug interface to dump a
     *        specific attribute from a given fapi target type/pos
     *        at any point in the IPL between isteps.  The debug interface
     *        is used to process a simple request, and then dump the
     *        atrribute(s) back down the debug interface via same
     *        mechanism as triggerAttrSync
     *
     */
    void dynAttrGet();

    /**
     * @brief This function actually sends the FAPI Sync tank attributes
     *  across the debug interface
     */
    void sendFapiAttrSyncs();

    /**
     * @brief This function performs the actions required on an attribute set
     *
     * This is a wrapper that calls setAttrActions on the AttrOverrideSync
     * singleton, it should be called by external modules to avoid the
     * performance penalty of calling theAttrOverrideSync() then setAttrActions
     *
     * @param[in] i_attrId  FAPI Attribute ID
     * @param[in] i_pTarget Reference to FAPI2 Target
     * @param[in] i_size    Size of attribute value
     * @param[in] i_pVal    Pointer to attribute value
     */
    static void setAttrActionsFunc(const AttributeId i_attrId,
                                   const Target<TARGET_TYPE_ALL>& i_pTarget,
                                   const uint32_t i_size,
                                   const void * i_pVal);

private:

    /**
     * @brief Utility function that sends attributes to the FSP
     *
     * This function frees the allocated memory in the input vector of chunks
     * and empties the vector
     *
     * @param[in] i_msgType     Message type (ID) to send
     * @param[in] i_tankLayer   Tank Layer to send attribute to
     * @param[io] io_attributes Attributes to send.
     *
     * @return error log handle
     */
    static errlHndl_t sendAttrsToFsp(
        const MAILBOX_MSG_TYPE i_msgType,
        const TARGETING::AttributeTank::TankLayer i_tankLayer,
        std::vector<TARGETING::AttributeTank::AttributeSerializedChunk> &
            io_attributes);

    /**
     * @brief Utility function that gets the target type of a FAPI Target as
     *        used in an attribute tank
     *
     * @param[in] i_pTarget Pointer to FAPI Target (NULL = system)
     *
     * @return Target Type
     */
    static uint32_t getTargetType(const Target<TARGET_TYPE_ALL>* i_pTarget);

    // The FAPI Attribute Tanks
    TARGETING::AttributeTank iv_overrideTank;
    TARGETING::AttributeTank iv_syncTank;
};

} // namespace fapi2

#endif // PLATATTROVERRIDESYNC_H_
