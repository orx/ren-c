static REBOOL Open_Nonblocking_Pipe_Fails(int pipefd[2]) {
#ifdef USE_PIPE2_NOT_PIPE
    //
    // NOTE: pipe() is POSIX, but pipe2() is Linux-specific.  With pipe() it
    // takes an additional call to fcntl() to request non-blocking behavior,
    // so it's a small amount more work.  However, there are other flags which
    // if aren't passed atomically at the moment of opening allow for a race
    // condition in threading if split, e.g. FD_CLOEXEC.
    //
    // (If you don't have FD_CLOEXEC set on the file descriptor, then all
    // instances of CALL will act as a /WAIT.)
    //
    // At time of writing, this is mostly academic...but the code needed to be
    // patched to work with pipe() since some older libcs do not have pipe2().
    // So the ability to target both are kept around, saving the pipe2() call
    // for later Linuxes known to have it (and O_CLOEXEC).
    //
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK))
        return TRUE;
#else
    if (pipe(pipefd) < 0)
        return TRUE;

    int direction; // READ=0, WRITE=1
    for (direction = 0; direction < 2; ++direction) {
        int oldflags;
        oldflags = fcntl(pipefd[direction], F_GETFL);
        if (oldflags < 0)
            return TRUE;
        if (fcntl(pipefd[direction], F_SETFL, oldflags | O_NONBLOCK) < 0)
            return TRUE;
        oldflags = fcntl(pipefd[direction], F_GETFD);
        if (oldflags < 0)
            return TRUE;
        if (fcntl(pipefd[direction], F_SETFD, oldflags | FD_CLOEXEC) < 0)
            return TRUE;
    }
#endif

    return FALSE;
}


