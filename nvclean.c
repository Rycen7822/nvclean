#define _GNU_SOURCE

#include <nvml.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef NVML_VALUE_NOT_AVAILABLE
#define NVML_VALUE_NOT_AVAILABLE (-1)
#endif

#define MIB (1024ULL * 1024ULL)

typedef struct {
    unsigned int gpu_index;
    unsigned int pid;
    char type[8];
    unsigned long long mem;
    char name[PATH_MAX];
} ProcRow;

typedef struct {
    ProcRow *rows;
    size_t len;
    size_t cap;
} ProcList;

typedef nvmlReturn_t (*ProcQueryFn)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *);

static const char *enable_str(nvmlEnableState_t s) {
    switch (s) {
        case NVML_FEATURE_ENABLED:
            return "On";
        case NVML_FEATURE_DISABLED:
            return "Off";
        default:
            return "N/A";
    }
}

static const char *compute_mode_str(nvmlComputeMode_t m) {
    switch (m) {
        case NVML_COMPUTEMODE_DEFAULT:
            return "Default";
        case NVML_COMPUTEMODE_EXCLUSIVE_THREAD:
            return "Exclusive_Thread";
        case NVML_COMPUTEMODE_PROHIBITED:
            return "Prohibited";
        case NVML_COMPUTEMODE_EXCLUSIVE_PROCESS:
            return "Exclusive_Process";
        default:
            return "N/A";
    }
}

static void fmt_mib(char *buf, size_t n, unsigned long long bytes) {
    if (bytes == (unsigned long long)NVML_VALUE_NOT_AVAILABLE) {
        snprintf(buf, n, "N/A");
        return;
    }

    snprintf(buf, n, "%lluMiB", bytes / MIB);
}

static void fmt_power(char *buf, size_t n, unsigned int milliwatts, int ok) {
    if (!ok) {
        snprintf(buf, n, "N/A");
        return;
    }

    if (milliwatts % 1000 == 0) {
        snprintf(buf, n, "%uW", milliwatts / 1000);
    } else {
        snprintf(buf, n, "%.1fW", milliwatts / 1000.0);
    }
}

static void get_proc_name(unsigned int pid, char *buf, size_t n) {
    char path[128];

    snprintf(path, sizeof(path), "/proc/%u/exe", pid);
    ssize_t len = readlink(path, buf, n - 1);
    if (len > 0) {
        buf[len] = '\0';
        return;
    }

    if (nvmlSystemGetProcessName(pid, buf, (unsigned int)n) == NVML_SUCCESS) {
        return;
    }

    snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fgets(buf, (int)n, f)) {
            buf[strcspn(buf, "\n")] = '\0';
            fclose(f);
            return;
        }
        fclose(f);
    }

    snprintf(buf, n, "pid:%u", pid);
}

static void proclist_add(ProcList *pl,
                         unsigned int gpu_index,
                         unsigned int pid,
                         char type,
                         unsigned long long mem) {
    char pname[PATH_MAX];
    get_proc_name(pid, pname, sizeof(pname));

    for (size_t i = 0; i < pl->len; i++) {
        ProcRow *row = &pl->rows[i];
        if (row->gpu_index == gpu_index && row->pid == pid) {
            if (!strchr(row->type, type)) {
                size_t len = strlen(row->type);
                if (len + 2 < sizeof(row->type)) {
                    row->type[len] = '+';
                    row->type[len + 1] = type;
                    row->type[len + 2] = '\0';
                }
            }

            if (row->mem == (unsigned long long)NVML_VALUE_NOT_AVAILABLE &&
                mem != (unsigned long long)NVML_VALUE_NOT_AVAILABLE) {
                row->mem = mem;
            }
            return;
        }
    }

    if (pl->len == pl->cap) {
        size_t new_cap = pl->cap ? pl->cap * 2 : 16;
        ProcRow *new_rows = realloc(pl->rows, new_cap * sizeof(*new_rows));
        if (!new_rows) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
        pl->rows = new_rows;
        pl->cap = new_cap;
    }

    ProcRow *row = &pl->rows[pl->len++];
    row->gpu_index = gpu_index;
    row->pid = pid;
    row->type[0] = type;
    row->type[1] = '\0';
    row->mem = mem;
    snprintf(row->name, sizeof(row->name), "%s", pname);
}

