#ifndef ZK_SHOTS_H
#define ZK_SHOTS_H

/* Render every scenario under fixtures_root into outdir.
 * Returns 0 on success. The body is filled in by the shot runner. */
int zk_shots_run(const char *fixtures_root, const char *outdir);

#endif
