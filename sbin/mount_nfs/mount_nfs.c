/*	$NetBSD: mount_nfs.c,v 1.76 2025/01/03 00:49:24 rillig Exp $	*/

/*
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
__COPYRIGHT("@(#) Copyright (c) 1992, 1993, 1994\
 The Regents of the University of California.  All rights reserved.");
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)mount_nfs.c	8.11 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: mount_nfs.c,v 1.76 2025/01/03 00:49:24 rillig Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>

#include <rpc/rpc.h>
#include <nfs/rpcv2.h>	/* XXX: redefines enums */
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsmount.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include <mntopts.h>

#include "mountprog.h"
#include "mount_nfs.h"

#define	ALTF_BG		0x00000001
#define ALTF_CONN	0x00000002
#define ALTF_DUMBTIMR	0x00000004
#define ALTF_INTR	0x00000008
#define ALTF_NOAC	0x00000010
#define ALTF_NFSV3	0x00000020
#define ALTF_RDIRPLUS	0x00000040
#define	ALTF_MNTUDP	0x00000080
#define ALTF_NORESPORT	0x00000100
#define ALTF_SEQPACKET	0x00000200
#define ALTF_NQNFS	0x00000400
#define ALTF_SOFT	0x00000800
#define ALTF_TCP	0x00001000
#define ALTF_NFSV2	0x00002000
#define ALTF_PORT	0x00004000
#define ALTF_RSIZE	0x00008000
#define ALTF_WSIZE	0x00010000
#define ALTF_RDIRSIZE	0x00020000
#define ALTF_MAXGRPS	0x00040000
#define ALTF_LEASETERM	0x00080000
#define ALTF_READAHEAD	0x00100000
#define ALTF_DEADTHRESH	0x00200000
#define ALTF_TIMEO	0x00400000
#define ALTF_RETRANS	0x00800000
#define ALTF_UDP	0x01000000

static const struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_FORCE,
	MOPT_UPDATE,
	MOPT_GETARGS,
	{ "bg", 0, ALTF_BG, 1 },
	{ "conn", 0, ALTF_CONN, 1 },
	{ "dumbtimer", 0, ALTF_DUMBTIMR, 1 },
	{ "intr", 0, ALTF_INTR, 1 },
	{ "ac", 1, ALTF_NOAC, 1 },
	{ "nfsv3", 0, ALTF_NFSV3, 1 },
	{ "rdirplus", 0, ALTF_RDIRPLUS, 1 },
	{ "mntudp", 0, ALTF_MNTUDP, 1 },
	{ "resport", 1, ALTF_NORESPORT, 1 },
	{ "nqnfs", 0, ALTF_NQNFS, 1 },
	{ "soft", 0, ALTF_SOFT, 1 },
	{ "tcp", 0, ALTF_TCP, 1 },
	{ "udp", 0, ALTF_UDP, 1 },
	{ "nfsv2", 0, ALTF_NFSV2, 1 },
	{ "port", 0, ALTF_PORT, 1 },
	{ "rsize", 0, ALTF_RSIZE, 1 },
	{ "wsize", 0, ALTF_WSIZE, 1 },
	{ "rdirsize", 0, ALTF_RDIRSIZE, 1 },
	{ "maxgrps", 0, ALTF_MAXGRPS, 1 },
	{ "leaseterm", 0, ALTF_LEASETERM, 1 },
	{ "readahead", 0, ALTF_READAHEAD, 1 },
	{ "deadthresh", 0, ALTF_DEADTHRESH, 1 },
	{ "timeo", 0, ALTF_TIMEO, 1 },
	MOPT_NULL,

};

struct nfs_args nfsdefargs = {
	.version = NFS_ARGSVERSION,
	.addr = NULL,
	.addrlen = sizeof(struct sockaddr_in),
	.sotype = SOCK_STREAM,
	.proto = 0,
	.fh = NULL,
	.fhsize = 0,
	.flags = NFSMNT_NFSV3|NFSMNT_NOCONN|NFSMNT_RESVPORT,
	.wsize = NFS_WSIZE,
	.rsize = NFS_RSIZE,
	.readdirsize = NFS_READDIRSIZE,
	.timeo = 10,
	.retrans = NFS_RETRANS,
	.maxgrouplist = NFS_MAXGRPS,
	.readahead = NFS_DEFRAHEAD,
	.leaseterm = 0,	/* Ignored; lease term */
	.deadthresh = NFS_DEFDEADTHRESH,
	.hostname = NULL,
};

