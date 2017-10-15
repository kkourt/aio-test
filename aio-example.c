#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h> // syscall numbers
#include <linux/aio_abi.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>
#include <assert.h>

// uses the kernel-based aio  syscalls directly
// (NOT libc's aio(7) that use threadS)

// syscall wrappers
static inline int
io_setup(unsigned maxevents, aio_context_t *ctx) {
    return syscall(SYS_io_setup, maxevents, ctx);
}

static inline int
io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp) {
    return syscall(SYS_io_submit, ctx, nr, iocbpp);
}

static inline int
io_getevents(aio_context_t ctx, long min_nr, long nr,
                 struct io_event *events, struct timespec *timeout) {
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, timeout);
}

static inline int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}


int main(int argc, char *argv[])
{
    aio_context_t ioctx = 0;
    unsigned maxevents = 128;

    int fd = open("/dev/zero", O_RDONLY /* | O_DIRECT */);
    if (fd == -1) {
        perror(argv[1]);
        exit(1);
    }

    if (io_setup(maxevents, &ioctx) < 0) {
        perror("io_setup");
        exit(1);
    }


    // setup an io operation
    char buff1[4096];    // IO has to be 4k-aligned
    struct iocb iocb1;
    iocb1.aio_data       = 0xbeef; /* will be returned in events data */
    iocb1.aio_fildes     = fd;
    iocb1.aio_lio_opcode = IOCB_CMD_PREAD;
    iocb1.aio_reqprio    = 0;
    iocb1.aio_buf        = (uintptr_t)buff1;
    iocb1.aio_nbytes     = sizeof(buff1);
    iocb1.aio_offset     = 0;

    // setup a second io operation
    char buff2[4096];    // IO has to be 4K-aligned
    struct iocb iocb2;
    iocb2.aio_data       = 0xbaba; /* will be returned in events data */
    iocb2.aio_fildes     = fd;
    iocb2.aio_lio_opcode = IOCB_CMD_PREAD;
    iocb2.aio_reqprio    = 0;
    iocb2.aio_buf        = (uintptr_t)buff2;
    iocb2.aio_nbytes     = sizeof(buff2);
    iocb2.aio_offset     = 0;

    struct iocb *iocb_ptrs[2] = { &iocb1, &iocb2 };

    /* submit operations */
    int ret = io_submit(ioctx, 2, iocb_ptrs);
    if (ret < 0) {
        perror("io_submit");
        abort();
    } else if (ret != 2) {
        perror("io_submit: partial success");
        abort();
    }

    size_t nevents = 2;
    struct io_event events[nevents];
    while (nevents > 0) {
        // wait for at least one event
        ret = io_getevents(ioctx, 1 /* min */, nevents, events, NULL);
        if (ret < 0) {
            perror("io_getevents");
            exit(1);
        }

        for (size_t i=0; i<ret; i++) {
            struct io_event *ev = &events[i];
            assert(ev->data == 0xbeef || ev->data == 0xbaba);
            printf("Event returned with res=%lld res2=%lld\n", ev->res, ev->res2);
            nevents--;
        }
    }

    io_destroy(ioctx);
    close(fd);
}
