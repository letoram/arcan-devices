# Stream Deck
This adds support for the [ElGato Stream Deck](https://www.elgato.com),
a hybrid display / input device.

## Compiling
Depends on: hiddev-/hiddev-libusb, and permissions to access the USB device
with the VID of 0x0fd9 and PID of 0x0060. Consult the documentation of your
linux distribution to see what is needed for your user to get those
permissions.

    meson build
		cd build ; ninja .

## Use
Normally, the streamdeck inputs are usable through the normal evdev- based
input platform. In order to control what to display and when, it relies on a
connection point that behaves like an 'encoder', meaning that the window
manager needs to do something like:

     local disp_w = 72 * 5;
		 local disp_h = 72 * 3;
		 local buf = alloc_surface(disp_w, disp_h);
		 local surf = null_surface(disp_w, disp_h);
		 image_sharestorage(WORLDID, surf);
		 define_rendertarget(buf, {surf});

     target_alloc("streamdeck", 72 * 5, 72 * 3,
		 function(source, status)
		     if status.kind == "registered" and status.segkind == "encoder" then
				     rendertarget_bind(buf, source);
				 end
		 end
		 );

Then the arcan-streamdeck binary needs to be pointed to use the connection
point, something to the effect of:

		 ARCAN_CONNPATH=streamdeck ./arcan-streamdeck

For further developer examples, the [durden](https://github.com/letoram/durden)
repository has an example tool for some advanced mapping / use of this driver.
