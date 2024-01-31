/* lkcapi_glue.c -- glue logic to register wolfCrypt implementations with
 * the Linux Kernel Cryptosystem
 *
 * Copyright (C) 2006-2024 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef LINUXKM_LKCAPI_REGISTER
    #error lkcapi_glue.c included in non-LINUXKM_LKCAPI_REGISTER project.
#endif

#if defined(LINUXKM_LKCAPI_REGISTER_AESGCM) && defined(WOLFSSL_AESNI) && \
    defined(WC_AES_C_DYNAMIC_FALLBACK)
    /* xxx temporary */
    #error LINUXKM_LKCAPI_REGISTER_AESGCM is incompatible with WOLFSSL_AESNI && WC_AES_C_DYNAMIC_FALLBACK
#endif

#if defined(LINUXKM_LKCAPI_REGISTER_AESGCM) && !defined(WOLFSSL_AESGCM_STREAM)
    #error LINUXKM_REGISTER_ALG requires AESGCM_STREAM.
#endif

#ifndef WOLFSSL_LINUXKM_LKCAPI_PRIORITY
/* Larger number means higher priority.  The highest in-tree priority is 4001,
 * in the Cavium driver.
 */
#define WOLFSSL_LINUXKM_LKCAPI_PRIORITY 10000
#endif

#ifndef NO_AES

#define WOLFKM_AESCBC_NAME   "cbc(aes)"
#define WOLFKM_AESCFB_NAME   "cfb(aes)"
#define WOLFKM_AESGCM_NAME   "gcm(aes)"
#define WOLFKM_AESXTS_NAME   "xts(aes)"

#ifdef WOLFSSL_AESNI
    #define WOLFKM_DRIVER_ISA_EXT "-aesni"
#else
    #define WOLFKM_DRIVER_ISA_EXT ""
#endif

#ifdef HAVE_FIPS
    #ifndef HAVE_FIPS_VERSION
        #define WOLFKM_DRIVER_FIPS "-fips-140"
    #elif HAVE_FIPS_VERSION >= 5
        #define WOLFKM_DRIVER_FIPS "-fips-140-3"
    #elif HAVE_FIPS_VERSION == 2
        #define WOLFKM_DRIVER_FIPS "-fips-140-2"
    #else
        #define WOLFKM_DRIVER_FIPS "-fips-140"
    #endif
#else
    #define WOLFKM_DRIVER_FIPS ""
#endif

#define WOLFKM_DRIVER_SUFFIX \
    WOLFKM_DRIVER_ISA_EXT WOLFKM_DRIVER_FIPS "-wolfcrypt"

#define WOLFKM_AESCBC_DRIVER ("cbc-aes" WOLFKM_DRIVER_SUFFIX)
#define WOLFKM_AESCFB_DRIVER ("cfb-aes" WOLFKM_DRIVER_SUFFIX)
#define WOLFKM_AESGCM_DRIVER ("gcm-aes" WOLFKM_DRIVER_SUFFIX)
#define WOLFKM_AESXTS_DRIVER ("xts-aes" WOLFKM_DRIVER_SUFFIX)

#if defined(HAVE_AES_CBC) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCBC))
static int  linuxkm_test_aescbc(void);
#endif
#if defined(WOLFSSL_AES_CFB) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCFB))
static int  linuxkm_test_aescfb(void);
#endif
#if defined(HAVE_AESGCM) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESGCM)) && \
    (! (defined(WOLFSSL_AESNI) && defined(WC_AES_C_DYNAMIC_FALLBACK)))
static int  linuxkm_test_aesgcm(void);
#endif
#if defined(WOLFSSL_AES_XTS) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESXTS))
static int  linuxkm_test_aesxts(void);
#endif

/* km_AesX(): wrappers to wolfcrypt wc_AesX functions and
 * structures.  */

#include <wolfssl/wolfcrypt/aes.h>

struct km_AesCtx {
    Aes          *aes_encrypt; /* allocated in km_AesInitCommon() to assure
                                * alignment, needed for AESNI.
                                */
    Aes          *aes_decrypt; /* same. */
};

#if defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
    defined(LINUXKM_LKCAPI_REGISTER_AESCBC) || \
    defined(LINUXKM_LKCAPI_REGISTER_AESCFB) || \
    defined(LINUXKM_LKCAPI_REGISTER_AESGCM)

static void km_AesExitCommon(struct km_AesCtx * ctx);

static int km_AesInitCommon(
    struct km_AesCtx * ctx,
    const char * name,
    int need_decryption)
{
    int err;

    ctx->aes_encrypt = (Aes *)malloc(sizeof(*ctx->aes_encrypt));

    if (! ctx->aes_encrypt) {
        pr_err("%s: allocation of %zu bytes for encryption key failed.\n",
               name, sizeof(*ctx->aes_encrypt));
        return MEMORY_E;
    }

    err = wc_AesInit(ctx->aes_encrypt, NULL, INVALID_DEVID);

    if (unlikely(err)) {
        pr_err("%s: wc_AesInit failed: %d\n", name, err);
        free(ctx->aes_encrypt);
        ctx->aes_encrypt = NULL;
        return -EINVAL;
    }

    if (! need_decryption) {
        ctx->aes_decrypt = NULL;
        return 0;
    }

    ctx->aes_decrypt = (Aes *)malloc(sizeof(*ctx->aes_decrypt));

    if (! ctx->aes_decrypt) {
        pr_err("%s: allocation of %zu bytes for decryption key failed.\n",
               name, sizeof(*ctx->aes_decrypt));
        km_AesExitCommon(ctx);
        return MEMORY_E;
    }

    err = wc_AesInit(ctx->aes_decrypt, NULL, INVALID_DEVID);

    if (unlikely(err)) {
        pr_err("%s: wc_AesInit failed: %d\n", name, err);
        free(ctx->aes_decrypt);
        ctx->aes_decrypt = NULL;
        km_AesExitCommon(ctx);
        return -EINVAL;
    }

    return 0;
}

static void km_AesExitCommon(struct km_AesCtx * ctx)
{
    if (ctx->aes_encrypt) {
        wc_AesFree(ctx->aes_encrypt);
        free(ctx->aes_encrypt);
        ctx->aes_encrypt = NULL;
    }
    if (ctx->aes_decrypt) {
        wc_AesFree(ctx->aes_decrypt);
        free(ctx->aes_decrypt);
        ctx->aes_decrypt = NULL;
    }
}

static int km_AesSetKeyCommon(struct km_AesCtx * ctx, const u8 *in_key,
                              unsigned int key_len, const char * name)
{
    int err;

    err = wc_AesSetKey(ctx->aes_encrypt, in_key, key_len, NULL, AES_ENCRYPTION);

    if (unlikely(err)) {
        pr_err("%s: wc_AesSetKey for encryption key failed: %d\n", name, err);
        return -ENOKEY;
    }

    if (ctx->aes_decrypt) {
        err = wc_AesSetKey(ctx->aes_decrypt, in_key, key_len, NULL,
                           AES_DECRYPTION);

        if (unlikely(err)) {
            pr_err("%s: wc_AesSetKey for decryption key failed: %d\n",
                   name, err);
            return -ENOKEY;
        }
    }

    return 0;
}

#if defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
    defined(LINUXKM_LKCAPI_REGISTER_AESCBC) || \
    defined(LINUXKM_LKCAPI_REGISTER_AESCFB)

static void km_AesExit(struct crypto_skcipher *tfm)
{
    struct km_AesCtx * ctx = crypto_skcipher_ctx(tfm);
    km_AesExitCommon(ctx);
}

#endif /* LINUXKM_LKCAPI_REGISTER_ALL ||
        * LINUXKM_LKCAPI_REGISTER_AESCBC ||
        * LINUXKM_LKCAPI_REGISTER_AESCFB
        */

#endif /* LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESCBC ||
        * LINUXKM_LKCAPI_REGISTER_AESCFB || LINUXKM_LKCAPI_REGISTER_AESGCM
        */

#if defined(HAVE_AES_CBC) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCBC))

static int km_AesCbcInit(struct crypto_skcipher *tfm)
{
    struct km_AesCtx * ctx = crypto_skcipher_ctx(tfm);
    return km_AesInitCommon(ctx, WOLFKM_AESCBC_DRIVER, 1);
}

static int km_AesCbcSetKey(struct crypto_skcipher *tfm, const u8 *in_key,
                          unsigned int key_len)
{
    struct km_AesCtx * ctx = crypto_skcipher_ctx(tfm);
    return km_AesSetKeyCommon(ctx, in_key, key_len, WOLFKM_AESCBC_DRIVER);
}

