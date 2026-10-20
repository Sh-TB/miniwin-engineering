# SWEEP-027 evidence «واگن های قطار» — one wagon batch, 45 imports
- sweep027_sweep.tsv — 16-prog corpus x3 on sha 0f4b4773: searchmyfiles
  139->exit0 (complete run #12), unresolved collapse across all tails
  (autoruns 26 / bginfo 145 / cports 6 / diskmon 5 / produkey 10 / rammap 4
  / procexp 128 / winmerge 150), zero regressions, deterministic 45/45.
- searchmyfiles/ — post-fix stderr (2 unresolved left) + exit0 evidence.
- winmergeu/ — post-batch stderr (150 left; crash decode open).
- G1 pin: evidence/master/g1/20261020_085912 (sha 0f4b4773, 12/12).
- Reconcile note: env reset detected after SWEEP-026 push; local reset to
  origin 3cd11c0, deterministic rebuild reproduced pin sha EXACTLY.