static void collect_processes(ProcList *pl,
                              nvmlDevice_t dev,
                              unsigned int gpu_index,
                              char type,
                              ProcQueryFn fn) {
    unsigned int count = 0;
    nvmlReturn_t r = fn(dev, &count, NULL);

    if (r == NVML_SUCCESS && count == 0) {
        return;
    }

    if (r != NVML_ERROR_INSUFFICIENT_SIZE || count == 0) {
        return;
    }

    nvmlProcessInfo_t *infos = calloc(count, sizeof(*infos));
    if (!infos) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }

    r = fn(dev, &count, infos);
    if (r == NVML_SUCCESS) {
        for (unsigned int i = 0; i < count; i++) {
            proclist_add(pl, gpu_index, infos[i].pid, type, infos[i].usedGpuMemory);
        }
    }

    free(infos);
}

static void print_time_line(void) {
    time_t now = time(NULL);
    struct tm tmv;
    char buf[128];

    localtime_r(&now, &tmv);
    strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Y", &tmv);
    printf("Time: %s\n", buf);
}

static void print_system_line(void) {
    char nvml_ver[96] = "N/A";
    char driver_ver[96] = "N/A";
    int cuda_ver = 0;

    nvmlSystemGetNVMLVersion(nvml_ver, sizeof(nvml_ver));
    nvmlSystemGetDriverVersion(driver_ver, sizeof(driver_ver));

    if (nvmlSystemGetCudaDriverVersion_v2(&cuda_ver) == NVML_SUCCESS) {
        printf("NVML: %s | Driver: %s | CUDA driver: %d.%d\n",
               nvml_ver,
               driver_ver,
               NVML_CUDA_DRIVER_VERSION_MAJOR(cuda_ver),
               NVML_CUDA_DRIVER_VERSION_MINOR(cuda_ver));
    } else {
        printf("NVML: %s | Driver: %s | CUDA driver: N/A\n", nvml_ver, driver_ver);
    }
}

