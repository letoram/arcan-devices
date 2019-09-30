/* Description: Basic Tobii Stream Engine to Arcan driver
 * Copyright: Bjorn Stahl, 2018-2019
 * License: 3-Clause BSD
 */
#include <arcan_shmif.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <tobii/tobii.h>
#include <tobii/tobii_streams.h>
#include <tobii/tobii_config.h>
#include <tobii/tobii_licensing.h>

#include "draw.h"
#include "license.h"

struct shmif_dev {
	struct arcan_shmif_cont C;
	arcan_event outev;
	int dirty;
};

/*
 * other callbacks include gaze point and gaze origin,
 * where gaze_point gives position_xy
 * and gaze_origin_t gives left_validity, left_xyz, right_xyz
 *
 * but our current data model just gives where each cuts the plane and no data
 * about the precision as such.
 *
 * validity_t is just a constant, TOBII_VALIDITY_INVALID, VALID?
 */

void calibrate(struct shmif_dev* dev, tobii_device_t* device)
{
	tobii_error_t err = tobii_calibration_start(device, TOBII_ENABLED_EYE_BOTH);
	struct point {
		float x, y;
	};

	if (err != TOBII_ERROR_NO_ERROR){
/* write signal and leave */
		printf("couldn't start calibration, reason: %d\n", err);
		return;
	}

/* list plucked from the calibration example */
	struct point points[] = {
		{0.1, 0.5},
		{0.5, 0.5},
		{0.9, 0.5}
	};

	arcan_shmif_signal(&dev->C, SHMIF_SIGVID);

	for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); i++){
/* draw new point data, draw_box[points[i].x - 10, y + 10, 20, 20] */
		float cp_x = (float)dev->C.w * points[i].x;
		float cp_y = (float)dev->C.w * points[i].x;

/* wasteful, but for 3-4 frames, not worth the effort */
		draw_box(&dev->C, 0, 0, dev->C.w, dev->C.h, SHMIF_RGBA(0, 0, 0, 255));
		for (size_t j = 0; j < 3; j++){
			draw_box(&dev->C,
				cp_x - 10, cp_y - 10, 20, 20, SHMIF_RGBA(
					j == 0 ? 255 : 0,
					j == 1 ? 255 : 0,
					j == 2 ? 255 : 0, 255));
			arcan_shmif_signal(&dev->C, SHMIF_SIGVID);

			if (TOBII_ERROR_NO_ERROR !=
				tobii_calibration_collect_data_2d(device, points[i].x, points[i].y)){
/* write that the calibration failed */
				goto out;
			}
		}
	}

/* write the new calibration status */
	if (TOBII_ERROR_NO_ERROR !=
		tobii_calibration_compute_and_apply(device)){
	}

out:
	tobii_calibration_stop(device);
}

void on_presence(tobii_user_presence_status_t status, int64_t timestamp_us, void* tag)
{
	struct shmif_dev* dev = tag;

/* detail here, the timestamp_us a. not necessarily in the clock of the recipient,
 * and that multiple events might arrive at different timestamps I guess -
 * TOBII_USER_PRESENCE_STATUS_UNKOWN, AWAY, PRESENCE */
	bool new_state = TOBII_USER_PRESENCE_STATUS_AWAY != status;
	if (dev->outev.io.input.eyes.present != new_state){
		dev->outev.io.input.eyes.present = new_state;
		printf("present is now: %d\n", new_state);
		dev->dirty = true;
	}
}

void on_head(tobii_head_pose_t const* head_pose, void* tag)
{
	struct shmif_dev* dev = tag;
	/* int64_t last_ts = head_pose->timestamp_us; */

	if (head_pose->position_validity == TOBII_VALIDITY_VALID){
		dev->dirty = true;
		dev->outev.io.input.eyes.head_pos[0] = head_pose->position_xyz[0];
		dev->outev.io.input.eyes.head_pos[1] = head_pose->position_xyz[1];
		dev->outev.io.input.eyes.head_pos[2] = head_pose->position_xyz[2];
	}

	for (size_t i = 0; i < 3; i++){
		if (head_pose->rotation_validity_xyz[i] == TOBII_VALIDITY_VALID){
			dev->dirty = true;
			dev->outev.io.input.eyes.head_pos[i] = head_pose->rotation_xyz[i];
		}
	}
}

