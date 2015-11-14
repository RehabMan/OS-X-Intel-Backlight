/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/version.h>

#include <IOKit/IONVRAM.h>
#include <IOKit/IOLib.h>
#include "IntelBacklight.h"
#include "Debug.h"

//REVIEW: avoids problem with Xcode 5.1.0 where -dead_strip eliminates these required symbols
#include <libkern/OSKextLib.h>
void* _org_rehabman_dontstrip_[] =
{
    (void*)&OSKextGetCurrentIdentifier,
    (void*)&OSKextGetCurrentLoadTag,
    (void*)&OSKextGetCurrentVersionString,
};

OSDefineMetaClassAndStructors(IntelBacklightPanel, IODisplayParameterHandler)

#define kIntelBacklightLevel "intel-backlight-level"
#define kRawBrightness "RawBrightness"

#define kBacklightLevelMin  0
#define kBacklightLevelMax  0x400

#ifdef DEBUG
#define kSmoothDelta "SmoothDelta%d"
#define kSmoothStep "SmoothStep%d"
#define kSmoothTimeout "SmoothTimeout%d"
#define kSmoothBufSize 16
#endif

#define countof(x) (sizeof(x)/sizeof(x[0]))
#define abs(x) ((x) < 0 ? -(x) : (x));

struct SmoothData
{
    int delta;
    int step;
    int timeout;
};

struct SmoothData smoothData[] =
{
    0x10,   1,  10000,
    0x40,   4,  10000,
    0xFFFF, 16, 10000,
};

#define m_max   (m_config.m_nLevels-1)
#define m_min   (0)

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma mark -
#pragma mark IOService functions override
#pragma mark -

bool IntelBacklightPanel::init()
{
	DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    m_handler = NULL;
    m_display = NULL;
    m_provider = NULL;

    m_workSource = NULL;
    m_smoothTimer = NULL;
    m_cmdGate = NULL;
    m_lock = NULL;
    
    memset(&m_config, 0, sizeof(m_config));

	return super::init();
}

IOService* IntelBacklightPanel::probe(IOService* provider, SInt32* score)
{
    DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);
    
    if (!super::probe(provider, score))
        return NULL;
    
    //TODO: check for proper PNLF patch?
    
    return this;
}

bool IntelBacklightPanel::start(IOService* provider)
{
    DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    // announce version
    extern kmod_info_t kmod_info;
    AlwaysLog("Version %s starting on OS X Darwin %d.%d.\n", kmod_info.version, version_major, version_minor);

    // place version/build info in ioreg properties RM,Build and RM,Version
    char buf[128];
    snprintf(buf, sizeof(buf), "%s %s", kmod_info.name, kmod_info.version);
    setProperty("RM,Version", buf);
#ifdef DEBUG
    setProperty("RM,Build", "Debug-" LOGNAME);
#else
    setProperty("RM,Build", "Release-" LOGNAME);
#endif
    
    // we need to retain provider to make ACPI calls to it
    m_provider = OSDynamicCast(IOACPIPlatformDevice, provider);
    if (!m_provider)
    {
        AlwaysLog("provider is not IOACPIPlatformDevice.\n");
        return false;
    }

    m_lock = IORecursiveLockAlloc();
    if (!m_lock)
        return false;
    
    m_hasSaveMethod = (kIOReturnSuccess == m_provider->validateObject("SAVE"));

    // add interrupt source for delayed actions...
    m_workSource = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &IntelBacklightPanel::processWorkQueue));
    if (!m_workSource)
        return false;
    IOWorkLoop* workLoop = getWorkLoop();
    if (!workLoop)
    {
        m_workSource->release();
        m_workSource = NULL;
        return false;
    }
    workLoop->addEventSource(m_workSource);
    m_workPending = 0;

    m_cmdGate = IOCommandGate::commandGate(this);
    if (m_cmdGate)
        workLoop->addEventSource(m_cmdGate);

    // initialize from properties
    OSDictionary* dict = getPropertyTable();
    setPropertiesGated(dict);

#ifdef DEBUG
    // write current values from smoothData
    for (int i = 0; i < countof(smoothData); i++)
    {
        char buf[kSmoothBufSize];
        snprintf(buf, sizeof(buf), kSmoothDelta, i);
        setProperty(buf, smoothData[i].delta, 32);
        snprintf(buf, sizeof(buf), kSmoothStep, i);
        setProperty(buf, smoothData[i].step, 32);
        snprintf(buf, sizeof(buf), kSmoothTimeout, i);
        setProperty(buf, smoothData[i].timeout, 32);
    }
