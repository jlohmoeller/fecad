#ifndef FECAD_BRIDGE_H
#define FECAD_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* every function returns 0 on success; on error *err gets a malloc'd string
   the caller must free with fecad_free_string(), or NULL to ignore */

int fecad_researcher_generate_key(const char *data_dir, char **err);
int fecad_researcher_decrypt(const char *data_dir, const char *result_path,
                             const char *out_path, int rows, char **err);

int fecad_provider_generate_keys(const char *data_dir, char **err);
int fecad_provider_encrypt(const char *data_dir, const char *schema_path, char **err);
int fecad_provider_evaluate_dynamic(const char *data_dir,
                                    const char *instructions, char **err);

int fecad_proxy_generate_ksk(const char *sk_researcher,
                              const char *sk_location, const char *ksk_path,
                              char **err);
int fecad_proxy_aggregate(const char *ksk_path, const char *in_file,
                           const char *out_file, char **err);

/* forward KSK, researcher → provider */
int fecad_proxy_generate_ksk_rl(const char *sk_researcher_dir,
                                  const char *sk_provider_dir,
                                  const char *ksk_path, char **err);

/* key-switch literal batch */
int fecad_proxy_keyswitch_literals(const char *ksk_path, const char *in_path,
                                    const char *out_path, char **err);

/* encrypt literals, one CT each */
int fecad_encrypt_literals(const char *researcher_dir, const char *values_json,
                            const char *out_path, char **err);

/* evaluate with encrypted literals
   condition i uses literal[i] */
int fecad_provider_evaluate_enc_literals(const char *data_dir,
                                          const char *instructions,
                                          const char *literals_path, char **err);

/* enroll patient batch
   Enc(+rp) → provider_dir/blindings/{pid}.bin, Enc(-rp) →
   proxy_dir/cancellations/{pid}.bin; patients.json is rewritten exactly once */
int fecad_setup_blinding(const char *provider_dir, const char *proxy_dir,
                         const char *patients_json, char **err);

/* add per-row blinding CTs */
int fecad_apply_blinding(const char *working_dir, const char *result_path,
                          const char *out_path, char **err);

/* add consented cancellation CTs */
int fecad_apply_cancellation(const char *result_path, const char *cancellations_dir,
                              const char *consented_json, const char *out_path,
                              char **err);

void fecad_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif /* FECAD_BRIDGE_H */