void on_gaze(const tobii_gaze_point_t* gaze, void* tag)
{
	struct shmif_dev* dev = tag;
	dev->outev.io.input.eyes.gaze_x1 =
		dev->outev.io.input.eyes.gaze_x2 = gaze->position_xy[0];

	dev->outev.io.input.eyes.gaze_y1 =
		dev->outev.io.input.eyes.gaze_y2 = gaze->position_xy[1];

	dev->dirty = true;
}

/*
 * void on_gaze_origin(const tobii_gaze_origin_t* gaze_origin, void* tag)
{
	struct shmif_dev* dev = tag;

	if (gaze_origin->left_validity == TOBII_VALIDITY_VALID){
			gaze_origin->left_xyz[0],
			gaze_origin->left_xyz[1],
			gaze_origin->left_xyz[2]);
	}

	if (gaze_origin->right_validity == TOBII_VALIDITY_VALID){
			gaze_origin->right_xyz[0],
			gaze_origin->right_xyz[1],
			gaze_origin->right_xyz[2]);
	}
}
*/

/*
 * device detection is done via a callback drivern url system
 */
static void devpath_recv(const char* url, void* tag)
{
	char** buf = tag;
	if( *buf != NULL || !url)
		return;

	*buf = strdup(url);
}

static void build_device(struct shmif_dev* dev)
{
	*dev = (struct shmif_dev){
		.outev = {
			.category = EVENT_IO,
			.io = {
				.devid = 0x2122,
				.subid = 1,
				.kind = EVENT_IO_EYES,
				.datatype = EVENT_IDATATYPE_EYES,
				.devkind = EVENT_IDEVKIND_EYETRACKER,
			}
		}
	};

	dev->C = arcan_shmif_open_ext(SHMIF_ACQUIRE_FATALFAIL,
		NULL, (struct shmif_open_ext){.type = SEGID_SENSOR},
		sizeof(struct shmif_open_ext)
	);
	arcan_shmif_signal(&dev->C, SHMIF_SIGVID);

/* enqueue information about the calibration API */
	arcan_shmif_enqueue(&dev->C, &(struct arcan_event){
		.category = EVENT_EXTERNAL,
		.ext.kind = ARCAN_EVENT(LABELHINT),
		.ext.labelhint.idatatype = EVENT_IDATATYPE_DIGITAL,
		.ext.labelhint.label = "CALIBRATE",
		.ext.labelhint.descr = "Run a calibration session"
	});

/* Need to push the first signal for all the server-side
 * resources to be setup-/ allocated-. */
	arcan_shmif_signal(&dev->C, SHMIF_SIGVID);
}

extern char** environ;