static void print_gpu(nvmlDevice_t dev, unsigned int index) {
    char name[128] = "N/A";
    nvmlPciInfo_t pci;
    memset(&pci, 0, sizeof(pci));

    nvmlDeviceGetName(dev, name, sizeof(name));

    const char *bus_id = "N/A";
    if (nvmlDeviceGetPciInfo_v3(dev, &pci) == NVML_SUCCESS) {
        bus_id = pci.busId;
    }

    nvmlEnableState_t persistence;
    nvmlEnableState_t display;
    nvmlEnableState_t ecc_cur;
    nvmlEnableState_t ecc_pending;
    const char *persistence_s = "N/A";
    const char *display_s = "N/A";
    const char *ecc_s = "N/A";

    if (nvmlDeviceGetPersistenceMode(dev, &persistence) == NVML_SUCCESS) {
        persistence_s = enable_str(persistence);
    }

    if (nvmlDeviceGetDisplayActive(dev, &display) == NVML_SUCCESS) {
        display_s = enable_str(display);
    }

    if (nvmlDeviceGetEccMode(dev, &ecc_cur, &ecc_pending) == NVML_SUCCESS) {
        ecc_s = enable_str(ecc_cur);
    }

    unsigned int fan = 0;
    int fan_ok = nvmlDeviceGetFanSpeed(dev, &fan) == NVML_SUCCESS;

    unsigned int temp = 0;
    int temp_ok = nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS;

    nvmlPstates_t pstate;
    int pstate_ok = nvmlDeviceGetPerformanceState(dev, &pstate) == NVML_SUCCESS;

    unsigned int power_use = 0;
    int power_use_ok = nvmlDeviceGetPowerUsage(dev, &power_use) == NVML_SUCCESS;

    unsigned int power_limit = 0;
    int power_limit_ok =
        nvmlDeviceGetEnforcedPowerLimit(dev, &power_limit) == NVML_SUCCESS ||
        nvmlDeviceGetPowerManagementLimit(dev, &power_limit) == NVML_SUCCESS;

    nvmlMemory_t mem;
    memset(&mem, 0, sizeof(mem));
    int mem_ok = nvmlDeviceGetMemoryInfo(dev, &mem) == NVML_SUCCESS;

    nvmlUtilization_t util;
    memset(&util, 0, sizeof(util));
    int util_ok = nvmlDeviceGetUtilizationRates(dev, &util) == NVML_SUCCESS;

    nvmlComputeMode_t cmode;
    const char *cmode_s = "N/A";
    if (nvmlDeviceGetComputeMode(dev, &cmode) == NVML_SUCCESS) {
        cmode_s = compute_mode_str(cmode);
    }

    char pwr_use_s[32];
    char pwr_lim_s[32];
    char mem_used_s[32];
    char mem_total_s[32];

    fmt_power(pwr_use_s, sizeof(pwr_use_s), power_use, power_use_ok);
    fmt_power(pwr_lim_s, sizeof(pwr_lim_s), power_limit, power_limit_ok);

    if (mem_ok) {
        fmt_mib(mem_used_s, sizeof(mem_used_s), mem.used);
        fmt_mib(mem_total_s, sizeof(mem_total_s), mem.total);
    } else {
        snprintf(mem_used_s, sizeof(mem_used_s), "N/A");
        snprintf(mem_total_s, sizeof(mem_total_s), "N/A");
    }

    printf("GPU %u: %s\n", index, name);
    printf("  Bus ID: %s\n", bus_id);
    printf("  Persistence: %s | Display: %s | ECC: %s\n", persistence_s, display_s, ecc_s);

    printf("  Fan: ");
    if (fan_ok) {
        printf("%u%%", fan);
    } else {
        printf("N/A");
    }

    printf(" | Temp: ");
    if (temp_ok) {
        printf("%uC", temp);
    } else {
        printf("N/A");
    }

    printf(" | Perf: ");
    if (pstate_ok && pstate <= NVML_PSTATE_15) {
        printf("P%d\n", (int)pstate);
    } else {
        printf("N/A\n");
    }

    printf("  Power: %s/%s | Memory: %s/%s | Utilization: ",
           pwr_use_s,
           pwr_lim_s,
           mem_used_s,
           mem_total_s);

    if (util_ok) {
        printf("%u%%\n", util.gpu);
    } else {
        printf("N/A\n");
    }

    printf("  Compute mode: %s\n", cmode_s);
}

int main(void) {
    nvmlReturn_t r = nvmlInit_v2();
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "nvmlInit_v2 failed: %s\n", nvmlErrorString(r));
        return 1;
    }

    print_time_line();
    print_system_line();
    printf("\n");

    unsigned int count = 0;
    r = nvmlDeviceGetCount_v2(&count);
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "nvmlDeviceGetCount_v2 failed: %s\n", nvmlErrorString(r));
        nvmlShutdown();
        return 1;
    }

    ProcList procs = {0};

    for (unsigned int i = 0; i < count; i++) {
        nvmlDevice_t dev;
        r = nvmlDeviceGetHandleByIndex_v2(i, &dev);
        if (r != NVML_SUCCESS) {
            printf("GPU %u: unavailable: %s\n", i, nvmlErrorString(r));
            continue;
        }

        if (i > 0) {
            printf("\n");
        }
        print_gpu(dev, i);

        collect_processes(&procs, dev, i, 'C', nvmlDeviceGetComputeRunningProcesses_v3);
        collect_processes(&procs, dev, i, 'G', nvmlDeviceGetGraphicsRunningProcesses_v3);
    }

    printf("\n");
    if (procs.len == 0) {
        printf("Processes: none\n");
    } else {
        printf("Processes:\n");
        for (size_t i = 0; i < procs.len; i++) {
            char mem_s[32];
            fmt_mib(mem_s, sizeof(mem_s), procs.rows[i].mem);
            printf("  GPU %u | PID %u | Type %s | %s | Memory %s\n",
                   procs.rows[i].gpu_index,
                   procs.rows[i].pid,
                   procs.rows[i].type,
                   procs.rows[i].name,
                   mem_s);
        }
    }

    free(procs.rows);
    nvmlShutdown();
    return 0;
}
