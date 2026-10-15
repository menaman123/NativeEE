# gate_run 2026-10-06 03:42:09
release=<work>/int_work/gates/bin_s12/nativeee_release
debug=<work>/int_work/gates/bin_s12/nativeee_debug
reps=3 lanes=frames,audio,mem timeout=900
set=(none: defaults)
legs=artdet_f37152,coldboot,coldboot_attract,ps2state_slot0_f36935,ps2state_slot0_f39807,ps2state_slot0_f40391,ps2state_slot0_f44111,ps2state_slot0_f46203_red_crystal,ps2state_slot0_f46949_yellow_crystal,ps2state_slot0_f57292,ps2state_slot0_f59991,slowspot_f37092_s1789428268,slowspot_f40298_s1789693059

## Re-based windows (REBASELINE.tsv; the effective reference, DT-4)
none: every lane window and probe pair against the frozen OLD files

## Guard
guard=IDENTICAL

## Battery (Release, 3 reps)
```
leg                                  frames reps        cross-engine (lanes=frames,audio,mem)    fingerprint note
artdet_f37152                           600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
coldboot                               1500 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=1500
coldboot_attract                       6000 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=6000
ps2state_slot0_f36935                   600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f39807                   600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f40391                   600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f44111                   600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f46203_red_crystal       600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f46949_yellow_crystal    600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f57292                   600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
ps2state_slot0_f59991                   600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
slowspot_f37092_s1789428268             600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
slowspot_f40298_s1789693059             600 ADMISSIBLE  META-OK DIVERGENT(frames,audio,mem)      FP-OK       frames=600
battery: META-OK 13/13 legs; PASS
```

## Scene pairing (per run; DT-6 -- the frozen LEG.tsv map is the reference, every deviation reported)
- coldboot_attract: paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85); status EDGE-FOUND, census 1283 frames, edge words b1046:case6/bit63=0/clock342483;b1047:case6/bit63=0/clock342632;b1048:case7/bit63=0/clock342781;b1049:case7/bit63=0/clock342930;b1050:case7/bit63=1/clock343079

## Score
```
score: legs_identical=0/13 outside_mask_words=20084447 pins_green=448  tuple=(0, 20084447, 448)
score (DT-7): booked_console_words=19538 outside_mask_words_unbooked=20064909  tuple_unbooked=(0, 20064909, 448)  [the decision number; the raw tuple above stays the reported one]
leg                                      reps         cross-engine                                 first divergent key (frames/audio/mem)
artdet_f37152                            ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          37157/1/0
coldboot                                 ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          69/142/0
coldboot_attract                         ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          69/142/0
ps2state_slot0_f36935                    ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          36936/0/0
ps2state_slot0_f39807                    ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          39808/0/0
ps2state_slot0_f40391                    ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          40392/8/0
ps2state_slot0_f44111                    ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          44112/1/0
ps2state_slot0_f46203_red_crystal        ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          46204/9/0
ps2state_slot0_f46949_yellow_crystal     ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          46950/1/0
ps2state_slot0_f57292                    ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          57293/0/0
ps2state_slot0_f59991                    ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          59992/6/0
slowspot_f37092_s1789428268              ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          37097/5/0
slowspot_f40298_s1789693059              ADMISSIBLE   META-OK DIVERGENT(frames,audio,mem)          40305/0/0
probe pair                               boundary       RAM0       SPAD      total   booked   unbooked  note
artdet_f37152                                   2         64         98        162        0        162  old vf 37159 / new vf 2
artdet_f37152                                  60        400         97        497        0        497  old vf 37217 / new vf 60
coldboot                                       60       1463         89       1552        0       1552  old vf 61 / new vf 60
coldboot                                      120     434670        171     434841    19538     415303  old vf 121 / new vf 120  booked_console 19538 (exact proved rules only (21210 words inside their ranges))
coldboot_attract                             1500    3868636       1175    3869811        0    3869811  old vf 2030 / new vf 1698  new_b1698: paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85)
coldboot_attract                             2700    3919245       1174    3920419        0    3920419  old vf 3230 / new vf 2898  new_b2898: paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85)
coldboot_attract                             3780    3948630       1174    3949804        0    3949804  old vf 4310 / new vf 3978  new_b3978: paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85)
coldboot_attract                             4200    3924783       1616    3926399        0    3926399  old vf 4730 / new vf 4398  new_b4398: paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85); pass-2 edge old_b3833=new_b4031 VERIFIED
coldboot_attract                             5400    3949210       1607    3950817        0    3950817  old vf 5930 / new vf 5598  new_b5598: paired by scene edge (run: old_b850=new_b1048, offset +198; frozen reference +283, deviation -85); pass-2 edge old_b3833=new_b4031 VERIFIED
ps2state_slot0_f36935                           2        101         92        193        0        193  old vf 36938 / new vf 2
ps2state_slot0_f36935                          60        136         92        228        0        228  old vf 36996 / new vf 60
ps2state_slot0_f39807                           2        417        145        562        0        562  old vf 39810 / new vf 2
ps2state_slot0_f39807                          60        216         97        313        0        313  old vf 39868 / new vf 60
ps2state_slot0_f40391                           2        117         91        208        0        208  old vf 40394 / new vf 2
ps2state_slot0_f40391                          60       1842        191       2033        0       2033  old vf 40452 / new vf 60
ps2state_slot0_f44111                           2       1086        164       1250        0       1250  old vf 44114 / new vf 2
ps2state_slot0_f44111                          60       2879        187       3066        0       3066  old vf 44172 / new vf 60
ps2state_slot0_f46203_red_crystal               2       1132         95       1227        0       1227  old vf 46206 / new vf 2
ps2state_slot0_f46203_red_crystal              60       1230         95       1325        0       1325  old vf 46264 / new vf 60
ps2state_slot0_f46949_yellow_crystal            2       1820        137       1957        0       1957  old vf 46952 / new vf 2
ps2state_slot0_f46949_yellow_crystal           60       8836        335       9171        0       9171  old vf 47010 / new vf 60
ps2state_slot0_f57292                           2        188         90        278        0        278  old vf 57295 / new vf 2
ps2state_slot0_f57292                          60       1068        173       1241        0       1241  old vf 57353 / new vf 60
ps2state_slot0_f59991                           2         84         93        177        0        177  old vf 59994 / new vf 2
ps2state_slot0_f59991                          60        763        171        934        0        934  old vf 60052 / new vf 60
slowspot_f37092_s1789428268                     2       4771        390       5161        0       5161  old vf 37099 / new vf 2
slowspot_f37092_s1789428268                    60        218         95        313        0        313  old vf 37157 / new vf 60
slowspot_f40298_s1789693059                     2        115         89        204        0        204  old vf 40307 / new vf 2
slowspot_f40298_s1789693059                    60        216         88        304        0        304  old vf 40365 / new vf 60
rebased_pairs=0 (every pair against the frozen OLD probe)
score.json: <work>/int_work/gates/s12/score.json
```