#endif

    IORecursiveLockLock(m_lock);

    // make the service available for clients like 'ioio' (and backlight handler!)
    registerService();

    // load and set default brightness level
    UInt32 value = loadFromNVRAM();
    DebugLog("loadFromNVRAM returns %d\n", value);

    // wait for backlight handler... will call setBacklightHandler during this wait
    DebugLog("Waiting for BacklightHandler\n");
    waitForService(serviceMatching("BacklightHandler2"));
    if (!m_handler || m_config.m_nLevels < 2)
    {
        if (!m_handler)
            AlwaysLog("backlight handler never showed up.\n");
        else
            AlwaysLog("backlight handler invalid configuration (nLevels=%d)\n", m_config.m_nLevels);
        stop(provider);
        IORecursiveLockUnlock(m_lock);
        return false;
    }

    // allow backlight handler to initialize the hardware
    m_handler->initBacklight(&m_config);

    // add timer for smooth fade ins
    if (!(m_config.m_options & kDisableSmooth))
    {
        m_smoothTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &IntelBacklightPanel::onSmoothTimer));
        if (m_smoothTimer)
            workLoop->addEventSource(m_smoothTimer);
    }
    
    // after backlight handler is in place, now we can manipulate backlight level
    UInt32 current = queryRawBrightnessLevel();
    setProperty(kRawBrightness, current, 32);

    m_committed_value = m_value = m_from_value = levelForValue(current);
    DebugLog("current brightness: %d (%d)\n", m_from_value, current);
    if (-1 != value)
    {
        m_committed_value = value;
        DebugLog("setting to value from nvram %d\n", value);
        setBrightnessLevelSmooth(value);
    }
    m_saved_value = m_committed_value;

    IORecursiveLockUnlock(m_lock);

	return true;
}

void IntelBacklightPanel::stop(IOService* provider)
{
    DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    OSSafeReleaseNULL(m_display);
    
    IOWorkLoop* workLoop = getWorkLoop();
    if (workLoop)
    {
        if (m_workSource)
        {
            workLoop->removeEventSource(m_workSource);
            m_workSource->release();
            m_workSource = NULL;
        }
        if (m_smoothTimer)
        {
            workLoop->removeEventSource(m_smoothTimer);
            m_smoothTimer->release();
            m_smoothTimer = NULL;
        }
        if (m_cmdGate)
        {
            workLoop->removeEventSource(m_cmdGate);
            m_cmdGate->release();
            m_cmdGate = NULL;
        }
    }

    if (m_lock)
    {
        IORecursiveLockFree(m_lock);
        m_lock = NULL;
    }

    m_provider = NULL;
    m_handler = NULL;

    if (m_config.m_backlightLevels)
    {
        delete[] m_config.m_backlightLevels;
        m_config.m_backlightLevels = NULL;
    }

    super::stop(provider);
}

static UInt32 getConfigInteger32(OSDictionary* dict, const char* key)
{
    UInt32 result = -1;
    if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(key)))
        result = num->unsigned32BitValue();
    else
        DebugLog("getConfigInteger32: %s is not a number\n", key);
    return result;
}

bool IntelBacklightPanel::loadConfiguration(OSDictionary* config)
{
    // simple params
    m_config.m_pwmMax = getConfigInteger32(config, "PWMMax");
    m_config.m_pchlInit = getConfigInteger32(config, "PCHLInit");
    m_config.m_levwInit = getConfigInteger32(config, "LEVWInit");
    m_config.m_options = getConfigInteger32(config, "Options");
    m_config.m_backlightMin = getConfigInteger32(config, "BacklightMin");
    m_config.m_backlightMax = getConfigInteger32(config, "BacklightMax");
    m_config.m_backlightLevelsScale = getConfigInteger32(config, "BacklightLevelsScale");
    
    // handle BacklightLevels in both OSData or OSArray format
    OSObject* obj = config->getObject("BacklightLevels");
    if (OSData* data = OSDynamicCast(OSData, obj))
    {
        // allocate
        UInt16* levels = (UInt16*)data->getBytesNoCopy();
        int count = data->getLength() / sizeof(UInt16);
        m_config.m_backlightLevels = new UInt16[count];
        if (!m_config.m_backlightLevels)
            return false;
        // byte swap copy
        for (int i = 0; i < count; i++)
            m_config.m_backlightLevels[i] = (levels[i] << 8) | (levels[i] >> 8);
        m_config.m_nLevels = count;
    }
    else if (OSArray* array = OSDynamicCast(OSArray, obj))
    {
        // allocate
        int count = array->getCount();
        m_config.m_backlightLevels = new UInt16[count];
        if (!m_config.m_backlightLevels)
            return false;
        // copy from numbers in array
        for (int i = 0; i < count; i++)
        {
            OSNumber* num = OSDynamicCast(OSNumber, array->getObject(i));
            if (num)
                m_config.m_backlightLevels[i] = num->unsigned16BitValue();
        }
        m_config.m_nLevels = count;
    }
    return m_config.m_nLevels >= 2;
}

