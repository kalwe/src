#	$OpenBSD: Makefile,v 1.6 2019/06/18 06:15:54 claudio Exp $

PROG=	rpki-client
SRCS=	as.c cert.c cms.c crl.c io.c ip.c log.c main.c mft.c \
	output-bgpd.c roa.c rsync.c tal.c validate.c x509.c
MAN=	rpki-client.8

LDADD=	/usr/local/lib/eopenssl/libssl.a /usr/local/lib/eopenssl/libcrypto.a
CFLAGS= -I/usr/local/include/eopenssl

.include <bsd.prog.mk>
