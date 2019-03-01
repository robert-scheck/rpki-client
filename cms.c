#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/cms.h>

#include "extern.h"

/*
 * Parse and validate a self-signed CMS message, where the signing X509
 * certificate has been signed by cacert.
 * The eContentType of the message must be an oid object.
 * Return the eContent as an octet string or NULL on failure.
 */
const ASN1_OCTET_STRING *
cms_parse_validate(int verb, X509 *cacert,
	const char *fn, const char *oid, const unsigned char *dgst)
{
	const ASN1_OBJECT  *obj;
	ASN1_OCTET_STRING **os = NULL;
	BIO 		   *bio = NULL, *shamd;
	CMS_ContentInfo    *cms;
	char 		    buf[128];
	int		    rc = 0, sz;
	STACK_OF(X509)	   *certs = NULL;
	X509		   *cert;
	EVP_MD		   *md;
	unsigned char	    mdbuf[EVP_MAX_MD_SIZE];

	if (NULL == (bio = BIO_new_file(fn, "rb"))) {
		CRYPTOX(verb, "%s: BIO_new_file", fn);
		goto out;
	}

	/*
	 * If we have a digest specified, create an MD chain that will
	 * automatically compute a digest during the CMS creation.
	 */

	if (dgst != NULL) {
		if ((shamd = BIO_new(BIO_f_md())) == NULL) {
			CRYPTOX(verb, "%s: BIO_new", fn);
			goto out;
		} else if (!BIO_set_md(shamd, EVP_sha256())) {
			CRYPTOX(verb, "%s: BIO_set_md", fn);
			goto out;
		}
		bio = BIO_push(shamd, bio);
	}

	if (NULL == (cms = d2i_CMS_bio(bio, NULL))) {
		CRYPTOX(verb, "%s: d2i_CMS_bio", fn);
		goto out;
	}

	/*
	 * If we have a digest, find it in the chain (we'll already have
	 * made it, so assert otherwise) and verify it.
	 */

	if (dgst != NULL) {
		shamd = BIO_find_type(bio, BIO_TYPE_MD);
		assert(shamd != NULL);
		if (!BIO_get_md(shamd, &md)) {
			CRYPTOX(verb, "%s: BIO_get_md", fn);
			goto out;
		}
		assert(EVP_MD_type(md) == NID_sha256);
		sz = BIO_gets(shamd, mdbuf, EVP_MAX_MD_SIZE);
		if (sz < 0) {
			CRYPTOX(verb, "%s: BIO_gets", fn);
			goto out;
		}
		assert(sz == SHA256_DIGEST_LENGTH);
		if (memcmp(mdbuf, dgst, SHA256_DIGEST_LENGTH)) {
			WARNX(verb, "%s: bad digest", fn);
			goto out;
		}
	}

	/* Check the CMS object's eContentType. */

	obj = CMS_get0_eContentType(cms);
	OBJ_obj2txt(buf, sizeof(buf), obj, 1);
	if (strcmp(buf, oid)) {
		WARNX(verb, "%s: incorrect OID value", fn);
		goto out;
	}

	/*
	 * The CMS is self-signed with a signing certifiate which must
	 * be signed by the public key.
	 */

	if (NULL != cacert) {
		if ( ! CMS_verify(cms, NULL, NULL, 
		     NULL, NULL, CMS_NO_SIGNER_CERT_VERIFY)) {
			CRYPTOX(verb, "%s: CMS_verify", fn);
			goto out;
		}
		LOG(verb, "%s: verified CMS", fn);

		certs = CMS_get0_signers(cms);
		if (NULL == certs ||
		    1 != sk_X509_num(certs)) {
			WARNX(verb, "%s: need single signer", fn);
			goto out;
		}

		cert = sk_X509_value(certs, 0);
		if (X509_verify(cert, X509_get_pubkey(cacert)) <= 0) {
			CRYPTOX(verb, "%s: X509_verify", fn);
			goto out;
		} 

		LOG(verb, "%s: verified signer", fn);
	}

	/* Extract eContents and pass to output function. */

	if (NULL == (os = CMS_get0_content(cms)) || NULL == *os)
		WARNX(verb, "%s: empty CMS content", fn);
	else
		rc = 1;
out:
	BIO_free_all(bio);
	sk_X509_free(certs);
	return rc ? *os : NULL;
}
