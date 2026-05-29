#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <drm.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define GAMMA_SIZE 256

/* BMP file header structures */
#pragma pack(push, 1)
typedef struct {
	uint16_t type;
	uint32_t size;
	uint16_t reserved1;
	uint16_t reserved2;
	uint32_t offset;
} bmp_file_header_t;

typedef struct {
	uint32_t size;
	int32_t  width;
	int32_t  height;
	uint16_t planes;
	uint16_t bits;
	uint32_t compression;
	uint32_t imagesize;
	int32_t  xresolution;
	int32_t  yresolution;
	uint32_t ncolors;
	uint32_t importantcolors;
} bmp_info_header_t;
#pragma pack(pop)

struct drm_buffer {
	uint32_t handle;
	uint32_t fb_id;
	uint32_t size;
	uint32_t stride;
	void *map;
};

/* Standard gamma values */
static const double valid_gamma[] = { 0.0, 1.8, 2.2, 2.4, 2.6 };
static uint16_t *gamma_luts[5] = { NULL, NULL, NULL, NULL, NULL };
#define NUM_VALID_GAMMA (sizeof(valid_gamma) / sizeof(valid_gamma[0]))

/*
 * Generate gamma table using formula:
 * output = 255 * (input/255)^gamma
 */
static uint16_t *generate_gamma_table(double gamma)
{
	uint16_t *table = malloc(GAMMA_SIZE * sizeof(uint16_t));
	if (!table)
		return NULL;

	for (int i = 0; i < GAMMA_SIZE; i++) {
		double normalized = i / 255.0;
		double val = 255.0 * pow(normalized, gamma);
		int ival = (int)round(val);
		table[i] = (ival > 255) ? 255 : ival;
	}

	return table;
}

static void init_gamma_tables(void)
{
	gamma_luts[0] = NULL;  /* Index 0 is for gamma=0 (clear) */
	for (int i = 1; i < 5; i++) {
		gamma_luts[i] = generate_gamma_table(valid_gamma[i]);
	}
}

static void free_gamma_tables(void)
{
	for (int i = 1; i < 5; i++) {
		free(gamma_luts[i]);
		gamma_luts[i] = NULL;
	}
}

static int create_dumb_buffer(int fd, struct drm_buffer *buf, uint32_t width, uint32_t height)
{
	struct drm_mode_create_dumb create = {0};
	struct drm_mode_map_dumb map = {0};
	int ret;

	create.width = width;
	create.height = height;
	create.bpp = 32;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
	if (ret < 0)
		return ret;

	buf->handle = create.handle;
	buf->stride = create.pitch;
	buf->size = create.size;

	ret = drmModeAddFB(fd, width, height, 24, 32, buf->stride, buf->handle, &buf->fb_id);
	if (ret)
		return ret;

	map.handle = buf->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
	if (ret)
		return ret;

	buf->map = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (buf->map == MAP_FAILED)
		return -1;

	memset(buf->map, 0, buf->size);
	return 0;
}

static void destroy_dumb_buffer(int fd, struct drm_buffer *buf)
{
	struct drm_mode_destroy_dumb destroy = {0};

	if (buf->map)
		munmap(buf->map, buf->size);
	if (buf->fb_id)
		drmModeRmFB(fd, buf->fb_id);
	if (buf->handle) {
		destroy.handle = buf->handle;
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
	}
}

