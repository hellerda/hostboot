/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/usr/fapi2/plat_wof_access.C $                             */
/*                                                                        */
/* OpenPOWER HostBoot Project                                             */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2012,2018                        */
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
/**
 *  @file plat_wof_access.C
 *
 *  @brief Implements the GetWofTables function
 */

#include <stdint.h>
#include <fapi2.H>
#include <attribute_service.H>
#include <attributeenums.H>
#include <errl/errludattribute.H>
#include <errl/errlreasoncodes.H>
#include <util/utillidmgr.H>
#include <p9_pstates_common.h>
#include <initservice/initserviceif.H>
#include <sys/mm.h>
#include <errl/errlmanager.H>
#include <util/misc.H>


namespace fapi2
{

namespace platAttrSvc
{

const uint32_t WOF_IMAGE_MAGIC_VALUE = 0x57544948;  // WTIH
const uint32_t WOF_TABLES_MAGIC_VALUE = 0x57465448; // WFTH
const uint32_t RES_VERSION_MASK = 0xFF;
const uint32_t WOF_IMAGE_VERSION = 1;
const uint32_t WOF_TABLE_VERSION_2 = 2;
const uint32_t WOF_TABLE_VERSION_POWERMODE = WOF_TABLE_VERSION_2;
const uint32_t MAX_WOF_TABLES_VERSION = WOF_TABLE_VERSION_2;

#ifndef __HOSTBOOT_RUNTIME
// Remember that we have already allocated the VMM space for
//  the WOFDATA lid
static void* g_wofdataVMM = nullptr;
#endif


/*
        WOF Tables In PNOR
    ---------------------------
    |      Image Header       |  Points to Section Table
    |-------------------------|
    |  Section Table Entry 1  |  Section Table
    |  Section Table Entry 2  |    Each entry points to
    |  ...                    |    a WOF Table
    |-------------------------|
    |   WOF Tables Header 1   |  WOF Table
    |        VRFT 1           |    Returned if match
    |        VRFT 2           |
    |        ...              |
    |-------------------------|
    |   WOF Tables Header 2   |  WOF Table
    |        VRFT 1           |    Returned if match
    |        VRFT 2           |
    |        ...              |
    |-------------------------|
    |   ...                   |
    ---------------------------

    The structs for the Image Header and
    Section Table Entry are defined below.
    The struct for the WOF Tables Header
    is defined in p9_pstates_common.h.
*/


typedef struct __attribute__((__packed__))  wofImageHeader
{
    uint32_t magicNumber;
    uint8_t  version;
    uint8_t  entryCount;
    uint32_t offset;
} wofImageHeader_t;


typedef struct __attribute__((__packed__))  wofSectionTableEntry
{
    uint32_t offset;
    uint32_t size;
} wofSectionTableEntry_t;


fapi2::ReturnCode platParseWOFTables(uint8_t* o_wofData)
{
    FAPI_DBG("Entering platParseWOFTables ....");

    errlHndl_t l_errl = nullptr;
    fapi2::ReturnCode l_rc = fapi2::FAPI2_RC_SUCCESS;
    uint8_t* l_simMatch = nullptr;
    size_t l_simMatchSize = 0;

    TARGETING::Target * l_sys = nullptr;
    TARGETING::Target * l_mProc = nullptr;
    TARGETING::targetService().getTopLevelTarget(l_sys);
    TARGETING::targetService().masterProcChipTargetHandle( l_mProc );

    // Get the number of present cores
    TARGETING::TargetHandleList l_coreTargetList;
    TARGETING::getChildAffinityTargetsByState( l_coreTargetList,
                                               l_mProc,
                                               TARGETING::CLASS_UNIT,
                                               TARGETING::TYPE_CORE,
                                               TARGETING::UTIL_FILTER_PRESENT);
    uint32_t l_numCores = l_coreTargetList.size();

    // Choose a sort freq
    uint32_t l_sortFreq = 0;

    // Get the socket power and mode
    WOF_MODE l_mode = WOF_MODE_UNKNOWN;
    uint32_t l_socketPower = 0;
    uint8_t l_wofPowerLimit =
                l_sys->getAttr<TARGETING::ATTR_WOF_POWER_LIMIT>();

    if(l_wofPowerLimit == TARGETING::WOF_POWER_LIMIT_TURBO)
    {
        l_socketPower =
                l_sys->getAttr<TARGETING::ATTR_SOCKET_POWER_TURBO>();
        l_sortFreq = l_sys->getAttr<TARGETING::ATTR_FREQ_CORE_MAX>();
        l_mode = WOF_MODE_TURBO;
    }
    else
    {
        l_socketPower =
                l_sys->getAttr<TARGETING::ATTR_SOCKET_POWER_NOMINAL>();
        l_sortFreq = l_sys->getAttr<TARGETING::ATTR_NOMINAL_FREQ_MHZ>();
        l_mode = WOF_MODE_NOMINAL;
    }

    // Get the frequencies
    uint32_t l_nestFreq =
                l_sys->getAttr<TARGETING::ATTR_FREQ_PB_MHZ>();

    // Trace the input params
    FAPI_INF("WOF table search: "
             "Cores %d SocketPower 0x%X NestFreq 0x%X SortFreq 0x%X Mode 0x%X",
             l_numCores, l_socketPower, l_nestFreq, l_sortFreq, l_mode);

    void* l_pWofImage = nullptr;
    size_t l_lidImageSize = 0;

    do {
        // @todo RTC 172776 Make WOF table parser PNOR accesses more efficient
        uint32_t l_lidNumber = Util::WOF_LIDID;
        if( INITSERVICE::spBaseServicesEnabled() )
        {
            // Lid number is system dependent on FSP systems
            l_lidNumber =
              l_sys->getAttr<TARGETING::ATTR_WOF_TABLE_LID_NUMBER>();
        }
        UtilLidMgr l_wofLidMgr(l_lidNumber);

        // Get the size of the full wof tables image
        l_errl = l_wofLidMgr.getLidSize(l_lidImageSize);
        if(l_errl)
        {
            FAPI_ERR("platParseWOFTables getLidSize failed");
            l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
            break;
        }

        FAPI_INF("WOFDATA lid is %d bytes", l_lidImageSize);

#ifdef __HOSTBOOT_RUNTIME
        // Locally allocate space for the lid
        l_pWofImage = static_cast<void*>(malloc(l_lidImageSize));

#else
        // Use a special VMM block to avoid the requirement for
        //  contiguous memory
        int l_mm_rc = 0;
        if( !g_wofdataVMM )
        {
            l_mm_rc = mm_alloc_block( nullptr,
                          reinterpret_cast<void*>(VMM_VADDR_WOFDATA_LID),
                          VMM_SIZE_WOFDATA_LID );
            if(l_mm_rc != 0)
            {
                FAPI_INF("Fail from mm_alloc_block for WOFDATA, rc=%d", l_mm_rc);
                /*@
                 * @errortype
                 * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
                 * @reasoncode        fapi2::RC_MM_ALLOC_BLOCK_FAILED
                 * @userdata1         Address being allocated
                 * @userdata2[00:31]  Size of block allocation
                 * @userdata2[32:63]  rc from mm_alloc_block
                 * @devdesc           Error calling mm_alloc_block for WOFDATA
                 * @custdesc          Firmware Error
                 */
                l_errl = new ERRORLOG::ErrlEntry(
                               ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                               fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                               fapi2::RC_MM_ALLOC_BLOCK_FAILED,
                               VMM_VADDR_WOFDATA_LID,
                               TWO_UINT32_TO_UINT64(VMM_SIZE_WOFDATA_LID,
                                                    l_mm_rc),
                               true); //software callout
                l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
                break;
            }

            g_wofdataVMM = reinterpret_cast<void*>(VMM_VADDR_WOFDATA_LID);
        }

        l_mm_rc = mm_set_permission(
                         reinterpret_cast<void*>(VMM_VADDR_WOFDATA_LID),
                         l_lidImageSize,
                         WRITABLE | ALLOCATE_FROM_ZERO );
        if(l_mm_rc != 0)
        {
            FAPI_INF("Fail from mm_set_permission for WOFDATA, rc=%d", l_mm_rc);
            /*@
             * @errortype
             * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
             * @reasoncode        fapi2::RC_MM_SET_PERMISSION_FAILED
             * @userdata1         Address being changed
             * @userdata2[00:31]  Size of change
             * @userdata2[32:63]  rc from mm_set_permission
             * @devdesc           Error calling mm_set_permission for WOFDATA
             * @custdesc          Firmware Error
             */
            l_errl = new ERRORLOG::ErrlEntry(
                               ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                               fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                               fapi2::RC_MM_SET_PERMISSION_FAILED,
                               VMM_VADDR_WOFDATA_LID,
                               TWO_UINT32_TO_UINT64(VMM_SIZE_WOFDATA_LID,
                                                    l_mm_rc),
                               true); //software callout
            l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
            break;
        }

        // Point my local pointer at the VMM space we allocated
        l_pWofImage = g_wofdataVMM;

#endif


        // Get the tables from pnor or lid
        l_errl = l_wofLidMgr.getLid(l_pWofImage, l_lidImageSize);
        if(l_errl)
        {
            FAPI_ERR("platParseWOFTables getLid failed "
                     "pLidImage %p imageSize %d",
                     l_pWofImage, l_lidImageSize);
            l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
            break;
        }

        // Get the Image Header
        wofImageHeader_t* l_img =
                reinterpret_cast<wofImageHeader_t*>(l_pWofImage);

        // Check for the eyecatcher
        if(l_img->magicNumber != WOF_IMAGE_MAGIC_VALUE)
        {
            FAPI_ERR("Image Header Magic Value != WTIH(0x%X)",
                     WOF_IMAGE_MAGIC_VALUE);
            /*@
            * @errortype
            * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
            * @reasoncode        fapi2::RC_WOF_IMAGE_MAGIC_MISMATCH
            * @userdata1[00:31]  Image header magic value
            * @userdata1[32:63]  Expected magic value
            * @userdata2[00:31]  LID ID
            * @userdata2[32:63]  Image header version
            * @devdesc           Image header magic value mismatch
            * @custdesc          Firmware Error
            */
            l_errl = new ERRORLOG::ErrlEntry(
                             ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                             fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                             fapi2::RC_WOF_IMAGE_MAGIC_MISMATCH,
                             TWO_UINT32_TO_UINT64(
                                l_img->magicNumber,
                                WOF_IMAGE_MAGIC_VALUE),
                             TWO_UINT32_TO_UINT64(
                                l_lidNumber,
                                l_img->version),
                             true); //software callout
            l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
            break;
        }

        // Check for a valid image header version
        if(l_img->version > WOF_IMAGE_VERSION)
        {
            FAPI_ERR("Image header version not supported: "
                     " Header Version %d Supported Version %d",
                     l_img->version, WOF_IMAGE_VERSION);
            /*@
            * @errortype
            * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
            * @reasoncode        fapi2::RC_WOF_IMAGE_VERSION_MISMATCH
            * @userdata1[00:31]  Image header version
            * @userdata1[32:63]  Supported header version
            * @userdata2         LID ID
            * @devdesc           Image header version not supported
            * @custdesc          Firmware Error
            */
            l_errl = new ERRORLOG::ErrlEntry(
                             ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                             fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                             fapi2::RC_WOF_IMAGE_VERSION_MISMATCH,
                             TWO_UINT32_TO_UINT64(
                                l_img->version,
                                WOF_IMAGE_VERSION),
                             l_lidNumber,
                             true); //software callout
            l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
            break;
        }

        // Image info
        FAPI_INF("WOF Image: Magic 0x%X Version %d Entries %d Offset %p",
                 l_img->magicNumber, l_img->version,
                 l_img->entryCount, l_img->offset);

        // Get a pointer to the first section table entry
        wofSectionTableEntry_t* l_ste =
            reinterpret_cast<wofSectionTableEntry_t*>
                (reinterpret_cast<uint8_t*>(l_pWofImage) + l_img->offset);

        WofTablesHeader_t* l_wth = nullptr;
        std::vector<WofTablesHeader_t*> l_headers;
        l_headers.clear();
        uint32_t l_ent = 0;
        uint32_t l_ver = 0;

        // Loop through all section table entries
        for( l_ent = 0; l_ent < l_img->entryCount; l_ent++ )
        {
            // Get a pointer to the WOF table
            l_wth = reinterpret_cast<WofTablesHeader_t*>
                        (reinterpret_cast<uint8_t*>(l_pWofImage)
                            + l_ste[l_ent].offset);
            l_ver = l_wth->version;

            // Check for the eyecatcher
            if(l_wth->magic_number != WOF_TABLES_MAGIC_VALUE)
            {
                FAPI_ERR("WOF Tables Header Magic Number != WFTH(0x%X)",
                         WOF_TABLES_MAGIC_VALUE);
                /*@
                * @errortype
                * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
                * @reasoncode        fapi2::RC_WOF_TABLES_MAGIC_MISMATCH
                * @userdata1[00:31]  WOF tables header magic value
                * @userdata1[32:63]  Expected magic value
                * @userdata2[00:31]  WOF tables entry number
                * @userdata2[32:63]  WOF tables header version
                * @devdesc           WOF tables header magic value mismatch
                * @custdesc          Firmware Error
                */
                l_errl = new ERRORLOG::ErrlEntry(
                                ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                                fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                                fapi2::RC_WOF_TABLES_MAGIC_MISMATCH,
                                TWO_UINT32_TO_UINT64(
                                    l_wth->magic_number,
                                    WOF_TABLES_MAGIC_VALUE),
                                TWO_UINT32_TO_UINT64(
                                    l_ent,
                                    l_ver),
                                true); //software callout
                l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
                break;
            }

            // Check for a valid tables header version
            // Valid versions (1 - MAX_WOF_TABLES_VERSION)
            if((l_ver > MAX_WOF_TABLES_VERSION) || (l_ver == 0))
            {
                FAPI_ERR("WOF tables header version not supported: "
                        " Header Version %d Max Supported Version %d",
                        l_ver, MAX_WOF_TABLES_VERSION);
                /*@
                * @errortype
                * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
                * @reasoncode        fapi2::RC_WOF_TABLES_VERSION_MISMATCH
                * @userdata1[00:31]  WOF tables header version
                * @userdata1[32:63]  Max supported header version
                * @userdata2         WOF tables entry number
                * @devdesc           WOF tables header version not supported
                * @custdesc          Firmware Error
                */
                l_errl = new ERRORLOG::ErrlEntry(
                                ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                                fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                                fapi2::RC_WOF_TABLES_VERSION_MISMATCH,
                                TWO_UINT32_TO_UINT64(
                                    l_ver,
                                    MAX_WOF_TABLES_VERSION),
                                l_ent,
                                true); //software callout
                l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
                break;
            }

            // Trace the WOF table fields
            FAPI_INF("WOF table fields "
                     "SectionTableEntry %d "
                     "SectionTableOffset 0x%X "
                     "SectionTableSize %d "
                     "Version %d Mode %d Cores %d SocketPower 0x%X "
                     "NestFreq 0x%X NomFreq 0x%X",
                     l_ent, l_ste[l_ent].offset, l_ste[l_ent].size,
                     l_ver, l_wth->mode, l_wth->core_count,
                     l_wth->socket_power_w,
                     l_wth->nest_frequency_mhz, l_wth->sort_power_freq_mhz);

            // Compare the fields
            if( (l_wth->core_count == l_numCores) &&
                (l_wth->socket_power_w == l_socketPower) &&
                (l_wth->sort_power_freq_mhz == l_sortFreq) &&
                ((l_ver < WOF_TABLE_VERSION_POWERMODE) ||  // mode is ignored
                 ((l_ver >= WOF_TABLE_VERSION_POWERMODE) &&
                  ((l_wth->mode == l_mode) ||  // match specific mode
                   (l_wth->mode == WOF_MODE_UNKNOWN)))) // or wild-card
              )
            {
                // Found a match
                FAPI_INF("Found a WOF table match");

                // Copy the WOF table to the output pointer
                memcpy(o_wofData,
                       reinterpret_cast<uint8_t*>(l_wth),
                       l_ste[l_ent].size);

                break;
            }
            else
            {
                // Save the header for later
                l_headers.push_back(l_wth);

                // We run with fewer cores in Simics, ignore the core field
                //  if we don't find a complete match
                if( Util::isSimicsRunning() && l_simMatch == nullptr )
                {
                    if( (l_wth->socket_power_w == l_socketPower) &&
                        (l_wth->sort_power_freq_mhz == l_sortFreq) &&
                        ((l_ver < WOF_TABLE_VERSION_POWERMODE) || //mode ignored
                         ((l_ver >= WOF_TABLE_VERSION_POWERMODE) &&
                          ((l_wth->mode == l_mode) ||  // match specific mode
                          (l_wth->mode == WOF_MODE_UNKNOWN)))) // or wild-card
                      )
                    {
                        FAPI_INF("Found a potential WOF table match for Simics");
                        // Copy the WOF table to a local var temporarily
                        l_simMatchSize = l_ste[l_ent].size;
                        l_simMatch = new uint8_t[l_simMatchSize];
                        memcpy(l_simMatch,
                               reinterpret_cast<uint8_t*>(l_wth),
                               l_simMatchSize);
                    }
                }
            }
        }

        if(l_errl)
        {
            break;
        }

        if( Util::isSimicsRunning()
            && (l_ent == l_img->entryCount)
            && (l_simMatch != nullptr) )
        {
            FAPI_INF("Using fuzzy match for Simics");
            // Copy the WOF table to the ouput pointer
            memcpy(o_wofData,
                   l_simMatch,
                   l_simMatchSize);
        }
        //Check for no match
        else if(l_ent == l_img->entryCount)
        {
            FAPI_ERR("No WOF table match found");
            /*@
            * @errortype
            * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
            * @reasoncode        fapi2::RC_WOF_TABLE_NOT_FOUND
            * @userdata1[00:15]  Number of cores
            * @userdata1[16:31]  WOF Power Mode (0=Nominal,1=Turbo)
            * @userdata1[32:63]  Socket power
            * @userdata2[00:31]  Nest frequency
            * @userdata2[32:63]  Sort frequency
            * @devdesc           No WOF table match found
            * @custdesc          Firmware Error
            */
            l_errl = new ERRORLOG::ErrlEntry(
                            ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                            fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                            fapi2::RC_WOF_TABLE_NOT_FOUND,
                            TWO_UINT16_ONE_UINT32_TO_UINT64(
                                    l_numCores,
                                    l_wofPowerLimit,
                                    l_socketPower),
                            TWO_UINT32_TO_UINT64(
                                    l_nestFreq,
                                    l_sortFreq),
                            true); //software callout
            l_errl->collectTrace(FAPI_TRACE_NAME);

            // Add data
            ERRORLOG::ErrlUserDetailsAttribute(
                            l_sys,
                            TARGETING::ATTR_WOF_TABLE_LID_NUMBER)
                            .addToLog(l_errl);
            while(l_headers.size())
            {
                l_errl->addFFDC(
                        HWPF_COMP_ID,
                        l_headers.back(),
                        sizeof(WofTablesHeader_t),
                        0,                           // version
                        ERRORLOG::ERRL_UDT_NOFORMAT, // parser ignores data
                        false );                     // merge
                l_headers.pop_back();
            }

            l_rc.setPlatDataPtr(reinterpret_cast<void *>(l_errl));
            break;
        }

    } while(0);

    // Free the wof tables memory
    if(l_pWofImage != nullptr)
    {
#ifdef __HOSTBOOT_RUNTIME
        free(l_pWofImage);
        l_pWofImage = nullptr;

#else
        errlHndl_t l_tmpErr = nullptr;
        // Release the memory we may still have allocated and set the
        //  permissions to prevent further access to it
        int l_mm_rc = mm_remove_pages(RELEASE,
                                      l_pWofImage,
                                      VMM_SIZE_WOFDATA_LID);
        if( l_mm_rc )
        {
            FAPI_INF("Fail from mm_remove_pages for WOFDATA, rc=%d", l_mm_rc);
            /*@
             * @errortype
             * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
             * @reasoncode        fapi2::RC_MM_REMOVE_PAGES_FAILED
             * @userdata1         Address being removed
             * @userdata2[00:31]  Size of removal
             * @userdata2[32:63]  rc from mm_remove_pages
             * @devdesc           Error calling mm_remove_pages for WOFDATA
             * @custdesc          Firmware Error
             */
            l_tmpErr = new ERRORLOG::ErrlEntry(
                                   ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                                   fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                                   fapi2::RC_MM_REMOVE_PAGES_FAILED,
                                   reinterpret_cast<uint64_t>(l_pWofImage),
                                   TWO_UINT32_TO_UINT64(VMM_SIZE_WOFDATA_LID,
                                                        l_mm_rc),
                                   true); //software callout
            errlCommit(l_tmpErr,FAPI2_COMP_ID);
        }
        l_mm_rc = mm_set_permission(l_pWofImage,
                                    VMM_SIZE_WOFDATA_LID,
                                    NO_ACCESS | ALLOCATE_FROM_ZERO );
        if( l_mm_rc )
        {
            FAPI_INF("Fail from mm_set_permission reset for WOFDATA, rc=%d", l_mm_rc);
            /*@
             * @errortype
             * @moduleid          fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES
             * @reasoncode        fapi2::RC_MM_SET_PERMISSION2_FAILED
             * @userdata1         Address being changed
             * @userdata2[00:31]  Size of change
             * @userdata2[32:63]  rc from mm_set_permission
             * @devdesc           Error calling mm_set_permission for WOFDATA
             * @custdesc          Firmware Error
             */
            l_tmpErr = new ERRORLOG::ErrlEntry(
                                   ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                                   fapi2::MOD_FAPI2_PLAT_PARSE_WOF_TABLES,
                                   fapi2::RC_MM_SET_PERMISSION2_FAILED,
                                   reinterpret_cast<uint64_t>(l_pWofImage),
                                   TWO_UINT32_TO_UINT64(VMM_SIZE_WOFDATA_LID,
                                                        l_mm_rc),
                                   true); //software callout
            errlCommit(l_tmpErr,FAPI2_COMP_ID);
        }
#endif

    }

    if( l_simMatch )
    {
        delete[] l_simMatch;
    }

    FAPI_DBG("Exiting platParseWOFTables ....");

    return l_rc;
}

} // End platAttrSvc namespace
} // End fapi2 namespace
