#ifndef PATDISCOVER_BRIDGE_H
#define PATDISCOVER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* every function returns 0 on success; on error *err gets a malloc'd string
   the caller must free with patdiscover_free_string(), or NULL to ignore */

int patdiscover_researcher_generate_key(const char *dir, char **err);
int patdiscover_researcher_decrypt(const char *dir, const char *in_path,
                                   const char *out_path, int rows, char **err);

int patdiscover_provider_generate_keys(const char *dir, char **err);
int patdiscover_provider_encrypt(const char *dir, const char *schema_path, char **err);
int patdiscover_provider_evaluate(const char *dir, const char *instructions, char **err);

/* KSK, provider → researcher */
int patdiscover_proxy_generate_ksk(const char *sk_researcher_dir,
                                   const char *sk_provider_dir,
                                   const char *ksk_path, char **err);
int patdiscover_proxy_reencrypt(const char *ksk_path, const char *in_path,
                                const char *out_path, char **err);

/* forward KSK, researcher → provider */
int patdiscover_proxy_generate_ksk_rl(const char *sk_researcher_dir,
                                      const char *sk_provider_dir,
                                      const char *ksk_path, char **err);
int patdiscover_proxy_reencrypt_literals(const char *ksk_path, const char *in_path,
                                         const char *out_path, char **err);

/* encrypt literals, one CT each */
int patdiscover_encrypt_literals(const char *researcher_dir, const char *values_json,
                                  const char *out_path, char **err);

/* evaluate with re-keyed literals */
int patdiscover_provider_evaluate_enc_literals(const char *dir,
                                               const char *instructions,
                                               const char *literals_path, char **err);

/* enroll patient batch
   per patient r_p: Enc(+r_p) appended to provider_dir/blindings/chunk_{k}.bin,
   Enc(-r_p) to the compact store proxy_dir/cancellations/store.{idx,dat} */
int patdiscover_setup_blinding(const char *provider_dir, const char *proxy_dir,
                               const char *patients_json, char **err);

/* add per-chunk blinding logs */
int patdiscover_apply_blinding(const char *blindings_dir, const char *result_path,
                               const char *out_path, char **err);

/* add consented cancellation masks */
int patdiscover_apply_cancellation(const char *result_path, const char *cancel_dir,
                                   const char *consented_json, const char *out_path,
                                   char **err);

void patdiscover_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif /* PATDISCOVER_BRIDGE_H */