static int load_bmp(const char *filename, uint8_t **pixels, int *width, int *height)
{
	FILE *fp;
	bmp_file_header_t file_header;
	bmp_info_header_t info_header;
	uint8_t *data;
	int row_size, padding, bpp, top_down;

	fp = fopen(filename, "rb");
	if (!fp)
		return -1;

	if (fread(&file_header, sizeof(file_header), 1, fp) != 1 ||
	    file_header.type != 0x4D42) {
		fclose(fp);
		return -1;
	}

	if (fread(&info_header, sizeof(info_header), 1, fp) != 1) {
		fclose(fp);
		return -1;
	}

	*width = info_header.width;
	top_down = (info_header.height < 0);
	*height = top_down ? -info_header.height : info_header.height;
	bpp = info_header.bits;

	if (bpp != 24 && bpp != 32) {
		fclose(fp);
		return -1;
	}

	row_size = ((*width * bpp / 8) + 3) & ~3;
	padding = row_size - (*width * bpp / 8);

	data = malloc(*width * *height * 4);
	if (!data) {
		fclose(fp);
		return -1;
	}

	fseek(fp, file_header.offset, SEEK_SET);

	for (int y = 0; y < *height; y++) {
		int dst_y = top_down ? y : (*height - 1 - y);
		uint8_t *row = data + dst_y * *width * 4;

		for (int x = 0; x < *width; x++) {
			uint8_t pixel[4] = {0, 0, 0, 255};
			if (fread(pixel, 1, bpp / 8, fp) != (size_t)(bpp / 8)) {
				free(data);
				fclose(fp);
				return -1;
			}
			row[x * 4 + 0] = pixel[0];  /* B */
			row[x * 4 + 1] = pixel[1];  /* G */
			row[x * 4 + 2] = pixel[2];  /* R */
			row[x * 4 + 3] = (bpp == 32) ? pixel[3] : 255;
		}
		if (padding)
			fseek(fp, padding, SEEK_CUR);
	}

	fclose(fp);
	*pixels = data;
	return 0;
}

static drmModeModeInfo *find_preferred_mode(drmModeConnector *conn)
{
	/* First try to find preferred mode */
	for (int i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED)
			return &conn->modes[i];
	}
	/* Fall back to first mode */
	if (conn->count_modes > 0)
		return &conn->modes[0];
	return NULL;
}

static uint32_t find_crtc(int fd, drmModeRes *res, drmModeConnector *conn)
{
	drmModeEncoder *enc;

	if (conn->encoder_id) {
		enc = drmModeGetEncoder(fd, conn->encoder_id);
		if (enc && enc->crtc_id) {
			uint32_t crtc_id = enc->crtc_id;
			drmModeFreeEncoder(enc);
			return crtc_id;
		}
		if (enc)
			drmModeFreeEncoder(enc);
	}

	for (int i = 0; i < conn->count_encoders; i++) {
		enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc)
			continue;
		for (int j = 0; j < res->count_crtcs; j++) {
			if (enc->possible_crtcs & (1 << j)) {
				drmModeFreeEncoder(enc);
				return res->crtcs[j];
			}
		}
		drmModeFreeEncoder(enc);
	}
	return 0;
}

static int get_gamma_index(double gamma)
{
	for (int i = 0; i < NUM_VALID_GAMMA; i++) {
		if (gamma == valid_gamma[i])
			return i;
	}
	return -1;
}

static uint32_t find_gamma_lut_prop(int fd, uint32_t crtc_id)
{
	drmModeObjectProperties *props;
	uint32_t prop_id = 0;

	props = drmModeObjectGetProperties(fd, crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!props)
		return 0;

	for (int i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		if (prop) {
			if (strcmp(prop->name, "GAMMA_LUT") == 0)
				prop_id = prop->prop_id;
			drmModeFreeProperty(prop);
		}
	}
	drmModeFreeObjectProperties(props);
	return prop_id;
}

static int set_gamma_legacy(int fd, uint32_t crtc_id, const uint16_t *lut)
{
	uint16_t r[GAMMA_SIZE], g[GAMMA_SIZE], b[GAMMA_SIZE];

	memcpy(r, lut, sizeof(r));
	memcpy(g, lut, sizeof(g));
	memcpy(b, lut, sizeof(b));

	return drmModeCrtcSetGamma(fd, crtc_id, GAMMA_SIZE, r, g, b);
}

static int set_gamma_atomic(int fd, uint32_t crtc_id, uint32_t gamma_prop_id, const uint16_t *lut)
{
	struct drm_color_lut drm_lut[GAMMA_SIZE];
	uint32_t blob_id;
	int ret;

	for (int i = 0; i < GAMMA_SIZE; i++) {
		drm_lut[i].red = drm_lut[i].green = drm_lut[i].blue = lut[i];
		drm_lut[i].reserved = 0;
	}

	ret = drmModeCreatePropertyBlob(fd, drm_lut, sizeof(drm_lut), &blob_id);
	if (ret)
		return ret;

	ret = drmModeObjectSetProperty(fd, crtc_id, DRM_MODE_OBJECT_CRTC, gamma_prop_id, blob_id);
	drmModeDestroyPropertyBlob(fd, blob_id);
	return ret;
}

