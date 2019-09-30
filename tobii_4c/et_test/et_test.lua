local et_connection;

local function spawn_cp()
	SYMTABLE = system_load("builtin/keyboard.lua")();

	et_connection = target_alloc("eyetracker",
		function(source, status, iotbl)
			if status.kind == "terminated" then
				warning("connection died, restarting");
				delete_image(source);
				spawn_cp();
			elseif status.kind == "registered" then
				if status.segkind ~= "sensor" then
					delete_image(source);
					warning("connected client did not register as sensor");
					spawn_cp();
				end
-- set the screeen dimensions so that we can do the calibration
			elseif status.kind == "preroll" then
				target_displayhint(source, VRESW, VRESH);

			elseif status.kind == "resized" then
				resize_image(source, status.width, status.height);
				show_image(source);

-- ignore labelhint, we know it's CALIBRATION
			elseif status.kind == "input" then
				et_process_gaze(iotbl);
			end
	end);
end

function et_test()
	spawn_cp();
end

function et_test_input(iotbl)
	if iotbl.translated and iotbl.active and SYMTABLE[iotbl.keysym] == "F1" then
		target_input(et_connection, {
			devid = 0,
			subid = 0,
			label = "CALIBRATE",
			kind = "digital",
			active = true
		});
	end
end

function et_process_gaze(iotbl)
	for k,v in pairs(iotbl) do
		print(k,v);
	end
end
