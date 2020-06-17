/*
 * Copyright (C) 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef GCSS_ISP_UTILS_H_
#define GCSS_ISP_UTILS_H_

#include "gcss.h"
#include "graph_utils.h"

namespace GCSS {

struct KernelConfigContainer {
    int32_t overwriteMode = 0; /**< use this flag to tell which values to update */
    uint32_t kernel_uuid;
    int32_t enable;
    uint32_t metadata[4];
    ia_isp_bxt_bpp_info_t bpp_info;
};

/**
* These modes are used to tell which kernel properties are to be overwritten
*/
enum OverwriteMode {
    OVERWRITE_ENABLE   = 1 << 1,
    OVERWRITE_BPP      = 1 << 2,
    OVERWRITE_METADATA = 1 << 3
};

static const std::string IPU_VER_5("IPU5");
static const std::string IPU_VER_6("IPU6");
static const std::string IPU_VER_7("IPU7");
/**
 * Vector that is used to hold kernelConfig structs per kernel
 */
typedef std::vector<KernelConfigContainer> KernelConfigs;

/** \class IspUtils
 * Provides IPU specific utilities that can be accessed through
 * the pointer constructed with the Factory method.
 */
class IspUtils {

public:
    virtual ~IspUtils() {}

    /**
    * Isp Utils Factory
    *
    * \ingroup gcss
    *
    * Returns pointer that allows access to common and IPU specific utilities.
    * Ipu is automatically selected based on the version attribute in the
    * graph descriptor.
    *
    * \param[in]  settings
    * \return     pointer to one of the ipu specific implementations
    */
    static std::shared_ptr<IspUtils> Factory(const IGraphConfig *settings);

    /**
    * Is dvs enabled
    *
    * \ingroup gcss
    *
    * Returns true if dvs enabled in settings. False otherwise,
    *
    * \return true if dvs is enabled in the settings
    * \return false if dvs disabled in the settings
    */
    virtual bool isDvsEnabled() = 0;

    /**
    * Get IPU version
    *
    * \ingroup gcss
    *
    * Returns ipu version as GdfVersion type
    *
    * \return GdfVersion
    */
    virtual GdfVersion getIpuVersion() = 0;

    /**
    * Get kernel configurations
    *
    * \ingroup gcss
    *
    * Returns map of runtime kernel configurations. KernelConfigurations map
    * contains pal uuid and enable value for kernel. Use through ipu specific
    * implementation.
    *
    * \param[out] KernelConfigContainer populated with kernel configs
    * \return css_err_none on success
    * \return css_err_nimpl if function not implemented
    * \return css_err_general in case of error
    */
    virtual css_err_t getKernelConfigurations(KernelConfigs &kConfig) = 0;

    /** Apply given format to output port
    *
    * \ingroup gcss
    *
    * Applies given format to the port that the sink is connected to.
    * The given format has to be present in the options list of the pg where the
    * port belongs to. If there is no options list for the pg, then no error is
    * returned and no format applied.
    *
    * \param[in] sink    Pointer to the sink in the graph.
    * \param[in] format  Name of the format that is being applied
    * \return css_err_none   in case of success
    * \return css_err_data   in case the given format is not in the options list
    */
    virtual css_err_t applyFormat(const IGraphConfig *sink,
        const std::string &format) = 0;

    /** Applies compression to full pipe and sets given format to output port
    *
    * \ingroup gcss
    *
    * Applies given format to the port that the sink is connected to.
    * The given format has to be present in the options list of the pg where the
    * port belongs to. If there is no options list for the pg, then no error is
    * returned and no format applied. Applies compression also to PSA and to
    * tnr ports if present.
    *
    * \param[in] sink    Pointer to the sink in the graph.
    * \param[in] format  Name of the compressed format that is being applied
    * \return css_err_none       in case of success
    * \return css_err_argument   in case the given format is not compressed
    * \return css_err_data       in case the given format is not in the options list
    */
    virtual css_err_t applyCompression(const IGraphConfig *sink,
        const std::string &format) = 0;
};
} // namespace
#endif