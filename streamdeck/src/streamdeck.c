/*
 * Simple driver for the ElGato Stream Deck
 */

#include <arcan_shmif.h>
#include <hidapi/hidapi.h>
#include <inttypes.h>
#include <time.h>
#include <strings.h>

struct streamdeck {
	size_t cell_w, cell_h;
	size_t rows, cols;
	uint16_t* cell_checks;
	size_t n_cells;
	hid_device* dev;
};

static uint16_t fletch(uint8_t* buf, size_t count)
{
	uint16_t s1 = 0, s2 = 0;
	for (size_t i = 0; i < count; i++){
		s1 = (s1 + buf[i]) % 255;
		s2 = (s2 + s1) % 255;
	}
	return (s2 << 8) | s1;
}

static void update_cell(
	struct streamdeck* deck, uint8_t row, uint8_t col, uint8_t* buf)
{
	uint8_t indx = (row * deck->cols) + (deck->cols - col);
	if (indx > deck->n_cells)
		return;

/* checksum and early- out if nothing */
	uint16_t chk = fletch(buf, deck->cell_w * deck->cell_h * 3);
	if (chk == deck->cell_checks[indx])
		return;

	deck->cell_checks[indx] = chk;

	uint8_t page_1[] = {
		0x02, 0x01, 0x01, 0x00, 0x00, indx, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x42, 0x4D, 0xF6, 0x3C, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x48, 0x00,
		0x00, 0x00, 0x48, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
		0x00, 0x00, 0xC0, 0x3C, 0x00, 0x00, 0xC4, 0x0E, 0x00, 0x00, 0xC4, 0x0E,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	uint8_t page_2[] = {
		0x02, 0x01, 0x02, 0x00, 0x01, indx, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	const size_t report_sz = 8191;
	uint8_t report[report_sz];
	memset(report, '\0', report_sz);

	memcpy(report, page_1, sizeof(page_1));
	memcpy(&report[sizeof(page_1)], buf, 2583 * 3);
	int nw = hid_write(deck->dev, report, report_sz);

	memcpy(report, page_2, sizeof(page_2));
	memcpy(&report[sizeof(page_2)], &buf[2583*3], 2601 * 3);
	nw = hid_write(deck->dev, report, report_sz);
}

/*
 * static void set_brightness(struct streamdeck* dest, uint8_t pct)
{
	if (pct > 100)
		pct = 100;

	uint8_t rep[17] = {
		0x05, 0x55, 0xaa, 0xd1, 0x01, pct
	};

	hid_send_feature_report(dest->dev, rep, 17);
}
 */

static int decode_keys(struct streamdeck* deck, uint8_t* buf)
{
	int key_mask = 0;
	for (size_t row = 0; row < deck->rows; row++){
		for (size_t col = 0; col < deck->cols; col++){
			if (buf[(row + 1) * deck->cols - col])
				key_mask |= 1 << (row * deck->cols + col);
		}
	}
	return key_mask;
}

static void reset(hid_device* dest)
{
	uint8_t rep[17] = {
		0x0b, 0x63
	};
	hid_send_feature_report(dest, rep, 17);
}

static void repack(
	struct streamdeck* deck, shmif_pixel* vidp, size_t pitch, uint8_t* dst)
{
	for (size_t y = 0; y < deck->cell_h; y++)
		for (size_t x = 0; x < deck->cell_w; x++){
			size_t ofs = (y * deck->cell_w + (deck->cell_w - 1 - x)) * 3;
			uint8_t a;
			SHMIF_RGBA_DECOMP(vidp[y * pitch + x],
				&dst[ofs+0], &dst[ofs+2], &dst[ofs+1], &a);
		}
}

static void ctx_to_buttons(struct streamdeck* D, struct arcan_shmif_cont* C)
{
	uint8_t scratch[D->cell_w * D->cell_h * 3];

	for (size_t y = 0; y < D->rows && (y+1 * D->cell_h) <= C->h; y++){
		for (size_t x = 0; x < D->cols && (x+1 * D->cell_w) <= C->w; x++){
			repack(D,
				&C->vidp[C->pitch * (y * D->cell_w) + x * D->cell_w], C->pitch, scratch);
			update_cell(D, y, x, scratch);
		}
	}

/* done with this buffer, request a new one */
	arcan_shmif_signal(C, SHMIF_SIGVID);
}

static void deploy_mask(struct arcan_shmif_cont* C, int mask, int changed)
{
	int ind;
	struct arcan_event ev = {
		.category = EVENT_IO,
		.io = {
			.devkind = EVENT_IDEVKIND_GAMEDEV,
			.datatype = EVENT_IDATATYPE_DIGITAL
		}
	};

	while ((ind = ffs(changed))){
		ev.io.subid = ind;
		ev.io.input.digital.active = !!(mask & (1 << (ind-1)));
		changed = changed & ~(1 << (ind-1));

		arcan_shmif_enqueue(C, &ev);
	}
}

static void arcan_loop(struct streamdeck* D, struct arcan_shmif_cont* C)
{
	uint8_t inbuf[64];
	int old_mask = 0;

	for(;;){
		struct arcan_event ev;
		ssize_t nr = hid_read_timeout(D->dev, inbuf, sizeof(inbuf), 16);
		if (-1 == nr)
			break;

/* did we get an input event? */
		if (nr >= D->n_cells){
			int mask = decode_keys(D, inbuf);
			deploy_mask(C, mask, mask ^ old_mask);
			old_mask = mask;
		}

/* check for termination or stepframe */
		if (arcan_shmif_poll(C, &ev) > 0){
			if (ev.category != EVENT_TARGET)
				continue;

			if (ev.tgt.kind == TARGET_COMMAND_EXIT)
				break;

			if (ev.tgt.kind == TARGET_COMMAND_STEPFRAME)
				ctx_to_buttons(D, C);
		}
	}

	arcan_shmif_drop(C);
}

int main(int argc, char** argv)
{
	hid_init();

	uint16_t cell_checks[16] = {0};
	struct streamdeck deck = {
		.cell_w = 72, .cell_h = 72,
		.rows = 3, .cols = 5,
		.n_cells = 16,
		.cell_checks = cell_checks,
		.dev = hid_open(0x0fd9, 0x0060, NULL)
	};

	if (!deck.dev){
		fprintf(stderr, "Couldn't open streamdeck-device (0fd9:0060)\n");
		return EXIT_FAILURE;
	}

/* try and register as an encoder that can emit input events */
	struct arcan_shmif_cont cont =
		arcan_shmif_open(SEGID_ENCODER, SHMIF_ACQUIRE_FATALFAIL, NULL);

	arcan_loop(&deck, &cont);

	reset(deck.dev);
	hid_close(deck.dev);
	return EXIT_SUCCESS;
}