//
//  OS_Create_Process: C
//
// flags:
//     1: wait, is implied when I/O redirection is enabled
//     2: console
//     4: shell
//     8: info
//     16: show
//
// input_type/output_type/err_type:
//     0: blank
//     1: string
//     2: file
//
// Return -1 on error, otherwise the process return code.
//
// POSIX previous simple version was just 'return system(call);'
// This uses 'execvp' which is "POSIX.1 conforming, UNIX compatible"
//
int OS_Create_Process(
    const char *call,
    int argc,
    const char* argv[],
    u32 flags,
    u64 *pid,
    int *exit_code,
    u32 input_type,
    char *input,
    u32 input_len,
    u32 output_type,
    char **output,
    u32 *output_len,
    u32 err_type,
    char **err,
    u32 *err_len
) {
    UNUSED(call);

    int status = 0;
    int ret = 0;
    char *info = NULL;
    off_t info_size = 0;
    u32 info_len = 0;
    pid_t fpid = 0;

    const unsigned int R = 0;
    const unsigned int W = 1;

    // We want to be able to compile with all warnings as errors, and
    // we'd like to use -Wcast-qual if possible.  This is currently
    // the only barrier in the codebase...so we tunnel under the cast.
    //
    char * const *argv_hack;

    REBOOL flag_wait = FALSE;
    REBOOL flag_console = FALSE;
    REBOOL flag_shell = FALSE;
    REBOOL flag_info = FALSE;

    if (flags & FLAG_WAIT) flag_wait = TRUE;
    if (flags & FLAG_CONSOLE) flag_console = TRUE;
    if (flags & FLAG_SHELL) flag_shell = TRUE;
    if (flags & FLAG_INFO) flag_info = TRUE;

    // suppress unused warnings but keep flags for future use
    UNUSED(flag_info);
    UNUSED(flag_console);

    int stdin_pipe[] = {-1, -1};
    int stdout_pipe[] = {-1, -1};
    int stderr_pipe[] = {-1, -1};
    int info_pipe[] = {-1, -1};

    if (
        input_type == STRING_TYPE
        || input_type == BINARY_TYPE
    ){
        if (Open_Nonblocking_Pipe_Fails(stdin_pipe))
            goto stdin_pipe_err;
    }

    if (
        output_type == STRING_TYPE
        || output_type == BINARY_TYPE
    ){
        if (Open_Nonblocking_Pipe_Fails(stdout_pipe))
            goto stdout_pipe_err;
    }

    if (
        err_type == STRING_TYPE
        || err_type == BINARY_TYPE
    ){
        if (Open_Nonblocking_Pipe_Fails(stderr_pipe))
            goto stdout_pipe_err;
    }

    if (Open_Nonblocking_Pipe_Fails(info_pipe))
        goto info_pipe_err;

    fpid = fork();
    if (fpid == 0) {
        //
        // This is the child branch of the fork.  In GDB if you want to debug
        // the child you need to use `set follow-fork-mode child`:
        //
        // http://stackoverflow.com/questions/15126925/

        if (
            input_type == STRING_TYPE
            || input_type == BINARY_TYPE
        ){
            close(stdin_pipe[W]);
            if (dup2(stdin_pipe[R], STDIN_FILENO) < 0)
                goto child_error;
            close(stdin_pipe[R]);
        }
        else if (input_type == FILE_TYPE) {
            int fd = open(input, O_RDONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (input_type == NONE_TYPE) {
            int fd = open("/dev/null", O_RDONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDIN_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            // inherit stdin from the parent
        }

        if (
            output_type == STRING_TYPE
            || output_type == BINARY_TYPE
        ){
            close(stdout_pipe[R]);
            if (dup2(stdout_pipe[W], STDOUT_FILENO) < 0)
                goto child_error;
            close(stdout_pipe[W]);
        }
        else if (output_type == FILE_TYPE) {
            int fd = open(*output, O_CREAT | O_WRONLY, 0666);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (output_type == NONE_TYPE) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDOUT_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            // inherit stdout from the parent
        }

        if (
            err_type == STRING_TYPE
            || err_type == BINARY_TYPE
        ){
            close(stderr_pipe[R]);
            if (dup2(stderr_pipe[W], STDERR_FILENO) < 0)
                goto child_error;
            close(stderr_pipe[W]);
        }
        else if (err_type == FILE_TYPE) {
            int fd = open(*err, O_CREAT | O_WRONLY, 0666);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else if (err_type == NONE_TYPE) {
            int fd = open("/dev/null", O_WRONLY);
            if (fd < 0)
                goto child_error;
            if (dup2(fd, STDERR_FILENO) < 0)
                goto child_error;
            close(fd);
        }
        else {
            // inherit stderr from the parent
        }

        close(info_pipe[R]);

        /* printf("flag_shell in child: %hhu\n", flag_shell); */

        if (flag_shell) {
            const char *sh = getenv("SHELL");

            if (sh == NULL) { // shell does not exist
                int err = 2;
                if (write(info_pipe[W], &err, sizeof(err)) == -1) {
                    //
                    // Nothing we can do, but need to stop compiler warning
                    // (cast to void is insufficient for warn_unused_result)
                }
                exit(EXIT_FAILURE);
            }

            const char ** argv_new = c_cast(
                const char**, OS_ALLOC_N(const char*, argc + 3)
            );
            argv_new[0] = sh;
            argv_new[1] = "-c";
            memcpy(&argv_new[2], argv, argc * sizeof(argv[0]));
            argv_new[argc + 2] = NULL;

            memcpy(&argv_hack, &argv_new, sizeof(argv_hack));
            execvp(sh, argv_hack);
        }
        else {
            memcpy(&argv_hack, &argv, sizeof(argv_hack));
            execvp(argv[0], argv_hack);
        }

child_error: ;
        //
        // The original implementation of this code would write errno to the
        // info pipe.  However, errno may be volatile (and it is on Android).
        // write() does not accept volatile pointers, so copy it to a
        // temporary value first.
        //
        int nonvolatile_errno = errno;

        if (write(info_pipe[W], &nonvolatile_errno, sizeof(int)) == -1) {
            //
            // Nothing we can do, but need to stop compiler warning
            // (cast to void is insufficient for warn_unused_result)
        }
        exit(EXIT_FAILURE); /* get here only when exec fails */
    }
    else if (fpid > 0) {
        //
        // This is the parent branch, so it may (or may not) wait on the
        // child fork branch, based on /WAIT.  Even if you are not using
        // /WAIT, it will use the info pipe to make sure the process did
        // actually start.
        //

#define BUF_SIZE_CHUNK 4096
        nfds_t nfds = 0;
        struct pollfd pfds[4];
        pid_t xpid;
        unsigned int i;
        ssize_t nbytes;
        off_t input_size = 0;
        off_t output_size = 0;
        off_t err_size = 0;
        int valid_nfds;

        // initialize outputs

        if (
            output_type != NONE_TYPE
            && output_type != INHERIT_TYPE
            && (output == NULL || output_len == NULL)
        ){
            return -1;
        }
        if (output != NULL)
            *output = NULL;
        if (output_len != NULL)
            *output_len = 0;

        if (
            err_type != NONE_TYPE
            && err_type != INHERIT_TYPE
            && (err == NULL || err_len == NULL)
        ){
            return -1;
        }
        if (err != NULL)
            *err = NULL;
        if (err_len != NULL)
            *err_len = 0;

        // Only put the input pipe in the consideration if we can write to
        // it and we have data to send to it.

        if ((stdin_pipe[W] > 0) && (input_size = strlen(input)) > 0) {
            /* printf("stdin_pipe[W]: %d\n", stdin_pipe[W]); */

            //
            // the passed in input_len is in characters, not in bytes
            //
            input_len = 0;

            pfds[nfds].fd = stdin_pipe[W];
            pfds[nfds].events = POLLOUT;
            nfds++;

            close(stdin_pipe[R]);
            stdin_pipe[R] = -1;
        }
        if (stdout_pipe[R] > 0) {
            /* printf("stdout_pipe[R]: %d\n", stdout_pipe[R]); */

            output_size = BUF_SIZE_CHUNK;

            *output = OS_ALLOC_N(char, output_size);

            pfds[nfds].fd = stdout_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stdout_pipe[W]);
            stdout_pipe[W] = -1;
        }
        if (stderr_pipe[R] > 0) {
            /* printf("stderr_pipe[R]: %d\n", stderr_pipe[R]); */

            err_size = BUF_SIZE_CHUNK;

            *err = OS_ALLOC_N(char, err_size);

            pfds[nfds].fd = stderr_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            close(stderr_pipe[W]);
            stderr_pipe[W] = -1;
        }

        if (info_pipe[R] > 0) {
            pfds[nfds].fd = info_pipe[R];
            pfds[nfds].events = POLLIN;
            nfds++;

            info_size = 4;

            info = OS_ALLOC_N(char, info_size);

            close(info_pipe[W]);
            info_pipe[W] = -1;
        }

        valid_nfds = nfds;
        while (valid_nfds > 0) {
            xpid = waitpid(fpid, &status, WNOHANG);
            if (xpid == -1) {
                ret = errno;
                goto error;
            }

            if (xpid == fpid) {
                //
                // try one more time to read any remainding output/err
                //
                if (stdout_pipe[R] > 0) {
                    nbytes = read(
                        stdout_pipe[R],
                        *output + *output_len,
                        output_size - *output_len
                    );

                    if (nbytes > 0) {
                        *output_len += nbytes;
                    }
                }

                if (stderr_pipe[R] > 0) {
                    nbytes = read(
                        stderr_pipe[R],
                        *err + *err_len,
                        err_size - *err_len
                    );
                    if (nbytes > 0) {
                        *err_len += nbytes;
                    }
                }

                if (info_pipe[R] > 0) {
                    nbytes = read(
                        info_pipe[R],
                        info + info_len,
                        info_size - info_len
                    );
                    if (nbytes > 0) {
                        info_len += nbytes;
                    }
                }

                break;
            }

            /*
            for (i = 0; i < nfds; ++i) {
                printf(" %d", pfds[i].fd);
            }
            printf(" / %d\n", nfds);
            */
            if (poll(pfds, nfds, -1) < 0) {
                ret = errno;
                goto kill;
            }

            for (i = 0; i < nfds && valid_nfds > 0; ++i) {
                /* printf("check: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                if (pfds[i].revents & POLLERR) {
                    /* printf("POLLERR: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
                else if (pfds[i].revents & POLLOUT) {
                    /* printf("POLLOUT: %d [%d/%d]\n", pfds[i].fd, i, nfds); */

                    nbytes = write(pfds[i].fd, input, input_size - input_len);
                    if (nbytes <= 0) {
                        ret = errno;
                        goto kill;
                    }
                    /* printf("POLLOUT: %d bytes\n", nbytes); */
                    input_len += nbytes;
                    if (cast(off_t, input_len) >= input_size) {
                        close(pfds[i].fd);
                        pfds[i].fd = -1;
                        valid_nfds --;
                    }
                }
                else if (pfds[i].revents & POLLIN) {
                    /* printf("POLLIN: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    char **buffer = NULL;
                    u32 *offset;
                    ssize_t to_read = 0;
                    size_t size;
                    if (pfds[i].fd == stdout_pipe[R]) {
                        buffer = (char**)output;
                        offset = output_len;
                        size = output_size;
                    } else if (pfds[i].fd == stderr_pipe[R]) {
                        buffer = (char**)err;
                        offset = err_len;
                        size = err_size;
                    } else { /* info pipe */
                        buffer = &info;
                        offset = &info_len;
                        size = info_size;
                    }
                    do {
                        to_read = size - *offset;
                        /* printf("to read %d bytes\n", to_read); */
                        nbytes = read(pfds[i].fd, *buffer + *offset, to_read);
                        if (nbytes < 0) {
                            break;
                        }
                        if (nbytes == 0) { // closed
                            /* printf("the other end closed\n"); */
                            close(pfds[i].fd);
                            pfds[i].fd = -1;
                            valid_nfds --;
                            break;
                        }
                        /* printf("POLLIN: %d bytes\n", nbytes); */
                        *offset += nbytes;
                        if (*offset >= size) {
                            char *larger =
                                OS_ALLOC_N(char, size + BUF_SIZE_CHUNK);
                            if (!larger) goto kill;
                            memcpy(larger, *buffer, size * sizeof(larger[0]));
                            OS_FREE(*buffer);
                            *buffer = larger;
                            size += BUF_SIZE_CHUNK;
                        }
                    } while (nbytes == to_read);
                }
                else if (pfds[i].revents & POLLHUP) {
                    /* printf("POLLHUP: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    valid_nfds --;
                }
                else if (pfds[i].revents & POLLNVAL) {
                    /* printf("POLLNVAL: %d [%d/%d]\n", pfds[i].fd, i, nfds); */
                    ret = errno;
                    goto kill;
                }
            }
        }

        if (valid_nfds == 0 && flag_wait) {
            if (waitpid(fpid, &status, 0) < 0) {
                ret = errno;
                goto error;
            }
        }

    }
    else { // error
        ret = errno;
        goto error;
    }

    if (info_len > 0) {
        //
        // exec in child process failed, set to errno for reporting
        ret = *(int*)info;
    }
    else if (WIFEXITED(status)) {
        if (exit_code != NULL)
            *exit_code = WEXITSTATUS(status);
        if (pid != NULL)
            *pid = fpid;
    }
    else
        goto error;

    goto cleanup;

kill:
    kill(fpid, SIGKILL);
    waitpid(fpid, NULL, 0);

error:
    if (ret == 0)
        ret = -1;

cleanup:
    if (output != NULL && *output != NULL && *output_len <= 0) {
        OS_FREE(*output);
    }
    if (err != NULL && *err != NULL && *err_len <= 0) {
        OS_FREE(*err);
    }
    if (info != NULL) {
        OS_FREE(info);
    }
    if (info_pipe[R] > 0) {
        close(info_pipe[R]);
    }
    if (info_pipe[W] > 0) {
        close(info_pipe[W]);
    }

info_pipe_err:
    if (stderr_pipe[R] > 0) {
        close(stderr_pipe[R]);
    }
    if (stderr_pipe[W] > 0) {
        close(stderr_pipe[W]);
    }

    goto stderr_pipe_err; // no jumps here yet, avoid warning

stderr_pipe_err:
    if (stdout_pipe[R] > 0) {
        close(stdout_pipe[R]);
    }
    if (stdout_pipe[W] > 0) {
        close(stdout_pipe[W]);
    }

stdout_pipe_err:
    if (stdin_pipe[R] > 0) {
        close(stdin_pipe[R]);
    }
    if (stdin_pipe[W] > 0) {
        close(stdin_pipe[W]);
    }

stdin_pipe_err:
    //
    // We will get to this point on success, as well as error (so ret may
    // be 0.  This is the return value of the host kit function to Rebol, not
    // the process exit code (that's written into the pointer arg 'exit_code')
    //
    return ret;
}


#ifdef TO_LINUX

static int Try_Browser(const char *browser, const char *url)
{
    // Must be compile-time const for '= {...}' style init (-Wc99-extensions)
    const char *argv[3];

    argv[0] = browser;
    argv[1] = url;
    argv[2] = NULL;

    // Delegate to process creation code in hostkit, the same one used
    // by the CALL native, but don't pipe any of the I/O.
    //
    // (Posix host code, in another file, uses fork() and calls /usr/bin/open)
    //
    return OS_Create_Process(
        browser,
        2,
        argv,
        0,
        NULL, // pid
        NULL, // exit_code
        INHERIT_TYPE, // input_type
        NULL, // input
        0, // input_len
        INHERIT_TYPE, // output_type
        NULL, // output
        NULL, // output_len
        INHERIT_TYPE, // err_type
        NULL, // err
        NULL // err_len
    );
}


//
//  OS_Browse: C
//
REBOOL OS_Browse(const char *url)
{
    if (Try_Browser("xdg-open", url) == 0)
        return TRUE;
    if (Try_Browser("x-www-browser", url) == 0)
        return TRUE;
    return FALSE;
}

#else

static REBOOL Try_Browser(const char *browser, const char *url)
{
    pid_t pid = fork();

    switch (pid) {
    case -1:
        return FALSE;

    case 0:
        execlp(browser, browser, url, NULL);
        exit(1);

    default: {
        int status;
        waitpid(pid, &status, WUNTRACED);
        return LOGICAL(WIFEXITED(status) != 0 && WEXITSTATUS(status) == 0); }
    }
}


//
//  OS_Browse: C
//
// On POSIX, use fork()
//
REBOOL OS_Browse(const char *url)
{
    if (Try_Browser("/usr/bin/open", url))
        return TRUE;
    return FALSE;
}

#endif