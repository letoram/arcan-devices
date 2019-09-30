# Tobii Stream Engine Integration

This folder contains the tobii Eye Tracker stream engine SDK integration. This,
unfortunately, depends on a tangled mess of binary ubuntu tied blobs. To mitigate
this damage somewhat, there are example docker containers at
[arcan-docker](https://github.com/letoram/arcan-docker). Due to the license you
need to provide and extract the binary drivers yourself, by registering with the
'developer zone' and grabbing the linux beta drivers there.

## Getting Started

Eye tracker is a complex enough input device that you kind of need application
level support to get anywhere. For this reason,
[durden](https://github.com/letoram/durden) has an eye tracker tool that can
be used if that is your arcan WM of choice, or as an example for how to write
integration. The rest of this section will assume to durden- tool behavior.

There is also a et\_test appl inside this repository that expose the connection
point 'eyetracker', so combined with the arcan-docker scripts for running the
tobii eye tracker:

    arcan ./et_test
    start_et.sh eyetracker

When using this test application, the values and the tracking will be wrong
unless you run it fullscreen.

F1 triggers calibration. For the calibration to work, you need a license file.
Like this wasn't bad enough already. Just lift the one from the binary blobs:

    cp /opt/tobii_config/resources/resources/se_license_key_tobii_config license
		xxd -i license > license.h

Before building (mkdir build; cd build; cmake .. ; make).