static void	shownfsargs(const struct nfs_args *);
int	mount_nfs(int argc, char **argv);
/* void	set_rpc_maxgrouplist(int); */
__dead static void	usage(void);

#ifndef MOUNT_NOMAIN
int
main(int argc, char **argv)
{

	setprogname(argv[0]);
	return mount_nfs(argc, argv);
}
#endif

void
mount_nfs_dogetargs(struct nfs_args *nfsargsp, int mntflags, const char *spec)
{
	static struct sockaddr_storage sa;
	char *tspec;

	if ((mntflags & MNT_GETARGS) != 0) {
		memset(&sa, 0, sizeof(sa));
		nfsargsp->addr = (struct sockaddr *)&sa;
		nfsargsp->addrlen = sizeof(sa);
	} else {
		if ((tspec = strdup(spec)) == NULL) {
			err(EXIT_FAILURE, "strdup");
		}
		if (!getnfsargs(tspec, nfsargsp)) {
			exit(EXIT_FAILURE);
		}
		free(tspec);
	}
}

static int
getnum(const char *s, int c)
{
	char *es;
	long num = strtol(s, &es, 10);
	if (*es || num <= 0 || num > INT_MAX)
		errx(EXIT_FAILURE, "Illegal value `%s' for option -%c", s, c);
	return (int)num;
}

static __dead void
conflicting(void)
{
	errx(EXIT_FAILURE, "Conflicting version options");
}