static void print_usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-d] [-b <bmp_file>] [-i] <crtc_id> <gamma>\n", prog);
	fprintf(stderr, "\nArguments:\n");
	fprintf(stderr, "  crtc_id         DRM CRTC identifier (e.g., 36)\n");
	fprintf(stderr, "  gamma           Gamma value: 0 (clear), 1.8, 2.2, 2.4, 2.6\n");
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -d              Dump all gamma tables before applying\n");
	fprintf(stderr, "  -b <bmp_file>   Display BMP file on CRTC before applying gamma\n");
	fprintf(stderr, "  -i              Interactive mode for changing gamma continuously\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  %s 36 2.2                    # Set gamma 2.2 on CRTC 36\n", prog);
	fprintf(stderr, "  %s -d 36 2.2                 # Dump tables, then set gamma\n", prog);
	fprintf(stderr, "  %s -i 36 2.2                 # Interactive mode on CRTC 36\n", prog);
	fprintf(stderr, "  %s -b image.bmp 36 2.2       # Display BMP and set gamma\n", prog);
	fprintf(stderr, "  %s -d -b image.bmp 36 2.2    # Dump tables, display BMP, set gamma\n", prog);
}

static void dump_gamma_tables(void)
{
	printf("Gamma Table Generation Formula:\n");
	printf("================================\n");
	printf("\n");
	printf("  output = 255 * (input / 255) ^ gamma\n");
	printf("\n");
	printf("  where: input = 0, 1, 2, ..., 255\n");
	printf("         gamma = 1.8, 2.2, 2.4, 2.6\n");
	printf("\n");

	for (int g = 1; g < NUM_VALID_GAMMA; g++) {
		printf("Gamma %.1f:  output = 255 * (input/255)^%.1f\n", valid_gamma[g], valid_gamma[g]);
		printf("---------\n");
		const uint16_t *lut = gamma_luts[g];
		if (!lut) {
			printf("  (not generated)\n");
			continue;
		}
		for (int i = 0; i < GAMMA_SIZE; i++) {
			printf("%3d", lut[i]);
			if ((i + 1) % 16 == 0)
				printf("\n");
			else
				printf(",");
		}
		printf("\n");
	}
}

static int apply_gamma(int fd, uint32_t crtc_id, uint32_t gamma_prop_id, double gamma_val, int gamma_idx)
{
	const uint16_t *lut = gamma_luts[gamma_idx];
	int ret;

	/* gamma_idx 0 means clear (NULL lut) */
	if (lut == NULL) {
		if (gamma_prop_id) {
			ret = drmModeObjectSetProperty(fd, crtc_id, DRM_MODE_OBJECT_CRTC, gamma_prop_id, 0);
			if (ret == 0)
				printf("Gamma cleared\n");
			else
				fprintf(stderr, "Failed to clear gamma\n");
		} else {
			fprintf(stderr, "Cannot clear gamma: GAMMA_LUT property not found\n");
			ret = -1;
		}
		return ret;
	}

	if (gamma_prop_id)
		ret = set_gamma_atomic(fd, crtc_id, gamma_prop_id, lut);
	else
		ret = set_gamma_legacy(fd, crtc_id, lut);

	if (ret == 0)
		printf("Gamma %.1f applied\n", gamma_val);
	else
		fprintf(stderr, "Failed to set gamma\n");

	return ret;
}

/* Get CRTC current mode dimensions */
static int get_crtc_size(int fd, uint32_t crtc_id, int *width, int *height)
{
	drmModeCrtc *crtc = drmModeGetCrtc(fd, crtc_id);
	if (!crtc)
		return -1;

	if (crtc->mode_valid) {
		*width = crtc->mode.hdisplay;
		*height = crtc->mode.vdisplay;
		drmModeFreeCrtc(crtc);
		return 0;
	}

	drmModeFreeCrtc(crtc);
	return -1;
}