static int km_AesCbcEncrypt(struct skcipher_request *req)
{
    struct crypto_skcipher * tfm = NULL;
    struct km_AesCtx *       ctx = NULL;
    struct skcipher_walk     walk;
    unsigned int             nbytes = 0;
    int                      err = 0;

    tfm = crypto_skcipher_reqtfm(req);
    ctx = crypto_skcipher_ctx(tfm);

    err = skcipher_walk_virt(&walk, req, false);

    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_virt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return err;
    }

    while ((nbytes = walk.nbytes) != 0) {
        err = wc_AesSetIV(ctx->aes_encrypt, walk.iv);

        if (unlikely(err)) {
            pr_err("%s: wc_AesSetIV failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = wc_AesCbcEncrypt(ctx->aes_encrypt, walk.dst.virt.addr,
                               walk.src.virt.addr, nbytes);

        if (unlikely(err)) {
            pr_err("%s: wc_AesCbcEncrypt failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, walk.nbytes - nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return err;
        }
    }

    return err;
}

static int km_AesCbcDecrypt(struct skcipher_request *req)
{
    struct crypto_skcipher * tfm = NULL;
    struct km_AesCtx *       ctx = NULL;
    struct skcipher_walk     walk;
    unsigned int             nbytes = 0;
    int                      err = 0;

    tfm = crypto_skcipher_reqtfm(req);
    ctx = crypto_skcipher_ctx(tfm);

    err = skcipher_walk_virt(&walk, req, false);

    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_virt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return err;
    }

    while ((nbytes = walk.nbytes) != 0) {
        err = wc_AesSetIV(ctx->aes_decrypt, walk.iv);

        if (unlikely(err)) {
            pr_err("%s: wc_AesSetKey failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = wc_AesCbcDecrypt(ctx->aes_decrypt, walk.dst.virt.addr,
                               walk.src.virt.addr, nbytes);

        if (unlikely(err)) {
            pr_err("%s: wc_AesCbcDecrypt failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, walk.nbytes - nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return err;
        }
    }

    return err;
}

static struct skcipher_alg cbcAesAlg = {
    .base.cra_name        = WOLFKM_AESCBC_NAME,
    .base.cra_driver_name = WOLFKM_AESCBC_DRIVER,
    .base.cra_priority    = WOLFSSL_LINUXKM_LKCAPI_PRIORITY,
    .base.cra_blocksize   = AES_BLOCK_SIZE,
    .base.cra_ctxsize     = sizeof(struct km_AesCtx),
    .base.cra_module      = THIS_MODULE,
    .init                 = km_AesCbcInit,
    .exit                 = km_AesExit,
    .min_keysize          = AES_128_KEY_SIZE,
    .max_keysize          = AES_256_KEY_SIZE,
    .ivsize               = AES_BLOCK_SIZE,
    .setkey               = km_AesCbcSetKey,
    .encrypt              = km_AesCbcEncrypt,
    .decrypt              = km_AesCbcDecrypt,
};
static int cbcAesAlg_loaded = 0;

#endif /* HAVE_AES_CBC &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESCBC)
        */

#if defined(WOLFSSL_AES_CFB) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCFB))

static int km_AesCfbInit(struct crypto_skcipher *tfm)
{
    struct km_AesCtx * ctx = crypto_skcipher_ctx(tfm);
    return km_AesInitCommon(ctx, WOLFKM_AESCFB_DRIVER, 0);
}

static int km_AesCfbSetKey(struct crypto_skcipher *tfm, const u8 *in_key,
                          unsigned int key_len)
{
    struct km_AesCtx * ctx = crypto_skcipher_ctx(tfm);
    return km_AesSetKeyCommon(ctx, in_key, key_len, WOLFKM_AESCFB_DRIVER);
}

static int km_AesCfbEncrypt(struct skcipher_request *req)
{
    struct crypto_skcipher * tfm = NULL;
    struct km_AesCtx *       ctx = NULL;
    struct skcipher_walk     walk;
    unsigned int             nbytes = 0;
    int                      err = 0;

    tfm = crypto_skcipher_reqtfm(req);
    ctx = crypto_skcipher_ctx(tfm);

    err = skcipher_walk_virt(&walk, req, false);

    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_virt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return err;
    }

    while ((nbytes = walk.nbytes) != 0) {
        err = wc_AesSetIV(ctx->aes_encrypt, walk.iv);

        if (unlikely(err)) {
            pr_err("%s: wc_AesSetKey failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = wc_AesCfbEncrypt(ctx->aes_encrypt, walk.dst.virt.addr,
                               walk.src.virt.addr, nbytes);

        if (unlikely(err)) {
            pr_err("%s: wc_AesCfbEncrypt failed %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, walk.nbytes - nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return err;
        }
    }

    return err;
}

static int km_AesCfbDecrypt(struct skcipher_request *req)
{
    struct crypto_skcipher * tfm = NULL;
    struct km_AesCtx *       ctx = NULL;
    struct skcipher_walk     walk;
    unsigned int             nbytes = 0;
    int                      err = 0;

    tfm = crypto_skcipher_reqtfm(req);
    ctx = crypto_skcipher_ctx(tfm);

    err = skcipher_walk_virt(&walk, req, false);

    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_virt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return err;
    }

    while ((nbytes = walk.nbytes) != 0) {
        err = wc_AesSetIV(ctx->aes_encrypt, walk.iv);

        if (unlikely(err)) {
            pr_err("%s: wc_AesSetKey failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = wc_AesCfbDecrypt(ctx->aes_encrypt, walk.dst.virt.addr,
                               walk.src.virt.addr, nbytes);

        if (unlikely(err)) {
            pr_err("%s: wc_AesCfbDecrypt failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, walk.nbytes - nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return err;
        }
    }

    return err;
}

static struct skcipher_alg cfbAesAlg = {
    .base.cra_name        = WOLFKM_AESCFB_NAME,
    .base.cra_driver_name = WOLFKM_AESCFB_DRIVER,
    .base.cra_priority    = WOLFSSL_LINUXKM_LKCAPI_PRIORITY,
    .base.cra_blocksize   = AES_BLOCK_SIZE,
    .base.cra_ctxsize     = sizeof(struct km_AesCtx),
    .base.cra_module      = THIS_MODULE,
    .init                 = km_AesCfbInit,
    .exit                 = km_AesExit,
    .min_keysize          = AES_128_KEY_SIZE,
    .max_keysize          = AES_256_KEY_SIZE,
    .ivsize               = AES_BLOCK_SIZE,
    .setkey               = km_AesCfbSetKey,
    .encrypt              = km_AesCfbEncrypt,
    .decrypt              = km_AesCfbDecrypt,
};
static int cfbAesAlg_loaded = 0;

#endif /* WOLFSSL_AES_CFB &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESCBC)
        */

#if defined(HAVE_AESGCM) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESGCM)) && \
    (! (defined(WOLFSSL_AESNI) && defined(WC_AES_C_DYNAMIC_FALLBACK)))

static int km_AesGcmInit(struct crypto_aead * tfm)
{
    struct km_AesCtx * ctx = crypto_aead_ctx(tfm);
    return km_AesInitCommon(ctx, WOLFKM_AESGCM_DRIVER, 0);
}

static void km_AesGcmExit(struct crypto_aead * tfm)
{
    struct km_AesCtx * ctx = crypto_aead_ctx(tfm);
    km_AesExitCommon(ctx);
}

static int km_AesGcmSetKey(struct crypto_aead *tfm, const u8 *in_key,
                           unsigned int key_len)
{
    int err;
    struct km_AesCtx * ctx = crypto_aead_ctx(tfm);

    err = wc_AesGcmSetKey(ctx->aes_encrypt, in_key, key_len);

    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmSetKey failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -ENOKEY;
    }

    return 0;
}

static int km_AesGcmSetAuthsize(struct crypto_aead *tfm, unsigned int authsize)
{
    (void)tfm;
    if (authsize > AES_BLOCK_SIZE ||
        authsize < WOLFSSL_MIN_AUTH_TAG_SZ) {
        pr_err("%s: invalid authsize: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), authsize);
        return -EINVAL;
    }
    return 0;
}

/*
 * aead ciphers recieve data in scatterlists in following order:
 *   encrypt
 *     req->src: aad||plaintext
 *     req->dst: aad||ciphertext||tag
 *   decrypt
 *     req->src: aad||ciphertext||tag
 *     req->dst: aad||plaintext, return 0 or -EBADMSG
 */

static int km_AesGcmEncrypt(struct aead_request *req)
{
    struct crypto_aead * tfm = NULL;
    struct km_AesCtx *   ctx = NULL;
    struct skcipher_walk walk;
    struct scatter_walk  assocSgWalk;
    unsigned int         nbytes = 0;
    u8                   authTag[AES_BLOCK_SIZE];
    int                  err = 0;
    unsigned int         assocLeft = 0;
    unsigned int         cryptLeft = 0;
    u8 *                 assoc = NULL;

    tfm = crypto_aead_reqtfm(req);
    ctx = crypto_aead_ctx(tfm);
    assocLeft = req->assoclen;
    cryptLeft = req->cryptlen;

    scatterwalk_start(&assocSgWalk, req->src);

    err = skcipher_walk_aead_encrypt(&walk, req, false);
    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_aead_encrypt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -1;
    }

    err = wc_AesGcmInit(ctx->aes_encrypt, NULL /*key*/, 0 /*keylen*/, walk.iv,
                        AES_BLOCK_SIZE);
    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmInit failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -EINVAL;
    }

    assoc = scatterwalk_map(&assocSgWalk);
    if (unlikely(IS_ERR(assoc))) {
        pr_err("%s: scatterwalk_map failed: %ld\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)),
               PTR_ERR(assoc));
        return err;
    }

    err = wc_AesGcmEncryptUpdate(ctx->aes_encrypt, NULL, NULL, 0,
                                 assoc, assocLeft);
    assocLeft -= assocLeft;
    scatterwalk_unmap(assoc);
    assoc = NULL;

    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmEncryptUpdate failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -EINVAL;
    }

    while ((nbytes = walk.nbytes) != 0) {
        int n = nbytes;

        if (likely(cryptLeft && nbytes)) {
            n = cryptLeft < nbytes ? cryptLeft : nbytes;

            err = wc_AesGcmEncryptUpdate(
                ctx->aes_encrypt,
                walk.dst.virt.addr,
                walk.src.virt.addr,
                cryptLeft,
                NULL, 0);
            nbytes -= n;
            cryptLeft -= n;
        }

        if (unlikely(err)) {
            pr_err("%s: wc_AesGcmEncryptUpdate failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
            return err;
        }
    }

    err = wc_AesGcmEncryptFinal(ctx->aes_encrypt, authTag, tfm->authsize);
    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmEncryptFinal failed with return code %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -EINVAL;
    }

    /* Now copy the auth tag into request scatterlist. */
    scatterwalk_map_and_copy(authTag, req->dst,
                             req->assoclen + req->cryptlen,
                             tfm->authsize, 1);

    return err;
}

static int km_AesGcmDecrypt(struct aead_request *req)
{
    struct crypto_aead * tfm = NULL;
    struct km_AesCtx *   ctx = NULL;
    struct skcipher_walk walk;
    struct scatter_walk  assocSgWalk;
    unsigned int         nbytes = 0;
    u8                   origAuthTag[AES_BLOCK_SIZE];
    int                  err = 0;
    unsigned int         assocLeft = 0;
    unsigned int         cryptLeft = 0;
    u8 *                 assoc = NULL;

    tfm = crypto_aead_reqtfm(req);
    ctx = crypto_aead_ctx(tfm);
    assocLeft = req->assoclen;
    cryptLeft = req->cryptlen - tfm->authsize;

    /* Copy out original auth tag from req->src. */
    scatterwalk_map_and_copy(origAuthTag, req->src,
                             req->assoclen + req->cryptlen - tfm->authsize,
                             tfm->authsize, 0);

    scatterwalk_start(&assocSgWalk, req->src);

    err = skcipher_walk_aead_decrypt(&walk, req, false);
    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_aead_decrypt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return err;
    }

    err = wc_AesGcmInit(ctx->aes_encrypt, NULL /*key*/, 0 /*keylen*/, walk.iv,
                        AES_BLOCK_SIZE);
    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmInit failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -EINVAL;
    }

    assoc = scatterwalk_map(&assocSgWalk);
    if (unlikely(IS_ERR(assoc))) {
        pr_err("%s: scatterwalk_map failed: %ld\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)),
               PTR_ERR(assoc));
        return err;
    }

    err = wc_AesGcmDecryptUpdate(ctx->aes_encrypt, NULL, NULL, 0,
                                 assoc, assocLeft);
    assocLeft -= assocLeft;
    scatterwalk_unmap(assoc);
    assoc = NULL;

    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmDecryptUpdate failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
        return -EINVAL;
    }

    while ((nbytes = walk.nbytes) != 0) {
        int n = nbytes;

        if (likely(cryptLeft && nbytes)) {
            n = cryptLeft < nbytes ? cryptLeft : nbytes;

            err = wc_AesGcmDecryptUpdate(
                ctx->aes_encrypt,
                walk.dst.virt.addr,
                walk.src.virt.addr,
                cryptLeft,
                NULL, 0);
            nbytes -= n;
            cryptLeft -= n;
        }

        if (unlikely(err)) {
            pr_err("%s: wc_AesGcmDecryptUpdate failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);
            return err;
        }
    }

    err = wc_AesGcmDecryptFinal(ctx->aes_encrypt, origAuthTag, tfm->authsize);
    if (unlikely(err)) {
        pr_err("%s: wc_AesGcmDecryptFinal failed with return code %d\n",
               crypto_tfm_alg_driver_name(crypto_aead_tfm(tfm)), err);

        if (err == AES_GCM_AUTH_E) {
            return -EBADMSG;
        }
        else {
            return -EINVAL;
        }
    }

    return err;
}