OSObject* IntelBacklightPanel::translateEntry(OSObject* obj)
{
    // Note: non-NULL result is retained...
    
    // if object is another array, translate it
    if (OSArray* array = OSDynamicCast(OSArray, obj))
        return translateArray(array);
    
    // if object is a string, may be translated to boolean
    if (OSString* string = OSDynamicCast(OSString, obj))
    {
        // object is string, translate special boolean values
        const char* sz = string->getCStringNoCopy();
        if (sz[0] == '>')
        {
            // boolean types true/false
            if (sz[1] == 'y' && !sz[2])
                return OSBoolean::withBoolean(true);
            else if (sz[1] == 'n' && !sz[2])
                return OSBoolean::withBoolean(false);
            // escape case ('>>n' '>>y'), replace with just string '>n' '>y'
            else if (sz[1] == '>' && (sz[2] == 'y' || sz[2] == 'n') && !sz[3])
                return OSString::withCString(&sz[1]);
        }
    }
    return NULL; // no translation
}

OSObject* IntelBacklightPanel::translateArray(OSArray* array)
{
    // may return either OSArray* or OSDictionary*
    
    int count = array->getCount();
    if (!count)
        return NULL;
    
    OSObject* result = array;
    
    // if first entry is an empty array, process as array, else dictionary
    OSArray* test = OSDynamicCast(OSArray, array->getObject(0));
    if (test && test->getCount() == 0)
    {
        // using same array, but translating it...
        array->retain();
        
        // remove bogus first entry
        array->removeObject(0);
        --count;
        
        // translate entries in the array
        for (int i = 0; i < count; ++i)
        {
            if (OSObject* obj = translateEntry(array->getObject(i)))
            {
                array->setObject(i, obj);
                obj->release();
            }
        }
    }
    else
    {
        // array is key/value pairs, so must be even
        if (count & 1)
            return NULL;
        
        // dictionary constructed to accomodate all pairs
        int size = count >> 1;
        if (!size) size = 1;
        OSDictionary* dict = OSDictionary::withCapacity(size);
        if (!dict)
            return NULL;
        
        // go through each entry two at a time, building the dictionary
        for (int i = 0; i < count; i += 2)
        {
            OSString* key = OSDynamicCast(OSString, array->getObject(i));
            if (!key)
            {
                dict->release();
                return NULL;
            }
            // get value, use translated value if translated
            OSObject* obj = array->getObject(i+1);
            OSObject* trans = translateEntry(obj);
            if (trans)
                obj = trans;
            dict->setObject(key, obj);
            OSSafeRelease(trans);
        }
        result = dict;
    }
    
    // Note: result is retained when returned...
    return result;
}

OSDictionary* IntelBacklightPanel::getConfigurationOverride(const char* method)
{
    // attempt to get configuration data from provider
    OSObject* r = NULL;
    if (kIOReturnSuccess != m_provider->evaluateObject(method, &r))
        return NULL;
    
    // for translation method must return array
    OSObject* obj = NULL;
    OSArray* array = OSDynamicCast(OSArray, r);
    if (array)
        obj = translateArray(array);
    OSSafeRelease(r);

    // must be dictionary after translation, even though array is possible
    OSDictionary* result = OSDynamicCast(OSDictionary, obj);
    if (!result)
    {
        OSSafeRelease(obj);
        return NULL;
    }
    return result;
}

