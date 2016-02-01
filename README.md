## IntelBacklight by RehabMan


### How to Install:

Install the kext using your favorite kext installer utility, such as Kext Wizard.  The Debug directory is for troubleshooting only, in normal "working" installs, you should install the Release version.

The typical "Backlight Fix" patch or AddPNLF_1000000 in Clover will do the trick.  The more complex patches "Brightness Fix (Haswell/Broadwell)" or "Brightness Fix (HD3000/HD4000)" are not necessary, although this kext will work with them.


### Downloads:

Downloads are available on bitbucket:

https://bitbucket.org/RehabMan/os-x-intel-backlight/downloads


### Feedback:

Please use this thread on tonymacx86.com for feedback, questions, and help:

http://www.tonymacx86.com/el-capitan-laptop-support/172743-new-brightness-kext-intelbacklight-kext.html


### Customization

The kext has default properties and backlight levels that will work on most Intel graphics devices (on those laptops that use the Intel IGPU PWM backlight controls).

But you may want to tweak the parameters provided in the Info.plist.  I have implemented a way to do this in ACPI so the Info.plist does not need to be modified.  In order to do this, you create a method 'RMCF' in the PNLF device (the PNLF device still needed to activate this driver, just like the AppleBacklight driver).'

The RMCF method provides overrides for the data in Info.plist.  What the kext does internally is build a dictionary from the data returned from RMCF and merges it with the dictionary pulled from the Info.plist.

The typical PNLF:
```
Device (PNLF)
{
    Name (_ADR, Zero)
    Name (_HID, EisaId ("APP0002"))
    Name (_CID, "backlight")
    Name (_UID, 0x0A)
    Name (_STA, 0x0B)
}
```

Much like a _DSM method used for OS X property injections, RMCF uses pairs of Package entries to build a dictionary (key/value pairs).

A PNLF with customizations.
```
Device (PNLF)
{
    Name (_ADR, Zero)
    Name (_HID, EisaId ("APP0002"))
    Name (_CID, "backlight")
    Name (_UID, 0x0A)
    Name (_STA, 0x0B)
    Method (RMCF)
    {
        Return(Package()
        {
            "PWMMax", 0, // PWMMax of Zero uses BIOS PWM Max instead of OS X values
            "Options", 0x01, // setting bit0 turns off smooth transitions
        })
    }
}
```

This RMCF method shows all properties (these properties are the same for the Haswell/Broadwell as in the Info.plist):
```
Method (RMCF)
{
    Return(Package()
    {
        "PWMMax", 0,
        "PCHLInit", Ones,  // some computers will need this zero
        "LEVWInit", 0xC0000000, // you can use 0 to skip writing LEVW
        "Options", 2,
        "BacklightMin", 25,
        "BacklightMax", 0xad9,
        "BacklightLevelsScale", 0xad9,
        "BacklightLevels", Package()
        {
            Package(){}, // empty package indicates array follows (instead of dictionary)
            0,\n
            35, 39, 44, 50,
            58, 67, 77, 88,
            101, 115, 130, 147,
            165, 184, 204, 226,
            249, 273, 299, 326,
            354, 383, 414, 446,
            479, 514, 549, 587,
            625, 665, 706, 748,
            791, 836, 882, 930,
            978, 1028, 1079, 1132,
            1186, 1241, 1297, 1355,
            1414, 1474, 1535, 1598,
            1662, 1728, 1794, 1862,
            1931, 2002, 2074, 2147,
            2221, 2296, 2373, 2452,
            2531, 2612, 2694, 0xad9,
        },
    })
}
```

The BacklightLevels entry can be specified as an array or buffer.  If specified as a buffer, it is an array of 16-bit values that are little-endian byte order (non-Intel) for readability and ease of entering.  They are byte swapped within the kext.  You will notice the same if you look at the Info.plist for the kext.

As a concrete example, I use the following patch (in addition to the normal PNLF patch) on the u430:
```
into device label PNLF insert
begin
Method(RMCF)\n
{\n
    Return(Package()\n
    {\n
        "PWMMax", 0,\n
    })
}\n
```

This overrides the default PWMMax (0xad9) to zero so it uses the BIOS value (happens to be 0x3a9 on the u430).

I plan to use this RMCF configuration capability with future versions of other kexts I build and use.  It makes it easy to configure a kext for a specific computer without modification of the kext itself.


### Build Environment

My build environment is currently Xcode 7, using SDK 10.8, targeting OS X 10.6.

No other build environment is supported.


### 32-bit Builds

Currently, builds are provided only for 64-bit systems.  32-bit/64-bit FAT binaries are not provided.  But you may be able build your own should you need them.  I do not test 32-bit, and there may be times when the repo is broken with respect to 32-bit builds.

Here's how to build 32-bit (universal):

- xcode 4.6.3
- open IntelBacklight.xcodeproj
- click on IntelBacklight at the top of the project tree
- select IntelBacklight under Project
- change Architectures to 'Standard (32/64-bit Intel)'

probably not necessary, but a good idea to check that the targets don't have overrides:
- multi-select all the Targets
- check/change Architectures to 'Standard (32/64-bit Intel)'
- build (either w/ menu or with make)

Or, if you have the command line tools installed, just run:

- For FAT binary (32-bit and 64-bit in one binary)
make BITS=3264

- For 32-bit only
make BITS=32


### Source Code:

The source code is maintained at the following sites:

https://github.com/RehabMan/OS-X-Intel-Backlight

https://bitbucket.org/RehabMan/os-x-intel-backlight


### Known issues:

- None yet.


### Change Log:

2016-02-01 v1.0.6

- change default for Options (Haswell only) and PWMMax (both Haswell and Ivy/Sandy)

- add device-id for missing Intel graphics device-ids (8086:0d26, 8086:0a26)

2015-11-14 v1.0.5

- add Options bit 1 (kWriteLEVWOnSet) to fix problems with wake up from hibernation

2015-10-19 v1.0.3

- fix potential race condition at startup

- add support for Haswell device 8086:0a2e

- fix problems with LEVW (typo/bug)

2015-09-17 v1.0.0

- Initial commit based on ACPIBacklight project


### History

See original post for ACPIBacklight.kext at:
http://www.insanelymac.com/forum/topic/268219-acpi-backlight-driver/

See my ACPIBacklight.kext at: 
https://github.com/RehabMan/OS-X-ACPI-Backlight

As of 10.11, ACPIBacklight.kext with my custom "Brightness Fix (HD3000/HD4000)" and "Brightness Fix (Haswell/Broadwell)" ACPI patches no longer works when using ACPI methods.  For 10.11, I wrote a new version of ACPIBacklight.kext that bypasses ACPI and goes direct to the hardware within the kext.  It was a relatively minor change, but it got me thinking about doing a dedicated kext for Intel backlight control that did not depend on ACPI to manipulate the backlight level or initialize the backlight hardware.

IntelBacklight.kext presented here is the result.