int main(int argc, char** argv)
{
	struct shmif_dev dev;
	tobii_api_t* api;
	tobii_error_t error = tobii_api_create(&api, NULL, NULL);
	if (error != TOBII_ERROR_NO_ERROR){
		fprintf(stderr, "Couldn't create api: %d\n", error);
		return EXIT_FAILURE;
	}

/* face: meet palm */
	if (strcmp(argv[0], "tobii_config") != 0){

/* boys, turn your heads and cough */
		int self = open("/proc/self/exe", O_RDONLY);
		pid_t child;
		if ((child = fork())){
			while (waitpid(child, NULL, 0) != child){}
			return EXIT_SUCCESS;
		}
		argv[0] = "tobii_config";
		fexecve(self, argv, environ);
		return EXIT_FAILURE;
	}

	const char* features[] = {
		"No Gaze Point",
		"No Gaze Origin",
		"No User Presence",
		"No Head Pose",
		NULL
	};

	tobii_license_key_t license = {
		.license_key = (uint16_t const*) se_license_key_tobii_config,
		.size_in_bytes = 1492
	};

	char* devpath = NULL;
	tobii_device_t* device;
	error = tobii_enumerate_local_device_urls(api, devpath_recv, &devpath);
	if (error != TOBII_ERROR_NO_ERROR){
		fprintf(stderr, "Couldn't scan devices: %d\n", error);
		return EXIT_FAILURE;
	}

	if (!devpath){
		fprintf(stderr, "No device detected\n");
		return EXIT_FAILURE;
	}

	tobii_license_validation_result_t vres = 0;
	error = tobii_device_create_ex(api, devpath, &license, 1, &vres, &device);

	printf("license status: %d\n", vres);

	if (error != TOBII_ERROR_NO_ERROR){
		fprintf(stderr, "Couldn't create device: %d\n", error);
		return EXIT_FAILURE;
	}

	build_device(&dev);

	tobii_supported_t supported;
	error = tobii_stream_supported(device, TOBII_STREAM_GAZE_POINT, &supported);
	if (supported == TOBII_SUPPORTED){
		tobii_gaze_point_subscribe(device, on_gaze, &dev);
	}
	else {
		features[0] = "Gaze Point";
	}

/* don't have much use for this at the moment */
	error = tobii_stream_supported(device, TOBII_STREAM_GAZE_ORIGIN, &supported);
	if (supported == TOBII_SUPPORTED){
/*		tobii_gaze_origin_subscribe(device, on_gaze_origin, &dev); */
		features[1] = "Gaze Origin";
	}

	error = tobii_stream_supported(device, TOBII_STREAM_USER_PRESENCE, &supported);
	if (supported == TOBII_SUPPORTED){
		tobii_user_presence_subscribe(device, on_presence, &dev);
		features[2] = "User Presence";
	}

	error = tobii_stream_supported(device, TOBII_STREAM_HEAD_POSE, &supported);
	if (supported == TOBII_SUPPORTED){
		tobii_head_pose_subscribe(device, on_head, &dev);
		features[3] = "Head Pose";
	}

/* move to drawing into the context */
	printf("Feature Report:\n");
	for (size_t i = 0; i < sizeof(features) / sizeof(features[0]) && features[i]; i++){
		printf("\t%sfeatures[i]\n", features[i]);
	}

	tobii_state_string_t value;
	error = tobii_get_state_string(device, TOBII_STATE_FAULT, value);

	int rv = 0;
	while (rv != -1){
		tobii_wait_for_callbacks(1, &device);
		tobii_device_process_callbacks(device);
		tobii_update_timesync(device);

		if (dev.dirty){
			arcan_event ev;

			while ((rv = arcan_shmif_poll(&dev.C, &ev)) > 0){

/* respect whatever the resizing might be, relevant for calibration */
				if (ev.category == EVENT_TARGET && ev.tgt.kind == TARGET_COMMAND_DISPLAYHINT){
					if (ev.tgt.ioevs[0].iv && ev.tgt.ioevs[1].iv)
						arcan_shmif_resize(&dev.C, ev.tgt.ioevs[0].iv, ev.tgt.ioevs[1].iv);
				}

/* reset calibration stage */
				if (ev.category == EVENT_IO &&
					ev.io.datatype == EVENT_IDATATYPE_DIGITAL &&
					ev.io.input.digital.active &&
					strcmp(ev.io.label, "CALIBRATE") == 0){
					calibrate(&dev, device);
				}
			}

			arcan_shmif_enqueue(&dev.C, &dev.outev);
			dev.dirty = false;
		}
	}
	tobii_gaze_point_unsubscribe(device);
	tobii_device_destroy(device);
	tobii_api_destroy(api);

	return EXIT_SUCCESS;
}