void IntelBacklightPanel::setBacklightHandler(BacklightHandler2* handler, OSDictionary* config)
{
    // lifetime of backlight handler is guaranteed -- no need to retain
    m_handler = handler;

    // config/params provided when setting (not clearing) backlight handler
    if (config)
    {
        DebugOnly(setProperty("Configuration.Handler", config));
        OSDictionary* merged = NULL;
        OSDictionary* custom = getConfigurationOverride("RMCF");
        if (custom)
        {
            DebugOnly(setProperty("Configuration.Override", custom));
            merged = OSDictionary::withDictionary(config);
            if (merged && merged->merge(custom))
            {
                DebugOnly(setProperty("Configuration.Merged", merged));
                config = merged;
            }
            custom->release();
        }
        loadConfiguration(config);
        OSSafeRelease(merged);
    }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma mark -
#pragma mark IODisplayParameterHandler functions override
#pragma mark -

UInt32 IntelBacklightPanel::indexForLevel(UInt32 value, UInt32* rem)
{
    UInt32 index = value * (m_max-m_min);
    if (rem)
        *rem = index % kBacklightLevelMax;
    index = index / kBacklightLevelMax + m_min;
    return index;
}

UInt32 IntelBacklightPanel::levelForIndex(UInt32 index)
{
    // not really possible, but quiets the static analyzer...
    if (m_max-m_min <= 0) return 0;
    UInt32 value = ((index-m_min) * kBacklightLevelMax + (m_max-m_min)/2) / (m_max-m_min);
    return value;
}

UInt32 IntelBacklightPanel::levelForValue(UInt32 value)
{
    // return approx. OS X level for raw value
    UInt32 index = findIndexForLevel(value);
    UInt32 level = levelForIndex(index);
    if (index < m_max)
    {
        // pro-rate between levels
        int diff = levelForIndex(index+1) - level;
        if (m_config.m_backlightLevels[index+1] != m_config.m_backlightLevels[index])
        {
            // now pro-rate diff for value as between m_backlightLevels[index] and m_backlightLevels[index+1]
            diff *= value - m_config.m_backlightLevels[index];
            diff /= m_config.m_backlightLevels[index+1] - m_config.m_backlightLevels[index];
            level += diff;
        }
    }
    return level;
}

bool IntelBacklightPanel::setDisplay(IODisplay* display)
{    
    DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    IORecursiveLockLock(m_lock);

    // retain new display (also allow setting to same instance as previous)
    if (display)
        display->retain();
    OSSafeRelease(m_display);
    m_display = display;
    if (m_display)
    {
        // automatically commit a non-zero value on display change
        if (m_value)
            m_saved_value = m_committed_value = m_value;
        // update brightness levels
        doUpdate();
    }

    IORecursiveLockUnlock(m_lock);

    return true;
}

bool IntelBacklightPanel::doIntegerSet(OSDictionary* params, const OSSymbol* paramName, UInt32 value)
{
    bool result = true;

    IORecursiveLockLock(m_lock);

    //DebugLog("%s::%s(\"%s\", %d)\n", this->getName(), __FUNCTION__, paramName->getCStringNoCopy(), value);
    if ( gIODisplayBrightnessKey->isEqualTo(paramName))
    {   
        //DebugLog("%s::%s(%s) map %d -> %d\n", this->getName(), __FUNCTION__, paramName->getCStringNoCopy(), value, indexForLevel(value));
        //REVIEW: workaround for Yosemite DP...
        if (value < 5 && m_value > 5)
        {
            //REVIEW: copied from commit case below...
            // setting to zero automatically commits prior value
            UInt32 index = indexForLevel(m_value);
            m_committed_value = m_value;
            // save to NVRAM in work loop
            scheduleWork(kWorkSave|kWorkSetBrightness);
            // save to BIOS nvram via ACPI
            if (m_hasSaveMethod)
                savePrebootBrightnessLevel(m_config.m_backlightLevels[index]);
        }
        if (0xFF == value)
        {
            setBrightnessLevelSmooth(m_saved_value);
            result = false;
        }
        //REVIEW: end workaround...
        else
        {
            setBrightnessLevelSmooth(value);
            if (value > 5) // more hacks for Yosemite (don't save really low values)
                m_saved_value = value;
        }
    }
    else if (gIODisplayParametersCommitKey->isEqualTo(paramName))
    {
        UInt32 index = indexForLevel(m_value);
        //DebugLog("%s::%s(%s) map %d -> %d\n", this->getName(), __FUNCTION__, paramName->getCStringNoCopy(), value, index);
        m_committed_value = m_value;
        IODisplay::setParameter(params, gIODisplayBrightnessKey, m_committed_value);
        // save to NVRAM in work loop
        scheduleWork(kWorkSave|kWorkSetBrightness);
        // save to BIOS nvram via ACPI
        if (m_hasSaveMethod)
            savePrebootBrightnessLevel(m_config.m_backlightLevels[index]);
    }

    IORecursiveLockUnlock(m_lock);

    return result;
}


bool IntelBacklightPanel::doDataSet(const OSSymbol* paramName, OSData* value )
{
    DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);
    return true;
}

