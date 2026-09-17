#ifndef ENGORGIO_BRIDGE_H
#define ENGORGIO_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* every function returns 0 on success; on error *err gets a malloc'd string
   the caller must free with engorgio_free_string(), or NULL to ignore */

int engorgio_researcher_generate_key(const char *dir, char **err);
int engorgio_researcher_decrypt(const char *dir, const char *in_path,
                                const char *out_path, int rows, char **err);

int engorgio_provider_generate_keys(const char *dir, char **err);
int engorgio_provider_encrypt(const char *dir, const char *schema_path, char **err);
int engorgio_provider_evaluate_dynamic(const char *dir, const char *instructions,
                                       char **err);

int engorgio_proxy_generate_ksk(const char *sk_researcher_dir,
                                 const char *sk_provider_dir,
                                 const char *ksk_path, char **err);
int engorgio_proxy_aggregate(const char *ksk_path, const char *in_path,
                              const char *out_path, char **err);

/* forward KSK, researcher → provider */
int engorgio_proxy_generate_ksk_rl(const char *sk_researcher_dir,
                                    const char *sk_provider_dir,
                                    const char *ksk_path, char **err);

/* key-switch literal batch */
int engorgio_proxy_keyswitch_literals(const char *ksk_path, const char *in_path,
                                      const char *out_path, char **err);

/* encrypt literals, one CT each */
int engorgio_encrypt_literals(const char *researcher_dir, const char *values_json,
                               const char *out_path, char **err);

/* evaluate with encrypted literals
   condition i uses literal[i] */
int engorgio_provider_evaluate_enc_literals(const char *dir,
                                             const char *instructions,
                                             const char *literals_path, char **err);

/* enroll patient batch
   fresh rp ∈ [2.0, 10.0) per patient, Enc(+rp) packed one CT per chunk into
   blindings/chunk_{k}.bin, Enc(-rp) per call into the store: granularity = batch */
int engorgio_setup_blinding(const char *provider_dir, const char *proxy_dir,
                            const char *patients_json, char **err);

/* add per-chunk blinding logs */
int engorgio_apply_blinding(const char *blindings_dir, const char *result_path,
                            const char *out_path, char **err);

/* add consented cancellation masks */
int engorgio_apply_cancellation(const char *result_path, const char *cancel_dir,
                                 const char *consented_json, const char *out_path,
                                 char **err);

void engorgio_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif /* ENGORGIO_BRIDGE_H */
