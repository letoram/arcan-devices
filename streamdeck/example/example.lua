local frag = [[
varying vec2 texco;
uniform int timestamp;
uniform float fract_timestamp;

float f1(float x, float y, float t)
{
	return sin(sqrt(10.0 * (x * x + y * y) + 0.05) + t) + 0.5;
}

float f2(float x, float y, float t)
{
	return sin(5.0 * sin(t * 0.5) + y * cos(t * 0.3) + t) + 0.5;
}

float f3(float x, float y, float t)
{
	return sin(3.0 * x + t) + 0.5;
}

const float pi = 3.1415926;

void main()
{
	float t = (float(timestamp) + fract_timestamp) * 0.01;
	float sin_ht = sin(t * 0.5) + 0.5;
	float cx = texco.s * 4.0;
	float cy = texco.t * 2.0;
	float v = f1(cx, cy, t);
	v += f2(cx, cy, t);
	v += f3(cx * sin_ht, cy * sin_ht, t);
	float r = sin(t);
	float g = cos(t + v * pi);
	float b = sin(t + v * pi);
	gl_FragColor = vec4(r, g, b, 1.0);
}
]]

local shid = build_shader(nil, frag, "plasma")

local function build_rendertarget(source, w, h)
	local buf = alloc_surface(w, h)
	local img = color_surface(w, h, 0, 255, 0)
	show_image(img)
	define_rendertarget(buf, {img})
	show_image(buf)
	image_shader(img, shid)
	rendertarget_bind(buf, source)
	link_image(buf, source)
end

local function translate_input(input)
	if not input.digital then
		return
	end
	local keyind = 97 + (input.subid % 25);
	return {
		kind = "digital",
		digital = true,
		devid = 0,
		subid = input.subid,
		translated = true,
		number = keyind,
		keysym = keyind,
		active = input.active,
		utf8 = input.active and string.char(keyind) or ""
	};
end

local function open_streamdeck_cp(name)
	local maxw = 72 * 5
	local maxh = 72 * 3
	target_alloc(name, maxw, maxh,
		function(source, status, input)
			if status.kind == "registered" then
				open_streamdeck_cp(name)
				build_rendertarget(source, maxw, maxh)
			end

			print(status.kind, _G[APPLID .. "_input"])
			if status.kind == "input" and _G[APPLID .. "_input"] then
				key = translate_input(input)
				if key then
					_G[APPLID .. "_input"](key)
				end
			end

			if status.kind == "terminated" then
				delete_image(source)
				open_streamdeck_cp(name)
			end
		end
	)
end

function example()
	local cs = color_surface(VRESW, VRESH, 0, 0, 0);
	show_image(cs);
	image_shader(cs, build_shader(nil, frag, "plasma"));
	open_streamdeck_cp("streamdeck")
end

function example_input(iotbl)
	print("got", iotbl.utf8);
end