bool IntelBacklightPanel::doUpdate( void )
{
    //DebugLog("enter %s::%s()\n", this->getName(), __FUNCTION__);
    bool result = false;

    IORecursiveLockLock(m_lock);

    OSDictionary* newDict = 0;
	OSDictionary* allParams = OSDynamicCast(OSDictionary, m_display->copyProperty(gIODisplayParametersKey));
    if (allParams)
    {
        newDict = OSDictionary::withDictionary(allParams);
        allParams->release();
    }
    
    OSDictionary* backlightParams = OSDictionary::withCapacity(2);
    ////OSDictionary* linearParams = OSDictionary::withCapacity(2);

    //REVIEW: myParams is not used...
    OSDictionary* myParams  = OSDynamicCast(OSDictionary, copyProperty(gIODisplayParametersKey));
    if (/*linearParams && */backlightParams && myParams)
	{				
		//DebugLog("%s: Level min %d, max %d, value %d\n", this->getName(), min, max, _value);
		
        IODisplay::addParameter(backlightParams, gIODisplayBrightnessKey, kBacklightLevelMin, kBacklightLevelMax);
        IODisplay::setParameter(backlightParams, gIODisplayBrightnessKey, m_committed_value);

        ////IODisplay::addParameter(linearParams, gIODisplayLinearBrightnessKey, 0, 0x710);
        ////IODisplay::setParameter(linearParams, gIODisplayLinearBrightnessKey, ((_index-min) * 0x710 + (max-min)/2) / (max-min));

        OSNumber* num = OSNumber::withNumber(0ULL, 32);
        OSDictionary* commitParams = OSDictionary::withCapacity(1);
        if (num && commitParams)
        {
            commitParams->setObject("reg", num);
            backlightParams->setObject(gIODisplayParametersCommitKey, commitParams);
            num->release();
            commitParams->release();
        }
        
        if (newDict)
        {
            newDict->merge(backlightParams);
            ////newDict->merge(linearParams);
            m_display->setProperty(gIODisplayParametersKey, newDict);
            newDict->release();
        }
        else
            m_display->setProperty(gIODisplayParametersKey, backlightParams);

        //refresh properties here too
        setProperty(gIODisplayParametersKey, backlightParams);
        
        backlightParams->release();
        myParams->release();
        ////linearParams->release();

        result = true;
	}

    IORecursiveLockUnlock(m_lock);

    //DebugLog("exit %s::%s()\n", this->getName(), __FUNCTION__);
    return result;
}


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma mark -
#pragma mark ACPI related functions
#pragma mark -

void IntelBacklightPanel::setRawBrightnessLevel(UInt32 level)
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);
    
    if (m_handler)
    {
        // adjust level to within limits set by XRGL and XRGH
        if (level > m_config.m_backlightMax)
            level = m_config.m_backlightMax;
        if (level && level < m_config.m_backlightMin)
            level = m_config.m_backlightMin;

        //set backlight via native handler
        m_handler->setBacklightLevel(level);
        
        // just FYI... set RawBrightness property to actual current level
        setProperty(kRawBrightness, queryRawBrightnessLevel(), 32);
    }
}

void IntelBacklightPanel::setBrightnessLevel(UInt32 level)
{
    //DebugLog("%s::%s(%d)\n", this->getName(), __FUNCTION__, level);

    UInt32 rem;
    UInt32 index = indexForLevel(level, &rem);
    UInt32 value = m_config.m_backlightLevels[index];
    //DebugLog("%s: level=%d, index=%d, value=%d\n", this->getName(), level, index, value);
    
    // can set "in between" level
    UInt32 next = index+1;
    if (next < m_config.m_nLevels)
    {
        // prorate the difference...
        UInt32 diff = m_config.m_backlightLevels[next] - value;
        value += (diff * rem) / kBacklightLevelMax;
        //DebugLog("%s: diff=%d, rem=%d, value=%d\n", this->getName(), diff, rem, value);
    }
    setRawBrightnessLevel(value);
}