static struct aead_alg gcmAesAead = {
    .base.cra_name        = WOLFKM_AESGCM_NAME,
    .base.cra_driver_name = WOLFKM_AESGCM_DRIVER,
    .base.cra_priority    = WOLFSSL_LINUXKM_LKCAPI_PRIORITY,
    .base.cra_blocksize   = 1,
    .base.cra_ctxsize     = sizeof(struct km_AesCtx),
    .base.cra_module      = THIS_MODULE,
    .init                 = km_AesGcmInit,
    .exit                 = km_AesGcmExit,
    .setkey               = km_AesGcmSetKey,
    .setauthsize          = km_AesGcmSetAuthsize,
    .encrypt              = km_AesGcmEncrypt,
    .decrypt              = km_AesGcmDecrypt,
    .ivsize               = AES_BLOCK_SIZE,
    .maxauthsize          = AES_BLOCK_SIZE,
    .chunksize            = AES_BLOCK_SIZE,
};
static int gcmAesAead_loaded = 0;

#endif /* HAVE_AESGCM &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESGCM) &&
        * (! (WOLFSSL_AESNI && WC_AES_C_DYNAMIC_FALLBACK))
        */

#if defined(WOLFSSL_AES_XTS) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESXTS))

struct km_AesXtsCtx {
    XtsAes *aesXts; /* allocated in km_AesXtsInitCommon() to assure alignment
                     * for AESNI.
                     */
};

static int km_AesXtsInitCommon(struct km_AesXtsCtx * ctx, const char * name)
{
    int err;

    ctx->aesXts = (XtsAes *)malloc(sizeof(*ctx->aesXts));

    if (! ctx->aesXts)
        return -MEMORY_E;

    err = wc_AesXtsInit(ctx->aesXts, NULL, INVALID_DEVID);

    if (unlikely(err)) {
        pr_err("%s: km_AesXtsInitCommon failed: %d\n", name, err);
        return -EINVAL;
    }

    return 0;
}

static int km_AesXtsInit(struct crypto_skcipher *tfm)
{
    struct km_AesXtsCtx * ctx = crypto_skcipher_ctx(tfm);
    return km_AesXtsInitCommon(ctx, WOLFKM_AESXTS_DRIVER);
}

static void km_AesXtsExit(struct crypto_skcipher *tfm)
{
    struct km_AesXtsCtx * ctx = crypto_skcipher_ctx(tfm);
    wc_AesXtsFree(ctx->aesXts);
    free(ctx->aesXts);
    ctx->aesXts = NULL;
}

static int km_AesXtsSetKey(struct crypto_skcipher *tfm, const u8 *in_key,
                          unsigned int key_len)
{
    int err;
    struct km_AesXtsCtx * ctx = crypto_skcipher_ctx(tfm);

    err = wc_AesXtsSetKeyNoInit(ctx->aesXts, in_key, key_len,
                                AES_ENCRYPTION_AND_DECRYPTION);

    if (unlikely(err)) {
        pr_err("%s: wc_AesXtsSetKeyNoInit failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return -EINVAL;
    }

    return 0;
}

/* see /usr/src/linux/drivers/md/dm-crypt.c */

static int km_AesXtsEncrypt(struct skcipher_request *req)
{
    int                      err = 0;

    struct crypto_skcipher * tfm = NULL;
    struct km_AesXtsCtx *    ctx = NULL;
    struct skcipher_walk     walk;
    unsigned int             nbytes = 0;

    tfm = crypto_skcipher_reqtfm(req);
    ctx = crypto_skcipher_ctx(tfm);

    err = skcipher_walk_virt(&walk, req, false);

    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_virt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return err;
    }

    while ((nbytes = walk.nbytes) != 0) {
        err = wc_AesXtsEncrypt(ctx->aesXts, walk.dst.virt.addr,
                               walk.src.virt.addr, nbytes,
                               walk.iv, walk.ivsize);

        if (unlikely(err)) {
            pr_err("%s: wc_AesXtsEncrypt failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, walk.nbytes - nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return err;
        }
    }

    return err;
}

static int km_AesXtsDecrypt(struct skcipher_request *req)
{
    int                      err = 0;
    struct crypto_skcipher * tfm = NULL;
    struct km_AesXtsCtx *    ctx = NULL;
    struct skcipher_walk     walk;
    unsigned int             nbytes = 0;

    tfm = crypto_skcipher_reqtfm(req);
    ctx = crypto_skcipher_ctx(tfm);

    err = skcipher_walk_virt(&walk, req, false);

    if (unlikely(err)) {
        pr_err("%s: skcipher_walk_virt failed: %d\n",
               crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
        return err;
    }

    while ((nbytes = walk.nbytes) != 0) {
        err = wc_AesXtsDecrypt(ctx->aesXts, walk.dst.virt.addr,
                               walk.src.virt.addr, nbytes,
                               walk.iv, walk.ivsize);

        if (unlikely(err)) {
            pr_err("%s: wc_AesCbcDecrypt failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return -EINVAL;
        }

        err = skcipher_walk_done(&walk, walk.nbytes - nbytes);

        if (unlikely(err)) {
            pr_err("%s: skcipher_walk_done failed: %d\n",
                   crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm)), err);
            return err;
        }
    }

    return err;
}

static struct skcipher_alg xtsAesAlg = {
    .base.cra_name          = WOLFKM_AESXTS_NAME,
    .base.cra_driver_name   = WOLFKM_AESXTS_DRIVER,
    .base.cra_priority      = WOLFSSL_LINUXKM_LKCAPI_PRIORITY,
    .base.cra_blocksize     = AES_BLOCK_SIZE,
    .base.cra_ctxsize       = sizeof(struct km_AesXtsCtx),
    .base.cra_module        = THIS_MODULE,

    .min_keysize            = 2 * AES_128_KEY_SIZE,
    .max_keysize            = 2 * AES_256_KEY_SIZE,
    .ivsize                 = AES_BLOCK_SIZE,
    .walksize               = 2 * AES_BLOCK_SIZE,
    .init                   = km_AesXtsInit,
    .exit                   = km_AesXtsExit,
    .setkey                 = km_AesXtsSetKey,
    .encrypt                = km_AesXtsEncrypt,
    .decrypt                = km_AesXtsDecrypt
};
static int xtsAesAlg_loaded = 0;

#endif /* WOLFSSL_AES_XTS &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESXTS)
        */

/* cipher tests, cribbed from test.c, with supplementary LKCAPI tests: */

#if defined(HAVE_AES_CBC) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCBC))

static int linuxkm_test_aescbc(void)
{
    int     ret = 0;
    struct crypto_skcipher *  tfm = NULL;
    struct skcipher_request * req = NULL;
    struct scatterlist        src, dst;
    Aes     aes;
    WOLFSSL_SMALL_STACK_STATIC const byte key32[] =
    {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66
    };
    WOLFSSL_SMALL_STACK_STATIC const byte vector[] =
    /* Now is the time for all good men w/o trailing 0 */
    {
        0x4e,0x6f,0x77,0x20,0x69,0x73,0x20,0x74,
        0x68,0x65,0x20,0x74,0x69,0x6d,0x65,0x20,
        0x66,0x6f,0x72,0x20,0x61,0x6c,0x6c,0x20,
        0x67,0x6f,0x6f,0x64,0x20,0x6d,0x65,0x6e
    };
    WOLFSSL_SMALL_STACK_STATIC const byte iv[] = "1234567890abcdef";
    byte    iv_copy[sizeof(iv)];
    byte    enc[sizeof(vector)];
    byte    dec[sizeof(vector)];
    u8 *    enc2 = NULL;
    u8 *    dec2 = NULL;

    XMEMSET(enc, 0, sizeof(enc));
    XMEMSET(dec, 0, sizeof(enc));

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret) {
        pr_err("wolfcrypt wc_AesInit failed with return code %d.\n", ret);
        return ret;
    }

    ret = wc_AesSetKey(&aes, key32, AES_BLOCK_SIZE * 2, iv, AES_ENCRYPTION);
    if (ret) {
        pr_err("wolfcrypt wc_AesSetKey failed with return code %d\n", ret);
        return ret;
    }

    ret = wc_AesCbcEncrypt(&aes, enc, vector, sizeof(vector));
    if (ret) {
        pr_err("wolfcrypt wc_AesCbcEncrypt failed with return code %d\n", ret);
        return ret;
    }

    /* Re init for decrypt and set flag. */
    wc_AesFree(&aes);

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret) {
        pr_err("wolfcrypt wc_AesInit failed with return code %d.\n", ret);
        return ret;
    }

    ret = wc_AesSetKey(&aes, key32, AES_BLOCK_SIZE * 2, iv, AES_DECRYPTION);
    if (ret) {
        pr_err("wolfcrypt wc_AesSetKey failed with return code %d.\n", ret);
        return ret;
    }

    ret = wc_AesCbcDecrypt(&aes, dec, enc, sizeof(vector));
    if (ret) {
        pr_err("wolfcrypt wc_AesCbcDecrypt failed with return code %d\n", ret);
        return ret;
    }

    ret = XMEMCMP(vector, dec, sizeof(vector));
    if (ret) {
        pr_err("error: vector and dec do not match: %d\n", ret);
        return ret;
    }

    /* now the kernel crypto part */
    enc2 = kmalloc(sizeof(vector), GFP_KERNEL);
    if (!enc2) {
        pr_err("error: kmalloc failed\n");
        goto test_cbc_end;
    }

    dec2 = kmalloc(sizeof(vector), GFP_KERNEL);
    if (!dec2) {
        pr_err("error: kmalloc failed\n");
        goto test_cbc_end;
    }

    memcpy(dec2, vector, sizeof(vector));

    tfm = crypto_alloc_skcipher(WOLFKM_AESCBC_DRIVER, 0, 0);
    if (IS_ERR(tfm)) {
        pr_err("error: allocating AES skcipher algorithm %s failed: %ld\n",
               WOLFKM_AESCBC_DRIVER, PTR_ERR(tfm));
        goto test_cbc_end;
    }

    ret = crypto_skcipher_setkey(tfm, key32, AES_BLOCK_SIZE * 2);
    if (ret) {
        pr_err("error: crypto_skcipher_setkey returned: %d\n", ret);
        goto test_cbc_end;
    }

    req = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (IS_ERR(req)) {
        pr_err("error: allocating AES skcipher request %s failed\n",
               WOLFKM_AESCBC_DRIVER);
        goto test_cbc_end;
    }

    sg_init_one(&src, dec2, sizeof(vector));
    sg_init_one(&dst, enc2, sizeof(vector));

    XMEMCPY(iv_copy, iv, sizeof(iv));
    skcipher_request_set_crypt(req, &src, &dst, sizeof(vector), iv_copy);

    ret = crypto_skcipher_encrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_encrypt returned: %d\n", ret);
        goto test_cbc_end;
    }

    ret = XMEMCMP(enc, enc2, sizeof(vector));
    if (ret) {
        pr_err("error: enc and enc2 do not match: %d\n", ret);
        goto test_cbc_end;
    }

    memset(dec2, 0, sizeof(vector));
    sg_init_one(&src, enc2, sizeof(vector));
    sg_init_one(&dst, dec2, sizeof(vector));

    XMEMCPY(iv_copy, iv, sizeof(iv));
    skcipher_request_set_crypt(req, &src, &dst, sizeof(vector), iv_copy);

    ret = crypto_skcipher_decrypt(req);

    if (ret) {
        pr_err("ERROR: crypto_skcipher_decrypt returned %d\n", ret);
        goto test_cbc_end;
    }

    ret = XMEMCMP(dec, dec2, sizeof(vector));
    if (ret) {
        pr_err("error: dec and dec2 do not match: %d\n", ret);
        goto test_cbc_end;
    }

