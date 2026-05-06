#define _GNU_SOURCE

#include <nvml.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MIB (1024ULL * 1024ULL)

typedef struct {
    unsigned int gpu;
    unsigned int pid;
    char type[8];
    char name[PATH_MAX];
} Proc;

typedef struct {
    Proc *rows;
    size_t len;
    size_t cap;
} Procs;

typedef nvmlReturn_t (*ProcQueryFn)(nvmlDevice_t, unsigned int *, nvmlProcessInfo_t *);

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash && slash[1] != '\0') {
        return slash + 1;
    }
    return path;
}

static void proc_name(unsigned int pid, char *buf, size_t n) {
    char path[128];

    snprintf(path, sizeof(path), "/proc/%u/exe", pid);
    ssize_t len = readlink(path, buf, n - 1);
    if (len > 0) {
        buf[len] = '\0';
        snprintf(buf, n, "%s", base_name(buf));
        return;
    }

    if (nvmlSystemGetProcessName(pid, buf, (unsigned int)n) == NVML_SUCCESS) {
        snprintf(buf, n, "%s", base_name(buf));
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

    snprintf(buf, n, "?");
}

static void procs_add(Procs *procs, unsigned int gpu, unsigned int pid, char type) {
    for (size_t i = 0; i < procs->len; i++) {
        Proc *p = &procs->rows[i];
        if (p->gpu == gpu && p->pid == pid) {
            if (!strchr(p->type, type)) {
                size_t len = strlen(p->type);
                if (len + 2 < sizeof(p->type)) {
                    p->type[len] = '+';
                    p->type[len + 1] = type;
                    p->type[len + 2] = '\0';
                }
            }
            return;
        }
    }

    if (procs->len == procs->cap) {
        size_t cap = procs->cap ? procs->cap * 2 : 16;
        Proc *rows = realloc(procs->rows, cap * sizeof(*rows));
        if (!rows) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
        procs->rows = rows;
        procs->cap = cap;
    }

    Proc *p = &procs->rows[procs->len++];
    p->gpu = gpu;
    p->pid = pid;
    p->type[0] = type;
    p->type[1] = '\0';
    proc_name(pid, p->name, sizeof(p->name));
}

static void collect_procs(Procs *procs, nvmlDevice_t dev, unsigned int gpu, char type, ProcQueryFn fn) {
    unsigned int count = 0;
    nvmlReturn_t r = fn(dev, &count, NULL);
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
            procs_add(procs, gpu, infos[i].pid, type);
        }
    }

    free(infos);
}

static void print_mib(unsigned long long bytes, int ok) {
    if (!ok) {
        printf("NA");
    } else {
        printf("%llu", bytes / MIB);
    }
}

static void print_percent(unsigned int value, int ok) {
    if (!ok) {
        printf("NA");
    } else {
        printf("%u%%", value);
    }
}

static void print_temp(unsigned int value, int ok) {
    if (!ok) {
        printf("NA");
    } else {
        printf("%uC", value);
    }
}

static void print_power(unsigned int mw, int ok) {
    if (!ok) {
        printf("NA");
    } else if (mw % 1000 == 0) {
        printf("%uW", mw / 1000);
    } else {
        printf("%.1fW", mw / 1000.0);
    }
}

static void print_gpu(nvmlDevice_t dev, unsigned int gpu) {
    nvmlMemory_t mem;
    memset(&mem, 0, sizeof(mem));
    int mem_ok = nvmlDeviceGetMemoryInfo(dev, &mem) == NVML_SUCCESS;

    nvmlUtilization_t util;
    memset(&util, 0, sizeof(util));
    int util_ok = nvmlDeviceGetUtilizationRates(dev, &util) == NVML_SUCCESS;

    unsigned int temp = 0;
    int temp_ok = nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS;

    unsigned int power = 0;
    int power_ok = nvmlDeviceGetPowerUsage(dev, &power) == NVML_SUCCESS;

    printf("g%u ", gpu);
    print_mib(mem.used, mem_ok);
    printf("/");
    print_mib(mem.total, mem_ok);
    printf("MiB ");
    print_percent(util.gpu, util_ok);
    printf(" ");
    print_temp(temp, temp_ok);
    printf(" ");
    print_power(power, power_ok);
    printf("\n");
}

int main(void) {
    nvmlReturn_t r = nvmlInit_v2();
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "nvmlInit_v2 failed: %s\n", nvmlErrorString(r));
        return 1;
    }

    unsigned int count = 0;
    r = nvmlDeviceGetCount_v2(&count);
    if (r != NVML_SUCCESS) {
        fprintf(stderr, "nvmlDeviceGetCount_v2 failed: %s\n", nvmlErrorString(r));
        nvmlShutdown();
        return 1;
    }

    Procs procs = {0};
    for (unsigned int i = 0; i < count; i++) {
        nvmlDevice_t dev;
        r = nvmlDeviceGetHandleByIndex_v2(i, &dev);
        if (r != NVML_SUCCESS) {
            printf("g%u unavailable\n", i);
            continue;
        }

        print_gpu(dev, i);
        collect_procs(&procs, dev, i, 'C', nvmlDeviceGetComputeRunningProcesses_v3);
        collect_procs(&procs, dev, i, 'G', nvmlDeviceGetGraphicsRunningProcesses_v3);
    }

    if (procs.len == 0) {
        printf("p none\n");
    } else {
        for (size_t i = 0; i < procs.len; i++) {
            printf("p%u %s %u %s\n",
                   procs.rows[i].gpu,
                   procs.rows[i].type,
                   procs.rows[i].pid,
                   procs.rows[i].name);
        }
    }

    free(procs.rows);
    nvmlShutdown();
    return 0;
}