void IntelBacklightPanel::setBrightnessLevelSmooth(UInt32 level)
{
    //DebugLog("%s::%s(%d)\n", this->getName(), __FUNCTION__, level);

    //DebugLog("%s: _from_value=%d, _value=%d\n", this->getName(), _from_value, _value);

    if (m_smoothTimer)
    {
        IORecursiveLockLock(m_lock);
        if (level != m_value)
        {
            // find appropriate movemement params in smoothData
            int diff = abs((int)level - m_from_value);
            m_smoothIndex = countof(smoothData)-1; // defensive
            for (int i = 0; i < countof(smoothData); i++)
            {
                if (diff <= smoothData[i].delta)
                {
                    m_smoothIndex = i;
                    break;
                }
            }
            // kick off timer if not already started
            bool start = (m_from_value == m_value);
            m_value = level;
            if (start)
                onSmoothTimer();
        }
        else if (m_from_value == m_value)
        {
            // in the case of already set to that value, set it for sure
            setBrightnessLevel(m_value);
        }
        IORecursiveLockUnlock(m_lock);
    }
    else
    {
        m_from_value = m_value = level;
        setBrightnessLevel(m_value);
    }
}

void IntelBacklightPanel::onSmoothTimer()
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    IORecursiveLockLock(m_lock);

    ////DebugLog("%s::%s(): _from_value=%d, _value=%d, _smoothIndex=%d\n", this->getName(), __FUNCTION__, _from_value, _value, _smoothIndex);

    // adjust smooth index based on current delta
    int diff = abs(m_value - m_from_value);
    if (m_smoothIndex > 0 && diff <= smoothData[m_smoothIndex-1].delta)
        --m_smoothIndex;

    // move _from_value in the direction of _value
    SmoothData* data = &smoothData[m_smoothIndex];
    if (m_value > m_from_value)
        m_from_value = min(m_value, m_from_value + data->step);
    else
        m_from_value = max(m_value, m_from_value - data->step);

    // set new brigthness level
    //DebugLog("%s::%s(): _from_value=%d, _value=%d\n", this->getName(), __FUNCTION__, _from_value, _value);
    setBrightnessLevel(m_from_value);
    // set new timer if not reached desired brightness previously set
    if (m_from_value != m_value)
        m_smoothTimer->setTimeoutUS(data->timeout);

    IORecursiveLockUnlock(m_lock);
}

void IntelBacklightPanel::savePrebootBrightnessLevel(UInt32 level)
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);
    
    if (OSNumber* number = OSNumber::withNumber(level, 32))
    {
        if (kIOReturnSuccess != m_provider->evaluateObject("SAVE", NULL, (OSObject**)&number, 1))
            AlwaysLog("Error in savePrebootBrightnessLevel SAVE(%u)\n", (unsigned int)level);
        
        //DebugLog("%s: savePrebootBrightnessLevel SAVE(%u)\n", this->getName(), (unsigned int) level);
        number->release();
    }
}

void IntelBacklightPanel::saveBrightnessLevelNVRAM(UInt32 level)
{
    //DebugLog("%s::%s(): level=%d\n", this->getName(), __FUNCTION__, level);

    if (IORegistryEntry *nvram = OSDynamicCast(IORegistryEntry, fromPath("/options", gIODTPlane)))
    {
        if (const OSSymbol* symbol = OSSymbol::withCString(kIntelBacklightLevel))
        {
            if (OSData* number = OSData::withBytes(&level, sizeof(level)))
            {
                //DebugLog("%s: saveBrightnessLevelNVRAM got nvram %p\n", this->getName(), nvram);
                if (!nvram->setProperty(symbol, number))
                    DebugLog("nvram->setProperty failed\n");
                number->release();
            }
            symbol->release();
        }
        nvram->release();
    }
}

