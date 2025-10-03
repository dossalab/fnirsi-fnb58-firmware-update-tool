Quick & dirty tool to update FNB58's firmware on Linux. Original tool works under wine, but that's a simpler alternative if you just want to update and get it over.

## How to build

Building on Linux or MacOS are the same by running the `make` command.
The makefile automatically detects the OS and runs the right build command and installs `hidapi` from brew if missing.

Note: Only tested it on MacOS 15. Other versions are not guaranteed.

### MacOS hidapi dependency

As the `hidapi` library is a dependency it needs to be installed. The make script should automatically install it from `brew` on MacOS. On Linux you have to install it manually.

## How to use?

Plug your device to your computer's USB port while pressing 'select' button (navigation wheel). You should see splash screen indicating that you're in DFU mode.

On Linux you might need to add the following udev rule to make sure you have access to the device:
```
KERNEL=="hidraw*", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="0038", GROUP="uucp", MODE="0660"
```
Alternatively, you can run the tool as root.

Then, simply run the tool:
```
./fnirsi-dfu-update Fnb58V0.68.ufn
```

And wait till it's done.
```
writing 58 byte(s) at index 6467
writing 58 byte(s) at index 6468
writing 58 byte(s) at index 6469
writing 58 byte(s) at index 6470
writing 26 byte(s) at index 6471
all done!
```

*WARNING* - this is tested only a couple times - expect issues!

## FNB58 Firmwares

Can be downloaded from:
1. https://www.fnirsi.com/pages/manuals-firmwares?category=signal-analysis
2. or https://github.com/Rhomboid/fnb58-archive

