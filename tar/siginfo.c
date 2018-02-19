/*-
 * Copyright 2008 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD: src/usr.bin/tar/siginfo.c,v 1.2 2008/05/22 21:08:36 cperciva Exp $");

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsdtar.h"
#include "humansize.h"
#include "tarsnap_opt.h"

/* Is there a pending SIGINFO or SIGUSR1? */
static volatile sig_atomic_t siginfo_received = 0;

struct siginfo_data {
	/* What sort of operation are we doing? */
	char * oper;

	/* What path are we handling? */
	char * path;

	/* How large is the archive entry? */
	int64_t size;

	/* How many files in total? */
	int file_count;

	/* How many bytes have we handled in total? */
	int64_t total_uncompressed;

	/* Old signal handlers. */
#ifdef SIGINFO
	struct sigaction siginfo_old;
#endif
	struct sigaction sigusr1_old;
};

static void		 siginfo_handler(int sig);
static void		 siginfo_raisetotal(int64_t total);

/* Handler for SIGINFO / SIGUSR1. */
static void
siginfo_handler(int sig)
{

	(void)sig; /* UNUSED */

	/* Record that SIGINFO or SIGUSR1 has been received. */
	siginfo_received = 1;
}

void
siginfo_init(struct bsdtar *bsdtar)
{
	struct sigaction sa;

	/* Allocate space for internal structure. */
	if ((bsdtar->siginfo = malloc(sizeof(struct siginfo_data))) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "malloc failed");

	/* Set the strings to NULL so that free() is safe. */
	bsdtar->siginfo->path = bsdtar->siginfo->oper = NULL;

	/* We want to catch SIGINFO, if it exists. */
	sa.sa_handler = siginfo_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
#ifdef SIGINFO
	if (sigaction(SIGINFO, &sa, &bsdtar->siginfo->siginfo_old))
		bsdtar_errc(bsdtar, 1, errno, "sigaction(SIGINFO) failed");
#endif
#ifdef SIGUSR1
	/* ... and treat SIGUSR1 the same way as SIGINFO. */
	if (sigaction(SIGUSR1, &sa, &bsdtar->siginfo->sigusr1_old))
		bsdtar_errc(bsdtar, 1, errno, "sigaction(SIGUSR1) failed");
#endif
}

void
siginfo_setinfo(struct bsdtar *bsdtar, const char * oper, const char * path,
    int64_t size, int file_count, int64_t total_uncompressed)
{

	/* Free old operation and path strings. */
	free(bsdtar->siginfo->oper);
	free(bsdtar->siginfo->path);

	/* Duplicate strings and store entry size. */
	if ((bsdtar->siginfo->oper = strdup(oper)) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "Cannot strdup");
	if ((bsdtar->siginfo->path = strdup(path)) == NULL)
		bsdtar_errc(bsdtar, 1, errno, "Cannot strdup");
	bsdtar->siginfo->size = size;
	bsdtar->siginfo->file_count = file_count;
	bsdtar->siginfo->total_uncompressed = total_uncompressed;

	siginfo_raisetotal(total_uncompressed);
}

void
siginfo_printinfo(struct bsdtar *bsdtar, off_t progress)
{
	char * s_progress;
	char * s_size;
	char * s_total_uncompressed;

	/* Sanity check. */
	assert(progress >= 0);

	/* If there's a signal to handle and we know what we're doing... */
	if ((siginfo_received == 1) &&
	    (bsdtar->siginfo->path != NULL) &&
	    (bsdtar->siginfo->oper != NULL)) {
		if (bsdtar->verbose)
			fprintf(stderr, "\n");

		/* Print current operation and filename. */
		safe_fprintf(stderr, "%s %s",
		    bsdtar->siginfo->oper, bsdtar->siginfo->path);

		/* Print progress on current file (if applicable). */
		if (bsdtar->siginfo->size > 0) {
			if (tarsnap_opt_humanize_numbers) {
				if ((s_progress = humansize((uint64_t)progress))
				    == NULL)
					goto err0;
				if ((s_size = humansize(
				    (uint64_t)bsdtar->siginfo->size)) == NULL)
					goto err1;
				safe_fprintf(stderr, " (%s / %s bytes)",
				    s_progress, s_size);

				/* Clean up. */
				free(s_progress);
				free(s_size);
			} else {
				safe_fprintf(stderr, " (%ju / %" PRId64
				    " bytes)", (uintmax_t)progress,
				    bsdtar->siginfo->size);
			}
		}
		fprintf(stderr, "\n");

		/* Print overall progress (if applicable). */
		if (bsdtar->siginfo->total_uncompressed > 0) {
			if (tarsnap_opt_humanize_numbers) {
				if ((s_total_uncompressed = humansize(
				    (uint64_t)bsdtar->siginfo->total_uncompressed
				    )) == NULL)
					goto err0;
				safe_fprintf(stderr, "Processed %zu files, %s",
				    bsdtar->siginfo->file_count,
				    s_total_uncompressed);

				/* Clean up. */
				free(s_total_uncompressed);
			} else {
				safe_fprintf(stderr,
				    "Processed %zu files, %" PRId64 " bytes",
				    bsdtar->siginfo->file_count,
				    bsdtar->siginfo->total_uncompressed);
			}
		}
		if (!bsdtar->verbose)
			fprintf(stderr, "\n");
		siginfo_received = 0;
	}

	/* Success! */
	return;

err1:
	free(s_progress);
err0:
	/* Failure! */
	bsdtar_errc(bsdtar, 1, ENOMEM, "Cannot allocate memory");
}

void
siginfo_done(struct bsdtar *bsdtar)
{

#ifdef SIGINFO
	/* Restore old SIGINFO handler. */
	sigaction(SIGINFO, &bsdtar->siginfo->siginfo_old, NULL);
#endif
#ifdef SIGUSR1
	/* And the old SIGUSR1 handler, too. */
	sigaction(SIGUSR1, &bsdtar->siginfo->sigusr1_old, NULL);
#endif

	/* Free strings. */
	free(bsdtar->siginfo->path);
	free(bsdtar->siginfo->oper);

	/* Free internal data structure. */
	free(bsdtar->siginfo);
}

/**
 * siginfo_raisetotal(total):
 * Look at how many bytes on disk have been processed since the last update,
 * and trigger a siginfo_printinfo() if it exceeds tarsnap_opt_processbytes.
 */
static void
siginfo_raisetotal(int64_t total)
{
	static uint64_t lastprogress = 0;

	if (tarsnap_opt_progressbytes != (uint64_t)(-1)) {
		if ((uint64_t)total > lastprogress +
		    (uint64_t)tarsnap_opt_progressbytes) {
			lastprogress = (uint64_t)total;

			/* Fake a SIGINFO (no need for an actual signal). */
			siginfo_received = 1;
		}
	}
}