/* Find connector for a given CRTC */
static uint32_t find_connector_for_crtc(int fd, drmModeRes *res, uint32_t crtc_id)
{
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;

		if (conn->connection == DRM_MODE_CONNECTED && conn->encoder_id) {
			drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
			if (enc && enc->crtc_id == crtc_id) {
				uint32_t conn_id = conn->connector_id;
				drmModeFreeEncoder(enc);
				drmModeFreeConnector(conn);
				return conn_id;
			}
			if (enc)
				drmModeFreeEncoder(enc);
		}
		drmModeFreeConnector(conn);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	const char *bmp_path = NULL;
	int fd, ret = 0;
	drmModeRes *res = NULL;
	struct drm_buffer buf = {0};
	uint32_t crtc_id, conn_id, gamma_prop_id;
	uint8_t *bmp_pixels = NULL;
	int bmp_width = 0, bmp_height = 0;
	int crtc_width = 0, crtc_height = 0;
	int gamma_idx;
	char input[16];
	double gamma_val;

	/* Options */
	int opt_dump = 0;
	int opt_interactive = 0;
	int opt_index = 1;

	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	/* Parse options */
	while (opt_index < argc && argv[opt_index][0] == '-') {
		if (strcmp(argv[opt_index], "-d") == 0) {
			opt_dump = 1;
			opt_index++;
		} else if (strcmp(argv[opt_index], "-i") == 0) {
			opt_interactive = 1;
			opt_index++;
		} else if (strcmp(argv[opt_index], "-b") == 0) {
			if (opt_index + 1 >= argc) {
				fprintf(stderr, "Error: -b requires a BMP file argument\n");
				return 1;
			}
			bmp_path = argv[opt_index + 1];
			opt_index += 2;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[opt_index]);
			print_usage(argv[0]);
			return 1;
		}
	}

	/* Initialize gamma tables */
	init_gamma_tables();

	/* Handle dump option */
	if (opt_dump)
		dump_gamma_tables();

	/* Need crtc_id and gamma */
	if (argc - opt_index < 2) {
		fprintf(stderr, "Error: crtc_id and gamma are required\n");
		print_usage(argv[0]);
		free_gamma_tables();
		return 1;
	}

	crtc_id = atoi(argv[opt_index]);
	gamma_val = atof(argv[opt_index + 1]);
	gamma_idx = get_gamma_index(gamma_val);

	if (gamma_idx < 0) {
		fprintf(stderr, "Invalid gamma value. Use: 0, 1.8, 2.2, 2.4, 2.6\n");
		free_gamma_tables();
		return 1;
	}

	/* Open DRM device */
	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		perror("open /dev/dri/card0");
		free_gamma_tables();
		return 1;
	}

	/* Get resources */
	res = drmModeGetResources(fd);
	if (!res) {
		perror("drmModeGetResources");
		ret = 1;
		goto cleanup;
	}

	/* Find gamma property for CRTC */
	gamma_prop_id = find_gamma_lut_prop(fd, crtc_id);
	if (!gamma_prop_id && gamma_idx != 0) {
		fprintf(stderr, "GAMMA_LUT property not found for CRTC %u\n", crtc_id);
		ret = 1;
		goto cleanup;
	}

	/* Handle BMP display mode */
	if (bmp_path) {
		drmModeConnector *conn;
		drmModeModeInfo *mode;

		printf("Loading BMP: %s\n", bmp_path);
		if (load_bmp(bmp_path, &bmp_pixels, &bmp_width, &bmp_height) < 0) {
			fprintf(stderr, "Failed to load BMP: %s\n", bmp_path);
			ret = 1;
			goto cleanup;
		}
		printf("BMP loaded: %dx%d\n", bmp_width, bmp_height);

		/* Get CRTC current mode size */
		if (get_crtc_size(fd, crtc_id, &crtc_width, &crtc_height) == 0) {
			printf("CRTC %u current mode: %dx%d\n", crtc_id, crtc_width, crtc_height);

			/* Check if BMP fits in CRTC */
			if (bmp_width > crtc_width || bmp_height > crtc_height) {
				fprintf(stderr, "Error: BMP image (%dx%d) is larger than CRTC (%dx%d)\n",
				        bmp_width, bmp_height, crtc_width, crtc_height);
				ret = 1;
				goto cleanup;
			}
		}

		/* Find connector for this CRTC */
		conn_id = find_connector_for_crtc(fd, res, crtc_id);
		if (!conn_id) {
			fprintf(stderr, "No connector found for CRTC %u\n", crtc_id);
			ret = 1;
			goto cleanup;
		}

		/* Get connector and mode */
		conn = drmModeGetConnector(fd, conn_id);
		if (!conn) {
			fprintf(stderr, "Failed to get connector %u\n", conn_id);
			ret = 1;
			goto cleanup;
		}

		mode = find_preferred_mode(conn);
		if (!mode) {
			fprintf(stderr, "No mode found for connector %u\n", conn_id);
			drmModeFreeConnector(conn);
			ret = 1;
			goto cleanup;
		}

		printf("Found display: connector %d, CRTC %d, %s@%dHz\n",
		       conn_id, crtc_id, mode->name, mode->vrefresh);

		/* Check BMP vs mode size */
		if (bmp_width > (int)mode->hdisplay || bmp_height > (int)mode->vdisplay) {
			fprintf(stderr, "Error: BMP image (%dx%d) is larger than display (%dx%d)\n",
			        bmp_width, bmp_height, mode->hdisplay, mode->vdisplay);
			drmModeFreeConnector(conn);
			ret = 1;
			goto cleanup;
		}

		drmSetMaster(fd);

		/* Create framebuffer */
		if (create_dumb_buffer(fd, &buf, mode->hdisplay, mode->vdisplay) < 0) {
			fprintf(stderr, "Failed to create framebuffer\n");
			drmModeFreeConnector(conn);
			ret = 1;
			goto cleanup;
		}

		/* Copy BMP to framebuffer (centered) */
		{
			int dst_x = 0, dst_y = 0;

			if (bmp_width < (int)mode->hdisplay)
				dst_x = (mode->hdisplay - bmp_width) / 2;
			if (bmp_height < (int)mode->vdisplay)
				dst_y = (mode->vdisplay - bmp_height) / 2;

			for (int y = 0; y < bmp_height; y++) {
				uint8_t *src = bmp_pixels + (y * bmp_width) * 4;
				uint8_t *dst = (uint8_t *)buf.map + ((dst_y + y) * buf.stride) + dst_x * 4;
				memcpy(dst, src, bmp_width * 4);
			}
		}

		/* Set mode */
		if (drmModeSetCrtc(fd, crtc_id, buf.fb_id, 0, 0, &conn_id, 1, mode)) {
			perror("drmModeSetCrtc");
			drmModeFreeConnector(conn);
			ret = 1;
			goto cleanup;
		}

		drmModeFreeConnector(conn);
		printf("BMP displayed on CRTC %u\n", crtc_id);
	}

	/* Apply initial gamma */
	ret = apply_gamma(fd, crtc_id, gamma_prop_id, gamma_val, gamma_idx);
	if (ret) {
		ret = 1;
		goto cleanup;
	}

	/* Interactive mode or wait for quit when BMP is displayed */
	if (opt_interactive || bmp_path) {
		if (opt_interactive)
			printf("\nInteractive mode. Available gamma values: 0 (clear), 1.8, 2.2, 2.4, 2.6\n");
		printf("Press 'q' to quit%s: ", opt_interactive ? " (or enter gamma value)" : "");
		fflush(stdout);

		while (1) {
			if (fgets(input, sizeof(input), stdin) == NULL)
				break;

			/* Check for quit */
			if (input[0] == 'q' || input[0] == 'Q')
				break;

			/* Parse gamma value only in interactive mode */
			if (opt_interactive) {
				gamma_val = atof(input);
				gamma_idx = get_gamma_index(gamma_val);
				if (gamma_idx < 0) {
					printf("Invalid gamma. Use: 0, 1.8, 2.2, 2.4, 2.6\n");
				} else {
					apply_gamma(fd, crtc_id, gamma_prop_id, valid_gamma[gamma_idx], gamma_idx);
				}
			}
			printf("Press 'q' to quit%s: ", opt_interactive ? " (or enter gamma value)" : "");
			fflush(stdout);
		}
	}

cleanup:
	destroy_dumb_buffer(fd, &buf);
	if (res)
		drmModeFreeResources(res);
	if (bmp_path)
		drmDropMaster(fd);
	close(fd);
	free(bmp_pixels);
	free_gamma_tables();
	return ret;
}
