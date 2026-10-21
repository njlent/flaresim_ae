// EffectCraft refuses plug-ins that import anything, but wasi-libc routes stdio and clocks
// through WASI imports. Defining the imported symbols here makes wasm-ld resolve them
// locally, so the module has no imports: output is discarded, the clock reads zero and
// exiting traps (EffectCraft then leaves the frame unchanged).

#include <cstddef>
#include <cstdint>

extern "C" {

typedef struct
{
    const uint8_t* buf;
    size_t buf_len;
} EcCiovec;

typedef struct
{
    uint8_t fs_filetype;
    uint16_t fs_flags;
    uint64_t fs_rights_base;
    uint64_t fs_rights_inheriting;
} EcFdstat;

enum
{
    kWasiSuccess = 0,
    kWasiBadf = 8,
    kWasiNosys = 52,
    kWasiFiletypeCharacterDevice = 2,
};

#define EC_WASI_STUB(name) __attribute__((used)) __imported_wasi_snapshot_preview1_##name

int32_t EC_WASI_STUB(fd_write)(int32_t fd, int32_t iovs, int32_t iovs_len, int32_t nwritten)
{
    (void)fd;
    const EcCiovec* vecs = (const EcCiovec*)(uintptr_t)iovs;
    size_t total = 0;
    for (int32_t i = 0; i < iovs_len; ++i) {
        total += vecs[i].buf_len;
    }
    *(uint32_t*)(uintptr_t)nwritten = (uint32_t)total;
    return kWasiSuccess;
}

int32_t EC_WASI_STUB(fd_close)(int32_t fd)
{
    (void)fd;
    return kWasiSuccess;
}

int32_t EC_WASI_STUB(fd_seek)(int32_t fd, int64_t offset, int32_t whence, int32_t newoffset)
{
    (void)fd;
    (void)offset;
    (void)whence;
    *(uint64_t*)(uintptr_t)newoffset = 0;
    return kWasiNosys;
}

int32_t EC_WASI_STUB(fd_fdstat_get)(int32_t fd, int32_t stat)
{
    if (fd > 2) {
        return kWasiBadf;
    }
    EcFdstat* out = (EcFdstat*)(uintptr_t)stat;
    out->fs_filetype = kWasiFiletypeCharacterDevice;
    out->fs_flags = 0;
    out->fs_rights_base = 0;
    out->fs_rights_inheriting = 0;
    return kWasiSuccess;
}

int32_t EC_WASI_STUB(clock_time_get)(int32_t clock_id, int64_t precision, int32_t time)
{
    (void)clock_id;
    (void)precision;
    *(uint64_t*)(uintptr_t)time = 0;
    return kWasiSuccess;
}

int32_t EC_WASI_STUB(environ_sizes_get)(int32_t count, int32_t buf_size)
{
    *(uint32_t*)(uintptr_t)count = 0;
    *(uint32_t*)(uintptr_t)buf_size = 0;
    return kWasiSuccess;
}

int32_t EC_WASI_STUB(environ_get)(int32_t environ, int32_t environ_buf)
{
    (void)environ;
    (void)environ_buf;
    return kWasiSuccess;
}

// No preopened directories: the first lookup fails, which ends wasi-libc's scan.
int32_t EC_WASI_STUB(fd_prestat_get)(int32_t fd, int32_t prestat)
{
    (void)fd;
    (void)prestat;
    return kWasiBadf;
}

int32_t EC_WASI_STUB(fd_prestat_dir_name)(int32_t fd, int32_t path, int32_t path_len)
{
    (void)fd;
    (void)path;
    (void)path_len;
    return kWasiBadf;
}

[[noreturn]] void EC_WASI_STUB(proc_exit)(int32_t code)
{
    (void)code;
    __builtin_trap();
}

} // extern "C"