void
mount_nfs_parseargs(int argc, char *argv[],
	struct nfs_args *nfsargsp, int *mntflags,
	char *spec, char *name)
{
	int altflags, num;
	int c;
	mntoptparse_t mp;

	*mntflags = 0;
	altflags = 0;
	memset(nfsargsp, 0, sizeof(*nfsargsp));
	*nfsargsp = nfsdefargs;
	while ((c = getopt(argc, argv,
	    "23Aa:bcCdD:g:I:iL:lo:PpqR:r:sTt:w:x:UuX")) != -1)
		switch (c) {
		case '3':
		case 'q':
			if (force2)
				conflicting();
			force3 = 1;
			break;
		case '2':
			if (force3)
				conflicting();
			force2 = 1;
			nfsargsp->flags &= ~NFSMNT_NFSV3;
			break;
		case 'A':
			nfsargsp->flags |= NFSMNT_NOAC;
			break;
		case 'a':
			nfsargsp->readahead = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_READAHEAD;
			break;
		case 'b':
			opflags |= BGRND;
			break;
		case 'c':
			nfsargsp->flags |= NFSMNT_NOCONN;
			break;
		case 'C':
			nfsargsp->flags &= ~NFSMNT_NOCONN;
			break;
		case 'D':
			nfsargsp->deadthresh = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_DEADTHRESH;
			break;
		case 'd':
			nfsargsp->flags |= NFSMNT_DUMBTIMR;
			break;
		case 'g':
			num = getnum(optarg, c);
			set_rpc_maxgrouplist(num);
			nfsargsp->maxgrouplist = num;
			nfsargsp->flags |= NFSMNT_MAXGRPS;
			break;
		case 'I':
			nfsargsp->readdirsize = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_READDIRSIZE;
			break;
		case 'i':
			nfsargsp->flags |= NFSMNT_INT;
			break;
		case 'L':
			/* ignore */
			break;
		case 'l':
			nfsargsp->flags |= NFSMNT_RDIRPLUS;
			break;
		case 'o':
			mp = getmntopts(optarg, mopts, mntflags, &altflags);
			if (mp == NULL)
				err(EXIT_FAILURE, "getmntopts");
			if (altflags & ALTF_BG)
				opflags |= BGRND;
			if (altflags & ALTF_CONN)
				nfsargsp->flags &= ~NFSMNT_NOCONN;
			if (altflags & ALTF_DUMBTIMR)
				nfsargsp->flags |= NFSMNT_DUMBTIMR;
			if (altflags & ALTF_INTR)
				nfsargsp->flags |= NFSMNT_INT;
			if (altflags & ALTF_NOAC)
				nfsargsp->flags |= NFSMNT_NOAC;
			if (altflags & (ALTF_NFSV3|ALTF_NQNFS)) {
				if (force2)
					conflicting();
				force3 = 1;
			}
			if (altflags & ALTF_NFSV2) {
				if (force3)
					conflicting();
				force2 = 1;
				nfsargsp->flags &= ~NFSMNT_NFSV3;
			}
			if (altflags & ALTF_RDIRPLUS)
				nfsargsp->flags |= NFSMNT_RDIRPLUS;
			if (altflags & ALTF_MNTUDP)
				mnttcp_ok = 0;
			if (altflags & ALTF_NORESPORT)
				nfsargsp->flags &= ~NFSMNT_RESVPORT;
			if (altflags & ALTF_SOFT)
				nfsargsp->flags |= NFSMNT_SOFT;
			if (altflags & ALTF_UDP) {
				nfsargsp->sotype = SOCK_DGRAM;
			}
			/*
			 * After UDP, because TCP overrides if both
			 * are present.
			 */
			if (altflags & ALTF_TCP) {
				nfsargsp->sotype = SOCK_STREAM;
			}
			if (altflags & ALTF_PORT) {
				port = (int)getmntoptnum(mp, "port");
			}
			if (altflags & ALTF_RSIZE) {
				nfsargsp->rsize =
				    (int)getmntoptnum(mp, "rsize");
				nfsargsp->flags |= NFSMNT_RSIZE;
			}
			if (altflags & ALTF_WSIZE) {
				nfsargsp->wsize =
				    (int)getmntoptnum(mp, "wsize");
				nfsargsp->flags |= NFSMNT_WSIZE;
			}
			if (altflags & ALTF_RDIRSIZE) {
				nfsargsp->rsize =
				    (int)getmntoptnum(mp, "rdirsize");
				nfsargsp->flags |= NFSMNT_READDIRSIZE;
			}
#if 0
			if (altflags & ALTF_MAXGRPS) {
				set_rpc_maxgrouplist(num);
				nfsargsp->maxgrouplist =
				    (int)getmntoptnum(mp, "maxgrps");
				nfsargsp->flags |= NFSMNT_MAXGRPS;
			}
#endif
			if (altflags & ALTF_LEASETERM) {
				nfsargsp->leaseterm =
				(int)getmntoptnum(mp, "leaseterm");
				nfsargsp->flags |= NFSMNT_LEASETERM;
			}
			if (altflags & ALTF_READAHEAD) {
				nfsargsp->readahead =
				    (int)getmntoptnum(mp, "readahead");
				nfsargsp->flags |= NFSMNT_READAHEAD;
			}
			if (altflags & ALTF_DEADTHRESH) {
				nfsargsp->deadthresh = 
				    (int)getmntoptnum(mp, "deadthresh");
				nfsargsp->flags |= NFSMNT_DEADTHRESH;
			}
			if (altflags & ALTF_TIMEO) {
				nfsargsp->timeo = 
				    (int)getmntoptnum(mp, "timeo");
				nfsargsp->flags |= NFSMNT_TIMEO;
			}
			if (altflags & ALTF_RETRANS) {
				nfsargsp->retrans = 
				    (int)getmntoptnum(mp, "retrans");
				nfsargsp->flags |= NFSMNT_RETRANS;
			}
			altflags = 0;
			freemntopts(mp);
			break;
		case 'P':
			nfsargsp->flags |= NFSMNT_RESVPORT;
			break;
		case 'p':
			nfsargsp->flags &= ~NFSMNT_RESVPORT;
			break;
		case 'R':
			retrycnt = getnum(optarg, c);
			break;
		case 'r':
			nfsargsp->rsize = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_RSIZE;
			break;
		case 's':
			nfsargsp->flags |= NFSMNT_SOFT;
			break;
		case 'T':
			nfsargsp->sotype = SOCK_STREAM;
			break;
		case 't':
			nfsargsp->timeo = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_TIMEO;
			break;
		case 'w':
			nfsargsp->wsize = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_WSIZE;
			break;
		case 'x':
			nfsargsp->retrans = getnum(optarg, c);
			nfsargsp->flags |= NFSMNT_RETRANS;
			break;
		case 'X':
			nfsargsp->flags |= NFSMNT_XLATECOOKIE;
			break;
		case 'u':
			nfsargsp->sotype = SOCK_DGRAM;
			break;
		case 'U':
			mnttcp_ok = 0;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	strlcpy(spec, *argv++, MAXPATHLEN);
	pathadj(*argv, name);
	mount_nfs_dogetargs(nfsargsp, *mntflags, spec);
}

int
mount_nfs(int argc, char *argv[])
{
	char spec[MAXPATHLEN], name[MAXPATHLEN];
	struct nfs_args args;
	int mntflags;
	int retval;

	mount_nfs_parseargs(argc, argv, &args, &mntflags, spec, name);

 retry:
	if ((retval = mount(MOUNT_NFS, name, mntflags,
	    &args, sizeof args)) == -1) {
		/* Did we just default to v3 on a v2-only kernel?
		 * If so, default to v2 & try again */
		if (errno == EPROGMISMATCH &&
		    (args.flags & NFSMNT_NFSV3) != 0 && !force3) {
			/*
			 * fall back to v2.  XXX lack of V3 umount.
			 */
			args.flags &= ~NFSMNT_NFSV3;
			mount_nfs_dogetargs(&args, mntflags, spec);
			goto retry;
		}
	}
	if (retval == -1)
		err(EXIT_FAILURE, "%s on %s", spec, name);
	if (mntflags & MNT_GETARGS) {
		shownfsargs(&args);
		return EXIT_SUCCESS;
	}
		
	exit(EXIT_SUCCESS);
}

static void
shownfsargs(const struct nfs_args *nfsargsp)
{
	char fbuf[2048];
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	int error;

	(void)snprintb(fbuf, sizeof(fbuf), NFSMNT_BITS,
	    (uint64_t)nfsargsp->flags);
	if (nfsargsp->addr != NULL) {
		error = getnameinfo(nfsargsp->addr,
		    (socklen_t)nfsargsp->addrlen, host,
		    sizeof(host), serv, sizeof(serv),
		    NI_NUMERICHOST | NI_NUMERICSERV);
		if (error != 0)
			warnx("getnameinfo: %s", gai_strerror(error));
	} else
		error = -1;

	if (error == 0)
		printf("addr=%s, port=%s, addrlen=%d, ",
		    host, serv, nfsargsp->addrlen);
	printf("sotype=%d, proto=%d, fhsize=%d, "
	    "flags=%s, wsize=%d, rsize=%d, readdirsize=%d, timeo=%d, "
	    "retrans=%d, maxgrouplist=%d, readahead=%d, leaseterm=%d, "
	    "deadthresh=%d\n",
	    nfsargsp->sotype,
	    nfsargsp->proto,
	    nfsargsp->fhsize,
	    fbuf,
	    nfsargsp->wsize,
	    nfsargsp->rsize,
	    nfsargsp->readdirsize,
	    nfsargsp->timeo,
	    nfsargsp->retrans,
	    nfsargsp->maxgrouplist,
	    nfsargsp->readahead,
	    nfsargsp->leaseterm,
	    nfsargsp->deadthresh);
}

static void
usage(void)
{
	(void)fprintf(stderr, "Usage: %s %s\n%s\n%s\n%s\n%s\n", getprogname(),
"[-23bCcdilPpqsTUuX] [-a maxreadahead] [-D deadthresh]",
"\t[-g maxgroups] [-I readdirsize] [-L leaseterm]",
"\t[-o options] [-R retrycnt] [-r readsize] [-t timeout]",
"\t[-w writesize] [-x retrans]",
"\trhost:path node");
	exit(EXIT_FAILURE);
}
