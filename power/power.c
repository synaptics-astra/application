// SPDX-License-Identifier: GPL-2.0-or-later
//
// Simple power monitor for sl1680 / sl1640 / sl1620 / sl2610
//  - Automatically discovers INA3221 / INA220 / INA226 and temp hwmon via /sys/class/hwmon
//  - Does not depend on hwmonX index ordering
//
// Usage:
//   ./power 1       # sl1680 single-shot
//   ./power m1      # sl1680 monitor mode
//   ./power 2       # sl1640 single-shot
//   ./power m2      # sl1640 monitor mode
//   ./power 3       # sl1620 single-shot
//   ./power m3      # sl1620 monitor mode
//   ./power 4       # sl2610 single-shot
//   ./power m4      # sl2610 monitor mode
//

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>

#define SAMPLES     10      /* number of samples per channel for averaging */
#define REFRESH_MS  500     /* refresh period in monitor mode (ms) */

/* ------------------------------------------------------------------------ */
/* Per-board hwmon path sets                                                */
/* ------------------------------------------------------------------------ */

struct sl1680_hwmon_paths {
    char ina3221_core[PATH_MAX];  /* ina3221@40 - VCORE / VCPU / VDDM */
    char ina3221_ddr[PATH_MAX];   /* ina3221@41 - PWR_3V3 / VDDM_1V8 / VDDM_1V1 */
    char v3v3[PATH_MAX];          /* INA2xx@40 - 3V3 */
    char v1v8[PATH_MAX];          /* INA2xx@41 - 1V8 */
    char temp[PATH_MAX];          /* SoC temperature hwmon */
};

struct sl1640_hwmon_paths {
    char ina3221[PATH_MAX];       /* i2c@1800: ina3221@40 - VCORE / VCPU / VDDM_1V1 */
    char vddm_0v6[PATH_MAX];      /* i2c@1800: INA2xx@41 - VDDM_0V6 */
    char v3v3[PATH_MAX];          /* i2c@c000: INA2xx@40 - 3V3 */
    char v1v8[PATH_MAX];          /* i2c@c000: INA2xx@41 - 1V8 */
    char temp[PATH_MAX];          /* SoC temperature hwmon */
};

struct sl1620_hwmon_paths {
    char ina3221[PATH_MAX];       /* i2c@1c00: ina3221@40 - VCORE / 1V2 / 2V5 */
    char v3v3[PATH_MAX];          /* i2c@2000: INA2xx@40 - 3V3 */
    char v1v8[PATH_MAX];          /* i2c@2000: INA2xx@41 - 1V8 */
    char temp[PATH_MAX];          /* SoC temperature hwmon */
};

struct sl2610_hwmon_paths {
    char v3v3[PATH_MAX];          /* sm/i2c@e5035000: INA2xx@40 - 3V3 */
    char v1v8[PATH_MAX];          /* sm/i2c@e5035000: INA2xx@41 - 1V8 */
    char ina3221[PATH_MAX];       /* sm/i2c@e5035000: ina3221@42 - VCORE / 1V2 / 2V5 */
    char vdd_sm[PATH_MAX];        /* sm/i2c@e5035000: INA2xx@45 - VDD_SM */
    char temp[PATH_MAX];          /* SoC temperature hwmon */
};

static struct sl1680_hwmon_paths g_sl1680;
static int g_sl1680_inited = 0;

static struct sl1640_hwmon_paths g_sl1640;
static int g_sl1640_inited = 0;

static struct sl1620_hwmon_paths g_sl1620;
static int g_sl1620_inited = 0;

static struct sl2610_hwmon_paths g_sl2610;
static int g_sl2610_inited = 0;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

static int read_int_file(const char *path, int *out_value)
{
    FILE *f;
    int v;

    f = fopen(path, "r");
    if (!f)
        return -1;

    if (fscanf(f, "%d", &v) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);
    *out_value = v;
    return 0;
}

/* Treat ina220 / ina226 / ina2xx as the same INA2xx family */
static int is_ina2xx_name(const char *name)
{
    return !strcmp(name, "ina220") ||
           !strcmp(name, "ina226") ||
           !strcmp(name, "ina2xx");
}

/*
 * Read hwmon of_node symlink.
 * Try "device/of_node" first, then fallback to "of_node".
 * Returns 0 on success, -1 on failure.
 */
static int read_hwmon_of_node(const char *hwmon_base, char *buf, size_t buf_len)
{
    char path[PATH_MAX];
    ssize_t len;

    snprintf(path, sizeof(path), "%s/device/of_node", hwmon_base);
    len = readlink(path, buf, buf_len - 1);
    if (len < 0) {
        snprintf(path, sizeof(path), "%s/of_node", hwmon_base);
        len = readlink(path, buf, buf_len - 1);
        if (len < 0)
            return -1;
    }

    buf[len] = '\0';
    return 0;
}

/*
 * Find a hwmon directory that looks like a temperature sensor:
 *  - chip name is NOT INA2xx family (ina220/ina226/ina2xx) and NOT ina3221
 *  - has temp1_input
 *
 * Returns 0 on success and stores the directory path in 'out', otherwise -1.
 */
