#include "sha256.h"
#include "common.h"

static int test_sha256(const char *input, unsigned int count, const char *expect)
{
    struct sha256_context ctx;
    const uint8_t *hash;
    unsigned int ii;
    char digest[65];

    sha256_init(&ctx);
    for (ii = 0; ii < count; ++ii)
        sha256_update(&ctx, input, strlen(input));
    hash = sha256_finish(&ctx);

    for (ii = 0; ii < SHA256_OUTPUT_SIZE; ++ii)
        sprintf(digest + 2*ii, "%02x", hash[ii]);

    if (strcmp(digest, expect)) {
        printf("Mismatch for \"%s\"x%u:\n  Got %s,\n  Expected %s\n",
	    input, count, digest, expect);
	return 1;
    }

    return 0;
}

int main(UNUSED_ARG(int argc), UNUSED_ARG(char *argv[]))
{
    size_t failed = 0;

    if (test_sha256("abc", 1,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
        failed++;

    if (test_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"))
        failed++;

    if (test_sha256("a", 1000000,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"))
        failed++;

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}