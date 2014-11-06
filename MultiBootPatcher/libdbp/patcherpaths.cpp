/*
 * Copyright (C) 2014  Xiao-Long Chen <chenxiaolong@cxl.epac.to>
 *
 * This file is part of MultiBootPatcher
 *
 * MultiBootPatcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiBootPatcher is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiBootPatcher.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "patcherpaths.h"

#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/regex.hpp>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "device.h"
#include "patcherinterface.h"
#include "patchinfo.h"
#include "private/logging.h"

// Patchers
#include "patchers/multiboot/multibootpatcher.h"
#include "patchers/primaryupgrade/primaryupgradepatcher.h"
#include "patchers/syncdaemonupdate/syncdaemonupdatepatcher.h"
#include "autopatchers/jflte/jfltepatcher.h"
#include "autopatchers/noobdev/noobdevpatcher.h"
#include "autopatchers/patchfile/patchfilepatcher.h"
#include "autopatchers/standard/standardpatcher.h"
#include "ramdiskpatchers/bacon/baconramdiskpatcher.h"
#include "ramdiskpatchers/d800/d800ramdiskpatcher.h"
#include "ramdiskpatchers/falcon/falconramdiskpatcher.h"
#include "ramdiskpatchers/hammerhead/hammerheadramdiskpatcher.h"
#include "ramdiskpatchers/hlte/hlteramdiskpatcher.h"
#include "ramdiskpatchers/jflte/jflteramdiskpatcher.h"
#include "ramdiskpatchers/klte/klteramdiskpatcher.h"


class PatcherPaths::Impl
{
public:
    ~Impl();

    // Directories
    std::string binariesDir;
    std::string dataDir;
    std::string initsDir;
    std::string patchesDir;
    std::string patchInfosDir;
    std::string scriptsDir;

    std::string version;
    std::vector<Device *> devices;
    std::vector<std::string> patchinfoIncludeDirs;

    // PatchInfos
    std::vector<PatchInfo *> patchInfos;

    // Partition configurations
    std::vector<PartitionConfig *> partConfigs;

    bool loadedConfig;

    // Errors
    PatcherError error;

    // XML parsing functions for the patchinfo files
    bool loadPatchInfoXml(const std::string &path, const std::string &pathId);
    void parsePatchInfoTagPatchinfo(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagMatches(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagNotMatched(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagName(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagRegex(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagExcludeRegex(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagRegexes(xmlNode *node, PatchInfo * const info);
    void parsePatchInfoTagHasBootImage(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagRamdisk(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagPatchedInit(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagAutopatchers(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagAutopatcher(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagDeviceCheck(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagPartconfigs(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagExclude(xmlNode *node, PatchInfo * const info, const std::string &type);
    void parsePatchInfoTagInclude(xmlNode *node, PatchInfo * const info, const std::string &type);
};


static const std::string BinariesDirName = "binaries";
static const std::string InitsDirName = "inits";
static const std::string PatchesDirName = "patches";
static const std::string PatchInfosDirName = "patchinfos";
static const std::string ScriptsDirName = "scripts";

PatcherPaths::Impl::~Impl()
{
    for (Device *device : devices) {
        delete device;
    }
}

// --------------------------------

const xmlChar *PatchInfoTagPatchinfo = (xmlChar *) "patchinfo";
const xmlChar *PatchInfoTagMatches = (xmlChar *) "matches";
const xmlChar *PatchInfoTagNotMatched = (xmlChar *) "not-matched";
const xmlChar *PatchInfoTagName = (xmlChar *) "name";
const xmlChar *PatchInfoTagRegex = (xmlChar *) "regex";
const xmlChar *PatchInfoTagExcludeRegex = (xmlChar *) "exclude-regex";
const xmlChar *PatchInfoTagRegexes = (xmlChar *) "regexes";
const xmlChar *PatchInfoTagHasBootImage = (xmlChar *) "has-boot-image";
const xmlChar *PatchInfoTagRamdisk = (xmlChar *) "ramdisk";
const xmlChar *PatchInfoTagPatchedInit = (xmlChar *) "patched-init";
const xmlChar *PatchInfoTagAutopatchers = (xmlChar *) "autopatchers";
const xmlChar *PatchInfoTagAutopatcher = (xmlChar *) "autopatcher";
const xmlChar *PatchInfoTagDeviceCheck = (xmlChar *) "device-check";
const xmlChar *PatchInfoTagPartconfigs = (xmlChar *) "partconfigs";
const xmlChar *PatchInfoTagInclude = (xmlChar *) "include";
const xmlChar *PatchInfoTagExclude = (xmlChar *) "exclude";

const xmlChar *PatchInfoAttrRegex = (xmlChar *) "regex";

const xmlChar *XmlTextTrue = (xmlChar *) "true";
const xmlChar *XmlTextFalse = (xmlChar *) "false";

PatcherPaths::PatcherPaths() : m_impl(new Impl())
{
    loadDefaultDevices();
    loadDefaultPatchers();

    m_impl->patchinfoIncludeDirs.push_back("Google_Apps");
    m_impl->patchinfoIncludeDirs.push_back("Other");

    m_impl->version = LIBDBP_VERSION;
}

PatcherPaths::~PatcherPaths()
{
    // Clean up devices
    for (Device *device : m_impl->devices) {
        delete device;
    }
    m_impl->devices.clear();

    // Clean up patchinfos
    for (PatchInfo *info : m_impl->patchInfos) {
        delete info;
    }
    m_impl->patchInfos.clear();

    // Clean up partconfigs
    for (PartitionConfig *config : m_impl->partConfigs) {
        delete config;
    }
    m_impl->partConfigs.clear();
}

PatcherError PatcherPaths::error() const
{
    return m_impl->error;
}

std::string PatcherPaths::binariesDirectory() const
{
    if (m_impl->binariesDir.empty()) {
        return dataDirectory() + "/" + BinariesDirName;
    } else {
        return m_impl->binariesDir;
    }
}

std::string PatcherPaths::dataDirectory() const
{
    return m_impl->dataDir;
}

std::string PatcherPaths::initsDirectory() const
{
    if (m_impl->initsDir.empty()) {
        return dataDirectory() + "/" + InitsDirName;
    } else {
        return m_impl->initsDir;
    }
}

std::string PatcherPaths::patchesDirectory() const
{
    if (m_impl->patchesDir.empty()) {
        return dataDirectory() + "/" + PatchesDirName;
    } else {
        return m_impl->patchesDir;
    }
}

std::string PatcherPaths::patchInfosDirectory() const
{
    if (m_impl->patchInfosDir.empty()) {
        return dataDirectory() + "/" + PatchInfosDirName;
    } else {
        return m_impl->patchInfosDir;
    }
}

std::string PatcherPaths::scriptsDirectory() const
{
    if (m_impl->scriptsDir.empty()) {
        return dataDirectory() + "/" + ScriptsDirName;
    } else {
        return m_impl->scriptsDir;
    }
}

void PatcherPaths::setBinariesDirectory(std::string path)
{
    m_impl->binariesDir = std::move(path);
}

void PatcherPaths::setDataDirectory(std::string path)
{
    m_impl->dataDir = std::move(path);
}

void PatcherPaths::setInitsDirectory(std::string path)
{
    m_impl->initsDir = std::move(path);
}

void PatcherPaths::setPatchesDirectory(std::string path)
{
    m_impl->patchesDir = std::move(path);
}

void PatcherPaths::setPatchInfosDirectory(std::string path)
{
    m_impl->patchInfosDir = std::move(path);
}

void PatcherPaths::setScriptsDirectory(std::string path)
{
    m_impl->scriptsDir = std::move(path);
}

void PatcherPaths::reset()
{
    // Paths
    m_impl->dataDir.clear();
    m_impl->initsDir.clear();
    m_impl->patchesDir.clear();
    m_impl->patchInfosDir.clear();

    for (Device *device : m_impl->devices) {
        delete device;
    }

    m_impl->devices.clear();
}

std::string PatcherPaths::version() const
{
    return m_impl->version;
}

std::vector<Device *> PatcherPaths::devices() const
{
    return m_impl->devices;
}

Device * PatcherPaths::deviceFromCodename(const std::string &codename) const
{
    for (Device * device : m_impl->devices) {
        if (device->codename() == codename) {
            return device;
        }
    }

    return nullptr;
}

std::vector<PatchInfo *> PatcherPaths::patchInfos() const
{
    return m_impl->patchInfos;
}

std::vector<PatchInfo *> PatcherPaths::patchInfos(const Device * const device) const
{
    std::vector<PatchInfo *> l;

    for (PatchInfo *info : m_impl->patchInfos) {
        if (boost::starts_with(info->id(), device->codename())) {
            l.push_back(info);
            continue;
        }

        for (auto const &include : m_impl->patchinfoIncludeDirs) {
            if (boost::starts_with(info->id(), include)) {
                l.push_back(info);
                break;
            }
        }
    }

    return l;
}

PatchInfo * PatcherPaths::findMatchingPatchInfo(Device *device,
                                                const std::string &filename)
{
    if (device == nullptr) {
        return nullptr;
    }

    if (filename.empty()) {
        return nullptr;
    }

    std::string noPath = boost::filesystem::path(filename).filename().string();

    for (PatchInfo *info : patchInfos(device)) {
        for (auto const &regex : info->regexes()) {
            if (boost::regex_search(noPath, boost::regex(regex))) {
                bool skipCurInfo = false;

                // If the regex matches, make sure the filename isn't matched
                // by one of the exclusion regexes
                for (auto const &excludeRegex : info->excludeRegexes()) {
                    if (boost::regex_search(noPath, boost::regex(excludeRegex))) {
                        skipCurInfo = true;
                        break;
                    }
                }

                if (skipCurInfo) {
                    break;
                }

                return info;
            }
        }
    }

    return nullptr;
}

void PatcherPaths::loadDefaultDevices()
{
    Device *device;

    // Samsung Galaxy S 4
    device = new Device();
    device->setCodename("jflte");
    device->setName("Samsung Galaxy S 4");
    device->setSelinux(Device::SelinuxPermissive);
    device->setPartition(Device::SystemPartition, "mmcblk0p16");
    device->setPartition(Device::CachePartition, "mmcblk0p18");
    device->setPartition(Device::DataPartition, "mmcblk0p29");
    m_impl->devices.push_back(device);

    // Samsung Galaxy S 5
    device = new Device();
    device->setCodename("klte");
    device->setName("Samsung Galaxy S 5");
    device->setSelinux(Device::SelinuxPermissive);
    device->setPartition(Device::SystemPartition, "mmcblk0p23");
    device->setPartition(Device::CachePartition, "mmcblk0p24");
    device->setPartition(Device::DataPartition, "mmcblk0p26");
    m_impl->devices.push_back(device);

    // Samsung Galaxy Note 3
    device = new Device();
    device->setCodename("hlte");
    device->setName("Samsung Galaxy Note 3");
    device->setSelinux(Device::SelinuxPermissive);
    device->setPartition(Device::SystemPartition, "mmcblk0p23");
    device->setPartition(Device::CachePartition, "mmcblk0p24");
    device->setPartition(Device::DataPartition, "mmcblk0p26");
    m_impl->devices.push_back(device);

    // Google/LG Nexus 5
    device = new Device();
    device->setCodename("hammerhead");
    device->setName("Google/LG Nexus 5");
    device->setSelinux(Device::SelinuxUnchanged);
    m_impl->devices.push_back(device);

    // OnePlus One
    device = new Device();
    device->setCodename("bacon");
    device->setName("OnePlus One");
    device->setSelinux(Device::SelinuxUnchanged);
    m_impl->devices.push_back(device);

    // LG G2
    device = new Device();
    device->setCodename("d800");
    device->setName("LG G2");
    device->setSelinux(Device::SelinuxUnchanged);
    m_impl->devices.push_back(device);

    // Falcon
    device = new Device();
    device->setCodename("falcon");
    device->setName("Motorola Moto G");
    device->setSelinux(Device::SelinuxUnchanged);
    m_impl->devices.push_back(device);
}

void PatcherPaths::loadDefaultPatchers()
{
    auto configs1 = MultiBootPatcher::partConfigs();
    auto configs2 = PrimaryUpgradePatcher::partConfigs();

    m_impl->partConfigs.insert(m_impl->partConfigs.end(),
                               configs1.begin(), configs1.end());
    m_impl->partConfigs.insert(m_impl->partConfigs.end(),
                               configs2.begin(), configs2.end());
}

std::vector<std::string> PatcherPaths::patchers() const
{
    std::vector<std::string> list;
    list.push_back(MultiBootPatcher::Id);
    list.push_back(PrimaryUpgradePatcher::Id);
    list.push_back(SyncdaemonUpdatePatcher::Id);
    return list;
}

std::vector<std::string> PatcherPaths::autoPatchers() const
{
    std::vector<std::string> list;
    list.push_back(JflteDalvikCachePatcher::Id);
    list.push_back(JflteGoogleEditionPatcher::Id);
    list.push_back(JflteSlimAromaBundledMount::Id);
    list.push_back(JflteImperiumPatcher::Id);
    list.push_back(JflteNegaliteNoWipeData::Id);
    list.push_back(JflteTriForceFixAroma::Id);
    list.push_back(JflteTriForceFixUpdate::Id);
    list.push_back(NoobdevMultiBoot::Id);
    list.push_back(NoobdevSystemProp::Id);
    list.push_back(PatchFilePatcher::Id);
    list.push_back(StandardPatcher::Id);
    return list;
}

std::vector<std::string> PatcherPaths::ramdiskPatchers() const
{
    std::vector<std::string> list;
    list.push_back(BaconRamdiskPatcher::Id);
    list.push_back(D800RamdiskPatcher::Id);
    list.push_back(FalconRamdiskPatcher::Id);
    list.push_back(HammerheadAOSPRamdiskPatcher::Id);
    list.push_back(HammerheadNoobdevRamdiskPatcher::Id);
    list.push_back(HlteAOSPRamdiskPatcher::Id);
    list.push_back(JflteAOSPRamdiskPatcher::Id);
    list.push_back(JflteGoogleEditionRamdiskPatcher::Id);
    list.push_back(JflteNoobdevRamdiskPatcher::Id);
    list.push_back(JflteTouchWizRamdiskPatcher::Id);
    list.push_back(KlteAOSPRamdiskPatcher::Id);
    list.push_back(KlteTouchWizRamdiskPatcher::Id);
    return list;
}

std::shared_ptr<Patcher> PatcherPaths::createPatcher(const std::string &id) const
{
    if (id == MultiBootPatcher::Id) {
        return std::make_shared<MultiBootPatcher>(this);
    } else if (id == PrimaryUpgradePatcher::Id) {
        return std::make_shared<PrimaryUpgradePatcher>(this);
    } else if (id == SyncdaemonUpdatePatcher::Id) {
        return std::make_shared<SyncdaemonUpdatePatcher>(this);
    }

    return std::shared_ptr<Patcher>();
}

std::shared_ptr<AutoPatcher> PatcherPaths::createAutoPatcher(const std::string &id,
                                                             const FileInfo * const info,
                                                             const PatchInfo::AutoPatcherArgs &args) const
{
    if (id == JflteDalvikCachePatcher::Id) {
        return std::make_shared<JflteDalvikCachePatcher>(this, info);
    } else if (id == JflteGoogleEditionPatcher::Id) {
        return std::make_shared<JflteGoogleEditionPatcher>(this, info);
    } else if (id == JflteSlimAromaBundledMount::Id) {
        return std::make_shared<JflteSlimAromaBundledMount>(this, info);
    } else if (id == JflteImperiumPatcher::Id) {
        return std::make_shared<JflteImperiumPatcher>(this, info);
    } else if (id == JflteNegaliteNoWipeData::Id) {
        return std::make_shared<JflteNegaliteNoWipeData>(this, info);
    } else if (id == JflteTriForceFixAroma::Id) {
        return std::make_shared<JflteTriForceFixAroma>(this, info);
    } else if (id == JflteTriForceFixUpdate::Id) {
        return std::make_shared<JflteTriForceFixUpdate>(this, info);
    } else if (id == NoobdevMultiBoot::Id) {
        return std::make_shared<NoobdevMultiBoot>(this, info);
    } else if (id == NoobdevSystemProp::Id) {
        return std::make_shared<NoobdevSystemProp>(this, info);
    } else if (id == PatchFilePatcher::Id) {
        return std::make_shared<PatchFilePatcher>(this, info, args);
    } else if (id == StandardPatcher::Id) {
        return std::make_shared<StandardPatcher>(this, info, args);
    }

    return std::shared_ptr<AutoPatcher>();
}

std::shared_ptr<RamdiskPatcher> PatcherPaths::createRamdiskPatcher(const std::string &id,
                                                                   const FileInfo * const info,
                                                                   CpioFile * const cpio) const
{
    if (id == BaconRamdiskPatcher::Id) {
        return std::make_shared<BaconRamdiskPatcher>(this, info, cpio);
    } else if (id == D800RamdiskPatcher::Id) {
        return std::make_shared<D800RamdiskPatcher>(this, info, cpio);
    } else if (id == FalconRamdiskPatcher::Id) {
        return std::make_shared<FalconRamdiskPatcher>(this, info, cpio);
    } else if (id == HammerheadAOSPRamdiskPatcher::Id) {
        return std::make_shared<HammerheadAOSPRamdiskPatcher>(this, info, cpio);
    } else if (id == HammerheadNoobdevRamdiskPatcher::Id) {
        return std::make_shared<HammerheadNoobdevRamdiskPatcher>(this, info, cpio);
    } else if (id == HlteAOSPRamdiskPatcher::Id) {
        return std::make_shared<HlteAOSPRamdiskPatcher>(this, info, cpio);
    } else if (id == JflteAOSPRamdiskPatcher::Id) {
        return std::make_shared<JflteAOSPRamdiskPatcher>(this, info, cpio);
    } else if (id == JflteGoogleEditionRamdiskPatcher::Id) {
        return std::make_shared<JflteGoogleEditionRamdiskPatcher>(this, info, cpio);
    } else if (id == JflteNoobdevRamdiskPatcher::Id) {
        return std::make_shared<JflteNoobdevRamdiskPatcher>(this, info, cpio);
    } else if (id == JflteTouchWizRamdiskPatcher::Id) {
        return std::make_shared<JflteTouchWizRamdiskPatcher>(this, info, cpio);
    } else if (id == KlteAOSPRamdiskPatcher::Id) {
        return std::make_shared<KlteAOSPRamdiskPatcher>(this, info, cpio);
    } else if (id == KlteTouchWizRamdiskPatcher::Id) {
        return std::make_shared<KlteTouchWizRamdiskPatcher>(this, info, cpio);
    }

    return std::shared_ptr<RamdiskPatcher>();
}

std::string PatcherPaths::patcherName(const std::string &id) const
{
    if (id == MultiBootPatcher::Id) {
        return MultiBootPatcher::Name;
    } else if (id == PrimaryUpgradePatcher::Id) {
        return PrimaryUpgradePatcher::Name;
    } else if (id == SyncdaemonUpdatePatcher::Id) {
        return SyncdaemonUpdatePatcher::Name;
    }

    return std::string();
}

std::vector<PartitionConfig *> PatcherPaths::partitionConfigs() const
{
    return m_impl->partConfigs;
}

PartitionConfig * PatcherPaths::partitionConfig(const std::string &id) const
{
    for (PartitionConfig *config : m_impl->partConfigs) {
        if (config->id() == id) {
            return config;
        }
    }

    return nullptr;
}

std::vector<std::string> PatcherPaths::initBinaries() const
{
    std::vector<std::string> inits;

    try {
        const std::string dir = initsDirectory() + "/";

        boost::filesystem::recursive_directory_iterator it(dir);
        boost::filesystem::recursive_directory_iterator end;

        for (; it != end; ++it) {
            if (boost::filesystem::is_regular_file(it->status())) {
                std::string relPath = it->path().string();
                boost::erase_first(relPath, dir);
                inits.push_back(relPath);
            }
        }
    } catch (std::exception &e) {
        Log::log(Log::Warning, e.what());
    }

    std::sort(inits.begin(), inits.end());

    return inits;
}

bool PatcherPaths::loadPatchInfos()
{
    try {
        const std::string dir = patchInfosDirectory() + "/";

        boost::filesystem::recursive_directory_iterator it(dir);
        boost::filesystem::recursive_directory_iterator end;

        for (; it != end; ++it) {
            if (boost::filesystem::is_regular_file(it->status())
                    && it->path().extension() == ".xml") {
                std::string id = it->path().string();
                boost::erase_first(id, dir);
                boost::erase_tail(id, 4);

                if (!m_impl->loadPatchInfoXml(it->path().string(), id)) {
                    m_impl->error = PatcherError::createXmlError(
                            PatcherError::XmlParseFileError, it->path().string());
                    return false;
                }
            }
        }

        return true;
    } catch (std::exception &e) {
        Log::log(Log::Warning, e.what());
    }

    return false;
}

bool PatcherPaths::Impl::loadPatchInfoXml(const std::string &path,
                                          const std::string &pathId)
{
    (void) pathId;

    LIBXML_TEST_VERSION

    xmlDoc *doc = xmlReadFile(path.c_str(), nullptr, 0);
    if (doc == nullptr) {
        error = PatcherError::createXmlError(
                PatcherError::XmlParseFileError, path);
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);

    for (auto *curNode = root; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagPatchinfo) == 0) {
            PatchInfo *info = new PatchInfo();
            parsePatchInfoTagPatchinfo(curNode, info);
            info->setId(pathId);
            patchInfos.push_back(info);
        } else {
            Log::log(Log::Warning, "Unknown tag: %s", (char *) curNode->name);
        }
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return true;
}

static std::string xmlStringToStdString(const xmlChar* xmlString) {
    if (xmlString) {
        return std::string(reinterpret_cast<const char*>(xmlString));
    } else {
        return std::string();
    }
}

void PatcherPaths::Impl::parsePatchInfoTagPatchinfo(xmlNode *node,
                                                    PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagPatchinfo) == 0);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagPatchinfo) == 0) {
            Log::log(Log::Warning, "Nested <patchinfo> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagMatches) == 0) {
            parsePatchInfoTagMatches(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagNotMatched) == 0) {
            parsePatchInfoTagNotMatched(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagName) == 0) {
            parsePatchInfoTagName(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRegex) == 0) {
            parsePatchInfoTagRegex(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRegexes) == 0) {
            parsePatchInfoTagRegexes(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagHasBootImage) == 0) {
            parsePatchInfoTagHasBootImage(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRamdisk) == 0) {
            parsePatchInfoTagRamdisk(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagPatchedInit) == 0) {
            parsePatchInfoTagPatchedInit(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            parsePatchInfoTagAutopatchers(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagDeviceCheck) == 0) {
            parsePatchInfoTagDeviceCheck(curNode, info, PatchInfo::Default);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagPartconfigs) == 0) {
            parsePatchInfoTagPartconfigs(curNode, info, PatchInfo::Default);
        } else {
            Log::log(Log::Warning, "Unrecognized tag within <patchinfo>: %s",
                     (char *) curNode->name);
        }
    }
}

void PatcherPaths::Impl::parsePatchInfoTagMatches(xmlNode *node,
                                                  PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagMatches) == 0);

    xmlChar *value = xmlGetProp(node, PatchInfoAttrRegex);
    if (value == nullptr) {
        Log::log(Log::Warning, "<matches> element has no 'regex' attribute");
        return;
    }

    const std::string regex = xmlStringToStdString(value);
    xmlFree(value);

    auto regexes = info->condRegexes();
    regexes.push_back(regex);
    info->setCondRegexes(std::move(regexes));

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagMatches) == 0) {
            Log::log(Log::Warning, "Nested <matches> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagHasBootImage) == 0) {
            parsePatchInfoTagHasBootImage(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRamdisk) == 0) {
            parsePatchInfoTagRamdisk(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagPatchedInit) == 0) {
            parsePatchInfoTagPatchedInit(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            parsePatchInfoTagAutopatchers(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagDeviceCheck) == 0) {
            parsePatchInfoTagDeviceCheck(curNode, info, regex);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagPartconfigs) == 0) {
            parsePatchInfoTagPartconfigs(curNode, info, regex);
        } else {
            Log::log(Log::Warning, "Unrecognized tag within <matches>: %s",
                     (char *) curNode->name);
        }
    }
}

void PatcherPaths::Impl::parsePatchInfoTagNotMatched(xmlNode *node,
                                                     PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagNotMatched) == 0);

    info->setHasNotMatched(true);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagNotMatched) == 0) {
            Log::log(Log::Warning, "Nested <not-matched> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagHasBootImage) == 0) {
            parsePatchInfoTagHasBootImage(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRamdisk) == 0) {
            parsePatchInfoTagRamdisk(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagPatchedInit) == 0) {
            parsePatchInfoTagPatchedInit(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            parsePatchInfoTagAutopatchers(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagDeviceCheck) == 0) {
            parsePatchInfoTagDeviceCheck(curNode, info, PatchInfo::NotMatched);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagPartconfigs) == 0) {
            parsePatchInfoTagPartconfigs(curNode, info, PatchInfo::NotMatched);
        } else {
            Log::log(Log::Warning, "Unrecognized tag within <not-matched>: %s",
                     (char *) curNode->name);
        }
    }
}

void PatcherPaths::Impl::parsePatchInfoTagName(xmlNode *node,
                                               PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagName) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (info->name().empty()) {
            info->setName(xmlStringToStdString(curNode->content));
        } else {
            Log::log(Log::Warning, "Ignoring additional <name> elements");
        }
    }

    if (!hasText) {
        Log::log(Log::Warning, "<name> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagRegex(xmlNode *node,
                                                PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagRegex) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        auto regexes = info->regexes();
        regexes.push_back(xmlStringToStdString(curNode->content));
        info->setRegexes(std::move(regexes));
    }

    if (!hasText) {
        Log::log(Log::Warning, "<regex> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagExcludeRegex(xmlNode *node,
                                                       PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagExcludeRegex) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        auto regexes = info->excludeRegexes();
        regexes.push_back(xmlStringToStdString(curNode->content));
        info->setExcludeRegexes(std::move(regexes));
    }

    if (!hasText) {
        Log::log(Log::Warning, "<exclude-regex> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagRegexes(xmlNode *node,
                                                  PatchInfo * const info)
{
    assert(xmlStrcmp(node->name, PatchInfoTagRegexes) == 0);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagRegexes) == 0) {
            Log::log(Log::Warning, "Nested <regexes> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagRegex) == 0) {
            parsePatchInfoTagRegex(curNode, info);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagExcludeRegex) == 0) {
            parsePatchInfoTagExcludeRegex(curNode, info);
        } else {
            Log::log(Log::Warning, "Unrecognized tag within <regexes>: %s",
                     (char *) curNode->name);
        }
    }
}

void PatcherPaths::Impl::parsePatchInfoTagHasBootImage(xmlNode *node,
                                                       PatchInfo * const info,
                                                       const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagHasBootImage) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (xmlStrcmp(curNode->content, XmlTextTrue) == 0) {
            info->setHasBootImage(type, true);
        } else if (xmlStrcmp(curNode->content, XmlTextFalse) == 0) {
            info->setHasBootImage(type, false);
        } else {
            Log::log(Log::Warning, "Unknown value for <has-boot-image>: %s",
                     (char *) curNode->content);
        }
    }

    if (!hasText) {
        Log::log(Log::Warning, "<has-boot-image> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagRamdisk(xmlNode *node,
                                                  PatchInfo * const info,
                                                  const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagRamdisk) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (info->ramdisk(type).empty()) {
            info->setRamdisk(type, xmlStringToStdString(curNode->content));
        } else {
            Log::log(Log::Warning, "Ignoring additional <ramdisk> elements");
        }
    }

    if (!hasText) {
        Log::log(Log::Warning, "<ramdisk> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagPatchedInit(xmlNode *node,
                                                      PatchInfo * const info,
                                                      const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagPatchedInit) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (info->patchedInit(type).empty()) {
            info->setPatchedInit(type, xmlStringToStdString(curNode->content));
        } else {
            Log::log(Log::Warning, "Ignoring additional <patched-init> elements");
        }
    }

    if (!hasText) {
        Log::log(Log::Warning, "<patched-init> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagAutopatchers(xmlNode *node,
                                                       PatchInfo * const info,
                                                       const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagAutopatchers) == 0);

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagAutopatchers) == 0) {
            Log::log(Log::Warning, "Nested <autopatchers> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagAutopatcher) == 0) {
            parsePatchInfoTagAutopatcher(curNode, info, type);
        } else {
            Log::log(Log::Warning, "Unrecognized tag within <autopatchers>: %s",
                     (char *) curNode->name);
        }
    }
}

void PatcherPaths::Impl::parsePatchInfoTagAutopatcher(xmlNode *node,
                                                      PatchInfo * const info,
                                                      const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagAutopatcher) == 0);

    PatchInfo::AutoPatcherArgs args;

    for (xmlAttr *attr = node->properties; attr; attr = attr->next) {
        auto name = attr->name;
        auto value = xmlGetProp(node, name);

        args[xmlStringToStdString(name)] = xmlStringToStdString(value);

        xmlFree(value);
    }

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        auto aps = info->autoPatchers(type);
        aps.push_back(std::make_pair(xmlStringToStdString(curNode->content), args));
        info->setAutoPatchers(type, std::move(aps));
    }

    if (!hasText) {
        Log::log(Log::Warning, "<autopatcher> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagDeviceCheck(xmlNode *node,
                                                      PatchInfo * const info,
                                                      const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagDeviceCheck) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;
        if (xmlStrcmp(curNode->content, XmlTextTrue) == 0) {
            info->setDeviceCheck(type, true);
        } else if (xmlStrcmp(curNode->content, XmlTextFalse) == 0) {
            info->setDeviceCheck(type, false);
        } else {
            Log::log(Log::Warning, "Unknown value for <device-check>: %s",
                     (char *) curNode->content);
        }
    }

    if (!hasText) {
        Log::log(Log::Warning, "<device-check> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagPartconfigs(xmlNode *node,
                                                      PatchInfo * const info,
                                                      const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagPartconfigs) == 0);

    auto configs = info->supportedConfigs(type);
    if (configs.empty()) {
        configs.push_back("all");
    }
    info->setSupportedConfigs(type, std::move(configs));

    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_ELEMENT_NODE) {
            continue;
        }

        if (xmlStrcmp(curNode->name, PatchInfoTagPartconfigs) == 0) {
            Log::log(Log::Warning, "Nested <partconfigs> is not allowed");
        } else if (xmlStrcmp(curNode->name, PatchInfoTagExclude) == 0) {
            parsePatchInfoTagExclude(curNode, info, type);
        } else if (xmlStrcmp(curNode->name, PatchInfoTagInclude) == 0) {
            parsePatchInfoTagInclude(curNode, info, type);
        } else {
            Log::log(Log::Warning, "Unrecognized tag within <partconfigs>: %s",
                     (char *) curNode->name);
        }
    }
}

void PatcherPaths::Impl::parsePatchInfoTagExclude(xmlNode *node,
                                                  PatchInfo * const info,
                                                  const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagExclude) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;

        const std::string text = xmlStringToStdString(curNode->content);
        const std::string negated = "!" + text;

        auto configs = info->supportedConfigs(type);

        auto itYes = std::find(configs.begin(), configs.end(), text);
        if (itYes != configs.end()) {
            configs.erase(itYes);
        }

        auto itNo = std::find(configs.begin(), configs.end(), negated);
        if (itNo != configs.end()) {
            configs.erase(itNo);
        }

        configs.push_back(negated);
        info->setSupportedConfigs(type, std::move(configs));
    }

    if (!hasText) {
        Log::log(Log::Warning, "<exclude> tag has no text");
    }
}

void PatcherPaths::Impl::parsePatchInfoTagInclude(xmlNode *node,
                                                  PatchInfo * const info,
                                                  const std::string &type)
{
    assert(xmlStrcmp(node->name, PatchInfoTagInclude) == 0);

    bool hasText = false;
    for (auto *curNode = node->children; curNode; curNode = curNode->next) {
        if (curNode->type != XML_TEXT_NODE) {
            continue;
        }

        hasText = true;

        const std::string text = xmlStringToStdString(curNode->content);
        const std::string negated = "!" + text;

        auto configs = info->supportedConfigs(type);

        auto itYes = std::find(configs.begin(), configs.end(), text);
        if (itYes != configs.end()) {
            configs.erase(itYes);
        }

        auto itNo = std::find(configs.begin(), configs.end(), negated);
        if (itNo != configs.end()) {
            configs.erase(itNo);
        }

        configs.push_back(text);
        info->setSupportedConfigs(type, std::move(configs));
    }

    if (!hasText) {
        Log::log(Log::Warning, "<include> tag has no text");
    }
}