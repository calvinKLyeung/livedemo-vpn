#include <stdio.h>
#include <sodium.h>

int main(int argc, char **argv) {
    if (argc != 2 || sodium_init() < 0) {
        fprintf(stderr, "usage: %s NAME_OF_KEY\n", argv[0]);
        return 1;  // returns 0 on success, -1 on failure, and 1 if the library had already been initialized
    }
    unsigned char pk[crypto_sign_PUBLICKEYBYTES];
    unsigned char sk[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(pk, sk);

    char path[256];
    snprintf(path, sizeof path, "%s.pk", argv[1]);

    FILE *f = fopen(path, "wb"); 
    if (!f) { perror(path); return 1; }
    fwrite(pk, 1, sizeof pk, f); 
    fclose(f);
    snprintf(path, sizeof path, "%s.sk", argv[1]);
    f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite(sk, 1, sizeof sk, f);
    fclose(f);

    char hex[2 * sizeof pk + 1];
    printf("%s\n", sodium_bin2hex(hex, sizeof hex, pk, sizeof pk));
    return 0;
}