test_cbc_end:

    if (enc2) { kfree(enc2); enc2 = NULL; }
    if (dec2) { kfree(dec2); dec2 = NULL; }
    if (req) { skcipher_request_free(req); req = NULL; }
    if (tfm) { crypto_free_skcipher(tfm); tfm = NULL; }

    return ret;
}

#endif /* HAVE_AES_CBC &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESCBC)
        */

#if defined(WOLFSSL_AES_CFB) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCFB))

static int linuxkm_test_aescfb(void)
{
    int ret = 0;
    struct crypto_skcipher *  tfm = NULL;
    struct skcipher_request * req = NULL;
    struct scatterlist        src, dst;
    Aes     aes;
    WOLFSSL_SMALL_STACK_STATIC const byte key32[] =
    {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66
    };
    WOLFSSL_SMALL_STACK_STATIC const byte vector[] =
    /* Now is the time for all good men w/o trailing 0 */
    {
        0x4e,0x6f,0x77,0x20,0x69,0x73,0x20,0x74,
        0x68,0x65,0x20,0x74,0x69,0x6d,0x65,0x20,
        0x66,0x6f,0x72,0x20,0x61,0x6c,0x6c,0x20,
        0x67,0x6f,0x6f,0x64,0x20,0x6d,0x65,0x6e
    };
    WOLFSSL_SMALL_STACK_STATIC const byte iv[] = "1234567890abcdef";
    byte    iv_copy[sizeof(iv)];
    byte    enc[sizeof(vector)];
    byte    dec[sizeof(vector)];
    u8 *    enc2 = NULL;
    u8 *    dec2 = NULL;

    XMEMSET(enc, 0, sizeof(enc));
    XMEMSET(dec, 0, sizeof(enc));

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret) {
        pr_err("wolfcrypt wc_AesInit failed with return code %d.\n", ret);
        return ret;
    }

    ret = wc_AesSetKey(&aes, key32, AES_BLOCK_SIZE * 2, iv, AES_ENCRYPTION);
    if (ret) {
        pr_err("wolfcrypt wc_AesSetKey failed with return code %d\n", ret);
        return ret;
    }

    ret = wc_AesCfbEncrypt(&aes, enc, vector, sizeof(vector));
    if (ret) {
        pr_err("wolfcrypt wc_AesCfbEncrypt failed with return code %d\n", ret);
        return ret;
    }

    /* Re init for decrypt and set flag. */
    wc_AesFree(&aes);

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret) {
        pr_err("wolfcrypt wc_AesInit failed with return code %d.\n", ret);
        return ret;
    }

    ret = wc_AesSetKey(&aes, key32, AES_BLOCK_SIZE * 2, iv, AES_ENCRYPTION);
    if (ret) {
        pr_err("wolfcrypt wc_AesSetKey failed with return code %d.\n", ret);
        return ret;
    }

    ret = wc_AesCfbDecrypt(&aes, dec, enc, sizeof(vector));
    if (ret) {
        pr_err("wolfcrypt wc_AesCfbDecrypt failed with return code %d\n", ret);
        return ret;
    }

    ret = XMEMCMP(vector, dec, sizeof(vector));
    if (ret) {
        pr_err("error: vector and dec do not match: %d\n", ret);
        return ret;
    }

    /* now the kernel crypto part */
    enc2 = kmalloc(sizeof(vector), GFP_KERNEL);
    if (!enc2) {
        pr_err("error: kmalloc failed\n");
        goto test_cfb_end;
    }

    dec2 = kmalloc(sizeof(vector), GFP_KERNEL);
    if (!dec2) {
        pr_err("error: kmalloc failed\n");
        goto test_cfb_end;
    }

    memcpy(dec2, vector, sizeof(vector));

    tfm = crypto_alloc_skcipher(WOLFKM_AESCFB_DRIVER, 0, 0);
    if (IS_ERR(tfm)) {
        pr_err("error: allocating AES skcipher algorithm %s failed: %ld\n",
               WOLFKM_AESCFB_DRIVER, PTR_ERR(tfm));
        goto test_cfb_end;
    }

    ret = crypto_skcipher_setkey(tfm, key32, AES_BLOCK_SIZE * 2);
    if (ret) {
        pr_err("error: crypto_skcipher_setkey returned: %d\n", ret);
        goto test_cfb_end;
    }

    req = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (IS_ERR(req)) {
        pr_err("error: allocating AES skcipher request %s failed\n",
               WOLFKM_AESCFB_DRIVER);
        goto test_cfb_end;
    }

    sg_init_one(&src, dec2, sizeof(vector));
    sg_init_one(&dst, enc2, sizeof(vector));

    XMEMCPY(iv_copy, iv, sizeof(iv));
    skcipher_request_set_crypt(req, &src, &dst, sizeof(vector), iv_copy);

    ret = crypto_skcipher_encrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_encrypt returned: %d\n", ret);
        goto test_cfb_end;
    }

    ret = XMEMCMP(enc, enc2, sizeof(vector));
    if (ret) {
        pr_err("error: enc and enc2 do not match: %d\n", ret);
        goto test_cfb_end;
    }

    memset(dec2, 0, sizeof(vector));
    sg_init_one(&src, enc2, sizeof(vector));
    sg_init_one(&dst, dec2, sizeof(vector));

    XMEMCPY(iv_copy, iv, sizeof(iv));
    skcipher_request_set_crypt(req, &src, &dst, sizeof(vector), iv_copy);

    ret = crypto_skcipher_decrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_decrypt returned: %d\n", ret);
        goto test_cfb_end;
    }

    ret = XMEMCMP(dec, dec2, sizeof(vector));
    if (ret) {
        pr_err("error: dec and dec2 do not match: %d\n", ret);
        goto test_cfb_end;
    }

test_cfb_end:

    if (enc2) { kfree(enc2); enc2 = NULL; }
    if (dec2) { kfree(dec2); dec2 = NULL; }
    if (req) { skcipher_request_free(req); req = NULL; }
    if (tfm) { crypto_free_skcipher(tfm); tfm = NULL; }

    return ret;
}

#endif /* WOLFSSL_AES_CFB &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESCFB)
        */

#if defined(HAVE_AESGCM) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESGCM)) &&                        \
    (! (defined(WOLFSSL_AESNI) && defined(WC_AES_C_DYNAMIC_FALLBACK)))