static int find_temp_hwmon(char *out, size_t out_len)
{
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir)
        return -1;

    struct dirent *de;
    char base[PATH_MAX];
    char path[PATH_MAX];
    char name[64];

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0)
            continue;

        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        /* Read chip name */
        snprintf(path, sizeof(path), "%s/name", base);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        if (!fgets(name, sizeof(name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        name[strcspn(name, "\n")] = '\0';

        /* Skip INA2xx family and ina3221 (they are power sensors, not temp-only) */
        if (is_ina2xx_name(name) || !strcmp(name, "ina3221"))
            continue;

        /* Check if temp1_input exists */
        snprintf(path, sizeof(path), "%s/temp1_input", base);
        if (access(path, R_OK) == 0) {
            strncpy(out, base, out_len);
            out[out_len - 1] = '\0';
            closedir(dir);
            return 0;
        }
    }

    closedir(dir);
    return -1;
}

/* Generic helper to read temperature from a hwmon directory */
static int read_temp_hwmon(const char *hwmon_dir, int *temp_c)
{
    if (!hwmon_dir || !hwmon_dir[0])
        return -1;

    char path[PATH_MAX];
    int val;

    snprintf(path, sizeof(path), "%s/temp1_input", hwmon_dir);
    if (read_int_file(path, &val) != 0)
        return -1;

    *temp_c = val / 1000;  /* milli-degree -> degree Celsius */
    return 0;
}

/* ------------------------------------------------------------------------ */
/* sl1680: hwmon discovery                                                  */
/* ------------------------------------------------------------------------ */

static int init_sl1680_hwmon_paths(void)
{
    if (g_sl1680_inited)
        return 0;

    memset(&g_sl1680, 0, sizeof(g_sl1680));

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) {
        perror("opendir /sys/class/hwmon");
        return -1;
    }

    struct dirent *de;
    char base[PATH_MAX];
    char path[PATH_MAX];
    char chip_name[64];
    char linkbuf[PATH_MAX];

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0)
            continue;

        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        /* Read chip name */
        snprintf(path, sizeof(path), "%s/name", base);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        if (!fgets(chip_name, sizeof(chip_name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        chip_name[strcspn(chip_name, "\n")] = '\0';

        if (strcmp(chip_name, "ina3221") && !is_ina2xx_name(chip_name))
            continue;

        if (read_hwmon_of_node(base, linkbuf, sizeof(linkbuf)) != 0)
            continue;

        if (!strcmp(chip_name, "ina3221")) {
            if (strstr(linkbuf, "ina3221@40")) {
                strncpy(g_sl1680.ina3221_core, base, sizeof(g_sl1680.ina3221_core));
                g_sl1680.ina3221_core[sizeof(g_sl1680.ina3221_core) - 1] = '\0';
            } else if (strstr(linkbuf, "ina3221@41")) {
                strncpy(g_sl1680.ina3221_ddr, base, sizeof(g_sl1680.ina3221_ddr));
                g_sl1680.ina3221_ddr[sizeof(g_sl1680.ina3221_ddr) - 1] = '\0';
            }
        } else {
            if (strstr(linkbuf, "ina220@40")) {
                strncpy(g_sl1680.v3v3, base, sizeof(g_sl1680.v3v3));
                g_sl1680.v3v3[sizeof(g_sl1680.v3v3) - 1] = '\0';
            } else if (strstr(linkbuf, "ina220@41")) {
                strncpy(g_sl1680.v1v8, base, sizeof(g_sl1680.v1v8));
                g_sl1680.v1v8[sizeof(g_sl1680.v1v8) - 1] = '\0';
            }
        }
    }

    closedir(dir);

    if (!g_sl1680.ina3221_core[0] ||
        !g_sl1680.ina3221_ddr[0]  ||
        !g_sl1680.v3v3[0]         ||
        !g_sl1680.v1v8[0]) {
        fprintf(stderr,
                "sl1680: failed to find all INA devices:\n"
                "  ina3221(core): %s\n"
                "  ina3221(ddr):  %s\n"
                "  3v3:           %s\n"
                "  1v8:           %s\n",
                g_sl1680.ina3221_core,
                g_sl1680.ina3221_ddr,
                g_sl1680.v3v3,
                g_sl1680.v1v8);
        return -1;
    }

    if (!g_sl1680.temp[0]) {
        if (find_temp_hwmon(g_sl1680.temp, sizeof(g_sl1680.temp)) != 0) {
            fprintf(stderr, "sl1680: warning: temperature hwmon not found\n");
        }
    }

    g_sl1680_inited = 1;
    return 0;
}

static int read_sl1680_temp(int *temp_c)
{
    if (!g_sl1680.temp[0])
        return -1;

    return read_temp_hwmon(g_sl1680.temp, temp_c);
}

/*
 * One snapshot of sl1680 rails:
 *  - VCORE / VCPU / VDDM:       ina3221@40 (ina3221_core)
 *  - PWR_3V3 / VDDM_1V8 / 1V1:  ina3221@41 (ina3221_ddr)
 *  - 3V3 / 1V8:                 INA2xx@40 / INA2xx@41
 */
static void monitor_sl1680_once(void)
{
    int shunt_vcore = 0,    bus_vcore = 0,    current_vcore = 0,    power_vcore = 0;
    int shunt_vcpu  = 0,    bus_vcpu  = 0,    current_vcpu  = 0,    power_vcpu  = 0;
    int shunt_vddm  = 0,    bus_vddm  = 0,    current_vddm  = 0,    power_vddm  = 0;

    int shunt_pwr_3v3 = 0,  bus_pwr_3v3 = 0,  current_pwr_3v3 = 0,  power_pwr_3v3 = 0;
    int shunt_vddm_1v8 = 0, bus_vddm_1v8 = 0, current_vddm_1v8 = 0, power_vddm_1v8 = 0;
    int shunt_vddm_1v1 = 0, bus_vddm_1v1 = 0, current_vddm_1v1 = 0, power_vddm_1v1 = 0;

    int shunt_3v3 = 0,      bus_3v3 = 0,      current_3v3 = 0,      power_3v3 = 0;
    int shunt_1v8 = 0,      bus_1v8 = 0,      current_1v8 = 0,      power_1v8 = 0;

    int temp = 0;
    int have_temp = 0;
    int power_total = 0;
    int i;
    char path[PATH_MAX];
    int val;

    if (init_sl1680_hwmon_paths() != 0)
        die("sl1680: hwmon init failed\n");

    for (i = 0; i < SAMPLES; i++) {
        /* ina3221_core@40: VCORE / VCPU / VDDM */

        /* VCORE (ch0) */
        snprintf(path, sizeof(path), "%s/in4_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) shunt_vcore += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) bus_vcore += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) current_vcore += val;

        /* VCPU (ch1) */
        snprintf(path, sizeof(path), "%s/in5_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) shunt_vcpu += val;
        snprintf(path, sizeof(path), "%s/in2_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) bus_vcpu += val;
        snprintf(path, sizeof(path), "%s/curr2_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) current_vcpu += val;

        /* VDDM (ch2) */
        snprintf(path, sizeof(path), "%s/in6_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) shunt_vddm += val;
        snprintf(path, sizeof(path), "%s/in3_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) bus_vddm += val;
        snprintf(path, sizeof(path), "%s/curr3_input", g_sl1680.ina3221_core);
        if (read_int_file(path, &val) == 0) current_vddm += val;

        /* ina3221_ddr@41: PWR_3V3 / VDDM_1V8 / VDDM_1V1 */

        /* PWR_3V3 (ch0) */
        snprintf(path, sizeof(path), "%s/in4_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) shunt_pwr_3v3 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) bus_pwr_3v3 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) current_pwr_3v3 += val;

        /* VDDM_1V8 (ch1) */
        snprintf(path, sizeof(path), "%s/in5_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) shunt_vddm_1v8 += val;
        snprintf(path, sizeof(path), "%s/in2_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) bus_vddm_1v8 += val;
        snprintf(path, sizeof(path), "%s/curr2_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) current_vddm_1v8 += val;

        /* VDDM_1V1 (ch2) */
        snprintf(path, sizeof(path), "%s/in6_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) shunt_vddm_1v1 += val;
        snprintf(path, sizeof(path), "%s/in3_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) bus_vddm_1v1 += val;
        snprintf(path, sizeof(path), "%s/curr3_input", g_sl1680.ina3221_ddr);
        if (read_int_file(path, &val) == 0) current_vddm_1v1 += val;

        /* INA2xx@40: 3V3 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1680.v3v3);
        if (read_int_file(path, &val) == 0) shunt_3v3 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1680.v3v3);
        if (read_int_file(path, &val) == 0) bus_3v3 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1680.v3v3);
        if (read_int_file(path, &val) == 0) current_3v3 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1680.v3v3);
        if (read_int_file(path, &val) == 0) power_3v3 += val;

        /* INA2xx@41: 1V8 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1680.v1v8);
        if (read_int_file(path, &val) == 0) shunt_1v8 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1680.v1v8);
        if (read_int_file(path, &val) == 0) bus_1v8 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1680.v1v8);
        if (read_int_file(path, &val) == 0) current_1v8 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1680.v1v8);
        if (read_int_file(path, &val) == 0) power_1v8 += val;
    }

    /* Average all accumulated values */
    shunt_vcore      /= SAMPLES;
    bus_vcore        /= SAMPLES;
    current_vcore    /= SAMPLES;

    shunt_vcpu       /= SAMPLES;
    bus_vcpu         /= SAMPLES;
    current_vcpu     /= SAMPLES;

    shunt_vddm       /= SAMPLES;
    bus_vddm         /= SAMPLES;
    current_vddm     /= SAMPLES;

    shunt_pwr_3v3    /= SAMPLES;
    bus_pwr_3v3      /= SAMPLES;
    current_pwr_3v3  /= SAMPLES;

    shunt_vddm_1v8   /= SAMPLES;
    bus_vddm_1v8     /= SAMPLES;
    current_vddm_1v8 /= SAMPLES;

    shunt_vddm_1v1   /= SAMPLES;
    bus_vddm_1v1     /= SAMPLES;
    current_vddm_1v1 /= SAMPLES;

    shunt_3v3        /= SAMPLES;
    bus_3v3          /= SAMPLES;
    current_3v3      /= SAMPLES;

    shunt_1v8        /= SAMPLES;
    bus_1v8          /= SAMPLES;
    current_1v8      /= SAMPLES;

    /* Power from power*_input: microwatts -> milliwatts */
    power_3v3    = power_3v3    / SAMPLES / 1000;
    power_1v8    = power_1v8    / SAMPLES / 1000;

    /* INA3221: power = bus(mV) * curr(mA) / 1000 -> mW */
    {
        long long p;

        p = (long long)bus_vcore * current_vcore;
        power_vcore = (int)(p / 1000);

        p = (long long)bus_vcpu * current_vcpu;
        power_vcpu = (int)(p / 1000);

        p = (long long)bus_vddm * current_vddm;
        power_vddm = (int)(p / 1000);

        p = (long long)bus_pwr_3v3 * current_pwr_3v3;
        power_pwr_3v3 = (int)(p / 1000);

        p = (long long)bus_vddm_1v8 * current_vddm_1v8;
        power_vddm_1v8 = (int)(p / 1000);

        p = (long long)bus_vddm_1v1 * current_vddm_1v1;
        power_vddm_1v1 = (int)(p / 1000);
    }

    power_total = power_vcore + power_vcpu + power_vddm +
                  power_pwr_3v3 + power_vddm_1v8 + power_vddm_1v1 +
                  power_3v3 + power_1v8;

    if (read_sl1680_temp(&temp) == 0)
        have_temp = 1;

    system("clear");
    printf("==================================== sl1680 power monitor =======================================\n");
    printf("CH             shunt(uV)      bus(mV)       current(mA)        power(mW)\n");

    printf("vcore          %-12d %-13d %-17d %d\n",
           shunt_vcore, bus_vcore, current_vcore, power_vcore);
    printf("vcpu           %-12d %-13d %-17d %d\n",
           shunt_vcpu, bus_vcpu, current_vcpu, power_vcpu);
    printf("vddm           %-12d %-13d %-17d %d\n",
           shunt_vddm, bus_vddm, current_vddm, power_vddm);

    printf("pwr_3v3        %-12d %-13d %-17d %d\n",
           shunt_pwr_3v3, bus_pwr_3v3, current_pwr_3v3, power_pwr_3v3);
    printf("vddm_1v8       %-12d %-13d %-17d %d\n",
           shunt_vddm_1v8, bus_vddm_1v8, current_vddm_1v8, power_vddm_1v8);
    printf("vddm_1v1       %-12d %-13d %-17d %d\n",
           shunt_vddm_1v1, bus_vddm_1v1, current_vddm_1v1, power_vddm_1v1);

    printf("3v3            %-12d %-13d %-17d %d\n",
           shunt_3v3, bus_3v3, current_3v3, power_3v3);
    printf("1v8            %-12d %-13d %-17d %d\n",
           shunt_1v8, bus_1v8, current_1v8, power_1v8);

    printf("--------------------------------------------------------------------------------------------------\n");
    printf("Total power: %d mW\n", power_total);
    if (have_temp)
        printf("Temperature: %d C\n", temp);
    printf("==================================================================================================\n");
}

/* ------------------------------------------------------------------------ */
/* sl1640: hwmon discovery                                                  */
/* ------------------------------------------------------------------------ */

static int init_sl1640_hwmon_paths(void)
{
    if (g_sl1640_inited)
        return 0;

    memset(&g_sl1640, 0, sizeof(g_sl1640));

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) {
        perror("opendir /sys/class/hwmon");
        return -1;
    }

    struct dirent *de;
    char base[PATH_MAX];
    char path[PATH_MAX];
    char chip_name[64];
    char linkbuf[PATH_MAX];

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0)
            continue;

        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        /* Read chip name */
        snprintf(path, sizeof(path), "%s/name", base);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        if (!fgets(chip_name, sizeof(chip_name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        chip_name[strcspn(chip_name, "\n")] = '\0';

        /* Only INA devices need of_node matching */
        if (strcmp(chip_name, "ina3221") && !is_ina2xx_name(chip_name))
            continue;

        if (read_hwmon_of_node(base, linkbuf, sizeof(linkbuf)) != 0)
            continue;

        if (!strcmp(chip_name, "ina3221")) {
            /* i2c0: i2c@1800/ina3221@40 */
            if (strstr(linkbuf, "/i2c@1800/ina3221@40")) {
                strncpy(g_sl1640.ina3221, base, sizeof(g_sl1640.ina3221));
                g_sl1640.ina3221[sizeof(g_sl1640.ina3221) - 1] = '\0';
            }
        } else {
            if (strstr(linkbuf, "/i2c@1800/ina220@41")) {
                strncpy(g_sl1640.vddm_0v6, base, sizeof(g_sl1640.vddm_0v6));
                g_sl1640.vddm_0v6[sizeof(g_sl1640.vddm_0v6) - 1] = '\0';
            } else if (strstr(linkbuf, "/i2c@c000/ina220@40")) {
                strncpy(g_sl1640.v3v3, base, sizeof(g_sl1640.v3v3));
                g_sl1640.v3v3[sizeof(g_sl1640.v3v3) - 1] = '\0';
            } else if (strstr(linkbuf, "/i2c@c000/ina220@41")) {
                strncpy(g_sl1640.v1v8, base, sizeof(g_sl1640.v1v8));
                g_sl1640.v1v8[sizeof(g_sl1640.v1v8) - 1] = '\0';
            }
        }
    }

    closedir(dir);

    if (!g_sl1640.ina3221[0] ||
        !g_sl1640.vddm_0v6[0] ||
        !g_sl1640.v3v3[0]     ||
        !g_sl1640.v1v8[0]) {
        fprintf(stderr,
                "sl1640: failed to find all INA devices:\n"
                "  ina3221:  %s\n"
                "  vddm_0v6: %s\n"
                "  3v3:      %s\n"
                "  1v8:      %s\n",
                g_sl1640.ina3221,
                g_sl1640.vddm_0v6,
                g_sl1640.v3v3,
                g_sl1640.v1v8);
        return -1;
    }

    if (!g_sl1640.temp[0]) {
        if (find_temp_hwmon(g_sl1640.temp, sizeof(g_sl1640.temp)) != 0) {
            fprintf(stderr, "sl1640: warning: temperature hwmon not found\n");
        }
    }

    g_sl1640_inited = 1;
    return 0;
}

static int read_sl1640_temp(int *temp_c)
{
    if (!g_sl1640.temp[0])
        return -1;

    return read_temp_hwmon(g_sl1640.temp, temp_c);
}

/*
 * One snapshot of sl1640 rails:
 *  - VCORE / VCPU / VDDM_1V1: ina3221@40
 *  - VDDM_0V6: INA2xx@41 (i2c@1800)
 *  - 3V3:      INA2xx@40 (i2c@c000)
 *  - 1V8:      INA2xx@41 (i2c@c000)
 */
static void monitor_sl1640_once(void)
{
    int shunt_vcore = 0, bus_vcore = 0, current_vcore = 0, power_vcore = 0;
    int shunt_vcpu  = 0, bus_vcpu  = 0, current_vcpu  = 0, power_vcpu  = 0;
    int shunt_vddm_1v1 = 0, bus_vddm_1v1 = 0, current_vddm_1v1 = 0, power_vddm_1v1 = 0;

    int shunt_vddm_0v6 = 0, bus_vddm_0v6 = 0, current_vddm_0v6 = 0, power_vddm_0v6 = 0;

    int shunt_3v3 = 0, bus_3v3 = 0, current_3v3 = 0, power_3v3 = 0;
    int shunt_1v8 = 0, bus_1v8 = 0, current_1v8 = 0, power_1v8 = 0;

    int temp = 0;
    int have_temp = 0;
    int power_total = 0;
    int i;
    char path[PATH_MAX];
    int val;

    if (init_sl1640_hwmon_paths() != 0)
        die("sl1640: hwmon init failed\n");

    for (i = 0; i < SAMPLES; i++) {
        /* INA3221@40: VCORE / VCPU / VDDM_1V1 */

        /* VCORE */
        snprintf(path, sizeof(path), "%s/in4_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) shunt_vcore += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) bus_vcore += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) current_vcore += val;

        /* VCPU */
        snprintf(path, sizeof(path), "%s/in5_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) shunt_vcpu += val;
        snprintf(path, sizeof(path), "%s/in2_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) bus_vcpu += val;
        snprintf(path, sizeof(path), "%s/curr2_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) current_vcpu += val;

        /* VDDM_1V1 */
        snprintf(path, sizeof(path), "%s/in6_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) shunt_vddm_1v1 += val;
        snprintf(path, sizeof(path), "%s/in3_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) bus_vddm_1v1 += val;
        snprintf(path, sizeof(path), "%s/curr3_input", g_sl1640.ina3221);
        if (read_int_file(path, &val) == 0) current_vddm_1v1 += val;

        /* INA2xx@41: VDDM_0V6 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1640.vddm_0v6);
        if (read_int_file(path, &val) == 0) shunt_vddm_0v6 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1640.vddm_0v6);
        if (read_int_file(path, &val) == 0) bus_vddm_0v6 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1640.vddm_0v6);
        if (read_int_file(path, &val) == 0) current_vddm_0v6 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1640.vddm_0v6);
        if (read_int_file(path, &val) == 0) power_vddm_0v6 += val;

        /* INA2xx@40: 3V3 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1640.v3v3);
        if (read_int_file(path, &val) == 0) shunt_3v3 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1640.v3v3);
        if (read_int_file(path, &val) == 0) bus_3v3 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1640.v3v3);
        if (read_int_file(path, &val) == 0) current_3v3 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1640.v3v3);
        if (read_int_file(path, &val) == 0) power_3v3 += val;

        /* INA2xx@41: 1V8 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1640.v1v8);
        if (read_int_file(path, &val) == 0) shunt_1v8 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1640.v1v8);
        if (read_int_file(path, &val) == 0) bus_1v8 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1640.v1v8);
        if (read_int_file(path, &val) == 0) current_1v8 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1640.v1v8);
        if (read_int_file(path, &val) == 0) power_1v8 += val;
    }

    /* Average all accumulated values */
    shunt_vcore      /= SAMPLES;
    bus_vcore        /= SAMPLES;
    current_vcore    /= SAMPLES;

    shunt_vcpu       /= SAMPLES;
    bus_vcpu         /= SAMPLES;
    current_vcpu     /= SAMPLES;

    shunt_vddm_1v1   /= SAMPLES;
    bus_vddm_1v1     /= SAMPLES;
    current_vddm_1v1 /= SAMPLES;

    shunt_vddm_0v6   /= SAMPLES;
    bus_vddm_0v6     /= SAMPLES;
    current_vddm_0v6 /= SAMPLES;

    shunt_3v3        /= SAMPLES;
    bus_3v3          /= SAMPLES;
    current_3v3      /= SAMPLES;

    shunt_1v8        /= SAMPLES;
    bus_1v8          /= SAMPLES;
    current_1v8      /= SAMPLES;

    /* Power from power*_input: microwatts -> milliwatts */
    power_vddm_0v6 = power_vddm_0v6 / SAMPLES / 1000;
    power_3v3      = power_3v3      / SAMPLES / 1000;
    power_1v8      = power_1v8      / SAMPLES / 1000;

    /* INA3221: power = bus(mV) * curr(mA) / 1000 -> mW */
    {
        long long p;

        p = (long long)bus_vcore * current_vcore;
        power_vcore = (int)(p / 1000);

        p = (long long)bus_vcpu * current_vcpu;
        power_vcpu = (int)(p / 1000);

        p = (long long)bus_vddm_1v1 * current_vddm_1v1;
        power_vddm_1v1 = (int)(p / 1000);
    }

    power_total = power_vcore + power_vcpu + power_vddm_1v1 +
                  power_vddm_0v6 + power_3v3 + power_1v8;

    if (read_sl1640_temp(&temp) == 0)
        have_temp = 1;

    system("clear");
    printf("==================================== sl1640 power monitor =======================================\n");
    printf("CH             shunt(uV)      bus(mV)       current(mA)        power(mW)\n");

    printf("vcore          %-12d %-13d %-17d %d\n",
           shunt_vcore, bus_vcore, current_vcore, power_vcore);
    printf("vcpu           %-12d %-13d %-17d %d\n",
           shunt_vcpu, bus_vcpu, current_vcpu, power_vcpu);
    printf("vddm_1v1       %-12d %-13d %-17d %d\n",
           shunt_vddm_1v1, bus_vddm_1v1, current_vddm_1v1, power_vddm_1v1);

    printf("vddm_0v6       %-12d %-13d %-17d %d\n",
           shunt_vddm_0v6, bus_vddm_0v6, current_vddm_0v6, power_vddm_0v6);
    printf("3v3            %-12d %-13d %-17d %d\n",
           shunt_3v3, bus_3v3, current_3v3, power_3v3);
    printf("1v8            %-12d %-13d %-17d %d\n",
           shunt_1v8, bus_1v8, current_1v8, power_1v8);

    printf("--------------------------------------------------------------------------------------------------\n");
    printf("Total power: %d mW\n", power_total);
    if (have_temp)
        printf("Temperature: %d C\n", temp);
    printf("==================================================================================================\n");
}

/* ------------------------------------------------------------------------ */
/* sl1620: hwmon discovery                                                  */
/* ------------------------------------------------------------------------ */

static int init_sl1620_hwmon_paths(void)
{
    if (g_sl1620_inited)
        return 0;

    memset(&g_sl1620, 0, sizeof(g_sl1620));

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) {
        perror("opendir /sys/class/hwmon");
        return -1;
    }

    struct dirent *de;
    char base[PATH_MAX];
    char path[PATH_MAX];
    char chip_name[64];
    char linkbuf[PATH_MAX];

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0)
            continue;

        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        /* Read chip name */
        snprintf(path, sizeof(path), "%s/name", base);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        if (!fgets(chip_name, sizeof(chip_name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        chip_name[strcspn(chip_name, "\n")] = '\0';

        if (strcmp(chip_name, "ina3221") && !is_ina2xx_name(chip_name))
            continue;

        if (read_hwmon_of_node(base, linkbuf, sizeof(linkbuf)) != 0)
            continue;

        /*
         * On sl1620 you showed:
         *   .../apb@f7e80000/i2c@1c00/ina3221@40
         *   .../apb@f7e80000/i2c@2000/ina220@40
         *   .../apb@f7e80000/i2c@2000/ina220@41
         * We match only by the final "inaXXX@addr" part.
         */
        if (!strcmp(chip_name, "ina3221")) {
            if (strstr(linkbuf, "ina3221@40")) {
                strncpy(g_sl1620.ina3221, base, sizeof(g_sl1620.ina3221));
                g_sl1620.ina3221[sizeof(g_sl1620.ina3221) - 1] = '\0';
            }
        } else {
            if (strstr(linkbuf, "ina220@40")) {
                strncpy(g_sl1620.v3v3, base, sizeof(g_sl1620.v3v3));
                g_sl1620.v3v3[sizeof(g_sl1620.v3v3) - 1] = '\0';
            } else if (strstr(linkbuf, "ina220@41")) {
                strncpy(g_sl1620.v1v8, base, sizeof(g_sl1620.v1v8));
                g_sl1620.v1v8[sizeof(g_sl1620.v1v8) - 1] = '\0';
            }
        }
    }

    closedir(dir);

    if (!g_sl1620.ina3221[0] ||
        !g_sl1620.v3v3[0]    ||
        !g_sl1620.v1v8[0]) {
        fprintf(stderr,
                "sl1620: failed to find all INA devices:\n"
                "  ina3221:  %s\n"
                "  3v3:      %s\n"
                "  1v8:      %s\n",
                g_sl1620.ina3221,
                g_sl1620.v3v3,
                g_sl1620.v1v8);
        return -1;
    }

    if (!g_sl1620.temp[0]) {
        if (find_temp_hwmon(g_sl1620.temp, sizeof(g_sl1620.temp)) != 0) {
            fprintf(stderr, "sl1620: warning: temperature hwmon not found\n");
        }
    }

    g_sl1620_inited = 1;
    return 0;
}

static int read_sl1620_temp(int *temp_c)
{
    if (!g_sl1620.temp[0])
        return -1;

    return read_temp_hwmon(g_sl1620.temp, temp_c);
}

/*
 * One snapshot of sl1620 rails:
 *  - 3V3:      INA2xx@40
 *  - 1V8:      INA2xx@41
 *  - VCORE:    ina3221@40 ch0
 *  - 1V2:      ina3221@40 ch1
 *  - 2V5:      ina3221@40 ch2
 */
static void monitor_sl1620_once(void)
{
    int shunt_3v3 = 0, bus_3v3 = 0, current_3v3 = 0, power_3v3 = 0;
    int shunt_1v8 = 0, bus_1v8 = 0, current_1v8 = 0, power_1v8 = 0;

    int shunt_vcore = 0, bus_vcore = 0, current_vcore = 0, power_vcore = 0;
    int shunt_1v2   = 0, bus_1v2   = 0, current_1v2   = 0, power_1v2   = 0;
    int shunt_2v5   = 0, bus_2v5   = 0, current_2v5   = 0, power_2v5   = 0;

    int temp = 0;
    int have_temp = 0;
    int power_total = 0;
    int i;
    char path[PATH_MAX];
    int val;

    if (init_sl1620_hwmon_paths() != 0)
        die("sl1620: hwmon init failed\n");

    for (i = 0; i < SAMPLES; i++) {
        /* 3V3: INA2xx@40 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1620.v3v3);
        if (read_int_file(path, &val) == 0) shunt_3v3 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1620.v3v3);
        if (read_int_file(path, &val) == 0) bus_3v3 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1620.v3v3);
        if (read_int_file(path, &val) == 0) current_3v3 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1620.v3v3);
        if (read_int_file(path, &val) == 0) power_3v3 += val;

        /* 1V8: INA2xx@41 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl1620.v1v8);
        if (read_int_file(path, &val) == 0) shunt_1v8 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1620.v1v8);
        if (read_int_file(path, &val) == 0) bus_1v8 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1620.v1v8);
        if (read_int_file(path, &val) == 0) current_1v8 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl1620.v1v8);
        if (read_int_file(path, &val) == 0) power_1v8 += val;

        /* INA3221@40: vcore / 1v2 / 2v5 */

        /* VCORE: channel 0 */
        snprintf(path, sizeof(path), "%s/in4_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) shunt_vcore += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) bus_vcore += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) current_vcore += val;

        /* 1V2: channel 1 */
        snprintf(path, sizeof(path), "%s/in5_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) shunt_1v2 += val;
        snprintf(path, sizeof(path), "%s/in2_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) bus_1v2 += val;
        snprintf(path, sizeof(path), "%s/curr2_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) current_1v2 += val;

        /* 2V5: channel 2 */
        snprintf(path, sizeof(path), "%s/in6_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) shunt_2v5 += val;
        snprintf(path, sizeof(path), "%s/in3_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) bus_2v5 += val;
        snprintf(path, sizeof(path), "%s/curr3_input", g_sl1620.ina3221);
        if (read_int_file(path, &val) == 0) current_2v5 += val;
    }

    /* Average all accumulated values */
    shunt_3v3      /= SAMPLES;
    bus_3v3        /= SAMPLES;
    current_3v3    /= SAMPLES;

    shunt_1v8      /= SAMPLES;
    bus_1v8        /= SAMPLES;
    current_1v8    /= SAMPLES;

    shunt_vcore    /= SAMPLES;
    bus_vcore      /= SAMPLES;
    current_vcore  /= SAMPLES;

    shunt_1v2      /= SAMPLES;
    bus_1v2        /= SAMPLES;
    current_1v2    /= SAMPLES;

    shunt_2v5      /= SAMPLES;
    bus_2v5        /= SAMPLES;
    current_2v5    /= SAMPLES;

    /* Power from power*_input: microwatts -> milliwatts */
    power_3v3 = power_3v3 / SAMPLES / 1000;
    power_1v8 = power_1v8 / SAMPLES / 1000;

    /* INA3221: power = bus(mV) * curr(mA) / 1000 -> mW */
    {
        long long p;

        p = (long long)bus_vcore * current_vcore;
        power_vcore = (int)(p / 1000);

        p = (long long)bus_1v2 * current_1v2;
        power_1v2 = (int)(p / 1000);

        p = (long long)bus_2v5 * current_2v5;
        power_2v5 = (int)(p / 1000);
    }

    power_total = power_3v3 + power_1v8 +
                  power_vcore + power_1v2 + power_2v5;

    if (read_sl1620_temp(&temp) == 0)
        have_temp = 1;

    system("clear");
    printf("==================================== sl1620 power monitor =======================================\n");
    printf("CH             shunt(uV)      bus(mV)       current(mA)        power(mW)\n");

    printf("3v3            %-12d %-13d %-17d %d\n",
           shunt_3v3, bus_3v3, current_3v3, power_3v3);
    printf("1v8            %-12d %-13d %-17d %d\n",
           shunt_1v8, bus_1v8, current_1v8, power_1v8);

    printf("vcore          %-12d %-13d %-17d %d\n",
           shunt_vcore, bus_vcore, current_vcore, power_vcore);
    printf("1v2            %-12d %-13d %-17d %d\n",
           shunt_1v2, bus_1v2, current_1v2, power_1v2);
    printf("2v5            %-12d %-13d %-17d %d\n",
           shunt_2v5, bus_2v5, current_2v5, power_2v5);

    printf("--------------------------------------------------------------------------------------------------\n");
    printf("Total power: %d mW\n", power_total);
    if (have_temp)
        printf("Temperature: %d C\n", temp);
    printf("==================================================================================================\n");
}

/* ------------------------------------------------------------------------ */
/* sl2610: hwmon discovery                                                  */
/* ------------------------------------------------------------------------ */

static int init_sl2610_hwmon_paths(void)
{
    if (g_sl2610_inited)
        return 0;

    memset(&g_sl2610, 0, sizeof(g_sl2610));

    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) {
        perror("opendir /sys/class/hwmon");
        return -1;
    }

    struct dirent *de;
    char base[PATH_MAX];
    char path[PATH_MAX];
    char chip_name[64];
    char linkbuf[PATH_MAX];

    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "hwmon", 5) != 0)
            continue;

        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        /* Read chip name */
        snprintf(path, sizeof(path), "%s/name", base);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;

        if (!fgets(chip_name, sizeof(chip_name), f)) {
            fclose(f);
            continue;
        }
        fclose(f);

        chip_name[strcspn(chip_name, "\n")] = '\0';

        /* Only INA devices need of_node matching */
        if (strcmp(chip_name, "ina3221") && !is_ina2xx_name(chip_name))
            continue;

        if (read_hwmon_of_node(base, linkbuf, sizeof(linkbuf)) != 0)
            continue;

        /*
         * On sl2610/sl2619:
         *   .../soc/sm/i2c@e5035000/ina220@40   (3v3)
         *   .../soc/sm/i2c@e5035000/ina220@41   (1v8)
         *   .../soc/sm/i2c@e5035000/ina3221@42  (vcore/1v2/2v5)
         *   .../soc/sm/i2c@e5035000/ina220@45   (vdd_sm)
         */
        if (!strcmp(chip_name, "ina3221")) {
            if (strstr(linkbuf, "ina3221@42")) {
                strncpy(g_sl2610.ina3221, base, sizeof(g_sl2610.ina3221));
                g_sl2610.ina3221[sizeof(g_sl2610.ina3221) - 1] = '\0';
            }
        } else {
            if (strstr(linkbuf, "ina220@40")) {
                strncpy(g_sl2610.v3v3, base, sizeof(g_sl2610.v3v3));
                g_sl2610.v3v3[sizeof(g_sl2610.v3v3) - 1] = '\0';
            } else if (strstr(linkbuf, "ina220@41")) {
                strncpy(g_sl2610.v1v8, base, sizeof(g_sl2610.v1v8));
                g_sl2610.v1v8[sizeof(g_sl2610.v1v8) - 1] = '\0';
            } else if (strstr(linkbuf, "ina220@45")) {
                strncpy(g_sl2610.vdd_sm, base, sizeof(g_sl2610.vdd_sm));
                g_sl2610.vdd_sm[sizeof(g_sl2610.vdd_sm) - 1] = '\0';
            }
        }
    }

    closedir(dir);

    if (!g_sl2610.v3v3[0]    ||
        !g_sl2610.v1v8[0]    ||
        !g_sl2610.ina3221[0] ||
        !g_sl2610.vdd_sm[0]) {
        fprintf(stderr,
                "sl2610: failed to find all INA devices:\n"
                "  3v3:      %s\n"
                "  1v8:      %s\n"
                "  ina3221:  %s\n"
                "  vdd_sm:   %s\n",
                g_sl2610.v3v3,
                g_sl2610.v1v8,
                g_sl2610.ina3221,
                g_sl2610.vdd_sm);
        return -1;
    }

    if (!g_sl2610.temp[0]) {
        if (find_temp_hwmon(g_sl2610.temp, sizeof(g_sl2610.temp)) != 0) {
            fprintf(stderr, "sl2610: warning: temperature hwmon not found\n");
        }
    }

    g_sl2610_inited = 1;
    return 0;
}

static int read_sl2610_temp(int *temp_c)
{
    if (!g_sl2610.temp[0])
        return -1;

    return read_temp_hwmon(g_sl2610.temp, temp_c);
}

/*
 * One snapshot of sl2610 rails:
 *  - 3V3:      INA2xx@40
 *  - 1V8:      INA2xx@41
 *  - VCORE:    ina3221@42 ch0
 *  - 1V2:      ina3221@42 ch1
 *  - 2V5:      ina3221@42 ch2
 *  - VDD_SM:   INA2xx@45
 */
static void monitor_sl2610_once(void)
{
    int shunt_3v3 = 0, bus_3v3 = 0, current_3v3 = 0, power_3v3 = 0;
    int shunt_1v8 = 0, bus_1v8 = 0, current_1v8 = 0, power_1v8 = 0;

    int shunt_vcore = 0, bus_vcore = 0, current_vcore = 0, power_vcore = 0;
    int shunt_1v2   = 0, bus_1v2   = 0, current_1v2   = 0, power_1v2   = 0;
    int shunt_2v5   = 0, bus_2v5   = 0, current_2v5   = 0, power_2v5   = 0;

    int shunt_vdd_sm = 0, bus_vdd_sm = 0, current_vdd_sm = 0, power_vdd_sm = 0;

    int temp = 0;
    int have_temp = 0;
    int power_total = 0;
    int i;
    char path[PATH_MAX];
    int val;

    if (init_sl2610_hwmon_paths() != 0)
        die("sl2610: hwmon init failed\n");

    for (i = 0; i < SAMPLES; i++) {
        /* 3V3: INA2xx@40 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl2610.v3v3);
        if (read_int_file(path, &val) == 0) shunt_3v3 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl2610.v3v3);
        if (read_int_file(path, &val) == 0) bus_3v3 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl2610.v3v3);
        if (read_int_file(path, &val) == 0) current_3v3 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl2610.v3v3);
        if (read_int_file(path, &val) == 0) power_3v3 += val;

        /* 1V8: INA2xx@41 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl2610.v1v8);
        if (read_int_file(path, &val) == 0) shunt_1v8 += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl2610.v1v8);
        if (read_int_file(path, &val) == 0) bus_1v8 += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl2610.v1v8);
        if (read_int_file(path, &val) == 0) current_1v8 += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl2610.v1v8);
        if (read_int_file(path, &val) == 0) power_1v8 += val;

        /* INA3221@42: vcore / 1v2 / 2v5 */

        /* VCORE: channel 0 */
        snprintf(path, sizeof(path), "%s/in4_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) shunt_vcore += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) bus_vcore += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) current_vcore += val;

        /* 1V2: channel 1 */
        snprintf(path, sizeof(path), "%s/in5_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) shunt_1v2 += val;
        snprintf(path, sizeof(path), "%s/in2_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) bus_1v2 += val;
        snprintf(path, sizeof(path), "%s/curr2_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) current_1v2 += val;

        /* 2V5: channel 2 */
        snprintf(path, sizeof(path), "%s/in6_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) shunt_2v5 += val;
        snprintf(path, sizeof(path), "%s/in3_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) bus_2v5 += val;
        snprintf(path, sizeof(path), "%s/curr3_input", g_sl2610.ina3221);
        if (read_int_file(path, &val) == 0) current_2v5 += val;

        /* VDD_SM: INA2xx@45 */
        snprintf(path, sizeof(path), "%s/in0_input", g_sl2610.vdd_sm);
        if (read_int_file(path, &val) == 0) shunt_vdd_sm += val;
        snprintf(path, sizeof(path), "%s/in1_input", g_sl2610.vdd_sm);
        if (read_int_file(path, &val) == 0) bus_vdd_sm += val;
        snprintf(path, sizeof(path), "%s/curr1_input", g_sl2610.vdd_sm);
        if (read_int_file(path, &val) == 0) current_vdd_sm += val;
        snprintf(path, sizeof(path), "%s/power1_input", g_sl2610.vdd_sm);
        if (read_int_file(path, &val) == 0) power_vdd_sm += val;
    }

    /* Average all accumulated values */
    shunt_3v3      /= SAMPLES;
    bus_3v3        /= SAMPLES;
    current_3v3    /= SAMPLES;

    shunt_1v8      /= SAMPLES;
    bus_1v8        /= SAMPLES;
    current_1v8    /= SAMPLES;

    shunt_vcore    /= SAMPLES;
    bus_vcore      /= SAMPLES;
    current_vcore  /= SAMPLES;

    shunt_1v2      /= SAMPLES;
    bus_1v2        /= SAMPLES;
    current_1v2    /= SAMPLES;

    shunt_2v5      /= SAMPLES;
    bus_2v5        /= SAMPLES;
    current_2v5    /= SAMPLES;

    shunt_vdd_sm   /= SAMPLES;
    bus_vdd_sm     /= SAMPLES;
    current_vdd_sm /= SAMPLES;

    /* Power from power*_input: microwatts -> milliwatts */
    power_3v3    = power_3v3    / SAMPLES / 1000;
    power_1v8    = power_1v8    / SAMPLES / 1000;
    power_vdd_sm = power_vdd_sm / SAMPLES / 1000;

    /* INA3221: power = bus(mV) * curr(mA) / 1000 -> mW */
    {
        long long p;

        p = (long long)bus_vcore * current_vcore;
        power_vcore = (int)(p / 1000);

        p = (long long)bus_1v2 * current_1v2;
        power_1v2 = (int)(p / 1000);

        p = (long long)bus_2v5 * current_2v5;
        power_2v5 = (int)(p / 1000);
    }

    power_total = power_3v3 + power_1v8 +
                  power_vcore + power_1v2 + power_2v5 +
                  power_vdd_sm;

    if (read_sl2610_temp(&temp) == 0)
        have_temp = 1;

    system("clear");
    printf("==================================== sl2610 power monitor =======================================\n");
    printf("CH             shunt(uV)      bus(mV)       current(mA)        power(mW)\n");

    printf("3v3            %-12d %-13d %-17d %d\n",
           shunt_3v3, bus_3v3, current_3v3, power_3v3);
    printf("1v8            %-12d %-13d %-17d %d\n",
           shunt_1v8, bus_1v8, current_1v8, power_1v8);

    printf("vcore          %-12d %-13d %-17d %d\n",
           shunt_vcore, bus_vcore, current_vcore, power_vcore);
    printf("1v2            %-12d %-13d %-17d %d\n",
           shunt_1v2, bus_1v2, current_1v2, power_1v2);
    printf("2v5            %-12d %-13d %-17d %d\n",
           shunt_2v5, bus_2v5, current_2v5, power_2v5);

    printf("vdd_sm         %-12d %-13d %-17d %d\n",
           shunt_vdd_sm, bus_vdd_sm, current_vdd_sm, power_vdd_sm);

    printf("--------------------------------------------------------------------------------------------------\n");
    printf("Total power: %d mW\n", power_total);
    if (have_temp)
        printf("Temperature: %d C\n", temp);
    printf("==================================================================================================\n");
}

/* ------------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s 1        # sl1680 single-shot\n"
            "  %s m1       # sl1680 monitor mode\n"
            "  %s 2        # sl1640 single-shot\n"
            "  %s m2       # sl1640 monitor mode\n"
            "  %s 3        # sl1620 single-shot\n"
            "  %s m3       # sl1620 monitor mode\n"
            "  %s 4        # sl2610 single-shot\n"
            "  %s m4       # sl2610 monitor mode\n",
            prog, prog, prog, prog, prog, prog, prog, prog);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        usage(argv[0]);

    if (!strcmp(argv[1], "1")) {
        monitor_sl1680_once();
    } else if (!strcmp(argv[1], "m1")) {
        for (;;) {
            monitor_sl1680_once();
            usleep(REFRESH_MS * 1000);
        }
    } else if (!strcmp(argv[1], "2")) {
        monitor_sl1640_once();
    } else if (!strcmp(argv[1], "m2")) {
        for (;;) {
            monitor_sl1640_once();
            usleep(REFRESH_MS * 1000);
        }
    } else if (!strcmp(argv[1], "3")) {
        monitor_sl1620_once();
    } else if (!strcmp(argv[1], "m3")) {
        for (;;) {
            monitor_sl1620_once();
            usleep(REFRESH_MS * 1000);
        }
    } else if (!strcmp(argv[1], "4")) {
        monitor_sl2610_once();
    } else if (!strcmp(argv[1], "m4")) {
        for (;;) {
            monitor_sl2610_once();
            usleep(REFRESH_MS * 1000);
        }
    } else {
        usage(argv[0]);
    }

    return 0;
}


