#	$OpenBSD: Makefile,v 1.15 2019/06/25 07:44:20 claudio Exp $

.PATH:		${.CURDIR}/../bgpd

PROG=	bgpctl
SRCS=	bgpctl.c parser.c mrtparser.c util.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../bgpd
MAN=	bgpctl.8
LDADD= -lutil -lm
DPADD+= ${LIBUTIL} ${LIBM}

.include <bsd.prog.mk>
