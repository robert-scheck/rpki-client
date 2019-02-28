#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include "extern.h"

/*
 * Parse results and data of the manifest file.
 */
struct	parse {
	const char	 *fn; /* manifest file name */
	int		  verbose; /* parse verbosity */
	struct mft	 *res;
};

/* 
 * Wrap around the existing log macros. 
 * Do this to pass the "verbose" variable into the log.
 */

#define MFT_WARNX(_p, _fmt, ...) \
	WARNX((_p)->verbose, (_fmt), ##__VA_ARGS__)
#define MFT_WARNX1(_p, _fmt, ...) \
	WARNX1((_p)->verbose, (_fmt), ##__VA_ARGS__)
#define MFT_LOG(_p, _fmt, ...) \
	LOG((_p)->verbose, (_fmt), ##__VA_ARGS__)
#define MFT_CRYPTOX(_p, _fmt, ...) \
	CRYPTOX((_p)->verbose, (_fmt), ##__VA_ARGS__)

/*
 * Parse the "FileAndHash" sequence, RFC 6486, sec. 4.2.
 */
static int
mft_parse_flist(struct parse *p, const ASN1_OCTET_STRING *os)
{
	const ASN1_SEQUENCE_ANY *seq, *sseq = NULL;
	const ASN1_TYPE		*type;
	const ASN1_IA5STRING	*str;
	const unsigned char     *d;
	size_t		         dsz;
	int		 	 i, rc = 0;
	void			*pp;

	d = os->data;
	dsz = os->length;
	seq = d2i_ASN1_SEQUENCE_ANY(NULL, &d, dsz);
	if (NULL == seq) {
		MFT_CRYPTOX(p, "%s: want ASN.1 sequence", p->fn);
		goto out;
	} 

	for (i = 0; i < sk_ASN1_TYPE_num(seq); i++) {
		type = sk_ASN1_TYPE_value(seq, i);
		if (V_ASN1_SEQUENCE != type->type) {
			MFT_WARNX(p, "%s: want ASN.1 sequence", p->fn);
			goto out;
		}
		d = type->value.octet_string->data;
		dsz = type->value.octet_string->length;
		sseq = d2i_ASN1_SEQUENCE_ANY(NULL, &d, dsz);
		if (NULL == sseq) {
			MFT_CRYPTOX(p, "%s: want "
				"ASN.1 sequence", p->fn);
			goto out;
		} else if (2 != sk_ASN1_TYPE_num(sseq)) {
			MFT_WARNX(p, "%s: want only "
				"two elements", p->fn);
			goto out;
		}

		if (V_ASN1_IA5STRING != 
		    sk_ASN1_TYPE_value(sseq, 0)->type) {
			MFT_WARNX(p, "%s: want "
				"V_ASN1_IA5STRING", p->fn);
			goto out;
		}

		pp = reallocarray(p->res->files, 
			p->res->filesz + 1, sizeof(char *));
		if (NULL == pp) {
			WARN("reallocarray");
			goto out;
		}
		p->res->files = pp;
		str = sk_ASN1_TYPE_value(sseq, 0)->value.ia5string;
		p->res->files[p->res->filesz] = 
			strndup(str->data, str->length);
		if (NULL == p->res->files[p->res->filesz]) {
			WARN("strdup");
			goto out;
		}
		p->res->filesz++;
		sk_ASN1_TYPE_free(sseq);
		sseq = NULL;
	}

	rc = 1;
out:
	sk_ASN1_TYPE_free(sseq);
	sk_ASN1_TYPE_free(seq);
	return rc;
}

/*
 * Handle the eContent of the manifest object, RFC 6486 sec. 4.2.
 * Returns zero on failure, non-zero on success.
 */
static int
mft_parse_econtent(const ASN1_OCTET_STRING *os, struct parse *p)
{
	const ASN1_SEQUENCE_ANY *seq;
	const unsigned char     *d;
	size_t		         dsz;
	const ASN1_TYPE	        *ver, *mftnum, *thisup, *nextup,
			        *flistalg, *fl;
	int		         i, rc = 0;

	d = os->data;
	dsz = os->length;

	seq = d2i_ASN1_SEQUENCE_ANY(NULL, &d, dsz);
	if (NULL == seq) {
		MFT_CRYPTOX(p, "%s: want ASN.1 sequence", p->fn);
		goto out;
	} 

	/* 
	 * We're supposed to have six elements.
	 * But it appears that some manifests don't have the version.
	 */

	if (5 != sk_ASN1_TYPE_num(seq) &&
	    6 != sk_ASN1_TYPE_num(seq)) {
		MFT_WARNX(p, "%s: want 5 or 6 elements", p->fn);
		goto out;
	}

	/* Start with optional version. */

	i = 0;
	if (6 == sk_ASN1_TYPE_num(seq)) {
		ver = sk_ASN1_TYPE_value(seq, i++);
		if (V_ASN1_INTEGER != ver->type) {
			MFT_WARNX(p, "%s: want ASN.1 integer", p->fn);
			goto out;
		} 
	} else
		ver = NULL;

	/* Now the manifest sequence number. */

	mftnum = sk_ASN1_TYPE_value(seq, i++);
	if (V_ASN1_INTEGER != mftnum->type) {
		MFT_WARNX(p, "%s: want ASN.1 integer", p->fn);
		goto out;
	}

	/* Timestamps: this and next update time. */

	thisup = sk_ASN1_TYPE_value(seq, i++);
	nextup = sk_ASN1_TYPE_value(seq, i++);
	if (V_ASN1_GENERALIZEDTIME != thisup->type &&
	    V_ASN1_GENERALIZEDTIME != nextup->type) {
		MFT_WARNX(p, "%s: want ASN.1 general time", p->fn);
		goto out;
	}

	/* File list algorithm and sequence. */

	flistalg = sk_ASN1_TYPE_value(seq, i++);
	fl = sk_ASN1_TYPE_value(seq, i++);

	/* FIXME: more checks defined in RFC 6486 sec. 4.4. */

	if (V_ASN1_SEQUENCE != fl->type) {
		MFT_WARNX(p, "%s: want ASN.1 sequence", p->fn);
		goto out;
	}
	if ( ! mft_parse_flist(p, fl->value.octet_string)) {
		MFT_WARNX1(p, "mft_parse_flist");
		goto out;
	}

	rc = 1;
out:
	sk_ASN1_TYPE_free(seq);
	return rc;
}

/*
 * Parse the objects that have been published by the authority whose
 * public key is optionally in "pkey".
 * This conforms to RFC 6486.
 * Returns zero on failure, non-zero on success.
 */
struct mft *
mft_parse(int verb, X509 *cacert, const char *fn)
{
	struct parse		 p;
	const ASN1_OCTET_STRING *os;

	memset(&p, 0, sizeof(struct parse));
	p.fn = fn;
	p.verbose = verb;

	os = cms_parse_validate(verb, cacert, 
		fn, "1.2.840.113549.1.9.16.1.26");

	if (os == NULL) {
		MFT_WARNX1(&p, "cms_parse_validate");
		return NULL;
	} else if ((p.res = calloc(1, sizeof(struct mft))) == NULL) {
		WARN("calloc");
		return NULL;
	} else if ((p.res->file = strdup(fn)) == NULL) {
		WARN("strdup");
		free(p.res);
		return NULL;
	} else if (mft_parse_econtent(os, &p))
		return p.res;

	MFT_WARNX1(&p, "mft_parse_econtent");
	mft_free(p.res);
	return NULL;
}

void
mft_free(struct mft *p)
{
	size_t	 i;

	if (p == NULL)
		return;

	for (i = 0; i < p->filesz; i++)
		free(p->files[i]);

	free(p->file);
	free(p->files);
	free(p);
}

/*
 * Serialise MFT parsed content into the given buffer.
 * Returns zero on failure, non-zero on success.
 */
int
mft_buffer(char **b, size_t *bsz, size_t *bmax,
	int verb, const struct mft *p)
{
	size_t		 i;

	if (!str_buffer(b, bsz, bmax, verb, p->file)) {
		WARNX1(verb, "str_buffer");
		return 0;
	}
	if (!simple_buffer(b, bsz,
	    bmax, &p->filesz, sizeof(size_t))) {
		WARNX1(verb, "simple_buffer");
		return 0;
	}

	for (i = 0; i < p->filesz; i++) 
		if (!str_buffer(b, bsz, bmax, verb, p->files[i])) {
			WARNX1(verb, "str_buffer");
			return 0;
		}

	return 1;
}

/*
 * Read an MFT structure from the file descriptor.
 * Returns NULL on failure or the valid pointer otherwise.
 * Result must be passed to mft_free().
 */
struct mft *
mft_read(int fd, int verb)
{
	struct mft 	*p = NULL;
	size_t		 i;

	if ((p = calloc(1, sizeof(struct mft))) == NULL) {
		WARN("calloc");
		goto out;
	} else if (!str_read(fd, verb, &p->file)) {
		WARNX1(verb, "str_read");
		goto out;
	} else if (!simple_read(fd, verb, &p->filesz, sizeof(size_t))) {
		WARNX1(verb, "simple_read");
		goto out;
	} else if ((p->files = calloc(p->filesz, sizeof(char *))) == NULL) {
		WARN("calloc");
		goto out;
	}

	for (i = 0; i < p->filesz; i++) 
		if (!str_read(fd, verb, &p->files[i])) {
			WARNX1(verb, "str_read");
			goto out;
		}

	return p;
out:
	mft_free(p);
	return NULL;
}