UInt32 IntelBacklightPanel::loadFromNVRAM(void)
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    IORegistryEntry* nvram = IORegistryEntry::fromPath("/chosen/nvram", gIODTPlane);
    if (!nvram)
    {
        DebugLog("no /chosen/nvram, trying IODTNVRAM\n");
        // probably booting w/ Clover
        if (OSDictionary* matching = serviceMatching("IODTNVRAM"))
        {
            nvram = waitForMatchingService(matching, 1000000000ULL * 15);
            matching->release();
        }
    }
    else DebugLog("have nvram from /chosen/nvram\n");
    UInt32 val = -1;
    if (nvram)
    {
        // need to serialize as getProperty on nvram does not work
        if (OSSerialize* serial = OSSerialize::withCapacity(0))
        {
            nvram->serializeProperties(serial);
            if (OSDictionary* props = OSDynamicCast(OSDictionary, OSUnserializeXML(serial->text())))
            {
                if (OSData* number = OSDynamicCast(OSData, props->getObject(kIntelBacklightLevel)))
                {
                    val = 0;
                    unsigned l = number->getLength();
                    if (l <= sizeof(val))
                        memcpy(&val, number->getBytesNoCopy(), l);
                    DebugLog("read level from nvram = %d\n", val);
                    //number->release();
                }
                else DebugLog("no intel-backlight-level in nvram\n");
                props->release();
            }
            serial->release();
        }
        nvram->release();
    }
    return val;
}

UInt32 IntelBacklightPanel::queryRawBrightnessLevel()
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    UInt32 result = m_handler->getBacklightLevel();

//REVIEW: maybe is really not necessary anymore...
    // adjust result to be within limits set by XRGL and XRGH
    if (result > m_config.m_backlightMax)
        result = m_config.m_backlightMax;
    if (result && result < m_config.m_backlightMin)
        result = m_config.m_backlightMin;

    return result;
}

UInt32 IntelBacklightPanel::findIndexForLevel(UInt32 level)
{
	for (int i = 0; i < m_config.m_nLevels; i++)
	{
		if (level < m_config.m_backlightLevels[i])
		{
			DebugLog("findIndexForLevel(%d) is %d\n", level, i-1);
			return i-1;
		}
	}
    DebugLog("findIndexForLevel(%d) did not find\n", level);
	return m_config.m_nLevels-1;
}

void IntelBacklightPanel::processWorkQueue(IOInterruptEventSource *, int)
{
    //DebugLog("%s::%s() _workPending=%x\n", this->getName(), __FUNCTION__, m_workPending);
    
    IORecursiveLockLock(m_lock);
    if (m_workPending & kWorkSave)
        saveBrightnessLevelNVRAM(m_committed_value);
    if (m_workPending & kWorkSetBrightness)
        setBrightnessLevel(m_committed_value);
    m_workPending = 0;
    IORecursiveLockUnlock(m_lock);
}

void IntelBacklightPanel::scheduleWork(unsigned newWork)
{
    IORecursiveLockLock(m_lock);
    m_workPending |= newWork;
    m_workSource->interruptOccurred(0, 0, 0);
    IORecursiveLockUnlock(m_lock);
}

IOReturn IntelBacklightPanel::setPropertiesGated(OSObject* props)
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    OSDictionary* dict = OSDynamicCast(OSDictionary, props);
    if (!dict)
        return kIOReturnSuccess;

    // set brightness
	if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(kRawBrightness)))
    {
		UInt32 raw = (int)num->unsigned32BitValue();
        setRawBrightnessLevel(raw);
        setProperty(kRawBrightness, queryRawBrightnessLevel(), 32);
    }

#ifdef DEBUG
    for (int i = 0; i < countof(smoothData); i++)
    {
        char buf[kSmoothBufSize];
        snprintf(buf, sizeof(buf), kSmoothDelta, i);
        if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(buf)))
        {
            smoothData[i].delta = (int)num->unsigned32BitValue();
            setProperty(buf, smoothData[i].delta, 32);
        }
        snprintf(buf, sizeof(buf), kSmoothStep, i);
        if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(buf)))
        {
            smoothData[i].step = (int)num->unsigned32BitValue();
            setProperty(buf, smoothData[i].step, 32);
        }
        snprintf(buf, sizeof(buf), kSmoothTimeout, i);
        if (OSNumber* num = OSDynamicCast(OSNumber, dict->getObject(buf)))
        {
            smoothData[i].timeout = (int)num->unsigned32BitValue();
            setProperty(buf, smoothData[i].timeout, 32);
        }
    }
#endif
    
    return kIOReturnSuccess;
}

IOReturn IntelBacklightPanel::setProperties(OSObject* props)
{
    //DebugLog("%s::%s()\n", this->getName(), __FUNCTION__);

    if (m_cmdGate)
    {
        // syncronize through workloop...
        IOReturn result = m_cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &IntelBacklightPanel::setPropertiesGated), props);
        if (kIOReturnSuccess != result)
            return result;
    }
    return kIOReturnSuccess;
}