static int linuxkm_test_aesgcm(void)
{
    int     ret = 0;
    struct crypto_aead *  tfm = NULL;
    struct aead_request * req = NULL;
    struct scatterlist *  src = NULL;
    struct scatterlist *  dst = NULL;
    Aes     aes;
    WOLFSSL_SMALL_STACK_STATIC const byte key32[] =
    {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66
    };
    WOLFSSL_SMALL_STACK_STATIC const byte vector[] =
    /* Now is the time for all w/o trailing 0 */
    {
        0x4e,0x6f,0x77,0x20,0x69,0x73,0x20,0x74,
        0x68,0x65,0x20,0x74,0x69,0x6d,0x65,0x20,
        0x66,0x6f,0x72,0x20,0x61,0x6c,0x6c,0x20
    };
    WOLFSSL_SMALL_STACK_STATIC const byte assoc[] =
    {
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
        0xab, 0xad, 0xda, 0xd2
    };
    WOLFSSL_SMALL_STACK_STATIC const byte ivstr[] = "1234567890abcdef";
    byte    enc[sizeof(vector)];
    byte    authTag[AES_BLOCK_SIZE];
    byte    dec[sizeof(vector)];
    u8 *    assoc2 = NULL;
    u8 *    enc2 = NULL;
    u8 *    dec2 = NULL;
    u8 *    iv = NULL;
    size_t  encryptLen = sizeof(vector);
    size_t  decryptLen = sizeof(vector) + sizeof(authTag);

    /* Init stack variables. */
    XMEMSET(enc, 0, sizeof(vector));
    XMEMSET(dec, 0, sizeof(vector));
    XMEMSET(authTag, 0, AES_BLOCK_SIZE);

    ret = wc_AesInit(&aes, NULL, INVALID_DEVID);
    if (ret) {
        pr_err("error: wc_AesInit failed with return code %d.\n", ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmInit(&aes, key32, sizeof(key32)/sizeof(byte), ivstr,
                        AES_BLOCK_SIZE);
    if (ret) {
        pr_err("error: wc_AesGcmInit failed with return code %d.\n", ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmEncryptUpdate(&aes, NULL, NULL, 0, assoc, sizeof(assoc));
    if (ret) {
        pr_err("error: wc_AesGcmEncryptUpdate failed with return code %d\n",
               ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmEncryptUpdate(&aes, enc, vector, sizeof(vector), NULL, 0);
    if (ret) {
        pr_err("error: wc_AesGcmEncryptUpdate failed with return code %d\n",
               ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmEncryptFinal(&aes, authTag, AES_BLOCK_SIZE);
    if (ret) {
        pr_err("error: wc_AesGcmEncryptFinal failed with return code %d\n",
               ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmInit(&aes, key32, sizeof(key32)/sizeof(byte), ivstr,
                        AES_BLOCK_SIZE);
    if (ret) {
        pr_err("error: wc_AesGcmInit failed with return code %d.\n", ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmDecryptUpdate(&aes, dec, enc, sizeof(vector),
                                 assoc, sizeof(assoc));
    if (ret) {
        pr_err("error: wc_AesGcmDecryptUpdate failed with return code %d\n",
               ret);
        goto test_gcm_end;
    }

    ret = wc_AesGcmDecryptFinal(&aes, authTag, AES_BLOCK_SIZE);
    if (ret) {
        pr_err("error: wc_AesGcmEncryptFinal failed with return code %d\n",
               ret);
        goto test_gcm_end;
    }

    ret = XMEMCMP(vector, dec, sizeof(vector));
    if (ret) {
        pr_err("error: gcm: vector and dec do not match: %d\n", ret);
        goto test_gcm_end;
    }

    /* now the kernel crypto part */
    assoc2 = kmalloc(sizeof(assoc), GFP_KERNEL);
    if (IS_ERR(assoc2)) {
        pr_err("error: kmalloc failed\n");
        goto test_gcm_end;
    }
    memset(assoc2, 0, sizeof(assoc));
    memcpy(assoc2, assoc, sizeof(assoc));

    iv = kmalloc(AES_BLOCK_SIZE, GFP_KERNEL);
    if (IS_ERR(iv)) {
        pr_err("error: kmalloc failed\n");
        goto test_gcm_end;
    }
    memset(iv, 0, AES_BLOCK_SIZE);
    memcpy(iv, ivstr, AES_BLOCK_SIZE);

    enc2 = kmalloc(decryptLen, GFP_KERNEL);
    if (IS_ERR(enc2)) {
        pr_err("error: kmalloc failed\n");
        goto test_gcm_end;
    }

    dec2 = kmalloc(decryptLen, GFP_KERNEL);
    if (IS_ERR(dec2)) {
        pr_err("error: kmalloc failed\n");
        goto test_gcm_end;
    }

    memset(enc2, 0, decryptLen);
    memset(dec2, 0, decryptLen);
    memcpy(dec2, vector, sizeof(vector));

    tfm = crypto_alloc_aead(WOLFKM_AESGCM_DRIVER, 0, 0);
    if (IS_ERR(tfm)) {
        pr_err("error: allocating AES skcipher algorithm %s failed: %ld\n",
               WOLFKM_AESGCM_DRIVER, PTR_ERR(tfm));
        goto test_gcm_end;
    }

    ret = crypto_aead_setkey(tfm, key32, AES_BLOCK_SIZE * 2);
    if (ret) {
        pr_err("error: crypto_aead_setkey returned: %d\n", ret);
        goto test_gcm_end;
    }

    ret = crypto_aead_setauthsize(tfm, sizeof(authTag));
    if (ret) {
        pr_err("error: crypto_aead_setauthsize returned: %d\n", ret);
        goto test_gcm_end;
    }

    req = aead_request_alloc(tfm, GFP_KERNEL);
    if (IS_ERR(req)) {
        pr_err("error: allocating AES aead request %s failed: %ld\n",
               WOLFKM_AESCBC_DRIVER, PTR_ERR(req));
        goto test_gcm_end;
    }

    src = kmalloc(sizeof(struct scatterlist) * 2, GFP_KERNEL);
    dst = kmalloc(sizeof(struct scatterlist) * 2, GFP_KERNEL);

    if (IS_ERR(src) || IS_ERR(dst)) {
        pr_err("error: kmalloc src or dst failed: %ld, %ld\n",
               PTR_ERR(src), PTR_ERR(dst));
        goto test_gcm_end;
    }

    sg_init_table(src, 2);
    sg_set_buf(src, assoc2, sizeof(assoc));
    sg_set_buf(&src[1], dec2, sizeof(vector));

    sg_init_table(dst, 2);
    sg_set_buf(dst, assoc2, sizeof(assoc));
    sg_set_buf(&dst[1], enc2, decryptLen);

    aead_request_set_callback(req, 0, NULL, NULL);
    aead_request_set_ad(req, sizeof(assoc));
    aead_request_set_crypt(req, src, dst, sizeof(vector), iv);

    ret = crypto_aead_encrypt(req);

    if (ret) {
        pr_err("error: crypto_aead_encrypt returned: %d\n", ret);
        goto test_gcm_end;
    }

    ret = XMEMCMP(enc, enc2, sizeof(vector));
    if (ret) {
        pr_err("error: enc and enc2 do not match: %d\n", ret);
        goto test_gcm_end;
    }

    ret = XMEMCMP(authTag, enc2 + encryptLen, sizeof(authTag));
    if (ret) {
        pr_err("error: authTags do not match: %d\n", ret);
        goto test_gcm_end;
    }

    /* Now decrypt crypto request. Reverse src and dst. */
    memset(dec2, 0, decryptLen);
    aead_request_set_ad(req, sizeof(assoc));
    aead_request_set_crypt(req, dst, src, decryptLen, iv);

    ret = crypto_aead_decrypt(req);

    if (ret) {
        pr_err("error: crypto_aead_decrypt returned: %d\n", ret);
        goto test_gcm_end;
    }

    ret = XMEMCMP(dec, dec2, sizeof(vector));
    if (ret) {
        pr_err("error: dec and dec2 do not match: %d\n", ret);
        goto test_gcm_end;
    }

test_gcm_end:
    if (req) { aead_request_free(req); req = NULL; }
    if (tfm) { crypto_free_aead(tfm); tfm = NULL; }

    if (src) { kfree(src); src = NULL; }
    if (dst) { kfree(dst); dst = NULL; }

    if (dec2) { kfree(dec2); dec2 = NULL; }
    if (enc2) { kfree(enc2); enc2 = NULL; }

    if (assoc2) { kfree(assoc2); assoc2 = NULL; }
    if (iv) { kfree(iv); iv = NULL; }

    return ret;
}

#endif /* HAVE_AESGCM &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESGCM) &&
        * (! (WOLFSSL_AESNI && WC_AES_C_DYNAMIC_FALLBACK))
        */

#if defined(WOLFSSL_AES_XTS) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESXTS))

#ifndef HEAP_HINT
#define HEAP_HINT NULL
#endif

#ifndef ERROR_OUT
#define ERROR_OUT(err, eLabel) do { ret = (err); goto eLabel; } while (0)
#endif

/* note the FIPS code will be returned on failure even in non-FIPS builds. */
#define LINUXKM_LKCAPI_AES_KAT_MISMATCH_E AES_KAT_FIPS_E

#ifndef WC_USE_DEVID
#define WC_USE_DEVID INVALID_DEVID
#endif
static const int devId = WC_USE_DEVID;

/* test vectors from
 * http://csrc.nist.gov/groups/STM/cavp/block-cipher-modes.html
 */
#ifdef WOLFSSL_AES_128
static int aes_xts_128_test(void)
{
#if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
    XtsAes *aes = NULL;
#else
    XtsAes aes[1];
#endif
    int aes_inited = 0;
    int ret = 0;
#define AES_XTS_128_TEST_BUF_SIZ (AES_BLOCK_SIZE * 2 + 8)
    unsigned char *buf = NULL;
    unsigned char *cipher = NULL;

    /* 128 key tests */
    WOLFSSL_SMALL_STACK_STATIC const unsigned char k1[] = {
        0xa1, 0xb9, 0x0c, 0xba, 0x3f, 0x06, 0xac, 0x35,
        0x3b, 0x2c, 0x34, 0x38, 0x76, 0x08, 0x17, 0x62,
        0x09, 0x09, 0x23, 0x02, 0x6e, 0x91, 0x77, 0x18,
        0x15, 0xf2, 0x9d, 0xab, 0x01, 0x93, 0x2f, 0x2f
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char i1[] = {
        0x4f, 0xae, 0xf7, 0x11, 0x7c, 0xda, 0x59, 0xc6,
        0x6e, 0x4b, 0x92, 0x01, 0x3e, 0x76, 0x8a, 0xd5
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char p1[] = {
        0xeb, 0xab, 0xce, 0x95, 0xb1, 0x4d, 0x3c, 0x8d,
        0x6f, 0xb3, 0x50, 0x39, 0x07, 0x90, 0x31, 0x1c
    };

    /* plain text test of partial block is not from NIST test vector list */
    WOLFSSL_SMALL_STACK_STATIC const unsigned char pp[] = {
        0xeb, 0xab, 0xce, 0x95, 0xb1, 0x4d, 0x3c, 0x8d,
        0x6f, 0xb3, 0x50, 0x39, 0x07, 0x90, 0x31, 0x1c,
        0x6e, 0x4b, 0x92, 0x01, 0x3e, 0x76, 0x8a, 0xd5
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char c1[] = {
        0x77, 0x8a, 0xe8, 0xb4, 0x3c, 0xb9, 0x8d, 0x5a,
        0x82, 0x50, 0x81, 0xd5, 0xbe, 0x47, 0x1c, 0x63
    };

    /* plain text test of partial block is not from NIST test vector list */
    WOLFSSL_SMALL_STACK_STATIC const unsigned char cp[] = {
        0x2b, 0xf7, 0x2c, 0xf3, 0xeb, 0x85, 0xef, 0x7b,
        0x0b, 0x76, 0xa0, 0xaa, 0xf3, 0x3f, 0x25, 0x8b,
        0x77, 0x8a, 0xe8, 0xb4, 0x3c, 0xb9, 0x8d, 0x5a
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char k2[] = {
        0x39, 0x25, 0x79, 0x05, 0xdf, 0xcc, 0x77, 0x76,
        0x6c, 0x87, 0x0a, 0x80, 0x6a, 0x60, 0xe3, 0xc0,
        0x93, 0xd1, 0x2a, 0xcf, 0xcb, 0x51, 0x42, 0xfa,
        0x09, 0x69, 0x89, 0x62, 0x5b, 0x60, 0xdb, 0x16
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char i2[] = {
        0x5c, 0xf7, 0x9d, 0xb6, 0xc5, 0xcd, 0x99, 0x1a,
        0x1c, 0x78, 0x81, 0x42, 0x24, 0x95, 0x1e, 0x84
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char p2[] = {
        0xbd, 0xc5, 0x46, 0x8f, 0xbc, 0x8d, 0x50, 0xa1,
        0x0d, 0x1c, 0x85, 0x7f, 0x79, 0x1c, 0x5c, 0xba,
        0xb3, 0x81, 0x0d, 0x0d, 0x73, 0xcf, 0x8f, 0x20,
        0x46, 0xb1, 0xd1, 0x9e, 0x7d, 0x5d, 0x8a, 0x56
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char c2[] = {
        0xd6, 0xbe, 0x04, 0x6d, 0x41, 0xf2, 0x3b, 0x5e,
        0xd7, 0x0b, 0x6b, 0x3d, 0x5c, 0x8e, 0x66, 0x23,
        0x2b, 0xe6, 0xb8, 0x07, 0xd4, 0xdc, 0xc6, 0x0e,
        0xff, 0x8d, 0xbc, 0x1d, 0x9f, 0x7f, 0xc8, 0x22
    };

#ifndef HAVE_FIPS /* FIPS requires different keys for main and tweak. */
    WOLFSSL_SMALL_STACK_STATIC const unsigned char k3[] = {
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    };
    WOLFSSL_SMALL_STACK_STATIC const unsigned char i3[] = {
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    };
    WOLFSSL_SMALL_STACK_STATIC const unsigned char p3[] = {
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0xff, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20
    };
    WOLFSSL_SMALL_STACK_STATIC const unsigned char c3[] = {
        0xA2, 0x07, 0x47, 0x76, 0x3F, 0xEC, 0x0C, 0x23,
        0x1B, 0xD0, 0xBD, 0x46, 0x9A, 0x27, 0x38, 0x12,
        0x95, 0x02, 0x3D, 0x5D, 0xC6, 0x94, 0x51, 0x36,
        0xA0, 0x85, 0xD2, 0x69, 0x6E, 0x87, 0x0A, 0xBF,
        0xB5, 0x5A, 0xDD, 0xCB, 0x80, 0xE0, 0xFC, 0xCD
    };
#endif /* HAVE_FIPS */

#if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
    if ((aes = (XtsAes *)XMALLOC(sizeof(*aes), HEAP_HINT, DYNAMIC_TYPE_AES))
        == NULL)
    {
        ERROR_OUT(MEMORY_E, out);
    }
#endif

    if ((buf = (unsigned char *)XMALLOC(AES_XTS_128_TEST_BUF_SIZ, HEAP_HINT,
                                        DYNAMIC_TYPE_AES)) == NULL)
    {
        ERROR_OUT(MEMORY_E, out);
    }
    if ((cipher = (unsigned char *)XMALLOC(AES_XTS_128_TEST_BUF_SIZ, HEAP_HINT,
                                           DYNAMIC_TYPE_AES)) == NULL)
    {
        ERROR_OUT(MEMORY_E, out);
    }

#if defined(OPENSSL_EXTRA) && !defined(WOLFCRYPT_ONLY) \
    && !defined(HAVE_SELFTEST) && !defined(HAVE_FIPS)
    ret = EVP_test(EVP_aes_128_xts(), k2, i2, p2, sizeof(p2), c2, sizeof(c2));
    if (ret != 0) {
        printf("EVP_aes_128_xts failed!\n");
        goto out;
    }
#endif

    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsInit(aes, HEAP_HINT, devId);
    if (ret != 0)
        ERROR_OUT(ret, out);
    else
        aes_inited = 1;

    ret = wc_AesXtsSetKeyNoInit(aes, k2, sizeof(k2), AES_ENCRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);

    ret = wc_AesXtsEncrypt(aes, buf, p2, sizeof(p2), i2, sizeof(i2));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c2, buf, sizeof(c2)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#if defined(DEBUG_VECTOR_REGISTER_ACCESS) && defined(WC_AES_C_DYNAMIC_FALLBACK)
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(SYSLIB_FAILED_E);
    ret = wc_AesXtsEncrypt(aes, buf, p2, sizeof(p2), i2, sizeof(i2));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(0);
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c2, buf, sizeof(c2)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);
#endif

    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);

    ret = wc_AesXtsSetKeyNoInit(aes, k1, sizeof(k1), AES_ENCRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsEncrypt(aes, buf, p1, sizeof(p1), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c1, buf, AES_BLOCK_SIZE))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#if defined(DEBUG_VECTOR_REGISTER_ACCESS) && defined(WC_AES_C_DYNAMIC_FALLBACK)
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(SYSLIB_FAILED_E);
    ret = wc_AesXtsEncrypt(aes, buf, p1, sizeof(p1), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(0);
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c1, buf, AES_BLOCK_SIZE))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);
#endif

    /* partial block encryption test */
    XMEMSET(cipher, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsEncrypt(aes, cipher, pp, sizeof(pp), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(cp, cipher, sizeof(cp)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#if defined(DEBUG_VECTOR_REGISTER_ACCESS) && defined(WC_AES_C_DYNAMIC_FALLBACK)
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(SYSLIB_FAILED_E);
    XMEMSET(cipher, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsEncrypt(aes, cipher, pp, sizeof(pp), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(0);
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(cp, cipher, sizeof(cp)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);
#endif

    /* partial block decrypt test */
    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsSetKeyNoInit(aes, k1, sizeof(k1), AES_DECRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsDecrypt(aes, buf, cipher, sizeof(pp), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(pp, buf, sizeof(pp)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#if defined(DEBUG_VECTOR_REGISTER_ACCESS) && defined(WC_AES_C_DYNAMIC_FALLBACK)
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(SYSLIB_FAILED_E);
    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsDecrypt(aes, buf, cipher, sizeof(pp), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(0);
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(pp, buf, sizeof(pp)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);
#endif

    /* NIST decrypt test vector */
    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsDecrypt(aes, buf, c1, sizeof(c1), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p1, buf, AES_BLOCK_SIZE))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#if defined(DEBUG_VECTOR_REGISTER_ACCESS) && defined(WC_AES_C_DYNAMIC_FALLBACK)
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(SYSLIB_FAILED_E);
    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsDecrypt(aes, buf, c1, sizeof(c1), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    WC_DEBUG_SET_VECTOR_REGISTERS_RETVAL(0);
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p1, buf, AES_BLOCK_SIZE))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);
#endif

    /* fail case with decrypting using wrong key */
    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsDecrypt(aes, buf, c2, sizeof(c2), i2, sizeof(i2));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p2, buf, sizeof(p2)) == 0) /* fail case with wrong key */
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    /* set correct key and retest */
    XMEMSET(buf, 0, AES_XTS_128_TEST_BUF_SIZ);
    ret = wc_AesXtsSetKeyNoInit(aes, k2, sizeof(k2), AES_DECRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsDecrypt(aes, buf, c2, sizeof(c2), i2, sizeof(i2));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p2, buf, sizeof(p2)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#ifndef HAVE_FIPS

    /* Test ciphertext stealing in-place. */
    XMEMCPY(buf, p3, sizeof(p3));
    ret = wc_AesXtsSetKeyNoInit(aes, k3, sizeof(k3), AES_ENCRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);

    ret = wc_AesXtsEncrypt(aes, buf, buf, sizeof(p3), i3, sizeof(i3));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c3, buf, sizeof(c3)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    ret = wc_AesXtsSetKeyNoInit(aes, k3, sizeof(k3), AES_DECRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsDecrypt(aes, buf, buf, sizeof(c3), i3, sizeof(i3));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p3, buf, sizeof(p3)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

#endif /* HAVE_FIPS */

#if !defined(BENCH_EMBEDDED) && !defined(HAVE_CAVIUM) && \
    !defined(WOLFSSL_AFALG)
    {
    #define LARGE_XTS_SZ        1024
    #if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
        byte* large_input = (byte *)XMALLOC(LARGE_XTS_SZ, HEAP_HINT,
            DYNAMIC_TYPE_TMP_BUFFER);
    #else
        byte large_input[LARGE_XTS_SZ];
    #endif
        int i;
        int j;
    #if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
        if (large_input == NULL)
            ERROR_OUT(MEMORY_E, out);
    #endif

        for (i = 0; i < (int)LARGE_XTS_SZ; i++)
            large_input[i] = (byte)i;

        for (j = 16; j < (int)LARGE_XTS_SZ; j++) {
            ret = wc_AesXtsSetKeyNoInit(aes, k1, sizeof(k1), AES_ENCRYPTION);
            if (ret != 0)
                ERROR_OUT(ret, out);
            ret = wc_AesXtsEncrypt(aes, large_input, large_input, j, i1,
                sizeof(i1));
        #if defined(WOLFSSL_ASYNC_CRYPT)
            ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev,
                               WC_ASYNC_FLAG_NONE);
        #endif
            if (ret != 0)
                ERROR_OUT(ret, out);

            ret = wc_AesXtsSetKeyNoInit(aes, k1, sizeof(k1), AES_DECRYPTION);
            if (ret != 0)
                ERROR_OUT(ret, out);
            ret = wc_AesXtsDecrypt(aes, large_input, large_input, j, i1,
                sizeof(i1));
        #if defined(WOLFSSL_ASYNC_CRYPT)
            ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev,
                               WC_ASYNC_FLAG_NONE);
        #endif
            if (ret != 0)
                ERROR_OUT(ret, out);
            for (i = 0; i < j; i++) {
                if (large_input[i] != (byte)i) {
                    ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);
                }
            }
        }
    #if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
        XFREE(large_input, HEAP_HINT, DYNAMIC_TYPE_TMP_BUFFER);
    #endif
    }
#endif /* !BENCH_EMBEDDED && !HAVE_CAVIUM &&
        * !WOLFSSL_AFALG
        */

    /* now the kernel crypto part */

    {
    u8 *    enc2 = NULL;
    u8 *    dec2 = NULL;
    struct scatterlist *  src = NULL;
    struct scatterlist *  dst = NULL;
    struct crypto_skcipher *tfm = NULL;
    struct skcipher_request *req = NULL;
    u8 iv[AES_BLOCK_SIZE];
    const char *driver_name;

    enc2 = XMALLOC(sizeof(p1), NULL, DYNAMIC_TYPE_AES);
    if (!enc2) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    dec2 = XMALLOC(sizeof(p1), NULL, DYNAMIC_TYPE_AES);
    if (!dec2) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    src = XMALLOC(sizeof(*src) * 2, NULL, DYNAMIC_TYPE_AES);
    if (! src) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    dst = XMALLOC(sizeof(*dst) * 2, NULL, DYNAMIC_TYPE_AES);
    if (! dst) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    tfm = crypto_alloc_skcipher(WOLFKM_AESXTS_NAME, 0, 0);
    if (IS_ERR(tfm)) {
        ret = PTR_ERR(tfm);
        pr_err("error: allocating AES skcipher algorithm %s failed: %d\n",
               WOLFKM_AESXTS_DRIVER, ret);
        goto test_xts_end;
    }

    driver_name = crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm));
    if (strcmp(driver_name, WOLFKM_AESXTS_DRIVER)) {
        pr_err("error: unexpected implementation for %s: %s (expected %s)\n",
               WOLFKM_AESXTS_NAME, driver_name, WOLFKM_AESXTS_DRIVER);
        ret = -ENOENT;
        goto test_xts_end;
    }

    ret = crypto_skcipher_ivsize(tfm);
    if (ret != sizeof(iv)) {
        pr_err("error: AES skcipher algorithm %s crypto_skcipher_ivsize()"
               " returned %d but expected %d\n",
               WOLFKM_AESXTS_DRIVER, ret, (int)sizeof(iv));
        ret = -EINVAL;
        goto test_xts_end;
    }

    ret = crypto_skcipher_setkey(tfm, k1, sizeof(k1));
    if (ret) {
        pr_err("error: crypto_skcipher_setkey for %s returned: %d\n",
               WOLFKM_AESXTS_NAME, ret);
        goto test_xts_end;
    }

    req = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (IS_ERR(req)) {
        ret = PTR_ERR(req);
        pr_err("error: allocating AES skcipher request %s failed: %d\n",
               WOLFKM_AESXTS_DRIVER, ret);
        goto test_xts_end;
    }

    memcpy(dec2, p1, sizeof(p1));
    memset(enc2, 0, sizeof(p1));

    sg_init_one(src, dec2, sizeof(p1));
    sg_init_one(dst, enc2, sizeof(p1));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(p1), iv);

    ret = crypto_skcipher_encrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_encrypt returned: %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(c1, enc2, sizeof(c1));
    if (ret) {
        pr_err("error: c1 and enc2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    memset(dec2, 0, sizeof(p1));
    sg_init_one(src, enc2, sizeof(p1));
    sg_init_one(dst, dec2, sizeof(p1));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(p1), iv);

    ret = crypto_skcipher_decrypt(req);

    if (ret) {
        pr_err("ERROR: crypto_skcipher_decrypt returned %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(p1, dec2, sizeof(p1));
    if (ret) {
        pr_err("error: p1 and dec2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    memcpy(dec2, pp, sizeof(pp));
    memset(enc2, 0, sizeof(pp));

    sg_init_one(src, dec2, sizeof(pp));
    sg_init_one(dst, enc2, sizeof(pp));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(pp), iv);

    ret = crypto_skcipher_encrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_encrypt returned: %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(cp, enc2, sizeof(cp));
    if (ret) {
        pr_err("error: cp and enc2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    memset(dec2, 0, sizeof(pp));
    sg_init_one(src, enc2, sizeof(pp));
    sg_init_one(dst, dec2, sizeof(pp));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(pp), iv);

    ret = crypto_skcipher_decrypt(req);

    if (ret) {
        pr_err("ERROR: crypto_skcipher_decrypt returned %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(pp, dec2, sizeof(pp));
    if (ret) {
        pr_err("error: pp and dec2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    test_xts_end:

    if (enc2)
        XFREE(enc2, NULL, DYNAMIC_TYPE_AES);
    if (dec2)
        XFREE(dec2, NULL, DYNAMIC_TYPE_AES);
    if (src)
        XFREE(src, NULL, DYNAMIC_TYPE_AES);
    if (dst)
        XFREE(dst, NULL, DYNAMIC_TYPE_AES);
    if (req)
        skcipher_request_free(req);
    if (tfm)
        crypto_free_skcipher(tfm);

    }

  out:

    if (aes_inited)
        wc_AesXtsFree(aes);

    if (buf)
        XFREE(buf, HEAP_HINT, DYNAMIC_TYPE_AES);
    if (cipher)
        XFREE(cipher, HEAP_HINT, DYNAMIC_TYPE_AES);

#if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
    if (aes)
        XFREE(aes, HEAP_HINT, DYNAMIC_TYPE_AES);
#endif

#undef AES_XTS_128_TEST_BUF_SIZ

    return ret;
}
#endif /* WOLFSSL_AES_128 */

#ifdef WOLFSSL_AES_256
static int aes_xts_256_test(void)
{
#if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
    XtsAes *aes = NULL;
#else
    XtsAes aes[1];
#endif
    int aes_inited = 0;
    int ret = 0;
#define AES_XTS_256_TEST_BUF_SIZ (AES_BLOCK_SIZE * 3)
    unsigned char *buf = NULL;
    unsigned char *cipher = NULL;

    /* 256 key tests */
    WOLFSSL_SMALL_STACK_STATIC const unsigned char k1[] = {
        0x1e, 0xa6, 0x61, 0xc5, 0x8d, 0x94, 0x3a, 0x0e,
        0x48, 0x01, 0xe4, 0x2f, 0x4b, 0x09, 0x47, 0x14,
        0x9e, 0x7f, 0x9f, 0x8e, 0x3e, 0x68, 0xd0, 0xc7,
        0x50, 0x52, 0x10, 0xbd, 0x31, 0x1a, 0x0e, 0x7c,
        0xd6, 0xe1, 0x3f, 0xfd, 0xf2, 0x41, 0x8d, 0x8d,
        0x19, 0x11, 0xc0, 0x04, 0xcd, 0xa5, 0x8d, 0xa3,
        0xd6, 0x19, 0xb7, 0xe2, 0xb9, 0x14, 0x1e, 0x58,
        0x31, 0x8e, 0xea, 0x39, 0x2c, 0xf4, 0x1b, 0x08
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char i1[] = {
        0xad, 0xf8, 0xd9, 0x26, 0x27, 0x46, 0x4a, 0xd2,
        0xf0, 0x42, 0x8e, 0x84, 0xa9, 0xf8, 0x75, 0x64
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char p1[] = {
        0x2e, 0xed, 0xea, 0x52, 0xcd, 0x82, 0x15, 0xe1,
        0xac, 0xc6, 0x47, 0xe8, 0x10, 0xbb, 0xc3, 0x64,
        0x2e, 0x87, 0x28, 0x7f, 0x8d, 0x2e, 0x57, 0xe3,
        0x6c, 0x0a, 0x24, 0xfb, 0xc1, 0x2a, 0x20, 0x2e
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char c1[] = {
        0xcb, 0xaa, 0xd0, 0xe2, 0xf6, 0xce, 0xa3, 0xf5,
        0x0b, 0x37, 0xf9, 0x34, 0xd4, 0x6a, 0x9b, 0x13,
        0x0b, 0x9d, 0x54, 0xf0, 0x7e, 0x34, 0xf3, 0x6a,
        0xf7, 0x93, 0xe8, 0x6f, 0x73, 0xc6, 0xd7, 0xdb
    };

    /* plain text test of partial block is not from NIST test vector list */
    WOLFSSL_SMALL_STACK_STATIC const unsigned char pp[] = {
        0xeb, 0xab, 0xce, 0x95, 0xb1, 0x4d, 0x3c, 0x8d,
        0x6f, 0xb3, 0x50, 0x39, 0x07, 0x90, 0x31, 0x1c,
        0x6e, 0x4b, 0x92, 0x01, 0x3e, 0x76, 0x8a, 0xd5
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char cp[] = {
        0x65, 0x5e, 0x1d, 0x37, 0x4a, 0x91, 0xe7, 0x6c,
        0x4f, 0x83, 0x92, 0xbc, 0x5a, 0x10, 0x55, 0x27,
        0x61, 0x0e, 0x5a, 0xde, 0xca, 0xc5, 0x12, 0xd8
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char k2[] = {
        0xad, 0x50, 0x4b, 0x85, 0xd7, 0x51, 0xbf, 0xba,
        0x69, 0x13, 0xb4, 0xcc, 0x79, 0xb6, 0x5a, 0x62,
        0xf7, 0xf3, 0x9d, 0x36, 0x0f, 0x35, 0xb5, 0xec,
        0x4a, 0x7e, 0x95, 0xbd, 0x9b, 0xa5, 0xf2, 0xec,
        0xc1, 0xd7, 0x7e, 0xa3, 0xc3, 0x74, 0xbd, 0x4b,
        0x13, 0x1b, 0x07, 0x83, 0x87, 0xdd, 0x55, 0x5a,
        0xb5, 0xb0, 0xc7, 0xe5, 0x2d, 0xb5, 0x06, 0x12,
        0xd2, 0xb5, 0x3a, 0xcb, 0x47, 0x8a, 0x53, 0xb4
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char i2[] = {
        0xe6, 0x42, 0x19, 0xed, 0xe0, 0xe1, 0xc2, 0xa0,
        0x0e, 0xf5, 0x58, 0x6a, 0xc4, 0x9b, 0xeb, 0x6f
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char p2[] = {
        0x24, 0xcb, 0x76, 0x22, 0x55, 0xb5, 0xa8, 0x00,
        0xf4, 0x6e, 0x80, 0x60, 0x56, 0x9e, 0x05, 0x53,
        0xbc, 0xfe, 0x86, 0x55, 0x3b, 0xca, 0xd5, 0x89,
        0xc7, 0x54, 0x1a, 0x73, 0xac, 0xc3, 0x9a, 0xbd,
        0x53, 0xc4, 0x07, 0x76, 0xd8, 0xe8, 0x22, 0x61,
        0x9e, 0xa9, 0xad, 0x77, 0xa0, 0x13, 0x4c, 0xfc
    };

    WOLFSSL_SMALL_STACK_STATIC const unsigned char c2[] = {
        0xa3, 0xc6, 0xf3, 0xf3, 0x82, 0x79, 0x5b, 0x10,
        0x87, 0xd7, 0x02, 0x50, 0xdb, 0x2c, 0xd3, 0xb1,
        0xa1, 0x62, 0xa8, 0xb6, 0xdc, 0x12, 0x60, 0x61,
        0xc1, 0x0a, 0x84, 0xa5, 0x85, 0x3f, 0x3a, 0x89,
        0xe6, 0x6c, 0xdb, 0xb7, 0x9a, 0xb4, 0x28, 0x9b,
        0xc3, 0xea, 0xd8, 0x10, 0xe9, 0xc0, 0xaf, 0x92
    };

#if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
    if ((aes = (XtsAes *)XMALLOC(sizeof(*aes), HEAP_HINT, DYNAMIC_TYPE_AES))
        == NULL)
    {
        ERROR_OUT(MEMORY_E, out);
    }
#endif

    if ((buf = (unsigned char *)XMALLOC(AES_XTS_256_TEST_BUF_SIZ, HEAP_HINT,
                                        DYNAMIC_TYPE_AES)) == NULL)
    {
        ERROR_OUT(MEMORY_E, out);
    }
    if ((cipher = (unsigned char *)XMALLOC(AES_XTS_256_TEST_BUF_SIZ, HEAP_HINT,
                                           DYNAMIC_TYPE_AES)) == NULL)
    {
        ERROR_OUT(MEMORY_E, out);
    }

#if defined(OPENSSL_EXTRA) && !defined(WOLFCRYPT_ONLY) \
    && !defined(HAVE_SELFTEST) && !defined(HAVE_FIPS)
    ret = EVP_test(EVP_aes_256_xts(), k2, i2, p2, sizeof(p2), c2, sizeof(c2));
    if (ret != 0) {
        printf("EVP_aes_256_xts failed\n");
        goto out;
    }
#endif

    ret = wc_AesXtsInit(aes, HEAP_HINT, devId);
    if (ret != 0)
        ERROR_OUT(ret, out);
    else
        aes_inited = 1;

    XMEMSET(buf, 0, AES_XTS_256_TEST_BUF_SIZ);
    ret = wc_AesXtsSetKeyNoInit(aes, k2, sizeof(k2), AES_ENCRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);

    ret = wc_AesXtsEncrypt(aes, buf, p2, sizeof(p2), i2, sizeof(i2));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c2, buf, sizeof(c2)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    XMEMSET(buf, 0, AES_XTS_256_TEST_BUF_SIZ);
    ret = wc_AesXtsSetKeyNoInit(aes, k1, sizeof(k1), AES_ENCRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsEncrypt(aes, buf, p1, sizeof(p1), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(c1, buf, AES_BLOCK_SIZE))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    /* partial block encryption test */
    XMEMSET(cipher, 0, AES_XTS_256_TEST_BUF_SIZ);
    ret = wc_AesXtsEncrypt(aes, cipher, pp, sizeof(pp), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_encrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);

    /* partial block decrypt test */
    XMEMSET(buf, 0, AES_XTS_256_TEST_BUF_SIZ);
    ret = wc_AesXtsSetKeyNoInit(aes, k1, sizeof(k1), AES_DECRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsDecrypt(aes, buf, cipher, sizeof(pp), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(pp, buf, sizeof(pp)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    /* NIST decrypt test vector */
    XMEMSET(buf, 0, AES_XTS_256_TEST_BUF_SIZ);
    ret = wc_AesXtsDecrypt(aes, buf, c1, sizeof(c1), i1, sizeof(i1));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p1, buf, AES_BLOCK_SIZE))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    XMEMSET(buf, 0, AES_XTS_256_TEST_BUF_SIZ);
    ret = wc_AesXtsSetKeyNoInit(aes, k2, sizeof(k2), AES_DECRYPTION);
    if (ret != 0)
        ERROR_OUT(ret, out);
    ret = wc_AesXtsDecrypt(aes, buf, c2, sizeof(c2), i2, sizeof(i2));
#if defined(WOLFSSL_ASYNC_CRYPT)
    ret = wc_AsyncWait(ret, &aes->aes_decrypt.asyncDev, WC_ASYNC_FLAG_NONE);
#endif
    if (ret != 0)
        ERROR_OUT(ret, out);
    if (XMEMCMP(p2, buf, sizeof(p2)))
        ERROR_OUT(LINUXKM_LKCAPI_AES_KAT_MISMATCH_E, out);

    /* now the kernel crypto part */

    {
    u8 *    enc2 = NULL;
    u8 *    dec2 = NULL;
    struct scatterlist *  src = NULL;
    struct scatterlist *  dst = NULL;
    struct crypto_skcipher *tfm = NULL;
    struct skcipher_request *req = NULL;
    u8 iv[AES_BLOCK_SIZE];
    const char *driver_name;

    enc2 = XMALLOC(sizeof(p1), NULL, DYNAMIC_TYPE_AES);
    if (!enc2) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    dec2 = XMALLOC(sizeof(p1), NULL, DYNAMIC_TYPE_AES);
    if (!dec2) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    src = XMALLOC(sizeof(*src) * 2, NULL, DYNAMIC_TYPE_AES);
    if (! src) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    dst = XMALLOC(sizeof(*dst) * 2, NULL, DYNAMIC_TYPE_AES);
    if (! dst) {
        pr_err("error: malloc failed\n");
        ret = -ENOMEM;
        goto test_xts_end;
    }

    tfm = crypto_alloc_skcipher(WOLFKM_AESXTS_NAME, 0, 0);
    if (IS_ERR(tfm)) {
        ret = PTR_ERR(tfm);
        pr_err("error: allocating AES skcipher algorithm %s failed: %d\n",
               WOLFKM_AESXTS_DRIVER, ret);
        goto test_xts_end;
    }

    driver_name = crypto_tfm_alg_driver_name(crypto_skcipher_tfm(tfm));
    if (strcmp(driver_name, WOLFKM_AESXTS_DRIVER)) {
        pr_err("error: unexpected implementation for %s: %s (expected %s)\n",
               WOLFKM_AESXTS_NAME, driver_name, WOLFKM_AESXTS_DRIVER);
        ret = -ENOENT;
        goto test_xts_end;
    }

    ret = crypto_skcipher_ivsize(tfm);
    if (ret != sizeof(iv)) {
        pr_err("error: AES skcipher algorithm %s crypto_skcipher_ivsize()"
               " returned %d but expected %d\n",
               WOLFKM_AESXTS_DRIVER, ret, (int)sizeof(iv));
        ret = -EINVAL;
        goto test_xts_end;
    }

    ret = crypto_skcipher_setkey(tfm, k1, sizeof(k1));
    if (ret) {
        pr_err("error: crypto_skcipher_setkey for %s returned: %d\n",
               WOLFKM_AESXTS_NAME, ret);
        goto test_xts_end;
    }

    req = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (IS_ERR(req)) {
        ret = PTR_ERR(req);
        pr_err("error: allocating AES skcipher request %s failed: %d\n",
               WOLFKM_AESXTS_DRIVER, ret);
        goto test_xts_end;
    }

    memcpy(dec2, p1, sizeof(p1));
    memset(enc2, 0, sizeof(p1));

    sg_init_one(src, dec2, sizeof(p1));
    sg_init_one(dst, enc2, sizeof(p1));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(p1), iv);

    ret = crypto_skcipher_encrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_encrypt returned: %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(c1, enc2, sizeof(c1));
    if (ret) {
        pr_err("error: c1 and enc2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    memset(dec2, 0, sizeof(p1));
    sg_init_one(src, enc2, sizeof(p1));
    sg_init_one(dst, dec2, sizeof(p1));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(p1), iv);

    ret = crypto_skcipher_decrypt(req);

    if (ret) {
        pr_err("ERROR: crypto_skcipher_decrypt returned %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(p1, dec2, sizeof(p1));
    if (ret) {
        pr_err("error: p1 and dec2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    memcpy(dec2, pp, sizeof(pp));
    memset(enc2, 0, sizeof(pp));

    sg_init_one(src, dec2, sizeof(pp));
    sg_init_one(dst, enc2, sizeof(pp));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(pp), iv);

    ret = crypto_skcipher_encrypt(req);

    if (ret) {
        pr_err("error: crypto_skcipher_encrypt returned: %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(cp, enc2, sizeof(cp));
    if (ret) {
        pr_err("error: cp and enc2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    memset(dec2, 0, sizeof(pp));
    sg_init_one(src, enc2, sizeof(pp));
    sg_init_one(dst, dec2, sizeof(pp));

    memcpy(iv, i1, sizeof(iv));
    skcipher_request_set_crypt(req, src, dst, sizeof(pp), iv);

    ret = crypto_skcipher_decrypt(req);

    if (ret) {
        pr_err("ERROR: crypto_skcipher_decrypt returned %d\n", ret);
        goto test_xts_end;
    }

    ret = XMEMCMP(pp, dec2, sizeof(pp));
    if (ret) {
        pr_err("error: pp and dec2 do not match: %d\n", ret);
        ret = -EINVAL;
        goto test_xts_end;
    }

    test_xts_end:

    if (enc2)
        XFREE(enc2, NULL, DYNAMIC_TYPE_AES);
    if (dec2)
        XFREE(dec2, NULL, DYNAMIC_TYPE_AES);
    if (src)
        XFREE(src, NULL, DYNAMIC_TYPE_AES);
    if (dst)
        XFREE(dst, NULL, DYNAMIC_TYPE_AES);
    if (req)
        skcipher_request_free(req);
    if (tfm)
        crypto_free_skcipher(tfm);

    }

  out:

    if (aes_inited)
        wc_AesXtsFree(aes);

    if (buf)
        XFREE(buf, HEAP_HINT, DYNAMIC_TYPE_AES);
    if (cipher)
        XFREE(cipher, HEAP_HINT, DYNAMIC_TYPE_AES);

#if defined(WOLFSSL_SMALL_STACK) && !defined(WOLFSSL_NO_MALLOC)
    if (aes)
        XFREE(aes, HEAP_HINT, DYNAMIC_TYPE_AES);
#endif

#undef AES_XTS_256_TEST_BUF_SIZ

    return ret;
}
#endif /* WOLFSSL_AES_256 */

static int linuxkm_test_aesxts(void) {
    int ret;

    #ifdef WOLFSSL_AES_128
    ret = aes_xts_128_test();
    if (ret != 0) {
        pr_err("aes_xts_128_test() failed with retval %d.\n", ret);
        goto out;
    }
    #endif
    #ifdef WOLFSSL_AES_256
    ret = aes_xts_256_test();
    if (ret != 0) {
        pr_err("aes_xts_256_test() failed with retval %d.\n", ret);
        goto out;
    }
    #endif

out:

    return ret;
}

#endif /* WOLFSSL_AES_XTS &&
        * (LINUXKM_LKCAPI_REGISTER_ALL || LINUXKM_LKCAPI_REGISTER_AESXTS)
        */

#endif /* !NO_AES */

static int linuxkm_lkcapi_register(void)
{
    int ret = 0;

#define REGISTER_ALG(alg, installer, tester) do {                       \
        if (alg ## _loaded) {                                           \
            pr_err("ERROR: %s is already registered.\n",                \
                   (alg).base.cra_driver_name);                         \
            return -EEXIST;                                             \
        }                                                               \
                                                                        \
        ret =  (installer)(&(alg));                                     \
                                                                        \
        if (ret) {                                                      \
            pr_err("ERROR: " #installer " for %s failed "               \
                   "with return code %d.\n",                            \
                   (alg).base.cra_driver_name, ret);                    \
            return ret;                                                 \
        }                                                               \
                                                                        \
        alg ## _loaded = 1;                                             \
                                                                        \
        ret = (tester());                                               \
                                                                        \
        if (ret) {                                                      \
            pr_err("ERROR: self-test for %s failed "                    \
                   "with return code %d.\n",                            \
                   (alg).base.cra_driver_name, ret);                    \
            return ret;                                                 \
        }                                                               \
        pr_info("%s self-test OK -- "                                   \
                "registered for %s with priority %d.\n",                \
                (alg).base.cra_driver_name,                             \
                (alg).base.cra_name,                                    \
                (alg).base.cra_priority);                               \
    } while (0)

#if defined(HAVE_AES_CBC) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCBC))

    REGISTER_ALG(cbcAesAlg, crypto_register_skcipher, linuxkm_test_aescbc);
#endif

#if defined(WOLFSSL_AES_CFB) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCFB))

    REGISTER_ALG(cfbAesAlg, crypto_register_skcipher, linuxkm_test_aescfb);
#endif

#if defined(HAVE_AESGCM) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESGCM)) &&                        \
    (! (defined(WOLFSSL_AESNI) && defined(WC_AES_C_DYNAMIC_FALLBACK)))

    REGISTER_ALG(gcmAesAead, crypto_register_aead, linuxkm_test_aesgcm);
#endif

#if defined(WOLFSSL_AES_XTS) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESXTS))

    REGISTER_ALG(xtsAesAlg, crypto_register_skcipher, linuxkm_test_aesxts);
#endif

#undef REGISTER_ALG

    return 0;
}

static void linuxkm_lkcapi_unregister(void)
{
#define UNREGISTER_ALG(alg, uninstaller) do {                           \
        if (alg ## _loaded) {                                           \
            (uninstaller)(&(alg));                                      \
            alg ## _loaded = 0;                                         \
        }                                                               \
    } while (0)

#if defined(HAVE_AES_CBC) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCBC))

    UNREGISTER_ALG(cbcAesAlg, crypto_unregister_skcipher);
#endif
#if defined(WOLFSSL_AES_CFB) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESCFB))

    UNREGISTER_ALG(cfbAesAlg, crypto_unregister_skcipher);
#endif
#if defined(HAVE_AESGCM) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESGCM)) && \
    (! (defined(WOLFSSL_AESNI) && defined(WC_AES_C_DYNAMIC_FALLBACK)))

    UNREGISTER_ALG(gcmAesAead, crypto_unregister_aead);
#endif
#if defined(WOLFSSL_AES_XTS) && \
    (defined(LINUXKM_LKCAPI_REGISTER_ALL) || \
     defined(LINUXKM_LKCAPI_REGISTER_AESXTS))

    UNREGISTER_ALG(xtsAesAlg, crypto_unregister_skcipher);
#endif

#undef UNREGISTER_ALG
}
